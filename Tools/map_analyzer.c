/**
 * map_analyzer.c — ARMCLANG MAP File Analyzer (CLI)
 *
 * Parses Keil MDK-ARM (armlink) generated .map files for STM32F407ZG projects
 * and displays Flash (RO) and SRAM (RW+ZI) usage breakdowns.
 *
 * Build:  gcc -o map_analyzer map_analyzer.c -Wall -Wextra -O2
 * Usage:  map_analyzer [options] <mapfile>
 *         map_analyzer -h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define MAX_NAME        384
#define MAX_PATH       2048
#define MAX_LINE       4096
#define DEFAULT_TOP_N    30

/* ── Data Structures ───────────────────────────────────────────────────── */

typedef struct {
    char    name[MAX_NAME];
    uint32_t exec_base;
    uint32_t load_base;
    uint32_t size;
    uint32_t max_size;
    uint32_t compressed_size;
} ExecRegion;

typedef struct {
    char    name[MAX_NAME];
    uint32_t base;
    uint32_t size;
    uint32_t max_size;
    uint32_t compressed_size;
    ExecRegion *exec_regions;
    int     exec_count;
    int     exec_cap;
} LoadRegion;

typedef struct {
    uint32_t code;
    uint32_t ro_data;
    uint32_t rw_data;
    uint32_t zi_data;
    uint32_t debug;
    char    name[MAX_NAME];
    char    library[MAX_NAME];
} ObjectRow;

typedef struct {
    char        filepath[MAX_PATH];
    uint32_t    entry_point;

    LoadRegion *load_regions;
    int         lr_count;
    int         lr_cap;

    ObjectRow  *objects;
    int         obj_count;
    int         obj_cap;

    ObjectRow  *libraries;
    int         lib_count;
    int         lib_cap;

    /* Grand totals */
    uint32_t total_code;
    uint32_t total_ro_data;
    uint32_t total_rw_data;
    uint32_t total_zi_data;
    uint32_t total_rom;

    /* MCU capacities derived from scatter file */
    uint32_t flash_capacity;
    uint32_t sram_capacity;
} MapData;

/* ── Forward declarations ──────────────────────────────────────────────── */

/* Helpers */
static uint32_t  parse_hex(const char *s);
static int       parse_comp_line(const char *line, uint32_t vals[6], char *name_out, size_t name_sz);
static int       str_starts_with(const char *s, const char *prefix);

/* Memory management */
static MapData  *mapdata_new(void);
static void      mapdata_free(MapData *md);
static int       lr_add_exec(LoadRegion *lr, const ExecRegion *er);

/* Parser */
static MapData  *parse_map_file(const char *filepath);

/* Output */
static void      print_summary(const MapData *md);
static void      print_objects(const MapData *md, int top_n, const char *sort_col, int reverse);
static void      print_dirs(const MapData *md, const char *sort_col, int reverse);
static void      print_regions(const MapData *md);

/* ── Helpers ───────────────────────────────────────────────────────────── */

/* Safe string copy — always null-terminates, avoids -Wstringop-truncation */
#define SAFE_STRCPY(dst, src, sz) do { \
    size_t _srclen = strlen(src); \
    size_t _n = (_srclen < (sz) - 1) ? _srclen : (sz) - 1; \
    memcpy((dst), (src), _n); \
    (dst)[_n] = '\0'; \
} while(0)

static uint32_t parse_hex(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return (uint32_t)strtoul(s, NULL, 16);
}

/**
 * Parse a component-size data line.
 * Format:  "    CODE  INLINE  RO  RW  ZI  DEBUG  NAME"
 * Returns the number of fields parsed (6 on success, 0 on failure).
 */
static int parse_comp_line(const char *line, uint32_t vals[6], char *name_out, size_t name_sz) {
    const char *p = line;
    char *end;
    int i;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Must start with a digit */
    if (!isdigit((unsigned char)*p)) return 0;

    for (i = 0; i < 6; i++) {
        vals[i] = (uint32_t)strtoul(p, &end, 10);
        if (end == p) return 0;  /* no number parsed */
        p = end;
        while (*p == ' ' || *p == '\t') p++;
    }

    /* The rest is the name */
    while (*p == ' ' || *p == '\t') p++;
    if (*p) {
        size_t len = strlen(p);
        /* Remove trailing whitespace */
        while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n'))
            len--;
        size_t cp = len < name_sz - 1 ? len : name_sz - 1;
        memcpy(name_out, p, cp);
        name_out[cp] = '\0';
    } else {
        name_out[0] = '\0';
    }

    return 6;
}

static int str_starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ── Memory management ─────────────────────────────────────────────────── */

static MapData *mapdata_new(void) {
    MapData *md = (MapData *)calloc(1, sizeof(MapData));
    return md;
}

static void mapdata_free(MapData *md) {
    if (!md) return;
    for (int i = 0; i < md->lr_count; i++) {
        free(md->load_regions[i].exec_regions);
    }
    free(md->load_regions);
    free(md->objects);
    free(md->libraries);
    free(md);
}

static int lr_add_exec(LoadRegion *lr, const ExecRegion *er) {
    if (lr->exec_count >= lr->exec_cap) {
        int new_cap = lr->exec_cap ? lr->exec_cap * 2 : 4;
        ExecRegion *tmp = (ExecRegion *)realloc(lr->exec_regions,
                                                 (size_t)new_cap * sizeof(ExecRegion));
        if (!tmp) return -1;
        lr->exec_regions = tmp;
        lr->exec_cap = new_cap;
    }
    lr->exec_regions[lr->exec_count++] = *er;
    return 0;
}

