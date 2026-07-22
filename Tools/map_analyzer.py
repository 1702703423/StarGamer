#!/usr/bin/env python3
"""
ARMCLANG MAP File Analyzer — FLASH / SRAM memory usage parser with tkinter GUI.

Parses Keil MDK-ARM (armlink) generated .map files for STM32F407ZG projects
and displays Flash (RO) and SRAM (RW+ZI) usage breakdowns interactively.

Usage:
    python map_analyzer.py [path/to/xxx.map]

If no path is given, a file-open dialog is shown.
"""

import re
import os
import sys
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ── Data models ────────────────────────────────────────────────────────────

@dataclass
class ExecRegion:
    """An execution region parsed from the Memory Map section."""
    name: str
    exec_base: int       # execution address
    load_base: int       # load address (or 0 if COMPRESSED)
    size: int            # actual size used
    max_size: int        # maximum allowed size
    compressed_size: int # compressed size hint (0 if none)

    @property
    def usage_pct(self) -> float:
        return (self.size / self.max_size * 100) if self.max_size else 0.0

    @property
    def free(self) -> int:
        return self.max_size - self.size


@dataclass
class LoadRegion:
    """A load region parsed from the Memory Map section."""
    name: str
    base: int
    size: int
    max_size: int
    compressed_size: int
    exec_regions: List[ExecRegion] = field(default_factory=list)


@dataclass
class ObjectRow:
    """One row from the Image Component Sizes table."""
    code: int = 0        # Code (inc. data) — includes inline data
    ro_data: int = 0     # RO Data (const, literal pools, etc.)
    rw_data: int = 0     # RW Data (initialised global/static)
    zi_data: int = 0     # ZI Data (zero-init = bss + stacks)
    debug: int = 0       # Debug info size
    name: str = ""       # source object name
    library: str = ""    # library it belongs to (empty = project object)

    @property
    def flash_usage(self) -> int:
        """Flash bytes consumed = Code + RO Data + RW Data."""
        return self.code + self.ro_data + self.rw_data

    @property
    def sram_usage(self) -> int:
        """SRAM bytes consumed = RW Data + ZI Data."""
        return self.rw_data + self.zi_data


@dataclass
class MapData:
    """Complete parsed MAP file data."""
    filepath: str = ""
    entry_point: int = 0
    load_regions: List[LoadRegion] = field(default_factory=list)
    objects: List[ObjectRow] = field(default_factory=list)
    libraries: List[ObjectRow] = field(default_factory=list)

    # Grand totals from the summary footer
    total_code: int = 0
    total_ro_data: int = 0
    total_rw_data: int = 0
    total_zi_data: int = 0
    total_rom: int = 0       # Code + RO Data + RW Data

    # MCU capacity hints (extracted from scatter file max sizes)
    flash_capacity: int = 0
    sram_capacity: int = 0       # total SRAM: internal + CCM + external
    internal_sram_capacity: int = 0
    ccm_capacity: int = 0
    external_sram_capacity: int = 0

    # Optional: loaded scatter-file for accurate region metadata
    sct: Optional["SctFile"] = None

    @property
    def flash_used(self) -> int:
        if self.total_rom > 0:
            return self.total_rom
        return self.total_code + self.total_ro_data + self.total_rw_data

    @property
    def sram_used(self) -> int:
        return self.total_rw_data + self.total_zi_data


# ── Scatter-loading file data models ────────────────────────────────────────

@dataclass
class SctInputSection:
    """One input-section selector inside an execution region, e.g. '*.o (RESET, +First)'."""
    module: str = ""       # e.g. "*.o", "video_memory.o", "*", ".ANY"
    section: str = ""      # e.g. "RESET", ".bss.video_sram", "InRoot$$Sections"
    attributes: List[str] = field(default_factory=list)  # e.g. ["+First"], ["+RO"], ["+RW","+ZI"]

    def __str__(self) -> str:
        mod = self.module or "*"
        if self.section:
            return f"{mod} ({self.section}{', ' + ', '.join(self.attributes) if self.attributes else ''})"
        if self.attributes:
            return f"{mod} ({', '.join(self.attributes)})"
        return mod


@dataclass
class SctExecRegion:
    """An execution region parsed from a scatter-loading (.sct) file."""
    name: str
    base: int
    size: int
    uninit: bool = False       # UNINIT — no load, treated as zero-init reserved area
    fixed: bool = False        # FIXED — address is absolute, not relocatable
    empty: bool = False        # EMPTY — reserves space but places nothing
    overlay: bool = False      # OVERLAY — shares address space with others
    align: int = 0             # ALIGN constraint (0 = none)
    input_sections: List[SctInputSection] = field(default_factory=list)

    @property
    def end_address(self) -> int:
        return self.base + self.size


@dataclass
class SctLoadRegion:
    """A load region parsed from a scatter-loading (.sct) file."""
    name: str
    base: int
    size: int
    exec_regions: List[SctExecRegion] = field(default_factory=list)

    @property
    def end_address(self) -> int:
        return self.base + self.size


@dataclass
class SctFile:
    """Complete scatter-loading description file."""
    filepath: str = ""
    load_regions: List[SctLoadRegion] = field(default_factory=list)

    def find_exec_region(self, name: str) -> Optional[SctExecRegion]:
        """Find an execution region by name (case-insensitive)."""
        name_lower = name.lower()
        for lr in self.load_regions:
            for er in lr.exec_regions:
                if er.name.lower() == name_lower:
                    return er
        return None

    @property
    def flash_regions(self) -> List[SctExecRegion]:
        """Return execution regions in internal Flash address space."""
        result: List[SctExecRegion] = []
        for lr in self.load_regions:
            for er in lr.exec_regions:
                if 0x08000000 <= er.base < 0x10000000:
                    result.append(er)
        return result

    @property
    def sram_regions(self) -> List[SctExecRegion]:
        """Return non-UNINIT execution regions in on-chip SRAM space (0x20000000)."""
        result: List[SctExecRegion] = []
        for lr in self.load_regions:
            for er in lr.exec_regions:
                if not er.uninit and 0x20000000 <= er.base < 0x40000000:
                    result.append(er)
        return result

    @property
    def external_sram_regions(self) -> List[SctExecRegion]:
        """Return execution regions in external-SRAM space (0x68000000)."""
        result: List[SctExecRegion] = []
        for lr in self.load_regions:
            for er in lr.exec_regions:
                if 0x68000000 <= er.base < 0x70000000:
                    result.append(er)
        return result

    @property
    def uninit_regions(self) -> List[SctExecRegion]:
        """Return UNINIT execution regions (reserved, zero-init, not loaded)."""
        result: List[SctExecRegion] = []
        for lr in self.load_regions:
            for er in lr.exec_regions:
                if er.uninit:
                    result.append(er)
        return result


