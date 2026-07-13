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
    sram_capacity: int = 0

    @property
    def flash_used(self) -> int:
        if self.total_rom > 0:
            return self.total_rom
        return self.total_code + self.total_ro_data + self.total_rw_data

    @property
    def sram_used(self) -> int:
        return self.total_rw_data + self.total_zi_data


# ── Parser ──────────────────────────────────────────────────────────────────

_RE_HEX = r"0x[0-9A-Fa-f]+"

def _parse_hex(s: str) -> int:
    """Parse a hex string like '0x00100000' to int."""
    return int(s.strip(), 16)


def _parse_size_kb(s: str) -> float:
    """Parse a string like '590.88kB' to float KB."""
    m = re.search(r"([\d.]+)\s*kB", s)
    return float(m.group(1)) if m else 0.0


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
    for lr in data.load_regions:
        for er in lr.exec_regions:
            # Check name first, then fall back to address range
            name_up = er.name.upper()
            if "IRAM" in name_up or "RAM" in name_up or "SRAM" in name_up:
                if er.max_size > data.sram_capacity:
                    data.sram_capacity = er.max_size
            elif "IROM" in name_up or "FLASH" in name_up or "ROM" in name_up:
                if er.max_size > data.flash_capacity:
                    data.flash_capacity = er.max_size
            elif 0x20000000 <= er.exec_base < 0x40000000:
                # SRAM address range (STM32 internal + external SRAM)
                if er.max_size > data.sram_capacity:
                    data.sram_capacity = er.max_size
            elif 0x08000000 <= er.exec_base < 0x10000000:
                # FLASH address range (STM32 internal flash)
                if er.max_size > data.flash_capacity:
                    data.flash_capacity = er.max_size

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
        if map_path and os.path.isfile(map_path):
            self._load(map_path)
        else:
            # Try default path relative to script
            script_dir = os.path.dirname(os.path.abspath(__file__))
            project_map = os.path.normpath(os.path.join(
                script_dir, "..", "MDK-ARM", "Star Gamer", "Star Gamer.map"
            ))
            if os.path.isfile(project_map):
                self._load(project_map)
            else:
                self._ask_open()

    # ── Menu ──────────────────────────────────────────────────────────

    def _build_menu(self):
        menubar = tk.Menu(self.root)
        self.root.config(menu=menubar)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Open MAP File...", command=self._ask_open, accelerator="Ctrl+O")
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.root.quit)
        menubar.add_cascade(label="File", menu=file_menu)

        self.root.bind("<Control-o>", lambda e: self._ask_open())

    # ── Toolbar ───────────────────────────────────────────────────────

    def _build_toolbar(self):
        toolbar = ttk.Frame(self.root)
        toolbar.pack(side=tk.TOP, fill=tk.X, padx=4, pady=2)

        ttk.Button(toolbar, text="Open MAP...", command=self._ask_open).pack(side=tk.LEFT, padx=2)

        self.lbl_file = ttk.Label(toolbar, text="No file loaded", foreground="gray")
        self.lbl_file.pack(side=tk.LEFT, padx=8)

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

    def _load(self, path: str):
        try:
            self.map_data = parse_map_file(path)
        except Exception as e:
            messagebox.showerror("Parse Error", f"Failed to parse MAP file:\n{e}")
            return

        self.lbl_file.config(text=os.path.basename(path), foreground="black")
        self.root.title(f"ARMCLANG MAP Analyzer — {os.path.basename(path)}")
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

        # Regions info
        region_lines = []
        for lr in md.load_regions:
            for er in lr.exec_regions:
                region_lines.append(
                    f"  {er.name}: {fmt_bytes(er.size)} / {fmt_bytes(er.max_size)} "
                    f"({er.usage_pct:.1f}%)  @ 0x{er.exec_base:08X}"
                )
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
        lines = []

        lines.append(f"Entry Point: 0x{md.entry_point:08X}")
        lines.append("")

        for lr in md.load_regions:
            lines.append(f"Load Region: {lr.name}")
            lines.append(f"  Base: 0x{lr.base:08X}   Size: {fmt_bytes(lr.size)}   Max: {fmt_bytes(lr.max_size)}")
            if lr.compressed_size:
                lines.append(f"  Compressed: {fmt_bytes(lr.compressed_size)}")
            lines.append("")

            for er in lr.exec_regions:
                lines.append(f"  Execution Region: {er.name}")
                lines.append(f"    Exec Base: 0x{er.exec_base:08X}   Load Base: 0x{er.load_base:08X}")
                lines.append(f"    Size: {fmt_bytes(er.size)}   Max: {fmt_bytes(er.max_size)}   "
                             f"Used: {er.usage_pct:.1f}%   Free: {fmt_bytes(er.free)}")
                if er.compressed_size:
                    lines.append(f"    Compressed: {fmt_bytes(er.compressed_size)}")
                lines.append("")

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