/* ── Parser ────────────────────────────────────────────────────────────── */

/**
 * Parse an ARMCLANG linker .map file into a MapData structure.
 * Returns NULL on error (prints message to stderr).
 */
static MapData *parse_map_file(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open file '%s'\n", filepath);
        return NULL;
    }

    MapData *md = mapdata_new();
    if (!md) { fclose(fp); return NULL; }
    SAFE_STRCPY(md->filepath, filepath, MAX_PATH);

    char line[MAX_LINE];
    int  in_memory_map = 0;
    int  in_component_sizes = 0;
    int  in_object_section = 0;
    int  in_library_section = 0;
    int  in_library_name_section = 0;
    int  in_totals = 0;
    char current_lib_name[MAX_NAME] = "";
    LoadRegion *current_lr = NULL;

    /* ── Helper macros for dynamic arrays ── */
    #define ENSURE_OBJ_CAP(md) do { \
        if ((md)->obj_count >= (md)->obj_cap) { \
            int nc = (md)->obj_cap ? (md)->obj_cap * 2 : 256; \
            ObjectRow *t = (ObjectRow *)realloc((md)->objects, (size_t)nc * sizeof(ObjectRow)); \
            if (!t) goto nomem; \
            (md)->objects = t; \
            (md)->obj_cap = nc; \
        } \
    } while(0)

    #define ENSURE_LIB_CAP(md) do { \
        if ((md)->lib_count >= (md)->lib_cap) { \
            int nc = (md)->lib_cap ? (md)->lib_cap * 2 : 64; \
            ObjectRow *t = (ObjectRow *)realloc((md)->libraries, (size_t)nc * sizeof(ObjectRow)); \
            if (!t) goto nomem; \
            (md)->libraries = t; \
            (md)->lib_cap = nc; \
        } \
    } while(0)

    #define ENSURE_LR_CAP(md) do { \
        if ((md)->lr_count >= (md)->lr_cap) { \
            int nc = (md)->lr_cap ? (md)->lr_cap * 2 : 8; \
            LoadRegion *t = (LoadRegion *)realloc((md)->load_regions, (size_t)nc * sizeof(LoadRegion)); \
            if (!t) goto nomem; \
            (md)->load_regions = t; \
            (md)->lr_cap = nc; \
        } \
    } while(0)

    /* ── First pass: read everything into memory for two-phase parsing ── */
    /* We parse Memory Map and Component Sizes in one pass over lines. */

    rewind(fp);
    while (fgets(line, sizeof(line), fp)) {
        char *s = line;

        /* ── Section boundary detection ── */
        if (strstr(s, "Memory Map of the image")) {
            in_memory_map = 1;
            in_component_sizes = 0;
            continue;
        }
        if (strstr(s, "Image component sizes")) {
            in_memory_map = 0;
            in_component_sizes = 1;
            in_object_section = 0;
            in_library_section = 0;
            in_library_name_section = 0;
            in_totals = 0;
            continue;
        }

        /* ── Memory Map section ── */
        if (in_memory_map) {
            /* Entry point */
            if (strstr(s, "Image Entry point")) {
                const char *ep = strstr(s, "0x");
                if (ep) md->entry_point = parse_hex(ep);
                continue;
            }

            /* Load Region */
            if (strstr(s, "Load Region")) {
                char lr_name[MAX_NAME] = "";
                uint32_t base = 0, size = 0, max_sz = 0;
                uint32_t compressed = 0;

                /* Extract name inside "Load Region XXX (" */
                const char *p = strstr(s, "Load Region ");
                if (p) {
                    p += 12;
                    while (*p == ' ') p++;
                    const char *name_end = strchr(p, ' ');
                    if (!name_end) name_end = strchr(p, '(');
                    if (name_end) {
                        size_t nlen = (size_t)(name_end - p);
                        if (nlen >= MAX_NAME) nlen = MAX_NAME - 1;
                        memcpy(lr_name, p, nlen);
                        lr_name[nlen] = '\0';
                    }
                }

                /* Parse hex values */
                p = strstr(s, "Base:");
                if (p) base = parse_hex(p + 5);
                p = strstr(s, "Size:");
                if (p) size = parse_hex(p + 5);
                p = strstr(s, "Max:");
                if (p) max_sz = parse_hex(p + 4);

                /* COMPRESSED tag */
                p = strstr(s, "COMPRESSED[");
                if (p) compressed = parse_hex(p + 11);

                ENSURE_LR_CAP(md);
                current_lr = &md->load_regions[md->lr_count++];
                memset(current_lr, 0, sizeof(LoadRegion));
                SAFE_STRCPY(current_lr->name, lr_name, MAX_NAME);
                current_lr->base = base;
                current_lr->size = size;
                current_lr->max_size = max_sz;
                current_lr->compressed_size = compressed;
                continue;
            }

            /* Execution Region */
            if (strstr(s, "Execution Region")) {
                char er_name[MAX_NAME] = "";
                uint32_t exec_base = 0, load_base = 0, size = 0, max_sz = 0;
                uint32_t compressed = 0;

                const char *p = strstr(s, "Execution Region ");
                if (p) {
                    p += 17;
                    while (*p == ' ') p++;
                    const char *name_end = strchr(p, ' ');
                    if (!name_end) name_end = strchr(p, '(');
                    if (name_end) {
                        size_t nlen = (size_t)(name_end - p);
                        if (nlen >= MAX_NAME) nlen = MAX_NAME - 1;
                        memcpy(er_name, p, nlen);
                        er_name[nlen] = '\0';
                    }
                }

                p = strstr(s, "Exec base:");
                if (p) exec_base = parse_hex(p + 10);
                p = strstr(s, "Load base:");
                if (p) load_base = parse_hex(p + 10);
                p = strstr(s, "Size:");
                if (p) size = parse_hex(p + 5);
                p = strstr(s, "Max:");
                if (p) max_sz = parse_hex(p + 4);

                p = strstr(s, "COMPRESSED[");
                if (p) compressed = parse_hex(p + 11);

                ExecRegion er;
                memset(&er, 0, sizeof(er));
                SAFE_STRCPY(er.name, er_name, MAX_NAME);
                er.exec_base = exec_base;
                er.load_base = load_base;
                er.size = size;
                er.max_size = max_sz;
                er.compressed_size = compressed;

                if (current_lr) {
                    lr_add_exec(current_lr, &er);
                }
                continue;
            }

            continue;
        }

        /* ── Component Sizes section ── */
        if (!in_component_sizes) continue;

        /* Detect separators */
        if (strstr(s, "======")) continue;

        /* Detect sub-section headers */
        if (strstr(s, "Code (inc. data)") && strstr(s, "RO Data")) {
            if (strstr(s, "Library Member Name")) {
                in_object_section = 0;
                in_library_section = 1;
                in_library_name_section = 0;
                in_totals = 0;
                continue;
            }
            if (strstr(s, "Library Name")) {
                in_object_section = 0;
                in_library_section = 0;
                in_library_name_section = 1;
                in_totals = 0;
                continue;
            }
            if (strstr(s, "Object Name")) {
                in_object_section = 1;
                in_library_section = 0;
                in_library_name_section = 0;
                in_totals = 0;
                continue;
            }
            /* Header line without sub-type — ignore */
            continue;
        }

        /* Grand Totals */
        if (strstr(s, "Grand Totals")) {
            in_object_section = 0;
            in_library_section = 0;
            in_library_name_section = 0;
            in_totals = 1;
            /* Fall through to parse */
        }

        /* Skip separator lines like ---- */
        {
            const char *t = s;
            while (*t == ' ' || *t == '\t') t++;
            if (str_starts_with(t, "----")) continue;
        }

        if (in_totals) {
            uint32_t vals[6];
            char name[MAX_NAME];
            if (parse_comp_line(s, vals, name, sizeof(name)) == 6) {
                if (strcmp(name, "Grand Totals") == 0) {
                    md->total_code    = vals[0];
                    md->total_ro_data = vals[2];
                    md->total_rw_data = vals[3];
                    md->total_zi_data = vals[4];
                    /* vals[5] is debug */
                    continue;
                }
                if (strcmp(name, "ROM Totals") == 0) {
                    md->total_rom = vals[0];
                    continue;
                }
            }
            /* Fall through — Total RO / RW / ROM lines handled below */
        }

        /* Object Totals / Library Totals — skip */
        if (strstr(s, "Object Totals") || strstr(s, "Library Totals")) {
            continue;
        }

        /* Total RO / RW / ROM summary lines (after Grand Totals) */
        if (strstr(s, "Total RO  Size") || strstr(s, "Total RO Size")) continue;
        if (strstr(s, "Total RW  Size") || strstr(s, "Total RW Size")) continue;
        if (strstr(s, "Total ROM Size")) {
            /* Format: "Total ROM Size (...)  NNNNNN ( xx.xkB)" */
            const char *p = strchr(s, ')');  /* first ) after description */
            if (p) {
                p++;
                while (*p == ' ') p++;
                md->total_rom = (uint32_t)strtoul(p, NULL, 10);
            }
            continue;
        }

        /* Parse object rows */
        if (in_object_section) {
            uint32_t vals[6];
            char name[MAX_NAME];
            if (parse_comp_line(s, vals, name, sizeof(name)) == 6) {
                if (name[0] == '(') continue; /* skip (incl. Generated), (incl. Padding) etc. */
                ENSURE_OBJ_CAP(md);
                ObjectRow *obj = &md->objects[md->obj_count++];
                memset(obj, 0, sizeof(ObjectRow));
                obj->code    = vals[0];
                /* vals[1] is inline data (included in code) — skip */
                obj->ro_data = vals[2];
                obj->rw_data = vals[3];
                obj->zi_data = vals[4];
                obj->debug   = vals[5];
                SAFE_STRCPY(obj->name, name, MAX_NAME);
            }
            continue;
        }

        /* Parse library member rows */
        if (in_library_section) {
            uint32_t vals[6];
            char name[MAX_NAME];
            if (parse_comp_line(s, vals, name, sizeof(name)) == 6) {
                ENSURE_LIB_CAP(md);
                ObjectRow *obj = &md->libraries[md->lib_count++];
                memset(obj, 0, sizeof(ObjectRow));
                obj->code    = vals[0];
                obj->ro_data = vals[2];
                obj->rw_data = vals[3];
                obj->zi_data = vals[4];
                obj->debug   = vals[5];
                SAFE_STRCPY(obj->name, name, MAX_NAME);
                SAFE_STRCPY(obj->library, current_lib_name, MAX_NAME);
            }
            continue;
        }

        /* Parse library name section rows */
        if (in_library_name_section) {
            uint32_t vals[6];
            char name[MAX_NAME];
            if (parse_comp_line(s, vals, name, sizeof(name)) == 6) {
                SAFE_STRCPY(current_lib_name, name, MAX_NAME);
            }
            continue;
        }
    }

    fclose(fp);

    /* ── Phase 3: Derive MCU capacities from execution region max sizes ── */
    for (int i = 0; i < md->lr_count; i++) {
        LoadRegion *lr = &md->load_regions[i];
        for (int j = 0; j < lr->exec_count; j++) {
            ExecRegion *er = &lr->exec_regions[j];

            /* Check name */
            if (strstr(er->name, "IRAM") || strstr(er->name, "RAM") || strstr(er->name, "SRAM")) {
                if (er->max_size > md->sram_capacity)
                    md->sram_capacity = er->max_size;
            } else if (strstr(er->name, "IROM") || strstr(er->name, "FLASH") || strstr(er->name, "ROM")) {
                if (er->max_size > md->flash_capacity)
                    md->flash_capacity = er->max_size;
            } else if (er->exec_base >= 0x20000000 && er->exec_base < 0x40000000) {
                /* SRAM address range */
                if (er->max_size > md->sram_capacity)
                    md->sram_capacity = er->max_size;
            } else if (er->exec_base >= 0x08000000 && er->exec_base < 0x10000000) {
                /* FLASH address range */
                if (er->max_size > md->flash_capacity)
                    md->flash_capacity = er->max_size;
            }
        }
    }

    return md;