# ── Parser ──────────────────────────────────────────────────────────────────

_RE_HEX = r"0x[0-9A-Fa-f]+"

def _parse_hex(s: str) -> int:
    """Parse a hex string like '0x00100000' to int."""
    return int(s.strip(), 16)


def _parse_size_kb(s: str) -> float:
    """Parse a string like '590.88kB' to float KB."""
    m = re.search(r"([\d.]+)\s*kB", s)
    return float(m.group(1)) if m else 0.0


def parse_sct_file(filepath: str) -> SctFile:
    """Parse an ARM scatter-loading description (.sct) file.

    Handles the standard armlink scatter-file syntax:
        LR_NAME  base  size  {
            ER_NAME  base  [UNINIT] [FIXED] [ALIGN n]  size  {
                module.o  (SECTION,  +attrs)
                ...
            }
            ...
        }
    """
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    # Remove C-style comments (both // line comments and / * block comments * /)
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)

    # Remove preprocessor directives (lines starting with #)
    content = re.sub(r'^\s*#.*?$', '', content, flags=re.MULTILINE)

    sct = SctFile(filepath=filepath)

    # Tokenize: split on whitespace but keep { } ( ) , as separate tokens
    tokens: List[str] = []
    for token in re.findall(r'0x[0-9A-Fa-f]+|\{|\}|\(|\)|,|[^\s\{\}\(\),]+', content):
        token = token.strip()
        if token:
            tokens.append(token)

    idx = 0
    n = len(tokens)

    def peek() -> str:
        return tokens[idx] if idx < n else ""

    def advance() -> str:
        nonlocal idx
        tok = tokens[idx]
        idx += 1
        return tok

    def expect(s: str):
        tok = advance()
        if tok != s:
            raise ValueError(f"Expected '{s}' but got '{tok}' at token {idx}")

    def parse_load_region() -> Optional[SctLoadRegion]:
        """LR_name base size { ... }"""
        if idx >= n:
            return None
        name = advance()
        if peek() == "{":
            # Not a valid LR — probably leftover from comment removal
            return None
        base = _parse_hex(advance())
        size = _parse_hex(advance())
        expect("{")

        lr = SctLoadRegion(name=name, base=base, size=size)
        while idx < n and peek() != "}":
            er = parse_exec_region()
            if er:
                lr.exec_regions.append(er)
        if idx < n:
            advance()  # skip '}'
        return lr

    def parse_exec_region() -> Optional[SctExecRegion]:
        """ER_name base [UNINIT] [FIXED] [ALIGN n] [EMPTY] [OVERLAY] size { ... }"""
        if idx >= n or peek() == "}":
            return None

        name = advance()
        base = _parse_hex(advance())

        # Parse optional attributes before size
        uninit = False
        fixed = False
        empty = False
        overlay = False
        align = 0

        while idx < n:
            tok = peek().upper()
            if tok == "UNINIT":
                advance()
                uninit = True
            elif tok == "FIXED":
                advance()
                fixed = True
            elif tok == "EMPTY":
                advance()
                empty = True
            elif tok == "OVERLAY":
                advance()
                overlay = True
            elif tok == "ALIGN":
                advance()
                align = _parse_hex(advance()) if idx < n and peek().startswith("0x") else int(advance())
            elif tok == "ABSOLUTE":
                advance()  # ABSOLUTE is the default, skip
            elif tok == "NOCOMPRESS":
                advance()  # attribute only, skip
            elif tok == "PADVALUE":
                advance()
                if idx < n:
                    advance()  # skip the pad value
            else:
                break

        size = _parse_hex(advance())

        er = SctExecRegion(name=name, base=base, size=size,
                           uninit=uninit, fixed=fixed, empty=empty,
                           overlay=overlay, align=align)

        # Parse body: either '{' input-sections '}' or just sections at the same level
        if idx < n and peek() == "{":
            advance()  # skip '{'
            er.input_sections = parse_input_sections()
            if idx < n and peek() == "}":
                advance()  # skip '}'
        elif idx < n and peek() not in ("}",):
            # Input sections without braces (some scatter files omit them for simple ERs)
            er.input_sections = parse_input_sections()

        return er

    def parse_input_sections() -> List[SctInputSection]:
        """Parse module.o (SECTION, +attrs) or * (SECTION) or .ANY (+attrs) entries.

        Stops when encountering '}' or a token that looks like a new ER (identifier followed by 0x...).
        """
        sections: List[SctInputSection] = []
        while idx < n and peek() != "}":
            tok = peek()
            # Check if this looks like the start of a new ER (name followed by hex base)
            # We peek ahead: if current is an identifier and next is a hex number
            # and the one after is '{' or another hex, it's likely a new region.
            if (re.match(r'^[A-Za-z_\.\*]', tok) and
                idx + 1 < n and re.match(r'^0x[0-9A-Fa-f]+$', tokens[idx + 1])):
                # Could be a new ER — but only if preceded by ',' or '{' in context
                # Heuristic: if tok looks like "ER_*" or "LR_*" or is a known region pattern
                if not tok.startswith(".") and tok not in (".ANY", "*"):
                    # Likely a new ER starting; break out
                    break

            module = advance()

            if module in (".ANY", "*"):
                # .ANY (+attrs)  or  * (section)
                if idx < n and peek() == "(":
                    advance()
                    sec_parts: List[str] = []
                    attrs: List[str] = []
                    while idx < n and peek() != ")":
                        part = advance()
                        if part == ",":
                            continue
                        if part.startswith("+"):
                            attrs.append(part)
                        else:
                            sec_parts.append(part)
                    if idx < n:
                        advance()  # skip ')'
                    section_name = ", ".join(sec_parts) if sec_parts else ""
                    sections.append(SctInputSection(
                        module=module, section=section_name, attributes=attrs))
                else:
                    sections.append(SctInputSection(module=module))
            else:
                # module.o (SECTION, +attrs)  or  module.o
                if idx < n and peek() == "(":
                    advance()
                    sec_parts: List[str] = []
                    attrs: List[str] = []
                    while idx < n and peek() != ")":
                        part = advance()
                        if part == ",":
                            continue
                        if part.startswith("+"):
                            attrs.append(part)
                        else:
                            sec_parts.append(part)
                    if idx < n:
                        advance()  # skip ')'
                    section_name = ", ".join(sec_parts) if sec_parts else ""
                    sections.append(SctInputSection(
                        module=module, section=section_name, attributes=attrs))
                else:
                    sections.append(SctInputSection(module=module))
        return sections

    # Main parsing loop
    while idx < n:
        tok = peek()
        if not tok:
            break
        # Skip any stray tokens
        if tok in ("}", ",", ";"):
            advance()
            continue
        # Start of a load region: name + 0x... + 0x... + {
        if re.match(r'^[A-Za-z_]', tok) and idx + 2 < n:
            if (re.match(r'^0x[0-9A-Fa-f]+$', tokens[idx + 1]) and
                re.match(r'^0x[0-9A-Fa-f]+$', tokens[idx + 2])):
                lr = parse_load_region()
                if lr:
                    sct.load_regions.append(lr)
                continue
        advance()  # skip unrecognized token

    return sct


