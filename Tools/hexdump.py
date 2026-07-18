#!/usr/bin/env python3
"""
Hex Dump Tool — 读取指定文件的十六进制数据并以格式化方式输出。

Usage:
    python Tools/hexdump.py <文件路径> <起始地址> <字节数>

参数:
    文件路径    : 要读取的文件路径（含空格时用引号包裹）
    起始地址    : 从文件开头的字节偏移量（支持十进制或 0x 前缀十六进制）
    字节数      : 要读取的字节数量（支持十进制或 0x 前缀十六进制）

示例:
    python Tools/hexdump.py "MDK-ARM/Star Gamer.bin" 0 256
    python Tools/hexdump.py "MDK-ARM/Star Gamer.bin" 0x100 0x80
    python Tools/hexdump.py myFont.bin 0x2000 512
"""

import sys
import os


def parse_int(s: str) -> int:
    """解析十进制或十六进制字符串（0x 前缀）为整数。"""
    s = s.strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def hexdump(filepath: str, offset: int, length: int) -> None:
    """读取文件指定区域并以 hexdump 格式输出到 stdout。"""

    if not os.path.exists(filepath):
        print(f"[ERROR] 文件不存在: {filepath}")
        sys.exit(1)

    if not os.path.isfile(filepath):
        print(f"[ERROR] 路径不是文件: {filepath}")
        sys.exit(1)

    file_size = os.path.getsize(filepath)

    if offset < 0:
        print(f"[ERROR] 起始地址不能为负数: {offset}")
        sys.exit(1)

    if length <= 0:
        print(f"[ERROR] 字节数必须大于 0: {length}")
        sys.exit(1)

    if offset >= file_size:
        print(f"[ERROR] 起始地址 0x{offset:X} ({offset}) 超出文件大小 "
              f"0x{file_size:X} ({file_size} bytes)")
        sys.exit(1)

    # 截断超过文件末尾的长度
    actual_length = min(length, file_size - offset)
    if actual_length < length:
        print(f"[WARN] 请求 {length} 字节，但文件仅剩 {actual_length} 字节，"
              f"已截断。\n")

    with open(filepath, "rb") as f:
        f.seek(offset)
        data = f.read(actual_length)

    # 打印文件信息头
    filename = os.path.basename(filepath)
    print(f"File:  {filename}")
    print(f"Path:  {os.path.abspath(filepath)}")
    print(f"Size:  {file_size} bytes (0x{file_size:X})")
    print(f"Range: 0x{offset:08X} — 0x{offset + actual_length - 1:08X} "
          f"({actual_length} bytes)")
    print("-" * 79)

    # hexdump 输出: 地址  hex bytes (16列)   ASCII
    BYTES_PER_ROW = 16

    for row_start in range(0, len(data), BYTES_PER_ROW):
        chunk = data[row_start:row_start + BYTES_PER_ROW]
        addr = offset + row_start

        # 地址列
        hex_bytes = " ".join(f"{b:02X}" for b in chunk)

        # ASCII 列（不可打印字符显示为 .）
        ascii_chars = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)

        # 对齐：hex 部分固定宽度 = 16*2 + 15 空格 = 47
        print(f"  {addr:08X}  {hex_bytes:<47s}  {ascii_chars}")

    print("-" * 79)


def main():
    if len(sys.argv) == 2 and sys.argv[1] in ("-h", "--help", "/?"):
        print(__doc__)
        sys.exit(0)

    if len(sys.argv) != 4:
        print("[ERROR] 参数数量不正确。")
        print(f"用法: python {os.path.basename(__file__)} "
              f"<文件路径> <起始地址> <字节数>")
        print(f"      python {os.path.basename(__file__)} --help  查看详细帮助")
        sys.exit(1)

    filepath = sys.argv[1]

    try:
        offset = parse_int(sys.argv[2])
    except ValueError:
        print(f"[ERROR] 无效的起始地址: {sys.argv[2]}")
        sys.exit(1)

    try:
        length = parse_int(sys.argv[3])
    except ValueError:
        print(f"[ERROR] 无效的字节数: {sys.argv[3]}")
        sys.exit(1)

    hexdump(filepath, offset, length)


if __name__ == "__main__":
    main()