nomem:
    fprintf(stderr, "ERROR: Out of memory while parsing MAP file.\n");
    fclose(fp);
    mapdata_free(md);
    return NULL;

    #undef ENSURE_OBJ_CAP
    #undef ENSURE_LIB_CAP
    #undef ENSURE_LR_CAP
}

/* ── Formatting helpers ────────────────────────────────────────────────── */

static const char *fmt_bytes(uint32_t n) {
    static char buf[3][32];
    static int idx = 0;
    char *b = buf[idx];
    idx = (idx + 1) % 3;
    if (n >= 1024 * 1024)
        snprintf(b, 32, "%.2f MB", n / (1024.0 * 1024.0));
    else if (n >= 1024)
        snprintf(b, 32, "%.2f KB", n / 1024.0);
    else
        snprintf(b, 32, "%u B", n);
    return b;
}

static double obj_flash(const ObjectRow *o) {
    return (double)(o->code + o->ro_data + o->rw_data);
}

static double obj_sram(const ObjectRow *o) {
    return (double)(o->rw_data + o->zi_data);
}

/* ── Object comparison functions ───────────────────────────────────────── */

/* All cmp functions use standard qsort signature */
static int cmp_name_asc(const void *a, const void *b) {
    const ObjectRow *oa = (const ObjectRow *)a;
    const ObjectRow *ob = (const ObjectRow *)b;
    return strcmp(oa->name, ob->name);
}
static int cmp_name_desc(const void *a, const void *b) { return -cmp_name_asc(a, b); }

