"""Reproduce the 0x0807D038 focus fault from the audited ARMCLANG AXF and SDMP.

Requires Python 3 and arm-none-eabi-nm/objcopy on PATH. Structure offsets below
were checked against this AXF's DWARF with GDB ptype /o; a hash guard prevents
silently interpreting a different firmware build with those offsets.
"""

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / "HardFault -dump/0x0807D038/Star Gamer.axf"
BOOT = ROOT / "BootLoader/StarGamerBootLoader/MDK-ARM/StarGamerBootLoader/StarGamerBootLoader.axf"
DUMP = ROOT / "HardFault -dump/hardfault_dump-1.1.4-0x0807D038.bin"
APP_SHA = "c45ae6fd3ea08f0c3a0d3890b8c8c383ddf5691c3ce75016975ececc10778e7a"
BOOT_SHA = "efaddcfc890d2f038058ef189551669f81dd11d9e038b07c99da84c50b25964d"
FAULT_FIELDS = (
    "sp frame exc_return msp psp cfsr hfsr dfsr afsr mmfar bfar "
    "r0 r1 r2 r3 r12 lr pc xpsr frame_flags"
).split()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def symbols(path):
    output = subprocess.check_output(
        ["arm-none-eabi-nm", "-n", str(path)], text=True
    )
    result = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3:
            result[fields[2]] = int(fields[0], 16)
    return result


