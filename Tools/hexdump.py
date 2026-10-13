#!/usr/bin/env python3
"""
Hex Dump Tool — 读取指定文件的十六进制数据并以格式化方式输出。

CLI 用法:
    python Tools/hexdump.py <文件路径> <起始地址> <字节数>

参数:
    文件路径    : 要读取的文件路径（含空格时用引号包裹）
    起始地址    : 从文件开头的字节偏移量（支持十进制或 0x 前缀十六进制）
    字节数      : 要读取的字节数量（支持十进制或 0x 前缀十六进制）

CLI 示例:
    python Tools/hexdump.py "MDK-ARM/Star Gamer.bin" 0 256
    python Tools/hexdump.py "MDK-ARM/Star Gamer.bin" 0x100 0x80
    python Tools/hexdump.py myFont.bin 0x2000 512

GUI 用法:
    python Tools/hexdump.py --gui [文件路径]   显式启动 Tkinter 图形界面
    python Tools/hexdump.py myFont.bin         单文件参数同样进入图形界面
    python Tools/hexdump.py                    无参数直接进入图形界面（弹窗选文件）

GUI 功能:
    - 虚拟滚动浏览（只渲染可见行），支持整文件浏览或按 [起始地址, 字节数] 区间浏览
    - 单击 / 拖拽选中字节，状态栏显示绝对偏移、字节值（HEX+DEC）与 ASCII
    - Hex / ASCII 双模式查找，匹配项高亮并计数（F3 / Shift+F3 循环跳转）
    - 复制为 CLI 同格式文本、复制为 C 数组、导出当前区间为文本文件
    - 界面内显示与当前视图等价的 CLI 命令行，便于脚本化复用
"""

import os
import re
import sys

try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    from tkinter import font as tkfont
    _TK_AVAILABLE = True
except ImportError:  # headless / minimal Python build → CLI only
    _TK_AVAILABLE = False


# ══════════════════════════════════════════════════════════════════════════
#  Dump core (shared by the CLI and the GUI)
# ══════════════════════════════════════════════════════════════════════════

BYTES_PER_ROW = 16
HEX_COL_WIDTH = BYTES_PER_ROW * 3 - 1   # "XX" + " XX" * 15 → 47 chars
SEPARATOR = "-" * 79


class LoadError(Exception):
    """A file region could not be read; the message is user facing."""


def parse_int(s: str) -> int:
    """解析十进制或十六进制字符串（0x 前缀）为整数。"""
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def load_region(filepath: str, offset: int, length: int):
    """校验并读取 [offset, offset+length) 区间。

    返回 (data, file_size, truncated)。参数非法时抛出 LoadError，
    消息文本与 CLI 报错保持一致。
    """
    if not os.path.exists(filepath):
        raise LoadError(f"[ERROR] 文件不存在: {filepath}")

    if not os.path.isfile(filepath):
        raise LoadError(f"[ERROR] 路径不是文件: {filepath}")

    file_size = os.path.getsize(filepath)

    if offset < 0:
        raise LoadError(f"[ERROR] 起始地址不能为负数: {offset}")

    if length <= 0:
        raise LoadError(f"[ERROR] 字节数必须大于 0: {length}")

    if offset >= file_size:
        raise LoadError(f"[ERROR] 起始地址 0x{offset:X} ({offset}) 超出文件大小 "
                        f"0x{file_size:X} ({file_size} bytes)")

    # 截断超过文件末尾的长度
    actual_length = min(length, file_size - offset)

    with open(filepath, "rb") as f:
        f.seek(offset)
        data = f.read(actual_length)

    return data, file_size, actual_length < length


def format_row(addr: int, chunk: bytes, bytes_per_row: int = BYTES_PER_ROW) -> str:
    """格式化一行：地址 + 十六进制字节（左对齐补空格）+ ASCII。"""
    col_width = bytes_per_row * 3 - 1
    hex_bytes = " ".join(f"{b:02X}" for b in chunk)
    ascii_chars = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
    return f"  {addr:08X}  {hex_bytes:<{col_width}s}  {ascii_chars}"


def dump_text(filepath: str, base: int, data: bytes, file_size: int,
              bytes_per_row: int = BYTES_PER_ROW) -> str:
    """生成与 CLI 完全一致的文本转储（文件信息头 + 数据行 + 分隔线）。"""
    lines = [
        f"File:  {os.path.basename(filepath)}",
        f"Path:  {os.path.abspath(filepath)}",
        f"Size:  {file_size} bytes (0x{file_size:X})",
        f"Range: 0x{base:08X} — 0x{base + len(data) - 1:08X} ({len(data)} bytes)",
        SEPARATOR,
    ]

    for row_start in range(0, len(data), bytes_per_row):
        chunk = data[row_start:row_start + bytes_per_row]
        lines.append(format_row(base + row_start, chunk, bytes_per_row))

    lines.append(SEPARATOR)
    return "\n".join(lines)


def hexdump(filepath: str, offset: int, length: int) -> None:
    """读取文件指定区域并以 hexdump 格式输出到 stdout。"""

    try:
        data, file_size, truncated = load_region(filepath, offset, length)
    except LoadError as e:
        print(e)
        sys.exit(1)

    if truncated:
        print(f"[WARN] 请求 {length} 字节，但文件仅剩 {len(data)} 字节，"
              f"已截断。\n")

    print(dump_text(filepath, offset, data, file_size))