static int cmp_flash_asc(const void *a, const void *b) {
    double d = obj_flash((const ObjectRow *)a) - obj_flash((const ObjectRow *)b);
    return (d > 0) ? 1 : ((d < 0) ? -1 : 0);
}
static int cmp_flash_desc(const void *a, const void *b) { return -cmp_flash_asc(a, b); }

static int cmp_code_asc(const void *a, const void *b) {
    const ObjectRow *oa = (const ObjectRow *)a;
    const ObjectRow *ob = (const ObjectRow *)b;
    return (oa->code > ob->code) ? 1 : ((oa->code < ob->code) ? -1 : 0);
}
static int cmp_code_desc(const void *a, const void *b) { return -cmp_code_asc(a, b); }

static int cmp_ro_asc(const void *a, const void *b) {
    const ObjectRow *oa = (const ObjectRow *)a;
    const ObjectRow *ob = (const ObjectRow *)b;
    return (oa->ro_data > ob->ro_data) ? 1 : ((oa->ro_data < ob->ro_data) ? -1 : 0);
}
static int cmp_ro_desc(const void *a, const void *b) { return -cmp_ro_asc(a, b); }

static int cmp_rw_asc(const void *a, const void *b) {
    const ObjectRow *oa = (const ObjectRow *)a;
    const ObjectRow *ob = (const ObjectRow *)b;
    return (oa->rw_data > ob->rw_data) ? 1 : ((oa->rw_data < ob->rw_data) ? -1 : 0);
}
static int cmp_rw_desc(const void *a, const void *b) { return -cmp_rw_asc(a, b); }

static int cmp_zi_asc(const void *a, const void *b) {
    const ObjectRow *oa = (const ObjectRow *)a;
    const ObjectRow *ob = (const ObjectRow *)b;
    return (oa->zi_data > ob->zi_data) ? 1 : ((oa->zi_data < ob->zi_data) ? -1 : 0);
}
static int cmp_zi_desc(const void *a, const void *b) { return -cmp_zi_asc(a, b); }

static int cmp_sram_asc(const void *a, const void *b) {
    double d = obj_sram((const ObjectRow *)a) - obj_sram((const ObjectRow *)b);
    return (d > 0) ? 1 : ((d < 0) ? -1 : 0);
}
static int cmp_sram_desc(const void *a, const void *b) { return -cmp_sram_asc(a, b); }

static int cmp_debug_asc(const void *a, const void *b) {
    const ObjectRow *oa = (const ObjectRow *)a;
    const ObjectRow *ob = (const ObjectRow *)b;
    return (oa->debug > ob->debug) ? 1 : ((oa->debug < ob->debug) ? -1 : 0);
}
static int cmp_debug_desc(const void *a, const void *b) { return -cmp_debug_asc(a, b); }

