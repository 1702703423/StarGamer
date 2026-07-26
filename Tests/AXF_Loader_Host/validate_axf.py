#!/usr/bin/env python3
"""Validate an ARM AXF against the relocation loader used by Star Gamer."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_AXF = ROOT / "Star Gamer ELF" / "Objects" / "Star Gamer ELF.axf"

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIIIIIII")
SECTION_HEADER = struct.Struct("<IIIIIIIIII")
REL_ENTRY = struct.Struct("<II")
SYMBOL_ENTRY = struct.Struct("<IIIBBH")

ET_EXEC = 2
ET_DYN = 3
EM_ARM = 40
PT_LOAD = 1
PF_X = 1
SHT_REL = 9
SHN_UNDEF = 0

SUPPORTED_RELOCATIONS = {
    0: "R_ARM_NONE",
    1: "R_ARM_PC24",
    2: "R_ARM_ABS32",
    3: "R_ARM_REL32",
    10: "R_ARM_THM_CALL",
    21: "R_ARM_GLOB_DAT",
    22: "R_ARM_JUMP_SLOT",
    23: "R_ARM_RELATIVE",
    27: "R_ARM_PLT32",
    28: "R_ARM_CALL",
    29: "R_ARM_JUMP24",
    30: "R_ARM_THM_JUMP24",
    40: "R_ARM_V4BX",
    47: "R_ARM_THM_MOVW_ABS_NC",
    48: "R_ARM_THM_MOVT_ABS",
}
SIMULATED_LOAD_ADDRESS = 0x68000000


def unpack_from(fmt: struct.Struct, data: bytes, offset: int, label: str) -> tuple[int, ...]:
    if offset < 0 or offset + fmt.size > len(data):
        raise AssertionError(f"{label} exceeds the AXF file")
    return fmt.unpack_from(data, offset)


def swap_halfwords(value: int) -> int:
    return ((value >> 16) | ((value << 16) & 0xFFFFFFFF)) & 0xFFFFFFFF


def decode_thumb_mov_immediate(value: int) -> int:
    value = swap_halfwords(value)
    return (
        (((value >> 16) & 0xF) << 12)
        | (((value >> 26) & 0x1) << 11)
        | (((value >> 12) & 0x7) << 8)
        | (value & 0xFF)
    )


def relocate_thumb_mov(image: bytearray, offset: int, symbol_address: int, high: bool) -> None:
    instruction = struct.unpack_from("<I", image, offset)[0]
    instruction = swap_halfwords(instruction) & 0xFBF08F00
    immediate = (symbol_address >> 16) & 0xFFFF if high else symbol_address & 0xFFFF
    instruction |= ((immediate >> 12) & 0xF) << 16
    instruction |= ((immediate >> 11) & 0x1) << 26
    instruction |= ((immediate >> 8) & 0x7) << 12
    instruction |= immediate & 0xFF
    instruction = swap_halfwords(instruction)
    struct.pack_into("<I", image, offset, instruction)
    if decode_thumb_mov_immediate(instruction) != immediate:
        raise AssertionError("Thumb MOVW/MOVT relocation produced a bad immediate")


def validate(path: Path) -> None:
    data = path.read_bytes()
    header = unpack_from(ELF_HEADER, data, 0, "ELF header")
    ident = header[0]
    if ident[:4] != b"\x7fELF" or ident[4] != 1 or ident[5] != 1:
        raise AssertionError("loader requires a 32-bit little-endian ELF file")

    elf_type = header[1]
    machine = header[2]
    entry = header[4]
    program_offset = header[5]
    section_offset = header[6]
    program_entry_size = header[9]
    program_count = header[10]
    section_entry_size = header[11]
    section_count = header[12]

    if elf_type not in (ET_EXEC, ET_DYN):
        raise AssertionError(f"unsupported ELF type {elf_type}")
    if machine != EM_ARM:
        raise AssertionError(f"AXF machine is {machine}, expected ARM ({EM_ARM})")
    if program_entry_size != PROGRAM_HEADER.size:
        raise AssertionError("unexpected program-header size")
    if section_entry_size != SECTION_HEADER.size:
        raise AssertionError("unexpected section-header size")

    program_headers = [
        unpack_from(
            PROGRAM_HEADER,
            data,
            program_offset + index * program_entry_size,
            f"program header {index}",
        )
        for index in range(program_count)
    ]
    load_segments = [item for item in program_headers if item[0] == PT_LOAD]
    if not load_segments:
        raise AssertionError("AXF has no PT_LOAD segment")

    previous_end = None
    max_alignment = 1
    for index, segment in enumerate(load_segments):
        _, file_offset, virtual_address, _, file_size, memory_size, flags, alignment = segment
        if memory_size < file_size:
            raise AssertionError(f"PT_LOAD {index} has p_memsz smaller than p_filesz")
        if file_offset + file_size > len(data):
            raise AssertionError(f"PT_LOAD {index} payload exceeds the AXF file")
        if virtual_address + memory_size > 0xFFFFFFFF:
            raise AssertionError(f"PT_LOAD {index} address overflows 32 bits")
        if previous_end is not None and virtual_address < previous_end:
            raise AssertionError("PT_LOAD segments are unsorted or overlapping")
        if alignment == 0 or (alignment & (alignment - 1)) != 0:
            raise AssertionError(f"PT_LOAD {index} alignment is not a power of two")
        previous_end = virtual_address + memory_size
        max_alignment = max(max_alignment, alignment)

    virtual_start = load_segments[0][2]
    virtual_end = load_segments[-1][2] + load_segments[-1][5]
    image = bytearray(virtual_end - virtual_start)
    for segment in load_segments:
        file_offset = segment[1]
        virtual_address = segment[2]
        file_size = segment[4]
        destination = virtual_address - virtual_start
        image[destination : destination + file_size] = data[
            file_offset : file_offset + file_size
        ]
    entry_address = entry & ~1
    if (entry & 1) == 0:
        raise AssertionError("AXF entry point does not have the Thumb bit set")
    if not any(
        (segment[6] & PF_X) != 0
        and segment[2] <= entry_address < segment[2] + segment[5]
        for segment in load_segments
    ):
        raise AssertionError("AXF entry point is outside an executable PT_LOAD segment")

    section_headers = [
        unpack_from(
            SECTION_HEADER,
            data,
            section_offset + index * section_entry_size,
            f"section header {index}",
        )
        for index in range(section_count)
    ]

    relocation_counts: dict[int, int] = {}
    relocation_total = 0
    for section_index, section in enumerate(section_headers):
        section_type = section[1]
        if section_type != SHT_REL:
            continue
        rel_offset = section[4]
        rel_size = section[5]
        symbol_section_index = section[6]
        rel_entry_size = section[9]
        if rel_entry_size != REL_ENTRY.size or rel_size % REL_ENTRY.size != 0:
            raise AssertionError(f"section {section_index} has invalid REL entries")
        if symbol_section_index >= len(section_headers):
            raise AssertionError(f"section {section_index} links an invalid symbol table")

        symbol_section = section_headers[symbol_section_index]
        symbol_offset = symbol_section[4]
        symbol_size = symbol_section[5]
        symbol_entry_size = symbol_section[9]
        if symbol_entry_size != SYMBOL_ENTRY.size or symbol_size % SYMBOL_ENTRY.size != 0:
            raise AssertionError("relocation symbol table has invalid entries")
        symbol_count = symbol_size // SYMBOL_ENTRY.size

        for rel_index in range(rel_size // REL_ENTRY.size):
            target, info = unpack_from(
                REL_ENTRY,
                data,
                rel_offset + rel_index * REL_ENTRY.size,
                f"relocation {rel_index}",
            )
            symbol_index = info >> 8
            relocation_type = info & 0xFF
            if relocation_type not in SUPPORTED_RELOCATIONS:
                raise AssertionError(
                    f"loader does not support relocation type {relocation_type}"
                )
            if symbol_index >= symbol_count:
                raise AssertionError(f"relocation {rel_index} has an invalid symbol index")
            if target < virtual_start or target + 4 > virtual_end:
                raise AssertionError(f"relocation {rel_index} writes outside PT_LOAD memory")

            symbol = unpack_from(
                SYMBOL_ENTRY,
                data,
                symbol_offset + symbol_index * SYMBOL_ENTRY.size,
                f"symbol {symbol_index}",
            )
            if symbol[5] == SHN_UNDEF:
                raise AssertionError(
                    f"relocation {rel_index} uses an undefined external symbol"
                )
            symbol_address = SIMULATED_LOAD_ADDRESS + symbol[1] - virtual_start
            image_offset = target - virtual_start
            if relocation_type == 47:
                relocate_thumb_mov(image, image_offset, symbol_address, high=False)
            elif relocation_type == 48:
                relocate_thumb_mov(image, image_offset, symbol_address, high=True)
            relocation_counts[relocation_type] = relocation_counts.get(relocation_type, 0) + 1
            relocation_total += 1

    relocation_summary = ", ".join(
        f"{SUPPORTED_RELOCATIONS[k]}={relocation_counts[k]}"
        for k in sorted(relocation_counts)
    )
    print(
        f"AXF loader compatibility OK: {path}\n"
        f"entry=0x{entry:08X} load_size={virtual_end - virtual_start} "
        f"alignment={max_alignment} relocations={relocation_total} "
        f"simulated_entry=0x{SIMULATED_LOAD_ADDRESS + entry - virtual_start:08X}\n"
        f"{relocation_summary or 'no relocations'}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("axf", nargs="?", type=Path, default=DEFAULT_AXF)
    args = parser.parse_args()
    validate(args.axf.resolve())


if __name__ == "__main__":
    main()