def cli_main(args) -> int:
    """命令行入口（参数不含程序名），返回进程退出码。"""
    if len(args) != 3:
        print("[ERROR] 参数数量不正确。")
        print(f"用法: python {os.path.basename(__file__)} "
              f"<文件路径> <起始地址> <字节数>")
        print(f"      python {os.path.basename(__file__)} --help  查看详细帮助")
        return 1

    filepath = args[0]

    try:
        offset = parse_int(args[1])
    except ValueError:
        print(f"[ERROR] 无效的起始地址: {args[1]}")
        return 1

    try:
        length = parse_int(args[2])
    except ValueError:
        print(f"[ERROR] 无效的字节数: {args[2]}")
        return 1

    hexdump(filepath, offset, length)
    return 0


# ══════════════════════════════════════════════════════════════════════════
#  GUI (Tkinter)
# ══════════════════════════════════════════════════════════════════════════

MAX_WHOLE_FILE = 64 * 1024 * 1024       # 超过此大小不提供整文件浏览
LARGE_FILE_WINDOW = 64 * 1024           # 大文件默认载入的窗口大小
DEFAULT_WINDOW = 4096                   # "Go to" 跳跃时的默认窗口大小
MAX_MATCHES = 20000                     # 查找结果上限（单字节模式会命中很多次）
MAX_C_ARRAY_BYTES = 64 * 1024           # "复制为 C 数组" 的字节数上限
C_ARRAY_PER_LINE = 12

# 文本行内各列的起始字符下标（与 format_row 的布局一一对应）
ADDR_COL_START = 2
HEX_COL_START = ADDR_COL_START + 8 + 2

COLOR_ADDR = "#8a8a8a"
COLOR_ASCII = "#0b6ea8"
COLOR_MATCH = "#ffe08a"
COLOR_MATCH_CUR = "#ffa94d"
COLOR_SEL = "#cfe6ff"
COLOR_CURSOR = "#3b82f6"
COLOR_HINT = "#b45309"
COLOR_OK = "#15803d"


def ascii_char(b: int) -> str:
    """矢量一个字节对应的显示字符（不可打印字符显示为 .）。"""
    return chr(b) if 32 <= b < 127 else "."


def mono_font():
    """返回一个可用的等宽字体，保证各列对齐。"""
    try:
        families = set(tkfont.families())
    except Exception:
        families = set()

    for name in ("Consolas", "Cascadia Mono", "Courier New",
                 "DejaVu Sans Mono", "monospace"):
        if name in families:
            return tkfont.Font(family=name, size=10)
    return tkfont.Font(family="TkFixedFont", size=10)