typedef int (*qsort_cmp)(const void *, const void *);

static qsort_cmp get_obj_cmp(const char *col, int reverse) {
    /* Numeric columns default: descending (largest first).
     * Name column default: ascending (A-Z).
     * 'reverse' flips the default direction. */
    int desc;
    if (col && strcmp(col, "name") == 0)
        desc = reverse;           /* name: default asc, reverse => desc */
    else
        desc = !reverse;          /* numeric: default desc, reverse => asc */

    if (!col || strcmp(col, "flash") == 0)
        return desc ? cmp_flash_desc : cmp_flash_asc;
    if (strcmp(col, "name") == 0)
        return desc ? cmp_name_desc : cmp_name_asc;
    if (strcmp(col, "code") == 0)
        return desc ? cmp_code_desc : cmp_code_asc;
    if (strcmp(col, "ro") == 0 || strcmp(col, "ro_data") == 0)
        return desc ? cmp_ro_desc : cmp_ro_asc;
    if (strcmp(col, "rw") == 0 || strcmp(col, "rw_data") == 0)
        return desc ? cmp_rw_desc : cmp_rw_asc;
    if (strcmp(col, "zi") == 0 || strcmp(col, "zi_data") == 0)
        return desc ? cmp_zi_desc : cmp_zi_asc;
    if (strcmp(col, "sram") == 0)
        return desc ? cmp_sram_desc : cmp_sram_asc;
    if (strcmp(col, "debug") == 0)
        return desc ? cmp_debug_desc : cmp_debug_asc;
    /* Default: flash descending */
    return desc ? cmp_flash_desc : cmp_flash_asc;
}

/* ── Output: Summary ───────────────────────────────────────────────────── */

static void print_summary(const MapData *md) {
    uint32_t flash_total = md->flash_capacity ? md->flash_capacity : 0x100000;   /* 1 MB */
    uint32_t sram_total  = md->sram_capacity  ? md->sram_capacity  : 0x20000;    /* 128 KB */

    uint32_t flash_used = md->total_rom > 0
        ? md->total_rom
        : md->total_code + md->total_ro_data + md->total_rw_data;
    uint32_t sram_used  = md->total_rw_data + md->total_zi_data;

    double flash_pct = flash_total ? (double)flash_used / flash_total * 100.0 : 0.0;
    double sram_pct  = sram_total  ? (double)sram_used  / sram_total  * 100.0 : 0.0;

    /* ── Header ── */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           ARMCLANG MAP Analyzer  —  %-24s ║\n", md->filepath[0] ? strrchr(md->filepath, '/') ? strrchr(md->filepath, '/') + 1 : strrchr(md->filepath, '\\') ? strrchr(md->filepath, '\\') + 1 : md->filepath : "");
    printf("╠══════════════════════════════════════════════════════════════╣\n");

    /* ── Flash bar ── */
    printf("║  FLASH:  %s / %s  (%5.1f%%)", fmt_bytes(flash_used), fmt_bytes(flash_total), flash_pct);
    {
        int bar_w = 30;
        int filled = (int)(flash_pct / 100.0 * bar_w + 0.5);
        int pad = 48 - (int)strlen(fmt_bytes(flash_used)) - (int)strlen(fmt_bytes(flash_total));
        if (pad < 1) pad = 1;
        printf("%*s[", pad, "");
        for (int i = 0; i < bar_w; i++) putchar(i < filled ? '#' : '-');
        printf("]  ║\n");
    }

    /* ── SRAM bar ── */
    printf("║  SRAM:   %s / %s  (%5.1f%%)", fmt_bytes(sram_used), fmt_bytes(sram_total), sram_pct);
    {
        int bar_w = 30;
        int filled = (int)(sram_pct / 100.0 * bar_w + 0.5);
        int pad = 48 - (int)strlen(fmt_bytes(sram_used)) - (int)strlen(fmt_bytes(sram_total));
        if (pad < 1) pad = 1;
        printf("%*s[", pad, "");
        for (int i = 0; i < bar_w; i++) putchar(i < filled ? '#' : '-');
        printf("]  ║\n");
    }

    printf("╠══════════════════════════════════════════════════════════════╣\n");

    /* ── Detail breakdown ── */
    printf("║  Code:       %10u B  (%8.1f KB)                       ║\n",
           md->total_code, md->total_code / 1024.0);
    printf("║  RO Data:    %10u B  (%8.1f KB)                       ║\n",
           md->total_ro_data, md->total_ro_data / 1024.0);
    printf("║  RW Data:    %10u B  (%8.1f KB)                       ║\n",
           md->total_rw_data, md->total_rw_data / 1024.0);
    printf("║  ZI Data:    %10u B  (%8.1f KB)                       ║\n",
           md->total_zi_data, md->total_zi_data / 1024.0);
    if (md->total_rom > 0) {
        printf("║  Total ROM:  %10u B  (%8.1f KB)                       ║\n",
               md->total_rom, md->total_rom / 1024.0);
    }

    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  FLASH used: %s   SRAM used: %s                  ║\n",
           fmt_bytes(flash_used), fmt_bytes(sram_used));
    printf("║  Objects: %-8d   Libraries: %-8d                     ║\n",
           md->obj_count, md->lib_count);
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* ── Regions ── */
    printf("── Execution Regions ─────────────────────────────────────────────\n");
    for (int i = 0; i < md->lr_count; i++) {
        const LoadRegion *lr = &md->load_regions[i];
        for (int j = 0; j < lr->exec_count; j++) {
            const ExecRegion *er = &lr->exec_regions[j];
            double upct = er->max_size ? (double)er->size / er->max_size * 100.0 : 0.0;
            printf("  %-24s  %s / %s  (%5.1f%%)  @ 0x%08X\n",
                   er->name,
                   fmt_bytes(er->size), fmt_bytes(er->max_size),
                   upct, er->exec_base);
        }
    }
    printf("\n");
}