def parse_map_file(filepath: str) -> MapData:
    """Parse an ARMCLANG linker .map file into a MapData structure."""
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    lines = content.split("\n")
    data = MapData(filepath=filepath)

    # ── Phase 1: Memory Map section ─────────────────────────────────────
    in_memory_map = False
    in_load_region = False
    in_exec_region = False
    current_lr: Optional[LoadRegion] = None
    current_er: Optional[ExecRegion] = None

    # Patterns
    re_load_region = re.compile(
        r"^\s*Load Region\s+(\S+)\s+\(Base:\s*(" + _RE_HEX +
        r"),\s*Size:\s*(" + _RE_HEX +
        r"),\s*Max:\s*(" + _RE_HEX + r")"
    )
    re_exec_region = re.compile(
        r"^\s*Execution Region\s+(\S+)\s+\(Exec base:\s*(" + _RE_HEX +
        r"),\s*Load base:\s*(" + _RE_HEX +
        r"),\s*Size:\s*(" + _RE_HEX +
        r"),\s*Max:\s*(" + _RE_HEX + r")"
    )
    re_compressed = re.compile(r"COMPRESSED\[(" + _RE_HEX + r")\]")

    for line in lines:
        # Detect section boundaries
        if "Memory Map of the image" in line:
            in_memory_map = True
            continue
        if "Image component sizes" in line:
            in_memory_map = False
            continue

        if not in_memory_map:
            continue

        # Entry point
        m = re.search(r"Image Entry point\s*:\s*(" + _RE_HEX + r")", line)
        if m:
            data.entry_point = _parse_hex(m.group(1))
            continue

        # Load Region header
        m = re_load_region.search(line)
        if m:
            compressed = 0
            cm = re_compressed.search(line)
            if cm:
                compressed = _parse_hex(cm.group(1))
            current_lr = LoadRegion(
                name=m.group(1),
                base=_parse_hex(m.group(2)),
                size=_parse_hex(m.group(3)),
                max_size=_parse_hex(m.group(4)),
                compressed_size=compressed,
            )
            data.load_regions.append(current_lr)
            in_load_region = True
            in_exec_region = False
            continue

        # Execution Region header
        m = re_exec_region.search(line)
        if m:
            compressed = 0
            cm = re_compressed.search(line)
            if cm:
                compressed = _parse_hex(cm.group(1))
            current_er = ExecRegion(
                name=m.group(1),
                exec_base=_parse_hex(m.group(2)),
                load_base=_parse_hex(m.group(3)),
                size=_parse_hex(m.group(4)),
                max_size=_parse_hex(m.group(5)),
                compressed_size=compressed,
            )
            if current_lr:
                current_lr.exec_regions.append(current_er)
            in_exec_region = True
            continue

    # ── Phase 2: Image Component Sizes ───────────────────────────────────
    in_component_sizes = False
    in_object_section = False
    in_library_section = False
    in_library_name_section = False
    in_totals = False
    current_lib_name = ""

    for line in lines:
        # Section boundaries
        if "Image component sizes" in line:
            in_component_sizes = True
            in_object_section = False
            in_library_section = False
            in_totals = False
            continue

        if not in_component_sizes:
            continue

        if "==============================================================================" in line:
            # Section separator — may end current sub-section
            continue

        # Detect sub-section headers
        if "Code (inc. data)" in line and "RO Data" in line and "Object Name" in line:
            in_object_section = True
            in_library_section = False
            in_library_name_section = False
            in_totals = False
            continue

        if "Code (inc. data)" in line and "RO Data" in line and "Library Member Name" in line:
            in_object_section = False
            in_library_section = True
            in_library_name_section = False
            in_totals = False
            continue

        if "Code (inc. data)" in line and "RO Data" in line and "Library Name" in line:
            in_object_section = False
            in_library_section = False
            in_library_name_section = True
            in_totals = False
            continue

        # Grand Totals — detect keyword then fall through to regex match
        if "Grand Totals" in line:
            in_object_section = False
            in_library_section = False
            in_library_name_section = False
            in_totals = True
            # fall through — don't continue, let the regex below capture the values

        # Skip separator lines
        stripped = line.strip()
        if stripped.startswith("----"):
            continue

        if in_totals:
            # Parse grand totals line
            m_gt = re.match(
                r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+Grand Totals",
                line,
            )
            if m_gt:
                data.total_code = int(m_gt.group(1))
                data.total_ro_data = int(m_gt.group(3))
                data.total_rw_data = int(m_gt.group(4))
                data.total_zi_data = int(m_gt.group(5))
                continue
            # Also capture the compressed ROM total
            m_rom = re.match(
                r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+ROM Totals",
                line,
            )
            if m_rom:
                data.total_rom = int(m_rom.group(1))
                continue

        # Total RO / RW lines at the very end
        m = re.search(r"Total RO\s+Size\s+\(Code \+ RO Data\)\s+(\d+)\s+\(([\d.]+)kB\)", line)
        if m:
            # Already parsed from Grand Totals; just capture for verification
            continue

        m = re.search(r"Total RW\s+Size\s+\(RW Data \+ ZI Data\)\s+(\d+)\s+\(([\d.]+)kB\)", line)
        if m:
            continue

        m = re.search(r"Total ROM Size\s+\(Code \+ RO Data \+ RW Data\)\s+(\d+)\s+\(\s*([\d.]+)kB\)", line)
        if m:
            data.total_rom = int(m.group(1))
            continue

        # Object Totals line
        if "Object Totals" in line:
            continue

        # Library Totals line
        if "Library Totals" in line:
            continue

        # Parse object rows
        if in_object_section:
            m = re.match(
                r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(.+?)\s*$",
                line,
            )
            if m:
                obj = ObjectRow(
                    code=int(m.group(1)),
                    ro_data=int(m.group(3)),
                    rw_data=int(m.group(4)),
                    zi_data=int(m.group(5)),
                    debug=int(m.group(6)),
                    name=m.group(7).strip(),
                )
                data.objects.append(obj)
            continue

        # Parse library member rows
        if in_library_section:
            m = re.match(
                r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(.+?)\s*$",
                line,
            )
            if m:
                obj = ObjectRow(
                    code=int(m.group(1)),
                    ro_data=int(m.group(3)),
                    rw_data=int(m.group(4)),
                    zi_data=int(m.group(5)),
                    debug=int(m.group(6)),
                    name=m.group(7).strip(),
                    library=current_lib_name,
                )
                data.libraries.append(obj)
            continue

        # Library name section rows (aggregated by library)
        if in_library_name_section:
            m = re.match(
                r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(.+?)\s*$",
                line,
            )
            if m:
                lib_name = m.group(7).strip()
                current_lib_name = lib_name
                # Not adding as separate object — library totals are summaries
            continue

    # ── Phase 3: Derive MCU capacities from max sizes ────────────────────
    # Strategy:
    #   Flash  → Load Region max_size (the physical flash storing everything).
    #   SRAM   → Sum of non-overlapping Execution Region max_sizes within
    #            each physical address space (internal SRAM, CCM, external SRAM).
    #   This avoids the old "max per region" approach that broke when multiple
    #   EXEC regions (e.g. RW_IRAM1 + RW_VIDEO_SRAM) shared the same physical bank.

    # Flash: use the largest Load Region max_size in the 0x08xxxxxx space
    for lr in data.load_regions:
        if 0x08000000 <= lr.base < 0x10000000:
            if lr.max_size > data.flash_capacity:
                data.flash_capacity = lr.max_size

    # Fallback: if no load region gave us flash capacity, try exec regions
    if data.flash_capacity == 0:
        for lr in data.load_regions:
            for er in lr.exec_regions:
                if 0x08000000 <= er.exec_base < 0x10000000:
                    if er.max_size > data.flash_capacity:
                        data.flash_capacity = er.max_size

    # SRAM: collect exec regions by physical bank, sum non-overlapping ones
    # Internal SRAM:  0x20000000–0x2001FFFF (128 KB for STM32F407ZG)
    # CCM:            0x10000000–0x1000FFFF ( 64 KB)
    # External SRAM:  0x68000000–0x680FFFFF (  1 MB)

    def _sum_disjoint_max_sizes(regions: List[ExecRegion], phys_base: int, phys_end: int) -> int:
        """Sum max_size of regions within [phys_base, phys_end), taking the
        union of their address ranges (non-overlapping by scatter-file design)."""
        covered: List[Tuple[int, int]] = []  # list of (base, end) ranges
        total = 0
        for er in sorted(regions, key=lambda r: r.exec_base):
            if not (phys_base <= er.exec_base < phys_end):
                continue
            er_end = er.exec_base + er.max_size
            if er_end > phys_end:
                er_end = phys_end
            # Check overlap with already-covered ranges
            overlaps = False
            for cb, ce in covered:
                if er.exec_base < ce and er_end > cb:
                    # Overlapping — use union
                    overlaps = True
                    break
            if not overlaps:
                total += er.max_size
                covered.append((er.exec_base, er_end))
        return total

    all_exec_regions: List[ExecRegion] = []
    for lr in data.load_regions:
        all_exec_regions.extend(lr.exec_regions)

    # Internal SRAM (0x20000000–0x20020000 covers both main 128K + aliased area)
    internal_sram = _sum_disjoint_max_sizes(all_exec_regions, 0x20000000, 0x20020000)
    # CCM
    ccm_sram = _sum_disjoint_max_sizes(all_exec_regions, 0x10000000, 0x10010000)
    # External SRAM
    external_sram = _sum_disjoint_max_sizes(all_exec_regions, 0x68000000, 0x68100000)

    data.sram_capacity = internal_sram + ccm_sram + external_sram
    data.internal_sram_capacity = internal_sram
    data.ccm_capacity = ccm_sram
    data.external_sram_capacity = external_sram

    return data


