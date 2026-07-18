/**
 * Hex Dump Tool — 读取指定文件的十六进制数据并以格式化方式输出。
 *
 * Usage:
 *   hexdump <filepath> <offset> <length>
 *
 * Arguments:
 *   filepath  : Path to the file to read
 *   offset    : Byte offset from start (decimal or 0x-prefixed hex)
 *   length    : Number of bytes to read (decimal or 0x-prefixed hex)
 *
 * Compile:
 *   gcc -o Tools/hexdump.exe Tools/hexdump.c -Wall -Wextra -O2
 *
 * Examples:
 *   hexdump "MDK-ARM/Star Gamer.bin" 0 256
 *   hexdump "MDK-ARM/Star Gamer.bin" 0x100 0x80
 *   hexdump myFont.bin 0x2000 512
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BYTES_PER_ROW   16
#define HEX_COL_WIDTH   47      /* 16*3 - 1 = 47 chars for hex bytes column */

#define SEPARATOR       "-------------------------------------------------------------------------------"

/* ── parse_int ────────────────────────────────────────────────────────────
 * Parse decimal or 0x-prefixed hex string into a long.
 * Exits on invalid input.
 */
static long parse_int(const char *s)
{
    char *endptr;
    long val;

    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        val = strtol(s + 2, &endptr, 16);
    } else {
        val = strtol(s, &endptr, 10);
    }

    if (*endptr != '\0') {
        fprintf(stderr, "[ERROR] Invalid number: %s\n", s);
        exit(1);
    }
    return val;
}

/* ── print_usage ───────────────────────────────────────────────────────── */
static void print_usage(const char *prog)
{
    /* Extract basename for cleaner output */
    const char *base = strrchr(prog, '/');
    if (!base) base = strrchr(prog, '\\');
    base = base ? base + 1 : prog;

    printf(
        "Hex Dump Tool — formatted hex dump of a file.\n"
        "\n"
        "Usage:\n"
        "  %s <filepath> <offset> <length>\n"
        "\n"
        "Arguments:\n"
        "  filepath   Path to the file to read\n"
        "  offset     Byte offset from start (decimal or 0x-prefixed hex)\n"
        "  length     Number of bytes to read (decimal or 0x-prefixed hex)\n"
        "\n"
        "Examples:\n"
        "  %s \"file.bin\" 0 256\n"
        "  %s \"file.bin\" 0x100 0x80\n"
        "  %s myFont.bin 0x2000 512\n",
        base, base, base, base);
}

/* ── get_file_size ────────────────────────────────────────────────────────
 * Return file size in bytes without disturbing the current seek position.
 * Returns -1 on error.
 */
static long get_file_size(FILE *fp)
{
    long cur, size;
    cur = ftell(fp);
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    size = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return size;
}

/* ── extract_basename ──────────────────────────────────────────────────── */
static const char *extract_basename(const char *path)
{
    const char *p = strrchr(path, '/');
    const char *q = strrchr(path, '\\');
    if (!p || (q && q > p)) p = q;
    return p ? p + 1 : path;
}

/* ── main ──────────────────────────────────────────────────────────────── */
int main(int argc, char **argv)
{
    /* --help */
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "/?") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    /* Argument count */
    if (argc != 4) {
        fprintf(stderr, "[ERROR] Invalid number of arguments.\n");
        fprintf(stderr, "Usage: %s <filepath> <offset> <length>\n", argv[0]);
        fprintf(stderr, "       %s --help  for detailed help\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    long offset          = parse_int(argv[2]);
    long length          = parse_int(argv[3]);

    /* Validate arguments */
    if (offset < 0) {
        fprintf(stderr, "[ERROR] Offset cannot be negative: %ld\n", offset);
        return 1;
    }
    if (length <= 0) {
        fprintf(stderr, "[ERROR] Length must be greater than 0: %ld\n", length);
        return 1;
    }

    /* Open file */
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        fprintf(stderr, "[ERROR] Cannot open file: %s\n", filepath);
        return 1;
    }

    /* Get file size */
    long file_size = get_file_size(fp);
    if (file_size < 0) {
        fprintf(stderr, "[ERROR] Cannot determine file size: %s\n", filepath);
        fclose(fp);
        return 1;
    }

    /* Range check */
    if (offset >= file_size) {
        fprintf(stderr,
                "[ERROR] Offset 0x%lX (%ld) exceeds file size "
                "0x%lX (%ld bytes)\n",
                offset, offset, file_size, file_size);
        fclose(fp);
        return 1;
    }

    /* Truncate length if it exceeds file boundary */
    long actual_length = length;
    if (offset + length > file_size) {
        actual_length = file_size - offset;
        fprintf(stderr,
                "[WARN] Requested %ld bytes, but only %ld remain — "
                "truncated.\n\n",
                length, actual_length);
    }

    /* Seek & read */
    if (fseek(fp, offset, SEEK_SET) != 0) {
        fprintf(stderr, "[ERROR] Cannot seek to offset 0x%lX\n", offset);
        fclose(fp);
        return 1;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)actual_length);
    if (!data) {
        fprintf(stderr, "[ERROR] Memory allocation failed for %ld bytes\n",
                actual_length);
        fclose(fp);
        return 1;
    }

    size_t bytes_read = fread(data, 1, (size_t)actual_length, fp);
    fclose(fp);

    if (bytes_read != (size_t)actual_length) {
        fprintf(stderr,
                "[WARN] Expected %ld bytes but only read %zu bytes\n",
                actual_length, bytes_read);
        actual_length = (long)bytes_read;
    }

    /* ── Print header ──────────────────────────────────────────────── */
    printf("File:  %s\n", extract_basename(filepath));
    printf("Size:  %ld bytes (0x%lX)\n", file_size, file_size);
    printf("Range: 0x%08lX — 0x%08lX (%ld bytes)\n",
           offset, offset + actual_length - 1, actual_length);
    printf("%s\n", SEPARATOR);

    /* ── Hexdump body ──────────────────────────────────────────────── */
    for (long row = 0; row < actual_length; row += BYTES_PER_ROW) {
        long chunk = actual_length - row;
        if (chunk > BYTES_PER_ROW) chunk = BYTES_PER_ROW;

        long addr = offset + row;

        /* Address column */
        printf("  %08lX  ", addr);

        /* Hex bytes column — print each byte as "XX", separated by space.
         * The full 16-byte hex column is 47 chars wide.
         * For partial rows we pad on the right to keep columns aligned. */
        int hex_printed = 0;
        for (long i = 0; i < chunk; i++) {
            if (i > 0) putchar(' ');
            printf("%02X", data[row + i]);
            hex_printed += (i == 0) ? 2 : 3;   /* "XX" or " XX" */
        }

        /* Pad remaining hex column width */
        int pad = HEX_COL_WIDTH - hex_printed;
        while (pad-- > 0) putchar(' ');

        /* ASCII column */
        printf("  ");
        for (long i = 0; i < chunk; i++) {
            unsigned char c = data[row + i];
            putchar((c >= 32 && c < 127) ? c : '.');
        }
        putchar('\n');
    }

    printf("%s\n", SEPARATOR);

    free(data);
    return 0;
}