/* ── Output: Object list ───────────────────────────────────────────────── */

static void print_objects(const MapData *md, int top_n, const char *sort_col, int reverse) {
    if (md->obj_count == 0) {
        printf("No project objects found.\n");
        return;
    }

    /* Make a mutable copy for sorting */
    size_t n = (size_t)md->obj_count;
    ObjectRow *sorted = (ObjectRow *)malloc(n * sizeof(ObjectRow));
    if (!sorted) { fprintf(stderr, "Out of memory.\n"); return; }
    memcpy(sorted, md->objects, n * sizeof(ObjectRow));

    qsort_cmp cmp = get_obj_cmp(sort_col, reverse);
    qsort(sorted, n, sizeof(ObjectRow), cmp);

    if (top_n > 0 && (int)n > top_n) n = (size_t)top_n;

    /* Header */
    char col_label[32] = "Flash";
    if (sort_col) {
        if (strcmp(sort_col, "code") == 0)      strcpy(col_label, "Code");
        else if (strcmp(sort_col, "ro") == 0 || strcmp(sort_col, "ro_data") == 0) strcpy(col_label, "RO Data");
        else if (strcmp(sort_col, "rw") == 0 || strcmp(sort_col, "rw_data") == 0) strcpy(col_label, "RW Data");
        else if (strcmp(sort_col, "zi") == 0 || strcmp(sort_col, "zi_data") == 0) strcpy(col_label, "ZI Data");
        else if (strcmp(sort_col, "sram") == 0)  strcpy(col_label, "SRAM");
        else if (strcmp(sort_col, "debug") == 0) strcpy(col_label, "Debug");
        else if (strcmp(sort_col, "name") == 0)  strcpy(col_label, "Name");
    }

    int desc = (sort_col && strcmp(sort_col, "name") == 0) ? reverse : !reverse;
    printf("── Top %zu Objects (by %s, %s) ──", n, col_label, desc ? "descending" : "ascending");
    /* Right-pad title to ~80 chars */
    printf("\n");
    printf("%-4s %-36s %10s %10s %10s %10s %10s %10s\n",
           "No.", "Object", "Flash", "Code", "RO", "RW", "ZI", "SRAM");
    printf("──── %-36s %10s %10s %10s %10s %10s %10s\n",
           "────────────────────────────────────", "", "", "", "", "", "");

    for (size_t i = 0; i < n; i++) {
        const ObjectRow *o = &sorted[i];
        printf("%3zu  %-36s %10u %10u %10u %10u %10u %10u\n",
               i + 1, o->name,
               o->code + o->ro_data + o->rw_data,
               o->code, o->ro_data, o->rw_data, o->zi_data,
               o->rw_data + o->zi_data);
    }
    printf("\n");
    free(sorted);
}

/* ── Output: Directory-grouped view ────────────────────────────────────── */

static const char *dir_of(const char *obj_name) {
    /* Library objects: .o( — embedded objects from libraries */
    if (strstr(obj_name, ".o(")) {
        static char buf[MAX_NAME];
        const char *end = strstr(obj_name, ".o(");
        size_t len = (size_t)(end - obj_name);
        if (len > MAX_NAME - 16) len = MAX_NAME - 16;
        memcpy(buf, "[Lib: ", 6);
        memcpy(buf + 6, obj_name, len);
        strcpy(buf + 6 + len, "]");
        return buf;
    }
    if (strlen(obj_name) > 2 && strcmp(obj_name + strlen(obj_name) - 2, ".l") == 0)
        return "[Library]";

    /* Known project directories */
    static const struct { const char *prefix; const char *label; } dir_map[] = {
        {"Core/", "Core"},
        {"Drivers/", "Drivers"},
        {"System/", "System"},
        {"BSP/", "BSP"},
        {"GUI/", "GUI"},
        {"Debug/", "Debug"},
        {"Media/", "Media"},
        {"FATFS/", "FATFS"},
        {"LIBJPEG/", "LIBJPEG"},
        {"Middlewares/", "Middlewares"},
        {"APP/", "APP"},
        {"IAP/", "IAP"},
        {"ELF Loader/", "ELF Loader"},
        {NULL, NULL}
    };
    for (int i = 0; dir_map[i].prefix; i++) {
        /* case-insensitive search */
        const char *p = obj_name;
        const char *q = dir_map[i].prefix;
        while (*p && *q) {
            if (tolower((unsigned char)*p) != tolower((unsigned char)*q)) break;
            p++; q++;
        }
        if (*q == '\0') return dir_map[i].label;
    }

    /* NES emulator */
    {
        const char *p = obj_name;
        if (strstr(p, "nes_") || strstr(p, "6502") || strstr(p, "mapper"))
            return "NES Emulator";
    }
    if (strstr(obj_name, "arduboy")) return "Arduboy";

    /* LVGL */
    if (strncmp(obj_name, "lv_", 3) == 0) return "LVGL";

    /* HAL */
    if (strstr(obj_name, "stm32f4xx_hal") || strstr(obj_name, "stm32f4xx_ll"))
        return "HAL";

    /* FreeRTOS */
    {
        const char *kw[] = {"freertos", "cmsis_os", "port.o", "queue.o",
                            "tasks.o", "timers.o", "heap_", NULL};
        for (int i = 0; kw[i]; i++)
            if (strstr(obj_name, kw[i])) return "FreeRTOS";
    }

    /* C runtime */
    if (strstr(obj_name, "c_w.l")  || strstr(obj_name, "fz_wm.l") ||
        strstr(obj_name, "m_wm.l"))
        return "[C Runtime Library]";

    return "[Other]";
}