# ── Helpers for display ─────────────────────────────────────────────────────

def fmt_bytes(n: int) -> str:
    """Format a byte count for human display."""
    if n >= 1024 * 1024:
        return f"{n / (1024 * 1024):.2f} MB"
    if n >= 1024:
        return f"{n / 1024:.2f} KB"
    return f"{n} B"


def fmt_pct(used: int, total: int) -> str:
    """Format 'used / total (xx.x%)'."""
    if total == 0:
        return f"{fmt_bytes(used)} / 0 (—)"
    pct = used / total * 100
    return f"{fmt_bytes(used)} / {fmt_bytes(total)} ({pct:.1f}%)"


# ── GUI Application ────────────────────────────────────────────────────────

class MapAnalyzerApp:
    """Tkinter GUI for MAP file memory analysis."""

    def __init__(self, root: tk.Tk, map_path: Optional[str] = None):
        self.root = root
        self.root.title("ARMCLANG MAP Memory Analyzer")
        self.root.geometry("1280x800")

        self.map_data: Optional[MapData] = None
        self.sort_column = "flash"
        self.sort_reverse = True

        # ── Build UI ─────────────────────────────────────────────────
        self._build_menu()
        self._build_toolbar()
        self._build_summary_frame()
        self._build_notebook()

        # ── Load file ────────────────────────────────────────────────
        loaded = False
        if map_path and os.path.isfile(map_path):
            self._load(map_path)
            loaded = True
        else:
            # Try default path relative to script
            script_dir = os.path.dirname(os.path.abspath(__file__))
            project_map = os.path.normpath(os.path.join(
                script_dir, "..", "MDK-ARM", "Star Gamer", "Star Gamer.map"
            ))
            if os.path.isfile(project_map):
                self._load(project_map)
                loaded = True

        # Auto-load SCT file alongside the MAP
        if loaded and self.map_data:
            # Try <project_root>/System/StarGamer_CCM.sct
            sct_candidates = [
                os.path.normpath(os.path.join(script_dir, "..", "System", "StarGamer_CCM.sct")),
                os.path.normpath(os.path.join(script_dir, "..", "System", "StarGamer.sct")),
            ]
            # Also try alongside the MAP file
            map_dir = os.path.dirname(self.map_data.filepath)
            for f in os.listdir(map_dir) if os.path.isdir(map_dir) else []:
                if f.lower().endswith(".sct") or f.lower().endswith(".scf"):
                    sct_candidates.append(os.path.join(map_dir, f))
            for sct_path in sct_candidates:
                if os.path.isfile(sct_path):
                    try:
                        self._load_sct(sct_path)
                    except Exception:
                        pass  # SCT is optional
                    break

        if not loaded:
            self._ask_open()

    # ── Menu ──────────────────────────────────────────────────────────

    def _build_menu(self):
        menubar = tk.Menu(self.root)
        self.root.config(menu=menubar)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Open MAP File...", command=self._ask_open, accelerator="Ctrl+O")
        file_menu.add_command(label="Open SCT File...", command=self._ask_open_sct)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.root.quit)
        menubar.add_cascade(label="File", menu=file_menu)

        self.root.bind("<Control-o>", lambda e: self._ask_open())

    # ── Toolbar ───────────────────────────────────────────────────────

    def _build_toolbar(self):
        toolbar = ttk.Frame(self.root)
        toolbar.pack(side=tk.TOP, fill=tk.X, padx=4, pady=2)

        ttk.Button(toolbar, text="Open MAP...", command=self._ask_open).pack(side=tk.LEFT, padx=2)
        ttk.Button(toolbar, text="Open SCT...", command=self._ask_open_sct).pack(side=tk.LEFT, padx=2)

        self.lbl_file = ttk.Label(toolbar, text="No file loaded", foreground="gray")
        self.lbl_file.pack(side=tk.LEFT, padx=8)

        self.lbl_sct = ttk.Label(toolbar, text="", foreground="gray")
        self.lbl_sct.pack(side=tk.LEFT, padx=4)

    # ── Summary frame ─────────────────────────────────────────────────

    def _build_summary_frame(self):
        self.summary_frame = ttk.LabelFrame(self.root, text="Memory Summary", padding=8)
        self.summary_frame.pack(side=tk.TOP, fill=tk.X, padx=8, pady=4)

        # Flash row
        flash_frame = ttk.Frame(self.summary_frame)
        flash_frame.pack(fill=tk.X, pady=2)

        ttk.Label(flash_frame, text="FLASH (Code+RO+RW):", width=22, anchor=tk.W).pack(side=tk.LEFT)
        self.flash_bar = ttk.Progressbar(flash_frame, length=400, mode="determinate")
        self.flash_bar.pack(side=tk.LEFT, padx=4)
        self.lbl_flash = ttk.Label(flash_frame, text="—", width=44, anchor=tk.W)
        self.lbl_flash.pack(side=tk.LEFT)

        # Flash detail row
        flash_detail = ttk.Frame(self.summary_frame)
        flash_detail.pack(fill=tk.X, pady=1)
        ttk.Label(flash_detail, text="", width=22).pack(side=tk.LEFT)  # indent
        ttk.Label(flash_detail, text="Code only:", width=12, anchor=tk.W).pack(side=tk.LEFT)
        self.lbl_code = ttk.Label(flash_detail, text="—", width=20, anchor=tk.W)
        self.lbl_code.pack(side=tk.LEFT)
        ttk.Label(flash_detail, text="RO Data:", width=12, anchor=tk.W).pack(side=tk.LEFT)
        self.lbl_ro = ttk.Label(flash_detail, text="—", width=20, anchor=tk.W)
        self.lbl_ro.pack(side=tk.LEFT)

        # SRAM row
        sram_frame = ttk.Frame(self.summary_frame)
        sram_frame.pack(fill=tk.X, pady=2)

        ttk.Label(sram_frame, text="SRAM (RW Data + ZI Data):", width=22, anchor=tk.W).pack(side=tk.LEFT)
        self.sram_bar = ttk.Progressbar(sram_frame, length=400, mode="determinate")
        self.sram_bar.pack(side=tk.LEFT, padx=4)
        self.lbl_sram = ttk.Label(sram_frame, text="—", width=44, anchor=tk.W)
        self.lbl_sram.pack(side=tk.LEFT)

        # SRAM detail row
        sram_detail = ttk.Frame(self.summary_frame)
        sram_detail.pack(fill=tk.X, pady=1)
        ttk.Label(sram_detail, text="", width=22).pack(side=tk.LEFT)
        ttk.Label(sram_detail, text="RW Data:", width=12, anchor=tk.W).pack(side=tk.LEFT)
        self.lbl_rw = ttk.Label(sram_detail, text="—", width=20, anchor=tk.W)
        self.lbl_rw.pack(side=tk.LEFT)
        ttk.Label(sram_detail, text="ZI Data:", width=12, anchor=tk.W).pack(side=tk.LEFT)
        self.lbl_zi = ttk.Label(sram_detail, text="—", width=20, anchor=tk.W)
        self.lbl_zi.pack(side=tk.LEFT)

        # Regions row
        self.lbl_regions = ttk.Label(self.summary_frame, text="", foreground="gray")
        self.lbl_regions.pack(fill=tk.X, pady=(4, 0))

    # ── Notebook (tabs) ───────────────────────────────────────────────

    def _build_notebook(self):
        self.notebook = ttk.Notebook(self.root)
        self.notebook.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=8, pady=4)

        # Tab 1: Object list (sortable table)
        self.tab_objects = ttk.Frame(self.notebook)
        self.notebook.add(self.tab_objects, text="Objects")

        self._build_object_table(self.tab_objects)

        # Tab 2: Grouped by directory
        self.tab_dirs = ttk.Frame(self.notebook)
        self.notebook.add(self.tab_dirs, text="By Directory")

        self._build_dir_table(self.tab_dirs)

        # Tab 3: Region details
        self.tab_regions = ttk.Frame(self.notebook)
        self.notebook.add(self.tab_regions, text="Regions")

        self._build_region_view(self.tab_regions)

    # ── Object table ──────────────────────────────────────────────────

    def _build_object_table(self, parent: ttk.Frame):
        columns = ("name", "flash", "code", "ro_data", "rw_data", "zi_data", "sram", "debug")
        self.obj_tree = ttk.Treeview(
            parent,
            columns=columns,
            show="headings",
            selectmode="browse",
        )

        headers = {
            "name": ("Object / Library", 280),
            "flash": ("Flash ↓", 90),
            "code": ("Code", 80),
            "ro_data": ("RO Data", 80),
            "rw_data": ("RW Data", 80),
            "zi_data": ("ZI Data", 80),
            "sram": ("SRAM", 80),
            "debug": ("Debug", 80),
        }

        for col, (text, width) in headers.items():
            self.obj_tree.heading(col, text=text,
                                  command=lambda c=col: self._sort_objects(c))
            self.obj_tree.column(col, width=width, anchor=tk.E if col != "name" else tk.W)

        # Scrollbar
        vsb = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=self.obj_tree.yview)
        hsb = ttk.Scrollbar(parent, orient=tk.HORIZONTAL, command=self.obj_tree.xview)
        self.obj_tree.configure(yscrollcommand=vsb.set, xscrollcommand=hsb.set)

        self.obj_tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")
        hsb.grid(row=1, column=0, sticky="ew")

        parent.grid_rowconfigure(0, weight=1)
        parent.grid_columnconfigure(0, weight=1)

        # Filter/search
        filter_frame = ttk.Frame(parent)
        filter_frame.grid(row=2, column=0, columnspan=2, sticky="ew", pady=2, padx=2)

        ttk.Label(filter_frame, text="Filter:").pack(side=tk.LEFT)
        self.filter_entry = ttk.Entry(filter_frame, width=30)
        self.filter_entry.pack(side=tk.LEFT, padx=4)
        self.filter_entry.bind("<KeyRelease>", lambda e: self._apply_filter())
        ttk.Button(filter_frame, text="Clear", command=self._clear_filter).pack(side=tk.LEFT, padx=2)

        ttk.Label(filter_frame, text="Show:").pack(side=tk.LEFT, padx=(16, 4))
        self.show_combo = ttk.Combobox(
            filter_frame, values=["All", "Project Objects", "Libraries Only"], state="readonly", width=16
        )
        self.show_combo.set("All")
        self.show_combo.pack(side=tk.LEFT, padx=4)
        self.show_combo.bind("<<ComboboxSelected>>", lambda e: self._apply_filter())

    # ── Directory-grouped table ───────────────────────────────────────

    def _build_dir_table(self, parent: ttk.Frame):
        columns = ("dir", "flash", "code", "ro_data", "rw_data", "zi_data", "sram", "count")
        self.dir_tree = ttk.Treeview(
            parent, columns=columns, show="headings", selectmode="browse"
        )

        headers = {
            "dir": ("Directory / Module", 300),
            "flash": ("Flash", 90),
            "code": ("Code", 80),
            "ro_data": ("RO Data", 80),
            "rw_data": ("RW Data", 80),
            "zi_data": ("ZI Data", 80),
            "sram": ("SRAM", 80),
            "count": ("Files", 60),
        }

        for col, (text, width) in headers.items():
            self.dir_tree.heading(col, text=text,
                                  command=lambda c=col: self._sort_dirs(c))
            self.dir_tree.column(col, width=width, anchor=tk.E if col != "dir" else tk.W)

        vsb = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=self.dir_tree.yview)
        self.dir_tree.configure(yscrollcommand=vsb.set)

        self.dir_tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")

        parent.grid_rowconfigure(0, weight=1)
        parent.grid_columnconfigure(0, weight=1)

    # ── Region details tab ────────────────────────────────────────────

    def _build_region_view(self, parent: ttk.Frame):
        self.region_text = tk.Text(parent, font=("Consolas", 10), wrap=tk.NONE)
        vsb = ttk.Scrollbar(parent, orient=tk.VERTICAL, command=self.region_text.yview)
        self.region_text.configure(yscrollcommand=vsb.set)

        self.region_text.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")

        parent.grid_rowconfigure(0, weight=1)
        parent.grid_columnconfigure(0, weight=1)

    # ── Data loading ──────────────────────────────────────────────────

    def _ask_open(self):
        path = filedialog.askopenfilename(
            title="Select ARMCLANG MAP file",
            filetypes=[("MAP files", "*.map"), ("All files", "*.*")],
        )
        if path:
            self._load(path)

    def _ask_open_sct(self):
        path = filedialog.askopenfilename(
            title="Select ARM Scatter-loading Description file",
            filetypes=[("SCT files", "*.sct"), ("Scatter files", "*.scf"), ("All files", "*.*")],
        )
        if path:
            self._load_sct(path)

    def _load(self, path: str):
        try:
            self.map_data = parse_map_file(path)
        except Exception as e:
            messagebox.showerror("Parse Error", f"Failed to parse MAP file:\n{e}")
            return

        self.lbl_file.config(text=os.path.basename(path), foreground="black")
        title = f"ARMCLANG MAP Analyzer — {os.path.basename(path)}"
        self.root.title(title)
        self._refresh_all()

    def _load_sct(self, path: str):
        """Load a scatter-loading (.sct) file and pair it with already-loaded MAP data."""
        try:
            sct = parse_sct_file(path)
        except Exception as e:
            messagebox.showerror("Parse Error", f"Failed to parse SCT file:\n{e}")
            return

        if self.map_data:
            self.map_data.sct = sct
            # Override capacity values with authoritative SCT data.
            # UNINIT regions DO count toward SRAM capacity — they consume physical
            # SRAM, they're just not initialised from flash.
            sct_flash = 0
            sct_internal_sram = 0
            sct_ccm = 0
            sct_external_sram = 0
            for lr in sct.load_regions:
                for er in lr.exec_regions:
                    if 0x08000000 <= er.base < 0x10000000:
                        sct_flash += er.size
                    elif 0x20000000 <= er.base < 0x20020000:
                        sct_internal_sram += er.size  # includes UNINIT
                    elif 0x10000000 <= er.base < 0x10010000:
                        sct_ccm += er.size
                    elif 0x68000000 <= er.base < 0x68100000:
                        sct_external_sram += er.size

            # Only override if SCT provided meaningful values
            if sct_flash:
                self.map_data.flash_capacity = sct_flash
            if sct_internal_sram:
                self.map_data.internal_sram_capacity = sct_internal_sram
            if sct_ccm:
                self.map_data.ccm_capacity = sct_ccm
            if sct_external_sram:
                self.map_data.external_sram_capacity = sct_external_sram
            self.map_data.sram_capacity = (sct_internal_sram + sct_ccm + sct_external_sram
                                           or self.map_data.sram_capacity)

        self.lbl_sct.config(text=f"SCT: {os.path.basename(path)}", foreground="blue")
        title = self.root.title()
        if " + SCT" not in title:
            self.root.title(title + f" + SCT {os.path.basename(path)}")
        self._refresh_all()

    def _refresh_all(self):
        if not self.map_data:
            return
        self._refresh_summary()
        self._apply_filter()
        self._refresh_dir_view()
        self._refresh_region_view()

    # ── Summary refresh ───────────────────────────────────────────────

    def _refresh_summary(self):
        md = self.map_data

        flash_total = md.flash_capacity or 0x100000  # default 1 MB
        sram_total = md.sram_capacity or 0x20000     # default 128 KB

        flash_used = md.flash_used
        sram_used = md.sram_used

        # Flash bar
        flash_pct = min(flash_used / flash_total * 100, 100) if flash_total else 0
        self.flash_bar["value"] = flash_pct
        self.lbl_flash.config(text=fmt_pct(flash_used, flash_total))

        # SRAM bar
        sram_pct = min(sram_used / sram_total * 100, 100) if sram_total else 0
        self.sram_bar["value"] = sram_pct
        self.lbl_sram.config(text=fmt_pct(sram_used, sram_total))

        # Detail labels
        self.lbl_code.config(text=f"{md.total_code:,} B  ({md.total_code / 1024:.1f} KB)")
        self.lbl_ro.config(text=f"{md.total_ro_data:,} B  ({md.total_ro_data / 1024:.1f} KB)")
        self.lbl_rw.config(text=f"{md.total_rw_data:,} B  ({md.total_rw_data / 1024:.1f} KB)")
        self.lbl_zi.config(text=f"{md.total_zi_data:,} B  ({md.total_zi_data / 1024:.1f} KB)")

        # Regions info — enriched with SCT metadata when available
        region_lines = []
        sct = md.sct
        for lr in md.load_regions:
            for er in lr.exec_regions:
                # Look up matching SCT exec region for UNINIT/FIXED attributes
                tags = ""
                if sct:
                    ser = sct.find_exec_region(er.name)
                    if ser:
                        if ser.uninit:
                            tags = " [UNINIT — reserved, not loaded]"
                        elif ser.fixed:
                            tags = " [FIXED]"
                region_lines.append(
                    f"  {er.name}: {fmt_bytes(er.size)} / {fmt_bytes(er.max_size)} "
                    f"({er.usage_pct:.1f}%)  @ 0x{er.exec_base:08X}{tags}"
                )

        # Show per-bank capacity breakdown if we have detailed info
        if md.internal_sram_capacity or md.ccm_capacity or md.external_sram_capacity:
            parts = []
            if md.internal_sram_capacity:
                parts.append(f"Internal SRAM: {fmt_bytes(md.internal_sram_capacity)}")
            if md.ccm_capacity:
                parts.append(f"CCM: {fmt_bytes(md.ccm_capacity)}")
            if md.external_sram_capacity:
                parts.append(f"External SRAM: {fmt_bytes(md.external_sram_capacity)}")
            region_lines.append("  ── Capacity breakdown: " + " | ".join(parts))

        self.lbl_regions.config(text="Regions:\n" + "\n".join(region_lines) if region_lines else "No region data")

    # ── Object table refresh ──────────────────────────────────────────

    def _gather_rows(self) -> List[ObjectRow]:
        """Collect all objects (project + library) depending on filter."""
        md = self.map_data
        if not md:
            return []

        show = self.show_combo.get()
        rows: List[ObjectRow] = []

        if show in ("All", "Project Objects"):
            rows.extend(md.objects)
        if show in ("All", "Libraries Only"):
            rows.extend(md.libraries)

        return rows

    def _sort_key(self, obj: ObjectRow) -> Tuple:
        """Return the sort key for the current sort column."""
        col = self.sort_column
        val = {
            "name": obj.name,
            "flash": obj.flash_usage,
            "code": obj.code,
            "ro_data": obj.ro_data,
            "rw_data": obj.rw_data,
            "zi_data": obj.zi_data,
            "sram": obj.sram_usage,
            "debug": obj.debug,
        }.get(col, 0)
        # For name, make it case-insensitive
        if col == "name":
            return (isinstance(val, str) and val.lower(),)  # type: ignore[arg-type]
        return (val,)

    def _sort_objects(self, column: str):
        if self.sort_column == column:
            self.sort_reverse = not self.sort_reverse
        else:
            self.sort_column = column
            self.sort_reverse = (column != "name")  # numeric columns default descending

        # Update header arrow indicator
        arrow = " ↓" if self.sort_reverse else " ↑"
        base_names = {
            "name": "Object / Library",
            "flash": "Flash",
            "code": "Code",
            "ro_data": "RO Data",
            "rw_data": "RW Data",
            "zi_data": "ZI Data",
            "sram": "SRAM",
            "debug": "Debug",
        }
        for c, base in base_names.items():
            self.obj_tree.heading(c, text=base + (arrow if c == column else ""))

        self._apply_filter()

    # Directory sort

    def _sort_dirs(self, column: str):
        items = []
        for child in self.dir_tree.get_children():
            values = self.dir_tree.item(child, 'values')
            items.append((child, values))
        col_idx = {'dir': 0, 'flash': 1, 'code': 2, 'ro_data': 3,
                   'rw_data': 4, 'zi_data': 5, 'sram': 6, 'count': 7}.get(column, 1)
        if not hasattr(self, '_dir_sort_col') or self._dir_sort_col != column:
            self._dir_sort_reverse = True
            self._dir_sort_col = column
        else:
            self._dir_sort_reverse = not self._dir_sort_reverse
        if column == 'dir':
            items.sort(key=lambda x: x[1][col_idx].lower(), reverse=self._dir_sort_reverse)
        else:
            items.sort(key=lambda x: int(x[1][col_idx]), reverse=self._dir_sort_reverse)
        for child, _ in items:
            self.dir_tree.move(child, '', tk.END)

    def _apply_filter(self):
        """Re-populate the object treeview with current filter and sort."""
        if not self.map_data:
            return

        rows = self._gather_rows()
        filter_text = self.filter_entry.get().strip().lower()

        # Filter
        if filter_text:
            rows = [r for r in rows if filter_text in r.name.lower()]

        # Sort
        rows.sort(key=self._sort_key, reverse=self.sort_reverse)

        # Populate tree
        self.obj_tree.delete(*self.obj_tree.get_children())
        for obj in rows:
            self.obj_tree.insert("", tk.END, values=(
                obj.name,
                obj.flash_usage,
                obj.code,
                obj.ro_data,
                obj.rw_data,
                obj.zi_data,
                obj.sram_usage,
                obj.debug,
            ))

    def _clear_filter(self):
        self.filter_entry.delete(0, tk.END)
        self._apply_filter()

    # ── Directory view ────────────────────────────────────────────────

    @staticmethod
    def _dir_of(obj_name: str) -> str:
        """Extract a logical group from an object file name."""
        name = obj_name.lower()

        # Library objects
        if ".o(" in obj_name:
            return "[Library — " + obj_name.split(".o(")[0] + "]"
        if obj_name.endswith(".l"):
            return "[Library]"

        # Known project directories
        dir_map = [
            ("Core/", "Core"),
            ("Drivers/", "Drivers"),
            ("System/", "System"),
            ("BSP/", "BSP"),
            ("GUI/", "GUI"),
            ("Debug/", "Debug"),
            ("Media/", "Media"),
            ("FATFS/", "FATFS"),
            ("LIBJPEG/", "LIBJPEG"),
            ("Middlewares/", "Middlewares"),
            ("APP/", "APP"),
            ("IAP/", "IAP"),
            ("ELF Loader/", "ELF Loader"),
        ]
        for prefix, label in dir_map:
            if prefix.lower() in name:
                return label

        # NES emulator special
        if "nes_" in name or "6502" in name or "mapper" in name:
            return "NES Emulator"
        if "arduboy" in name:
            return "Arduboy"

        # LVGL
        if name.startswith("lv_"):
            return "LVGL"

        # HAL
        if "stm32f4xx_hal" in name or "stm32f4xx_ll" in name:
            return "HAL"

        # RTOS
        if any(x in name for x in ("freertos", "cmsis_os", "port.o", "queue.o", "tasks.o", "timers.o", "heap_")):
            return "FreeRTOS"

        # C runtime library
        if "c_w.l" in name or "fz_wm.l" in name or "m_wm.l" in name:
            return "[C Runtime Library]"

        return "[Other]"

    def _refresh_dir_view(self):
        if not self.map_data:
            return

        md = self.map_data
        groups: dict = {}

        for obj in md.objects:
            d = self._dir_of(obj.name)
            if d not in groups:
                groups[d] = {"flash": 0, "code": 0, "ro_data": 0, "rw_data": 0, "zi_data": 0, "sram": 0, "count": 0}
            g = groups[d]
            g["flash"] += obj.flash_usage
            g["code"] += obj.code
            g["ro_data"] += obj.ro_data
            g["rw_data"] += obj.rw_data
            g["zi_data"] += obj.zi_data
            g["sram"] += obj.sram_usage
            g["count"] += 1

        # Sort by flash descending
        sorted_groups = sorted(groups.items(), key=lambda x: x[1]["flash"], reverse=True)

        self.dir_tree.delete(*self.dir_tree.get_children())
        for dir_name, g in sorted_groups:
            self.dir_tree.insert("", tk.END, values=(
                dir_name,
                g["flash"],
                g["code"],
                g["ro_data"],
                g["rw_data"],
                g["zi_data"],
                g["sram"],
                g["count"],
            ))

    # ── Region view ───────────────────────────────────────────────────

    def _refresh_region_view(self):
        self.region_text.delete("1.0", tk.END)
        if not self.map_data:
            return

        md = self.map_data
        sct = md.sct
        lines: List[str] = []

        lines.append(f"Entry Point: 0x{md.entry_point:08X}")
        lines.append("")

        # ── MAP-derived regions ──
        for lr in md.load_regions:
            lines.append(f"Load Region: {lr.name}")
            lines.append(f"  Base: 0x{lr.base:08X}   Size: {fmt_bytes(lr.size)}   Max: {fmt_bytes(lr.max_size)}")
            if lr.compressed_size:
                lines.append(f"  Compressed: {fmt_bytes(lr.compressed_size)}")
            lines.append("")

            for er in lr.exec_regions:
                # Cross-reference with SCT for extra metadata
                tags = ""
                sct_er = sct.find_exec_region(er.name) if sct else None
                if sct_er:
                    attr_parts = []
                    if sct_er.uninit:
                        attr_parts.append("UNINIT — reserved, zero-init, not loaded from flash")
                    if sct_er.fixed:
                        attr_parts.append("FIXED")
                    if sct_er.empty:
                        attr_parts.append("EMPTY")
                    if sct_er.overlay:
                        attr_parts.append("OVERLAY")
                    if sct_er.align:
                        attr_parts.append(f"ALIGN {sct_er.align}")
                    if attr_parts:
                        tags = "  [" + ", ".join(attr_parts) + "]"

                lines.append(f"  Execution Region: {er.name}{tags}")
                lines.append(f"    Exec Base: 0x{er.exec_base:08X}   Load Base: 0x{er.load_base:08X}")
                lines.append(f"    Size: {fmt_bytes(er.size)}   Max: {fmt_bytes(er.max_size)}   "
                             f"Used: {er.usage_pct:.1f}%   Free: {fmt_bytes(er.free)}")
                if er.compressed_size:
                    lines.append(f"    Compressed: {fmt_bytes(er.compressed_size)}")

                # Show input sections from SCT for this exec region
                if sct_er and sct_er.input_sections:
                    lines.append(f"    SCT input sections ({len(sct_er.input_sections)}):")
                    for sec in sct_er.input_sections[:20]:  # limit to first 20
                        lines.append(f"      {sec}")
                    if len(sct_er.input_sections) > 20:
                        lines.append(f"      ... and {len(sct_er.input_sections) - 20} more")

                lines.append("")

        # ── SCT-only regions (not found in MAP) ──
        if sct:
            map_er_names = set()
            for lr in md.load_regions:
                for er in lr.exec_regions:
                    map_er_names.add(er.name.lower())

            orphan_sct_regions = []
            for sct_lr in sct.load_regions:
                for sct_er in sct_lr.exec_regions:
                    if sct_er.name.lower() not in map_er_names:
                        orphan_sct_regions.append((sct_lr.name, sct_er))

            if orphan_sct_regions:
                lines.append("─" * 60)
                lines.append("SCT-defined regions not found in MAP (may be EMPTY or unused):")
                lines.append("")
                for lr_name, sct_er in orphan_sct_regions:
                    attr = " [UNINIT]" if sct_er.uninit else ""
                    lines.append(f"  {sct_er.name}{attr}  (load region: {lr_name})")
                    lines.append(f"    Base: 0x{sct_er.base:08X}   Size: {fmt_bytes(sct_er.size)}")
                    lines.append("")

        # ── Grand totals ──
        lines.append("─" * 60)
        lines.append(f"Grand Totals:")
        lines.append(f"  Code:          {md.total_code:>10,} B  ({md.total_code / 1024:>8.1f} KB)")
        lines.append(f"  RO Data:       {md.total_ro_data:>10,} B  ({md.total_ro_data / 1024:>8.1f} KB)")
        lines.append(f"  RW Data:       {md.total_rw_data:>10,} B  ({md.total_rw_data / 1024:>8.1f} KB)")
        lines.append(f"  ZI Data:       {md.total_zi_data:>10,} B  ({md.total_zi_data / 1024:>8.1f} KB)")
        lines.append(f"  ────────────────────────────────────────")
        flash_used = md.total_code + md.total_ro_data + md.total_rw_data
        sram_used = md.total_rw_data + md.total_zi_data
        lines.append(f"  FLASH total:   {flash_used:>10,} B  ({flash_used / 1024:>8.1f} KB)")
        lines.append(f"  SRAM total:    {sram_used:>10,} B  ({sram_used / 1024:>8.1f} KB)")
        lines.append(f"  Object count:  {len(md.objects):>10}")
        lines.append(f"  Library count: {len(md.libraries):>10}")

        # Capacity details
        if md.flash_capacity:
            lines.append(f"\n  MCU Flash capacity:  {fmt_bytes(md.flash_capacity)}")
        if md.internal_sram_capacity:
            lines.append(f"  Internal SRAM alloc: {fmt_bytes(md.internal_sram_capacity)}")
        if md.ccm_capacity:
            lines.append(f"  CCM SRAM alloc:      {fmt_bytes(md.ccm_capacity)}")
        if md.external_sram_capacity:
            lines.append(f"  External SRAM alloc: {fmt_bytes(md.external_sram_capacity)}")
        if md.sram_capacity:
            lines.append(f"  Total SRAM capacity: {fmt_bytes(md.sram_capacity)}")

        self.region_text.insert("1.0", "\n".join(lines))


# ── Entry point ─────────────────────────────────────────────────────────────

def main():
    root = tk.Tk()

    # Set a ttk theme if available
    style = ttk.Style()
    available = style.theme_names()
    if "clam" in available:
        style.theme_use("clam")
    elif "vista" in available:
        style.theme_use("vista")

    map_path = sys.argv[1] if len(sys.argv) > 1 else None
    app = MapAnalyzerApp(root, map_path)
    root.mainloop()


if __name__ == "__main__":
    main()
