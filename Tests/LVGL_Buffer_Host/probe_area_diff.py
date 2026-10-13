"""Compile the supplied LVGL area-difference function and check pixel coverage.

This is a geometry probe, not a complete LVGL or display performance test.
The supplied source tree is read only; build artifacts use a temporary folder.
"""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


PRELUDE = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef int32_t lv_coord_t;
typedef struct { lv_coord_t x1, y1, x2, y2; } lv_area_t;
static int32_t lv_area_get_width(const lv_area_t *a) { return a->x2-a->x1+1; }
static int32_t lv_area_get_height(const lv_area_t *a) { return a->y2-a->y1+1; }
static bool _lv_area_is_on(const lv_area_t *a, const lv_area_t *b) {
    return a->x1 <= b->x2 && b->x1 <= a->x2 &&
           a->y1 <= b->y2 && b->y1 <= a->y2;
}
static bool _lv_area_is_in(const lv_area_t *a, const lv_area_t *b, int radius) {
    (void)radius; /* The extracted function only requests radius zero. */
    return a->x1 >= b->x1 && a->x2 <= b->x2 &&
           a->y1 >= b->y1 && a->y2 <= b->y2;
}
"""

HARNESS = r"""
static bool contains(const lv_area_t *a, int x, int y) {
    return x >= a->x1 && x <= a->x2 && y >= a->y1 && y <= a->y2;
}
static int check(const lv_area_t *a, const lv_area_t *b, int *redundant) {
    lv_area_t out[4] = {{0}};
    int n = _lv_area_diff(out, a, b);
    if(n < -1 || n > 4) return 1000;
    if(n == -1) { out[0] = *a; n = 1; }
    int missing = 0;
    for(int y = a->y1; y <= a->y2; ++y) {
        for(int x = a->x1; x <= a->x2; ++x) {
            bool copied = false;
            for(int i = 0; i < n; ++i) copied |= contains(&out[i], x, y);
            bool redraw = contains(b, x, y);
            if(!redraw && !copied) ++missing;
            if(redraw && copied) ++*redundant;
        }
    }
    return missing;
}
int main(void) {
    lv_area_t a = {0, 0, 9, 0}, b = {5, 0, 9, 0};
    int redundant = 0;
    int missing = check(&a, &b, &redundant);
    printf("Single row: A=(0,0)-(9,0), B=(5,0)-(9,0); missing=%d\n", missing);
    unsigned pairs = 0, missing_pairs = 0, redundant_pairs = 0;
    for(int ay1 = 0; ay1 < 4; ++ay1)
    for(int ay2 = ay1; ay2 < 4; ++ay2)
    for(int ax1 = 0; ax1 < 4; ++ax1)
    for(int ax2 = ax1; ax2 < 4; ++ax2)
    for(int by1 = 0; by1 < 4; ++by1)
    for(int by2 = by1; by2 < 4; ++by2)
    for(int bx1 = 0; bx1 < 4; ++bx1)
    for(int bx2 = bx1; bx2 < 4; ++bx2) {
        a = (lv_area_t){ax1, ay1, ax2, ay2};
        b = (lv_area_t){bx1, by1, bx2, by2};
        redundant = 0;
        if(check(&a, &b, &redundant)) ++missing_pairs;
        if(redundant) ++redundant_pairs;
        ++pairs;
    }
    printf("4x4 rectangle pairs=%u, missing-coverage pairs=%u, "
           "redundant-copy pairs=%u\n", pairs, missing_pairs, redundant_pairs);
    return missing_pairs ? 1 : 0;
}
"""


def extract_function(source):
    match = re.search(r"int8_t\s+_lv_area_diff\s*\(", source)
    if match is None:
        raise ValueError("Source does not contain _lv_area_diff")
    opening = source.index("{", match.start())
    depth = 0
    # Both examined releases contain no brace characters in strings/comments.
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[match.start():index + 1]
    raise ValueError("Unterminated function")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lvgl_root", type=Path)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    source_path = args.lvgl_root / "src/misc/lv_area.c"
    source = source_path.read_text(encoding="utf-8")
    program = PRELUDE + extract_function(source) + HARNESS
    with tempfile.TemporaryDirectory(prefix="lvgl-area-probe-") as temp:
        binary = Path(temp) / "probe.exe"
        subprocess.run(
            [args.cc, "-std=c99", "-O0", "-Wall", "-Wextra", "-x", "c", "-", "-o", str(binary)],
            input=program, text=True, check=True, capture_output=True,
        )
        print(f"Source: {source_path}", flush=True)
        result = subprocess.run([str(binary)], check=False)
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