typedef struct {
    char     name[64];
    uint32_t flash;
    uint32_t code;
    uint32_t ro_data;
    uint32_t rw_data;
    uint32_t zi_data;
    uint32_t sram;
    int      count;
} DirGroup;

static int dg_cmp_flash_desc(const void *a, const void *b) {
    const DirGroup *da = (const DirGroup *)a;
    const DirGroup *db = (const DirGroup *)b;
    return (db->flash > da->flash) ? 1 : ((db->flash < da->flash) ? -1 : 0);
}

static void print_dirs(const MapData *md, const char *sort_col, int reverse) {
    (void)sort_col;
    if (md->obj_count == 0) {
        printf("No objects to group.\n");
        return;
    }

    /* Aggregate by directory */
    DirGroup groups[64];
    int gcount = 0;
    memset(groups, 0, sizeof(groups));

    for (int i = 0; i < md->obj_count; i++) {
        const ObjectRow *o = &md->objects[i];
        const char *dn = dir_of(o->name);

        /* Find or create group */
        int gi = -1;
        for (int j = 0; j < gcount; j++) {
            if (strcmp(groups[j].name, dn) == 0) { gi = j; break; }
        }
        if (gi < 0) {
            if (gcount >= 64) continue;  /* max 64 groups */
            gi = gcount++;
            SAFE_STRCPY(groups[gi].name, dn, 64);
        }
        DirGroup *g = &groups[gi];
        g->flash   += o->code + o->ro_data + o->rw_data;
        g->code    += o->code;
        g->ro_data += o->ro_data;
        g->rw_data += o->rw_data;
        g->zi_data += o->zi_data;
        g->sram    += o->rw_data + o->zi_data;
        g->count++;
    }

    /* Sort by flash descending (or by chosen column) */
    qsort(groups, (size_t)gcount, sizeof(DirGroup), dg_cmp_flash_desc);

    /* Reverse if needed */
    if (reverse) {
        for (int i = 0; i < gcount / 2; i++) {
            DirGroup tmp = groups[i];
            groups[i] = groups[gcount - 1 - i];
            groups[gcount - 1 - i] = tmp;
        }
    }

    printf("── Directory / Module Groups ──────────────────────────────────────\n");
    printf("%-28s %10s %10s %10s %10s %10s %10s %6s\n",
           "Group", "Flash", "Code", "RO", "RW", "ZI", "SRAM", "Files");
    printf("%-28s %10s %10s %10s %10s %10s %10s %6s\n",
           "────────────────────────────", "", "", "", "", "", "", "");

    for (int i = 0; i < gcount; i++) {
        const DirGroup *g = &groups[i];
        printf("%-28s %10u %10u %10u %10u %10u %10u %6d\n",
               g->name, g->flash, g->code, g->ro_data, g->rw_data,
               g->zi_data, g->sram, g->count);
    }
    printf("\n");
}

/* ── Output: Libraries ─────────────────────────────────────────────────── */

static void print_libraries(const MapData *md, int top_n) {
    if (md->lib_count == 0) {
        printf("No library members found.\n");
        return;
    }

    size_t n = (size_t)md->lib_count;
    ObjectRow *sorted = (ObjectRow *)malloc(n * sizeof(ObjectRow));
    if (!sorted) { fprintf(stderr, "Out of memory.\n"); return; }
    memcpy(sorted, md->libraries, n * sizeof(ObjectRow));
    qsort(sorted, n, sizeof(ObjectRow), cmp_flash_desc);

    if (top_n > 0 && (int)n > top_n) n = (size_t)top_n;

    printf("── Top %zu Library Members (by Flash) ──\n", n);
    printf("%-4s %-36s %10s %10s %10s %10s %10s %10s\n",
           "No.", "Member", "Flash", "Code", "RO", "RW", "ZI", "SRAM");
    printf("──── %-36s %10s %10s %10s %10s %10s %10s\n",
           "────────────────────────────────────", "", "", "", "", "", "");

    for (size_t i = 0; i < n; i++) {
        const ObjectRow *o = &sorted[i];
        printf("%3zu  %-36s %10u %10u %10u %10u %10u %10u\n",
               i + 1, o->name,
               o->code + o->ro_data + o->rw_data,
               o->code, o->ro_data, o->rw_data, o->zi_data,
               o->rw_data + o->zi_data);
    }
    printf("\n");
    free(sorted);
}

/* ── Output: Region details ────────────────────────────────────────────── */

