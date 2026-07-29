#!/usr/bin/env python3
"""Validate the main firmware's scatter regions and fixed pool symbols."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


REGION_RE = re.compile(
    r"Execution Region (?P<name>\S+) \(Exec base: 0x(?P<base>[0-9a-f]+),"
    r".*?Size: 0x(?P<size>[0-9a-f]+), Max: 0x(?P<max>[0-9a-f]+)",
    re.IGNORECASE,
)
SYMBOL_RE = re.compile(
    r"^\s*(?P<name>\S+)\s+0x(?P<address>[0-9a-f]+)\s+Data\s+"
    r"(?P<size>\d+)\s+",
    re.IGNORECASE | re.MULTILINE,
)


EXPECTED_REGIONS = {
    "RW_IRAM1": (0x20000000, 0x00006000, None),
    "RW_VIDEO_SRAM": (0x20006000, 0x0000A000, 0x0000A000),
    "RW_TASK_STACK": (0x20010000, 0x00010000, 0x00010000),
    "RW_CCM_MAP": (0x1000F000, 0x00001000, None),
}

EXPECTED_SYMBOLS = {
    "mem2mapbase": (0x1000F000, 3840),
    "s_video_memory_pool": (0x20006000, 40 * 1024),
    "defaultTaskBuffer": (0x20010000, 4 * 1024),
    "thread_stack_pool": (0x20011000, 60 * 1024),
}


def validate(map_path: Path) -> None:
    text = map_path.read_text(encoding="utf-8", errors="replace")
    regions = {
        match.group("name"): tuple(
            int(match.group(field), 16) for field in ("base", "size", "max")
        )
        for match in REGION_RE.finditer(text)
    }
    symbols = {
        match.group("name"): (
            int(match.group("address"), 16),
            int(match.group("size")),
        )
        for match in SYMBOL_RE.finditer(text)
    }

    errors: list[str] = []
    for name, (expected_base, expected_max, expected_size) in EXPECTED_REGIONS.items():
        actual = regions.get(name)
        if actual is None:
            errors.append(f"missing execution region {name}")
            continue
        base, size, maximum = actual
        if base != expected_base:
            errors.append(f"{name} base 0x{base:08X}, expected 0x{expected_base:08X}")
        if maximum != expected_max:
            errors.append(f"{name} max 0x{maximum:X}, expected 0x{expected_max:X}")
        if size > maximum:
            errors.append(f"{name} size 0x{size:X} exceeds max 0x{maximum:X}")
        if expected_size is not None and size != expected_size:
            errors.append(f"{name} size 0x{size:X}, expected 0x{expected_size:X}")

    for name, expected in EXPECTED_SYMBOLS.items():
        actual = symbols.get(name)
        if actual != expected:
            errors.append(f"{name} is {actual!r}, expected {expected!r}")

    nes_stripe = symbols.get("nes_lcd_stripe")
    if nes_stripe is not None and nes_stripe[1] != 4:
        errors.append(
            f"nes_lcd_stripe still consumes {nes_stripe[1]} static bytes; expected pointer"
        )

    if errors:
        raise SystemExit("Memory layout validation failed:\n- " + "\n- ".join(errors))

    iram_size = regions["RW_IRAM1"][1]
    print(
        "Memory layout OK: "
        f"RW/ZI=0x{iram_size:X}/0x6000, video=40KB, task=64KB, CCM map=3840B"
    )


def main() -> None:
    default_map = Path(__file__).resolve().parents[2] / "MDK-ARM" / "Star Gamer" / "Star Gamer.map"
    parser = argparse.ArgumentParser()
    parser.add_argument("map", nargs="?", type=Path, default=default_map)
    args = parser.parse_args()
    validate(args.map.resolve())


if __name__ == "__main__":
    main()