class HexDumpApp:
    """Tkinter hex viewer over a whole file or a byte range of a file."""

    def __init__(self, root, path=None):
        self.root = root
        self.root.title("Hex Dump Viewer")
        self.root.geometry("1180x760")
        self.root.minsize(880, 520)

        # ── Viewer state ──────────────────────────────────────────────
        self.file_path = None        # absolute path of the loaded file
        self.file_size = 0
        self.data = b""              # currently loaded window
        self.data_base = 0           # absolute file offset of data[0]
        self.bytes_per_row = BYTES_PER_ROW
        self.top_row = 0             # first rendered row inside the window
        self.matches = []            # match offsets, relative to data
        self.match_index = -1
        self.needle = b""
        self.matches_capped = False
        self.highlight = None        # (absolute offset, length) of current match
        self.sel_anchor = None       # absolute offset where the drag started
        self.sel_cursor = None       # absolute offset of the drag cursor
        self._rendered_rows = 0
        self._last_vis_rows = 30

        # ── Build UI (top → bottom) ───────────────────────────────────
        self._build_menu()
        self._build_source_bar()
        self._build_range_bar()
        self._build_find_bar()
        self._build_view()
        self._build_status_bar()
        self._bind_keys()

        # ── Initial load ──────────────────────────────────────────────
        if path:
            if not self.open_path(path):
                self.root.after(120, self._ask_open)
        else:
            self.root.after(120, self._ask_open)

    # ── Menu ──────────────────────────────────────────────────────────

    def _build_menu(self):
        menubar = tk.Menu(self.root)
        self.root.config(menu=menubar)

        file_menu = tk.Menu(menubar, tearoff=0)
        file_menu.add_command(label="Open...", accelerator="Ctrl+O", command=self._ask_open)
        file_menu.add_command(label="Reload", accelerator="F5", command=self._reload)
        file_menu.add_command(label="Whole File", command=self._show_whole_file)
        file_menu.add_separator()
        file_menu.add_command(label="Save View as Text...", accelerator="Ctrl+S",
                              command=self._save_view_text)
        file_menu.add_command(label="Save Selection as Text...",
                              command=self._save_selection_text)
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.root.destroy)
        menubar.add_cascade(label="File", menu=file_menu)

        edit_menu = tk.Menu(menubar, tearoff=0)
        edit_menu.add_command(label="Copy Hex Text", accelerator="Ctrl+C",
                              command=self._copy_hex_text)
        edit_menu.add_command(label="Copy Hex Bytes", command=self._copy_hex_bytes)
        edit_menu.add_command(label="Copy ASCII", command=self._copy_ascii)
        edit_menu.add_command(label="Copy as C Array", command=self._copy_c_array)
        menubar.add_cascade(label="Edit", menu=edit_menu)

        view_menu = tk.Menu(menubar, tearoff=0)
        self.bpr_var = tk.IntVar(value=BYTES_PER_ROW)
        for n in (8, 16, 32):
            view_menu.add_radiobutton(label=f"{n} bytes / row", variable=self.bpr_var,
                                      value=n, command=lambda v=n: self._set_bpr(v))
        view_menu.add_separator()
        view_menu.add_command(label="Go to Start", accelerator="Ctrl+Home",
                              command=lambda: self._goto_offset(0))
        view_menu.add_command(label="Go to End", accelerator="Ctrl+End",
                              command=self._goto_end)
        menubar.add_cascade(label="View", menu=view_menu)

        help_menu = tk.Menu(menubar, tearoff=0)
        help_menu.add_command(label="CLI Help", command=self._show_cli_help)
        help_menu.add_command(label="About", command=self._about)
        menubar.add_cascade(label="Help", menu=help_menu)

    # ── Toolbar: source / range / find ────────────────────────────────

    def _build_source_bar(self):
        bar = ttk.Frame(self.root)
        bar.pack(side=tk.TOP, fill=tk.X, padx=6, pady=(6, 0))

        ttk.Button(bar, text="Open...", command=self._ask_open).pack(side=tk.LEFT)
        ttk.Button(bar, text="Reload", command=self._reload).pack(side=tk.LEFT, padx=(4, 0))
        ttk.Button(bar, text="Whole File", command=self._show_whole_file).pack(side=tk.LEFT, padx=(4, 0))

        self.lbl_file = ttk.Label(bar, text="No file loaded", foreground="gray")
        self.lbl_file.pack(side=tk.LEFT, padx=10)

    def _build_range_bar(self):
        bar = ttk.LabelFrame(self.root, text="View Range", padding=(8, 4))
        bar.pack(side=tk.TOP, fill=tk.X, padx=6, pady=(6, 0))

        ttk.Label(bar, text="Offset:").pack(side=tk.LEFT)
        self.ent_offset = ttk.Entry(bar, width=14)
        self.ent_offset.pack(side=tk.LEFT, padx=(4, 0))
        self.ent_offset.bind("<Return>", lambda e: self._apply_range())

        ttk.Label(bar, text="Length:").pack(side=tk.LEFT, padx=(8, 0))
        self.ent_length = ttk.Entry(bar, width=12)
        self.ent_length.pack(side=tk.LEFT, padx=(4, 0))
        self.ent_length.bind("<Return>", lambda e: self._apply_range())

        ttk.Button(bar, text="Show Range", command=self._apply_range).pack(side=tk.LEFT, padx=6)
        ttk.Label(bar, text="(decimal or 0x-prefixed hex)",
                  foreground="gray").pack(side=tk.LEFT)

        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)

        ttk.Label(bar, text="Go to:").pack(side=tk.LEFT)
        self.ent_goto = ttk.Entry(bar, width=14)
        self.ent_goto.pack(side=tk.LEFT, padx=(4, 0))
        self.ent_goto.bind("<Return>", lambda e: self._go_to_entry())
        ttk.Button(bar, text="Go", command=self._go_to_entry).pack(side=tk.LEFT, padx=4)

        ttk.Separator(bar, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)

        ttk.Label(bar, text="Bytes / row:").pack(side=tk.LEFT)
        self.cmb_bpr = ttk.Combobox(bar, values=("8", "16", "32"), width=4, state="readonly")
        self.cmb_bpr.set(str(BYTES_PER_ROW))
        self.cmb_bpr.pack(side=tk.LEFT, padx=4)
        self.cmb_bpr.bind("<<ComboboxSelected>>",
                          lambda e: self._set_bpr(int(self.cmb_bpr.get())))

    def _build_find_bar(self):
        bar = ttk.LabelFrame(self.root, text="Find", padding=(8, 4))
        bar.pack(side=tk.TOP, fill=tk.X, padx=6, pady=(6, 0))

        ttk.Label(bar, text="Pattern:").pack(side=tk.LEFT)
        self.ent_find = ttk.Entry(bar, width=32)
        self.ent_find.pack(side=tk.LEFT, padx=(4, 0))
        self.ent_find.bind("<Return>", lambda e: self._do_find(True))
        self.ent_find.bind("<Shift-Return>", lambda e: self._do_find(False))

        self.cmb_mode = ttk.Combobox(bar, values=("ASCII", "Hex"), width=7, state="readonly")
        self.cmb_mode.set("ASCII")
        self.cmb_mode.pack(side=tk.LEFT, padx=6)

        ttk.Button(bar, text="Find Next (F3)",
                   command=lambda: self._do_find(True)).pack(side=tk.LEFT, padx=(2, 0))
        ttk.Button(bar, text="Find Prev",
                   command=lambda: self._do_find(False)).pack(side=tk.LEFT, padx=4)

        ttk.Label(bar, text="Hex mode example: DE AD BE EF / 0xDEADBEEF",
                  foreground="gray").pack(side=tk.LEFT, padx=8)

    # ── Hex view ──────────────────────────────────────────────────────

    def _build_view(self):
        view = ttk.Frame(self.root)
        view.pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=6, pady=(6, 2))

        self.font = mono_font()

        # width/height = 1 keeps the widget from dictating the layout: the
        # grid cell (and therefore the row count) follows the window size.
        self.text = tk.Text(view, width=1, height=1, wrap=tk.NONE,
                            font=self.font, state="disabled", cursor="xterm",
                            background="white", foreground="black",
                            borderwidth=1, relief="solid", padx=4, pady=2)
        self.vsb = ttk.Scrollbar(view, orient=tk.VERTICAL, command=self._on_scrollbar)

        self.text.grid(row=0, column=0, sticky="nsew")
        self.vsb.grid(row=0, column=1, sticky="ns")
        view.grid_rowconfigure(0, weight=1)
        view.grid_columnconfigure(0, weight=1)

        # Tag creation order defines the overlay priority.
        self.text.tag_configure("addr", foreground=COLOR_ADDR)
        self.text.tag_configure("ascii", foreground=COLOR_ASCII)
        self.text.tag_configure("match", background=COLOR_MATCH)
        self.text.tag_configure("match_cur", background=COLOR_MATCH_CUR)
        self.text.tag_configure("sel", background=COLOR_SEL)
        self.text.tag_configure("cursor", background=COLOR_CURSOR, foreground="white")

        self.text.bind("<Configure>", self._on_text_configure)
        self.text.bind("<MouseWheel>", self._on_wheel)
        self.text.bind("<Button-4>", self._on_wheel)
        self.text.bind("<Button-5>", self._on_wheel)
        self.text.bind("<Button-1>", self._on_button1)
        self.text.bind("<B1-Motion>", self._on_drag)
        self.text.bind("<ButtonRelease-1>", lambda e: self._update_status())
        self.text.bind("<Prior>", self._on_prior)
        self.text.bind("<Next>", self._on_next)
        self.text.bind("<Up>", self._on_up)
        self.text.bind("<Down>", self._on_down)
        self.text.bind("<Left>", self._on_left)
        self.text.bind("<Right>", self._on_right)
        self.text.bind("<Home>", self._on_home)
        self.text.bind("<End>", self._on_end)
        self.text.bind("<Control-Home>", self._on_home)
        self.text.bind("<Control-End>", self._on_end)
        self.text.bind("<Control-c>", self._on_ctrl_c)

    def _build_status_bar(self):
        bar = ttk.Frame(self.root)
        bar.pack(side=tk.BOTTOM, fill=tk.X, padx=6, pady=(0, 6))

        row1 = ttk.Frame(bar)
        row1.pack(fill=tk.X)
        self.lbl_info = ttk.Label(row1, text="No file loaded", foreground="gray")
        self.lbl_info.pack(side=tk.LEFT)
        self.lbl_matches = ttk.Label(row1, text="", foreground="#1f6feb")
        self.lbl_matches.pack(side=tk.RIGHT)

        row2 = ttk.Frame(bar)
        row2.pack(fill=tk.X)
        self.lbl_cursor = ttk.Label(row2, text="—", font=self.font)
        self.lbl_cursor.pack(side=tk.LEFT)
        ttk.Button(row2, text="Copy CLI", width=10,
                   command=self._copy_cli_command).pack(side=tk.RIGHT)
        self.lbl_cli = ttk.Label(row2, text="", font=self.font, foreground="#4b5563")
        self.lbl_cli.pack(side=tk.RIGHT, padx=8)

        self.lbl_hint = ttk.Label(bar, text="", foreground=COLOR_HINT,
                                  wraplength=1120, justify=tk.LEFT)
        self.lbl_hint.pack(fill=tk.X, pady=(2, 0))

    def _bind_keys(self):
        self.root.bind("<Control-o>", lambda e: self._ask_open())
        self.root.bind("<Control-s>", lambda e: self._save_view_text())
        self.root.bind("<F5>", lambda e: self._reload())
        self.root.bind("<F3>", lambda e: self._do_find(True))
        self.root.bind("<Shift-F3>", lambda e: self._do_find(False))
        self.root.bind("<Control-f>", lambda e: self._focus_find())
        self.root.bind("<Control-g>", lambda e: self._focus_entry(self.ent_goto))

    # ── Loading ───────────────────────────────────────────────────────

    def _ask_open(self):
        path = filedialog.askopenfilename(
            title="Select a file to dump",
            filetypes=[("Binary / firmware", "*.bin *.hex *.elf *.axf *.dat *.dump"),
                       ("All files", "*.*")],
        )
        if path:
            self.open_path(path)

    def open_path(self, path, whole_file=True):
        """Open <path> and show it whole (or a first window if it is huge)."""
        path = os.path.abspath(path)

        if not os.path.isfile(path):
            messagebox.showerror("Open File", f"[ERROR] 文件不存在或不是文件: {path}")
            return False

        size = os.path.getsize(path)
        if size == 0:
            messagebox.showerror("Open File", f"[ERROR] 文件为空，无法转储: {path}")
            return False

        self.file_path = path
        self.file_size = size
        self.root.title(f"Hex Dump Viewer — {os.path.basename(path)}")
        self.lbl_file.config(text=f"{os.path.basename(path)}   ({size:,} bytes)",
                             foreground="black")

        if whole_file and size <= MAX_WHOLE_FILE:
            return self._load_view(0, size)

        note = (f"文件 {size:,} 字节超过整文件浏览上限 {MAX_WHOLE_FILE:,} 字节，"
                f"已载入前 {min(LARGE_FILE_WINDOW, size):,} 字节；"
                f"请用 Offset / Length 或 Go to 浏览其他区域。")
        return self._load_view(0, min(LARGE_FILE_WINDOW, size), note)

    def _load_view(self, offset, length, note=""):
        """Read [offset, offset+length) of the current file into the view."""
        try:
            data, file_size, truncated = load_region(self.file_path, offset, length)
        except LoadError as e:
            messagebox.showerror("Load Range", str(e))
            return False

        self.data = data
        self.data_base = offset
        self.file_size = file_size
        self.top_row = 0
        self.matches = []
        self.match_index = -1
        self.needle = b""
        self.matches_capped = False
        self.highlight = None
        self.sel_anchor = None
        self.sel_cursor = None

        self.ent_offset.delete(0, tk.END)
        self.ent_offset.insert(0, f"0x{offset:X}")
        self.ent_length.delete(0, tk.END)
        self.ent_length.insert(0, f"{length}")

        if truncated:
            hint = (f"[WARN] 请求 {length} 字节，但文件仅剩 {len(data)} 字节，已截断。")
            if note:
                hint += "  " + note
        else:
            hint = note
        self._set_hint(hint)

        self._render()
        self._update_status()
        self.text.focus_set()
        return True

    def _reload(self):
        if not self._require_file():
            return
        self._load_view(self.data_base, max(1, len(self.data)))

    def _show_whole_file(self):
        if not self._require_file():
            return
        if self.file_size > MAX_WHOLE_FILE:
            messagebox.showinfo(
                "Whole File",
                f"文件 {self.file_size:,} 字节超过整文件浏览上限 "
                f"{MAX_WHOLE_FILE:,} 字节。\n请使用 Offset / Length 浏览区间。")
            return
        self._load_view(0, self.file_size)

    def _apply_range(self):
        if not self._require_file():
            return
        try:
            offset = parse_int(self.ent_offset.get())
        except ValueError:
            messagebox.showerror("Load Range", f"[ERROR] 无效的起始地址: {self.ent_offset.get()}")
            return
        try:
            length = parse_int(self.ent_length.get())
        except ValueError:
            messagebox.showerror("Load Range", f"[ERROR] 无效的字节数: {self.ent_length.get()}")
            return
        self._load_view(offset, length)

    def _go_to_entry(self):
        if not self._require_file():
            return
        raw = self.ent_goto.get().strip()
        if not raw:
            return
        try:
            off = parse_int(raw)
        except ValueError:
            messagebox.showerror("Go to", f"[ERROR] 无效的地址: {raw}")
            return
        if off < 0 or off >= self.file_size:
            messagebox.showerror(
                "Go to",
                f"[ERROR] 地址 0x{off:X} 超出文件范围 "
                f"(0x00000000 — 0x{self.file_size - 1:08X})")
            return

        if not (self.data_base <= off < self.data_base + len(self.data)):
            # Outside the loaded window → load a window that starts at the target.
            window = min(max(len(self.data), DEFAULT_WINDOW), self.file_size - off)
            if not self._load_view(off, window):
                return
        # Put the cursor on the target either way, so the status bar shows it.
        self.sel_anchor = self.sel_cursor = off
        self._goto_offset(off, recenter=True)

    # ── Rendering ─────────────────────────────────────────────────────

    def _total_rows(self):
        if not self.data:
            return 0
        return (len(self.data) + self.bytes_per_row - 1) // self.bytes_per_row

    def _ascii_col(self):
        """Character column where the ASCII part of a row starts."""
        return HEX_COL_START + (self.bytes_per_row * 3 - 1) + 2

    def _visible_rows(self):
        """How many rows fit into the text widget (falls back before mapping)."""
        height = self.text.winfo_height()
        linespace = max(1, self.font.metrics("linespace"))
        if height <= 1:
            return self._last_vis_rows
        self._last_vis_rows = max(1, (height - 8) // linespace)
        return self._last_vis_rows

    def _on_text_configure(self, event=None):
        if self.data and self._visible_rows() != self._rendered_rows:
            self._render()

    def _render(self):
        bpr = self.bytes_per_row
        total = self._total_rows()
        vis = self._visible_rows()
        self._rendered_rows = vis

        self.text.configure(state="normal")
        self.text.delete("1.0", tk.END)

        if total == 0:
            self.text.configure(state="disabled")
            self.vsb.set(0.0, 1.0)
            return

        # Keep the last page anchored to the end of the data, like a normal
        # scrollbar: the view stops moving once the tail is on screen.
        max_top = max(0, total - vis)
        self.top_row = max(0, min(self.top_row, max_top))
        first = self.top_row
        last = min(total, first + vis)

        lines = []
        for row in range(first, last):
            start = row * bpr
            lines.append(format_row(self.data_base + start,
                                    self.data[start:start + bpr], bpr))
        self.text.insert("1.0", "\n".join(lines))

        # Column colouring
        for row in range(first, last):
            li = row - first + 1
            self.text.tag_add("addr", f"{li}.{ADDR_COL_START}", f"{li}.{HEX_COL_START - 2}")
            self.text.tag_add("ascii", f"{li}.{self._ascii_col()}", f"{li}.end")

        # Search matches (all visible ones) and the current match
        view_start = self.data_base + first * bpr
        view_end = self.data_base + last * bpr
        if self.matches and self.needle:
            for m in self.matches:
                m_abs = self.data_base + m
                if view_start <= m_abs < view_end:
                    self._tag_range("match", m_abs, len(self.needle))
        if self.highlight:
            self._tag_range("match_cur", self.highlight[0], self.highlight[1])

        # Selection (blue) and the moving cursor byte
        sel = self._sel_range()
        if sel:
            if sel[1] > sel[0]:
                self._tag_range("sel", sel[0], sel[1] - sel[0] + 1)
            self._tag_range("cursor", sel[1], 1)

        self.text.configure(state="disabled")
        self.text.yview_moveto(0.0)
        self.vsb.set(first / total, last / total)

    def _tag_range(self, tag, abs_start, length):
        """Tag the hex and ASCII characters covering <length> bytes at <abs_start>."""
        if not self.data or length <= 0:
            return

        bpr = self.bytes_per_row
        first_row = self.top_row
        last_row = min(self._total_rows(), first_row + self._rendered_rows)

        rel_start = abs_start - self.data_base
        rel_end = rel_start + length - 1
        if rel_end < 0 or rel_start >= len(self.data):
            return
        rel_start = max(0, rel_start)
        rel_end = min(rel_end, len(self.data) - 1)

        asc = self._ascii_col()
        row = rel_start // bpr
        while row <= rel_end // bpr:
            if first_row <= row < last_row:      # only rows that are on screen
                row_start = row * bpr
                lo_bi = max(rel_start, row_start) - row_start
                hi_bi = min(rel_end, row_start + bpr - 1) - row_start
                li = row - first_row + 1
                self.text.tag_add(tag, f"{li}.{HEX_COL_START + lo_bi * 3}",
                                  f"{li}.{HEX_COL_START + hi_bi * 3 + 2}")
                self.text.tag_add(tag, f"{li}.{asc + lo_bi}", f"{li}.{asc + hi_bi + 1}")
            row += 1

    def _set_bpr(self, n):
        n = int(n)
        if n == self.bytes_per_row:
            return
        old = self.bytes_per_row
        self.bytes_per_row = n
        self.bpr_var.set(n)
        self.cmb_bpr.set(str(n))
        if self.data:
            cur = self.sel_cursor if self.sel_cursor is not None \
                else self.data_base + self.top_row * old
            self._ensure_visible(cur, recenter=True)
        self._render()
        self._update_status()

    # ── Scrolling / navigation ────────────────────────────────────────

    def _scroll_rows(self, delta):
        if not self.data:
            return
        total = self._total_rows()
        max_top = max(0, total - self._visible_rows())
        self.top_row = max(0, min(self.top_row + delta, max_top))
        self._render()
        self._update_status()

    def _page(self, direction):
        self._scroll_rows(direction * max(1, self._visible_rows() - 1))

    def _on_scrollbar(self, *args):
        """Drive the virtual row model from the scrollbar."""
        if not self.data:
            return
        total = self._total_rows()
        vis = self._visible_rows()
        if args[0] == "moveto":
            self.top_row = int(float(args[1]) * total)
        elif args[0] == "scroll":
            amount = int(args[1])
            if args[2] == "pages":
                self._page(amount)
                return
            self._scroll_rows(amount)
            return
        self.top_row = max(0, min(self.top_row, max(0, total - vis)))
        self._render()
        self._update_status()

    def _on_wheel(self, event):
        num = getattr(event, "num", None)
        if num == 4:
            self._scroll_rows(-3)
        elif num == 5:
            self._scroll_rows(3)
        elif getattr(event, "delta", 0):
            self._scroll_rows(-3 if event.delta > 0 else 3)
        return "break"

    def _ensure_visible(self, off, recenter=False):
        if not self.data:
            return
        rel = max(0, min(off - self.data_base, len(self.data) - 1))
        row = rel // self.bytes_per_row
        vis = self._visible_rows()
        if recenter or row < self.top_row or row >= self.top_row + vis:
            self.top_row = max(0, row - vis // 2)

    def _goto_offset(self, off, recenter=True):
        if not self.data:
            return
        off = max(self.data_base, min(off, self.data_base + len(self.data) - 1))
        self._ensure_visible(off, recenter=recenter)
        self._render()
        self._update_status()

    def _goto_end(self):
        if not self.data:
            return
        self.top_row = max(0, self._total_rows() - self._visible_rows())
        self._render()
        self._update_status()

    def _move_cursor(self, delta):
        if not self.data:
            return
        cur = self.sel_cursor if self.sel_cursor is not None else self.data_base
        cur = max(self.data_base, min(cur + delta, self.data_base + len(self.data) - 1))
        self.sel_anchor = self.sel_cursor = cur
        self._ensure_visible(cur)
        self._render()
        self._update_status()

    def _focus_entry(self, entry):
        entry.focus_set()
        entry.select_range(0, tk.END)
        return "break"

    def _focus_find(self):
        self._focus_entry(self.ent_find)

    # ── Key handlers (return "break" to keep Tk's own Text bindings out) ──

    def _on_prior(self, event):
        self._page(-1)
        return "break"

    def _on_next(self, event):
        self._page(1)
        return "break"

    def _on_up(self, event):
        self._scroll_rows(-1)
        return "break"

    def _on_down(self, event):
        self._scroll_rows(1)
        return "break"

    def _on_left(self, event):
        self._move_cursor(-1)
        return "break"

    def _on_right(self, event):
        self._move_cursor(1)
        return "break"

    def _on_home(self, event):
        self._goto_offset(0)
        return "break"

    def _on_end(self, event):
        self._goto_end()
        return "break"

    def _on_ctrl_c(self, event):
        self._copy_hex_text()
        return "break"

    # ── Byte selection ────────────────────────────────────────────────

    def _offset_at(self, event):
        """Absolute file offset under a mouse event, or None on blank space."""
        if not self.data:
            return None
        try:
            line_s, col_s = self.text.index(f"@{event.x},{event.y}").split(".")
            return self._offset_at_cell(int(line_s), int(col_s))
        except (ValueError, tk.TclError):
            return None

    def _offset_at_cell(self, line_index, col):
        """Map a widget (line, column) cell to an absolute file offset."""
        if not self.data:
            return None

        bpr = self.bytes_per_row
        row = self.top_row + (line_index - 1)
        row_start = row * bpr
        if row_start >= len(self.data):
            return None
        row_len = min(bpr, len(self.data) - row_start)

        ascii_start = self._ascii_col()
        if col >= ascii_start:
            bi = col - ascii_start
        elif col >= HEX_COL_START:
            bi = (col - HEX_COL_START) // 3
        else:
            return None            # address column

        if bi < 0 or bi >= row_len:
            return None
        return self.data_base + row_start + bi

    def _sel_range(self):
        """Inclusive (low, high) absolute offsets of the selection, or None."""
        if self.sel_anchor is None or self.sel_cursor is None:
            return None
        return (min(self.sel_anchor, self.sel_cursor),
                max(self.sel_anchor, self.sel_cursor))

    def _selected_bytes(self):
        """Bytes covered by the selection (or the visible window when empty)."""
        if not self.data:
            return None, 0, b""

        sel = self._sel_range()
        if sel:
            lo, hi = sel
        else:
            lo = self.data_base + self.top_row * self.bytes_per_row
            hi = min(self.data_base + len(self.data) - 1,
                     lo + self._visible_rows() * self.bytes_per_row - 1)
        data = self.data[lo - self.data_base:hi - self.data_base + 1]
        return lo, hi, data

    def _on_button1(self, event):
        off = self._offset_at(event)
        self.text.focus_set()
        if off is None:
            return "break"
        self.sel_anchor = off
        self.sel_cursor = off
        self._render()
        self._update_status()
        return "break"

    def _on_drag(self, event):
        if self.sel_anchor is None:
            return "break"
        off = self._offset_at(event)
        if off is None or off == self.sel_cursor:
            return "break"
        self.sel_cursor = off
        self._render()
        self._update_status()
        return "break"

    # ── Find ──────────────────────────────────────────────────────────

    def _parse_needle(self):
        text = self.ent_find.get().strip()
        if not text:
            self._set_hint("请先输入查找内容（ASCII 文本或十六进制字节）。")
            return None

        if self.cmb_mode.get().lower().startswith("hex"):
            cleaned = re.sub(r"0[xX]", "", text)
            cleaned = re.sub(r"[^0-9a-fA-F]", "", cleaned)
            if not cleaned or len(cleaned) % 2:
                messagebox.showerror(
                    "Find",
                    f"Invalid hex pattern: {text}\n"
                    f"Expect bytes like 'DE AD BE EF', '0xDEADBEEF' or 'deadbeef'.")
                return None
            return bytes.fromhex(cleaned)

        try:
            return text.encode("latin-1")
        except UnicodeEncodeError:
            return text.encode("utf-8")

    def _find_all(self, needle):
        found = []
        start = 0
        while len(found) < MAX_MATCHES:
            i = self.data.find(needle, start)
            if i < 0:
                break
            found.append(i)
            start = i + 1              # +1 keeps overlapping matches visible
        # Only claim "capped" when there really is at least one more match.
        self.matches_capped = (len(found) >= MAX_MATCHES
                               and self.data.find(needle, found[-1] + 1) >= 0)
        return found

    def _next_match_index(self, forward):
        n = len(self.matches)
        if n == 0:
            return -1
        if self.match_index >= 0:
            return (self.match_index + (1 if forward else -1)) % n

        anchor = (self.sel_cursor if self.sel_cursor is not None
                  else self.data_base) - self.data_base
        if forward:
            for i, m in enumerate(self.matches):
                if m >= anchor:
                    return i
            return 0
        for i in range(n - 1, -1, -1):
            if self.matches[i] <= anchor:
                return i
        return n - 1

    def _do_find(self, forward=True):
        if not self._require_file():
            return
        needle = self._parse_needle()
        if needle is None:
            return

        if needle != self.needle:
            self.needle = needle
            self.matches = self._find_all(needle)
            self.match_index = -1

        if not self.matches:
            self.highlight = None
            self._set_hint(f"未找到匹配项（搜索范围: 当前载入的 "
                           f"{len(self.data):,} 字节视图）。", COLOR_HINT)
            self._render()
            self._update_status()
            return

        self.match_index = self._next_match_index(forward)
        match_abs = self.data_base + self.matches[self.match_index]
        self.highlight = (match_abs, len(needle))
        self.sel_anchor = self.sel_cursor = match_abs
        self._goto_offset(match_abs, recenter=True)
        self._set_hint(f"匹配 {self.match_index + 1} / {len(self.matches)}: "
                       f"0x{match_abs:08X}", COLOR_OK)

    # ── Clipboard / export ────────────────────────────────────────────

    def _copy(self, text, what):
        if not text:
            self._set_hint("没有可复制的内容。", COLOR_HINT)
            return
        self.root.clipboard_clear()
        self.root.clipboard_append(text)
        self._set_hint(f"已复制{what}到剪贴板（{len(text):,} 字符）。", COLOR_OK)

    def _copy_cli_command(self):
        self._copy(self._cli_command(), "CLI 命令行")

    def _copy_hex_text(self):
        if not self._require_file():
            return
        lo, _hi, data = self._selected_bytes()
        self._copy(dump_text(self.file_path, lo, data, self.file_size,
                             self.bytes_per_row), "HEX 文本")

    def _copy_hex_bytes(self):
        if not self._require_file():
            return
        _lo, _hi, data = self._selected_bytes()
        self._copy(" ".join(f"{b:02X}" for b in data), "字节序列")

    def _copy_ascii(self):
        if not self._require_file():
            return
        _lo, _hi, data = self._selected_bytes()
        self._copy("".join(ascii_char(b) for b in data), "ASCII 文本")

    def _copy_c_array(self):
        if not self._require_file():
            return
        lo, hi, data = self._selected_bytes()
        if len(data) > MAX_C_ARRAY_BYTES:
            messagebox.showinfo(
                "Copy as C Array",
                f"选择的 {len(data):,} 字节超过上限 {MAX_C_ARRAY_BYTES:,} 字节，"
                f"请缩小选择范围。")
            return

        lines = [f"/* {os.path.basename(self.file_path)}  "
                 f"0x{lo:08X} — 0x{hi:08X}  ({len(data)} bytes) */",
                 "const unsigned char data[] = {"]
        for i in range(0, len(data), C_ARRAY_PER_LINE):
            chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + C_ARRAY_PER_LINE])
            lines.append("    " + chunk + ",")
        lines.append("};")
        self._copy("\n".join(lines), "C 数组")

    def _save_view_text(self):
        if not self._require_file():
            return
        name = os.path.basename(self.file_path)
        self._save_text(self.data_base, self.data, f"{name}_{self.data_base:08X}.txt")

    def _save_selection_text(self):
        if not self._require_file():
            return
        sel = self._sel_range()
        if not sel:
            self._set_hint("请先选中要保存的字节范围。", COLOR_HINT)
            return
        lo, hi, data = self._selected_bytes()
        name = os.path.basename(self.file_path)
        self._save_text(lo, data, f"{name}_{lo:08X}-{hi:08X}.txt")

    def _save_text(self, base, data, initial_name):
        if not data:
            self._set_hint("没有可保存的内容。", COLOR_HINT)
            return
        path = filedialog.asksaveasfilename(
            title="Save hex dump as text",
            defaultextension=".txt",
            initialfile=initial_name,
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(dump_text(self.file_path, base, data, self.file_size,
                                  self.bytes_per_row) + "\n")
        except OSError as e:
            messagebox.showerror("Save Error", f"Failed to write {path}:\n{e}")
            return
        self._set_hint(f"已保存到 {path}", COLOR_OK)

    # ── Status / hints ────────────────────────────────────────────────

    def _require_file(self):
        if self.file_path and self.data:
            return True
        self._set_hint("请先打开一个文件 (Ctrl+O)。", COLOR_HINT)
        return False

    def _set_hint(self, text, color=COLOR_HINT):
        self.lbl_hint.config(text=text or "", foreground=color)

    def _cli_command(self):
        if not self.file_path:
            return ""
        return (f'python {os.path.join("Tools", "hexdump.py")} '
                f'"{self.file_path}" 0x{self.data_base:X} {len(self.data)}')

    def _update_status(self):
        if not self.file_path:
            self.lbl_info.config(text="No file loaded", foreground="gray")
            self.lbl_cursor.config(text="—")
            self.lbl_matches.config(text="")
            self.lbl_cli.config(text="")
            return

        end = self.data_base + len(self.data) - 1
        mode = "whole file" if (self.data_base == 0 and len(self.data) == self.file_size) \
            else "range"
        self.lbl_info.config(
            text=f"{os.path.basename(self.file_path)}   "
                 f"size {self.file_size:,} B (0x{self.file_size:X})   "
                 f"{mode} 0x{self.data_base:08X} — 0x{end:08X} "
                 f"({len(self.data):,} B)",
            foreground="black")

        sel = self._sel_range()
        if sel is None:
            cursor = "—"
        elif sel[0] == sel[1]:
            b = self.data[sel[0] - self.data_base]
            cursor = (f"0x{sel[0]:08X}   byte 0x{b:02X} ({b})   "
                      f"ASCII '{ascii_char(b)}'")
        else:
            cursor = (f"sel 0x{sel[0]:08X} — 0x{sel[1]:08X} "
                      f"({sel[1] - sel[0] + 1:,} bytes)")
        self.lbl_cursor.config(text=cursor)

        if self.needle:
            cap = "+" if self.matches_capped else ""
            idx = f"{self.match_index + 1}/{len(self.matches)}" if self.matches else "0/0"
            self.lbl_matches.config(text=f"Matches: {len(self.matches):,}{cap}   [{idx}]")
        else:
            self.lbl_matches.config(text="")

        self.lbl_cli.config(text="CLI:  " + self._cli_command())

    def _show_cli_help(self):
        win = tk.Toplevel(self.root)
        win.title("CLI Help")
        win.geometry("820x520")
        text = tk.Text(win, wrap=tk.NONE, font=("Consolas", 10))
        vsb = ttk.Scrollbar(win, orient=tk.VERTICAL, command=text.yview)
        text.configure(yscrollcommand=vsb.set)
        text.insert("1.0", __doc__)
        text.configure(state="disabled")
        text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        ttk.Button(win, text="Close", command=win.destroy).pack(side=tk.BOTTOM)

    def _about(self):
        messagebox.showinfo(
            "About",
            "Hex Dump Viewer\n\n"
            "同一套转储内核驱动的 CLI 与 GUI 两种前端：\n"
            "  - CLI 与 Tools/hexdump.c 输出格式一致\n"
            "  - GUI 支持虚拟滚动、字节选择、查找、导出\n\n"
            "快捷键: Ctrl+O 打开, Ctrl+S 保存, Ctrl+F 查找, F3/Shift+F3 查找下一个/上一个,\n"
            "        Ctrl+G 定位, F5 重新载入, Home/End 首尾, PgUp/PgDn 翻页")


# ══════════════════════════════════════════════════════════════════════════
#  Entry point
# ══════════════════════════════════════════════════════════════════════════

def _is_number(s: str) -> bool:
    """True if <s> parses as a decimal / 0x-prefixed hex number."""
    try:
        parse_int(s)
        return True
    except ValueError:
        return False


def launch_gui(path=None):
    root = tk.Tk()

    style = ttk.Style()
    available = style.theme_names()
    if "clam" in available:
        style.theme_use("clam")
    elif "vista" in available:
        style.theme_use("vista")

    HexDumpApp(root, path)
    root.mainloop()


def main(argv=None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)

    if len(args) == 1 and args[0] in ("-h", "--help", "/?"):
        print(__doc__)
        return 0

    gui_flag = False
    positional = []
    for a in args:
        if a in ("-g", "--gui"):
            gui_flag = True
        elif a.startswith("-") and not os.path.exists(a) and not _is_number(a):
            # Anything else that looks like a flag is a mistake; numbers such as
            # -1 are left to the range checks in load_region (same text as before).
            print(f"[ERROR] 未知选项: {a}")
            print(f"      python {os.path.basename(__file__)} --help  查看详细帮助")
            return 1
        else:
            positional.append(a)

    # 0 / 1 positional arguments (or --gui) → GUI; 2 or more → CLI semantics
    if gui_flag or len(positional) <= 1:
        if not _TK_AVAILABLE:
            print("[ERROR] 当前 Python 环境不含 tkinter，无法启动图形界面。")
            print(f"用法: python {os.path.basename(__file__)} "
                  f"<文件路径> <起始地址> <字节数>")
            return 2
        launch_gui(positional[0] if positional else None)
        return 0

    return cli_main(positional)


if __name__ == "__main__":
    sys.exit(main())