static void print_regions(const MapData *md) {
    printf("── Region Details ─────────────────────────────────────────────────\n");
    printf("Entry Point: 0x%08X\n\n", md->entry_point);

    for (int i = 0; i < md->lr_count; i++) {
        const LoadRegion *lr = &md->load_regions[i];
        printf("Load Region: %s\n", lr->name);
        printf("  Base: 0x%08X   Size: %s   Max: %s\n",
               lr->base, fmt_bytes(lr->size), fmt_bytes(lr->max_size));
        if (lr->compressed_size)
            printf("  Compressed: %s\n", fmt_bytes(lr->compressed_size));
        printf("\n");

        for (int j = 0; j < lr->exec_count; j++) {
            const ExecRegion *er = &lr->exec_regions[j];
            double upct = er->max_size ? (double)er->size / er->max_size * 100.0 : 0.0;
            uint32_t free_sz = er->max_size > er->size ? er->max_size - er->size : 0;
            printf("  Execution Region: %s\n", er->name);
            printf("    Exec Base: 0x%08X   Load Base: 0x%08X\n",
                   er->exec_base, er->load_base);
            printf("    Size: %s   Max: %s   Used: %.1f%%   Free: %s\n",
                   fmt_bytes(er->size), fmt_bytes(er->max_size),
                   upct, fmt_bytes(free_sz));
            if (er->compressed_size)
                printf("    Compressed: %s\n", fmt_bytes(er->compressed_size));
            printf("\n");
        }
    }

    /* Grand totals recap */
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("Grand Totals:\n");
    printf("  Code:          %10u B  (%8.1f KB)\n", md->total_code, md->total_code / 1024.0);
    printf("  RO Data:       %10u B  (%8.1f KB)\n", md->total_ro_data, md->total_ro_data / 1024.0);
    printf("  RW Data:       %10u B  (%8.1f KB)\n", md->total_rw_data, md->total_rw_data / 1024.0);
    printf("  ZI Data:       %10u B  (%8.1f KB)\n", md->total_zi_data, md->total_zi_data / 1024.0);
    printf("  ────────────────────────────────────\n");
    {
        uint32_t flash_u = md->total_code + md->total_ro_data + md->total_rw_data;
        uint32_t sram_u  = md->total_rw_data + md->total_zi_data;
        printf("  FLASH total:   %10u B  (%8.1f KB)\n", flash_u, flash_u / 1024.0);
        printf("  SRAM total:    %10u B  (%8.1f KB)\n", sram_u, sram_u / 1024.0);
        printf("  Object count:  %10d\n", md->obj_count);
        printf("  Library count: %10d\n", md->lib_count);
    }
    printf("\n");
}

/* ── Usage ─────────────────────────────────────────────────────────────── */

static void print_usage(const char *prog) {
    printf(
        "Usage: %s [options] <mapfile>\n"
        "\n"
        "ARMCLANG MAP File Analyzer — parses Keil MDK-ARM (armlink) .map files\n"
        "and displays Flash / SRAM memory usage breakdowns.\n"
        "\n"
        "Options:\n"
        "  -s, --summary       Show summary only (default includes objects + dirs)\n"
        "  -o, --objects       Show object list sorted by flash usage\n"
        "  -d, --dirs          Show directory / module grouping\n"
        "  -l, --libraries     Show library member list\n"
        "  -r, --regions       Show detailed region info\n"
        "  -a, --all           Show all views (summary, objects, dirs, libs, regions)\n"
        "  -n N                Show top N objects / libs (default: %d, 0 = all)\n"
        "  --sort COL          Sort column: name, flash, code, ro, rw, zi, sram, debug\n"
        "                      (default: flash)\n"
        "  -R, --reverse       Reverse sort order\n"
        "  -h, --help          Show this help and exit\n"
        "\n"
        "Examples:\n"
        "  %s project.map                Default: summary + top objects + dirs\n"
        "  %s -a project.map             All views\n"
        "  %s -s project.map             Summary only\n"
        "  %s -o -n 50 --sort sram project.map   Top 50 by SRAM usage\n"
        "\n",
        prog, DEFAULT_TOP_N, prog, prog, prog, prog
    );
}

/* ── Main ──────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    const char  *filepath   = NULL;
    const char  *sort_col   = "flash";
    int          top_n      = DEFAULT_TOP_N;
    int          reverse    = 0;
    int          show_summary = 0;
    int          show_objects = 0;
    int          show_dirs    = 0;
    int          show_libs    = 0;
    int          show_regions = 0;
    int          explicit_views = 0;  /* whether user specified any view flag */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--summary") == 0) {
            show_summary = 1;
            explicit_views = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--objects") == 0) {
            show_objects = 1;
            explicit_views = 1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dirs") == 0) {
            show_dirs = 1;
            explicit_views = 1;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--libraries") == 0) {
            show_libs = 1;
            explicit_views = 1;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--regions") == 0) {
            show_regions = 1;
            explicit_views = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            show_summary = 1;
            show_objects = 1;
            show_dirs    = 1;
            show_libs    = 1;
            show_regions = 1;
            explicit_views = 1;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            top_n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            sort_col = argv[++i];
        } else if (strcmp(argv[i], "-R") == 0 || strcmp(argv[i], "--reverse") == 0) {
            reverse = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
            return 1;
        } else {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        fprintf(stderr, "ERROR: No MAP file specified.\n");
        fprintf(stderr, "Try '%s --help' for usage.\n", argv[0]);
        return 1;
    }

    /* Default views if none explicitly specified */
    if (!explicit_views) {
        show_summary = 1;
        show_objects = 1;
        show_dirs    = 1;
    }

    /* Parse the MAP file */
    MapData *md = parse_map_file(filepath);
    if (!md) return 1;

    /* Output views */
    if (show_summary) print_summary(md);
    if (show_objects) print_objects(md, top_n, sort_col, reverse);
    if (show_dirs)    print_dirs(md, sort_col, reverse);
    if (show_libs)    print_libraries(md, top_n);
    if (show_regions) print_regions(md);

    mapdata_free(md);
    return 0;
}