class Snapshot:
    def __init__(self, path):
        self.data = path.read_bytes()
        require(len(self.data) >= 256, "Truncated SDMP header")
        header = struct.unpack_from("<8I", self.data)
        magic, version, size, total, payload, count, _, _ = header
        require((magic, version, size) == (0x504D4453, 1, 160), "Unsupported SDMP format")
        require(total == len(self.data) and payload == total - 256, "Invalid SDMP size")
        require(count == 3, "This analysis requires CCM, main SRAM and SRAMEX")
        self.regions = [struct.unpack_from("<3I", self.data, 112 + i * 12) for i in range(count)]
        require(self.regions == [
            (0x10000000, 0x10000, 0x100),
            (0x20000000, 0x20000, 0x10100),
            (0x68000000, 0x100000, 0x30100),
        ], "Unexpected memory region layout")
        checksums = struct.unpack_from("<2I", self.data, 148)
        require(fnv1a(self.data[256:]) == checksums[0], "Payload checksum failed")
        require(fnv1a(self.data[:152]) == checksums[1], "Header checksum failed")
        self.fault = dict(zip(FAULT_FIELDS, struct.unpack_from("<20I", self.data, 32)))
        print(f"SDMP checksum OK: header=0x{checksums[1]:08X}, payload=0x{checksums[0]:08X}")

    def read(self, address, length):
        for base, size, offset in self.regions:
            if base <= address and address + length <= base + size:
                start = offset + address - base
                return self.data[start:start + length]
        raise ValueError(f"Address outside snapshot: 0x{address:08X}")

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def u16(self, address):
        return struct.unpack("<H", self.read(address, 2))[0]

    def allocation(self, address):
        require(0x68000000 <= address < 0x680F0000, "Pointer outside SRAMEX heap")
        index = (address - 0x68000000) // 32
        while index >= 0 and self.u16(0x680F0000 + index * 2) == 0xFFFF:
            index -= 1
        require(index >= 0, "Invalid allocation continuation")
        entry = self.u16(0x680F0000 + index * 2)
        require(entry & 0x8000, "Pointer refers to free memory")
        return 0x68000000 + index * 32, (entry & 0x7FFF) * 32

    def walk_list(self, address):
        size, head, tail = struct.unpack("<3I", self.read(address, 12))
        node, previous = head, 0
        nodes = []
        while node:
            require(node not in nodes and len(nodes) < 256, "Cyclic or oversized list")
            require(self.u32(node + size) == previous, "Broken previous link")
            nodes.append(node)
            previous, node = node, self.u32(node + size + 4)
        require(previous == tail, "Broken tail link")
        return nodes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, default=DUMP)
    parser.add_argument("--axf", type=Path, default=APP)
    parser.add_argument("--bootloader", type=Path, default=BOOT)
    args = parser.parse_args()
    for path, expected in [(args.axf, APP_SHA), (args.bootloader, BOOT_SHA)]:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        require(digest == expected, f"Different AXF build; recheck DWARF layouts: {path}")
    sym = symbols(args.axf)
    dump = Snapshot(args.dump)
    fault = dump.fault
    require(fault["pc"] == sym["lv_obj_has_flag"], "Different fault PC")
    require(fault["frame_flags"] & 1 and not fault["frame_flags"] & 0x100, "Expected basic valid frame")
    print("Fault:", " ".join(f"{key}=0x{fault[key]:08X}" for key in ["pc", "lr", "r0", "bfar", "psp"]))
    require(fault["bfar"] == fault["r0"] + 28, "BFAR differs from R0 + flags offset")
    hardware_frame = struct.unpack("<8I", dump.read(fault["frame"], 32))
    require(hardware_frame == tuple(fault[k] for k in ["r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"]), "PSP frame differs from header")
    tcb = dump.u32(sym["pxCurrentTCB"])
    require(tcb == dump.u32(sym["lvglThreadId"]), "Faulting task is not LVGL")
    stack_base = dump.u32(tcb + 48)
    stack = dump.read(stack_base, 8192)
    fill = next((i for i, value in enumerate(stack) if value != 0xA5), len(stack))
    print(f"LVGL TCB=0x{tcb:08X}, stack=0x{stack_base:08X}, untouched bottom={fill} bytes")

    group = dump.u32(sym["default_group"])
    message_group = dump.u32(sym["s_msg_group"])
    global_groups = dump.walk_list(sym["_lv_group_ll"])
    print("Global group links OK:", " -> ".join(f"0x{x:08X}" for x in global_groups))
    print(f"Default group=0x{group:08X}, focus node=0x{dump.u32(group + 12):08X}")
    print(f"Message group=0x{message_group:08X}, members={len(dump.walk_list(message_group))}")
    print("Dialog active:", any(dump.read(sym["s_dialog"], 44)))
    nodes = dump.walk_list(group)
    objects = [dump.u32(node) for node in nodes]
    class_names = {value: key for key, value in sym.items() if key.endswith("_class")}
    for i, (node, obj) in enumerate(zip(nodes, objects), 1):
        base, size = dump.allocation(obj)
        kind = class_names.get(dump.u32(obj), "INVALID_CLASS")
        owners = [p for p in objects if dump.u32(p + 12) == base]
        print(f"{i:2}: node=0x{node:08X} object=0x{obj:08X} {kind} allocation=0x{base:08X}+{size}",
              "style_owners=" + ",".join(f"0x{x:08X}" for x in owners))
    print(f"Default object links OK: {len(nodes)} nodes")

    # Only the bootloader's low Flash image is needed to follow the bogus
    # parent through address 4 (the boot Flash alias) into its vector table.
    with tempfile.TemporaryDirectory(prefix="stargamer-focus-") as directory:
        output = Path(directory) / "boot.bin"
        subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", "--only-section=ER_IROM1",
                        str(args.bootloader), str(output)], check=True)
        boot = output.read_bytes()

    def read_word(address):
        flash_address = address + 0x08000000 if address < 0x10000 else address
        offset = flash_address - 0x08000000
        if 0 <= offset and offset + 4 <= len(boot):
            return struct.unpack_from("<I", boot, offset)[0]
        return dump.u32(address)

    focus = dump.u32(group + 12)
    require(focus in nodes, "Focused node is outside group")
    index = nodes.index(focus)
    for step in range(1, len(nodes) + 1):
        obj = objects[(index - step) % len(nodes)]
        if dump.u16(obj + 32) & 0x80:
            print(f"Reverse traversal skips disabled object 0x{obj:08X}")
            continue
        parent, chain = obj, []
        while parent and parent not in chain and len(chain) < 32:
            chain.append(parent)
            try:
                flags = read_word(parent + 28)
            except ValueError:
                print("Parent chain:", " -> ".join(f"0x{x:08X}" for x in chain))
                require(parent == fault["r0"], "Predicted pointer differs from stacked R0")
                print(f"Reproduced: read 0x{parent + 28:08X} == BFAR; parent == R0")
                return
            if flags & 1:
                break
            parent = read_word(parent + 4)
        if not parent:
            break
    raise ValueError("Snapshot did not reproduce the recorded pointer chain")


if __name__ == "__main__":
    main()
