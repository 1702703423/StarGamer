#include "Malloc.h"
#include "System_Status.h"

#define SYS_MEM_INVALID 0xFFU
#define SYS_ALIGN8(size) (((size) + 7U) & ~7U)
#define SYS_MEM_BLOCK_START_FLAG ((MT_TYPE)0x8000U)
#define SYS_MEM_BLOCK_COUNT_MASK ((MT_TYPE)0x7FFFU)
#define SYS_MEM_BLOCK_CONTINUE   ((MT_TYPE)0xFFFFU)
#define SYS_MEM_LOCK_UNUSED      ((s32)-1)

__attribute__((section(".bss.ccm_heap_map"), aligned(32), used))
static MT_TYPE mem2mapbase[MEM2_ALLOC_TABLE_SIZE];
#define mem3mapbase ((MT_TYPE *)MEM3_MAP_BASE)
static MT_TYPE thread_stack_map[THREAD_STACK_ALLOC_TABLE_SIZE];

__attribute__((section(".bss.thread_stack_sram"), aligned(8), used))
static u8 thread_stack_pool[THREAD_STACK_POOL_SIZE];

const u32 memtblsize[SRAMBANK] =
{
    MEM2_ALLOC_TABLE_SIZE,
    MEM3_ALLOC_TABLE_SIZE,
    THREAD_STACK_ALLOC_TABLE_SIZE,
};

const u32 memblksize[SRAMBANK] =
{
    MEM2_BLOCK_SIZE,
    MEM3_BLOCK_SIZE,
    THREAD_STACK_BLOCK_SIZE,
};

const u32 memsize[SRAMBANK] =
{
    MEM2_MAX_SIZE,
    MEM3_MAX_SIZE,
    THREAD_STACK_POOL_SIZE,
};

struct _m_mallco_dev mallco_dev =
{
    sys_mem_init,
    sys_mem_perused,
    {
        (u8 *)CCM_HEAP_BASE,
        (u8 *)SRAMEX_BASE,
        thread_stack_pool,
    },
    {
        mem2mapbase,
        mem3mapbase,
        thread_stack_map,
    },
    {
        0U,
        0U,
        0U,
    },
};

static s32 sys_mem_lock(void)
{
    if (osKernelGetState() == osKernelRunning)
    {
        return (s32)osKernelLock();
    }

    return SYS_MEM_LOCK_UNUSED;
}

static void sys_mem_unlock(s32 lock_state)
{
    if (lock_state >= 0)
    {
        (void)osKernelRestoreLock(lock_state);
    }
}

static u8 sys_memx_from_ptr(const void *ptr)
{
    u8 memx;
    u32 addr;

    if (ptr == NULL)
    {
        return SYS_MEM_INVALID;
    }

    addr = (u32)ptr;
    for (memx = 0U; memx < SRAMBANK; memx++)
    {
        u32 base = (u32)mallco_dev.membase[memx];
        u32 end = base + memsize[memx];

        if (addr >= base && addr < end)
        {
            return memx;
        }
    }

    return SYS_MEM_INVALID;
}

static u32 sys_mem_used_size(u8 memx, const void *ptr)
{
    u32 addr;
    u32 base;
    u32 offset;
    u32 index;
    u32 blocks;
    u32 i;
    MT_TYPE entry;

    if (ptr == NULL || memx >= SRAMBANK)
    {
        return 0U;
    }

    addr = (u32)ptr;
    base = (u32)mallco_dev.membase[memx];
    if (addr < base || addr >= (base + memsize[memx]))
    {
        return 0U;
    }

    offset = addr - base;
    if ((offset % memblksize[memx]) != 0U)
    {
        return 0U;
    }
    index = offset / memblksize[memx];
    if (index >= memtblsize[memx])
    {
        return 0U;
    }

    entry = mallco_dev.memmap[memx][index];
    if (entry == 0U || entry == SYS_MEM_BLOCK_CONTINUE ||
        (entry & SYS_MEM_BLOCK_START_FLAG) == 0U)
    {
        return 0U;
    }

    blocks = (u32)(entry & SYS_MEM_BLOCK_COUNT_MASK);
    if (blocks == 0U || blocks > (memtblsize[memx] - index))
    {
        return 0U;
    }

    for (i = 1U; i < blocks; i++)
    {
        if (mallco_dev.memmap[memx][index + i] != SYS_MEM_BLOCK_CONTINUE)
        {
            return 0U;
        }
    }

    return blocks * memblksize[memx];
}

void sys_mem_copy(void *des, void *src, u32 n)
{
    u8 *xdes = des;
    u8 *xsrc = src;

    while (n--)
    {
        *xdes++ = *xsrc++;
    }
}

void sys_mem_set(void *s, u8 c, u32 count)
{
    u8 *xs = s;

    while (count--)
    {
        *xs++ = c;
    }
}

void sys_mem_init(u8 memx)
{
    s32 lock_state;
    u32 mttsize;

    if (memx >= SRAMBANK)
    {
        return;
    }

    lock_state = sys_mem_lock();
    mttsize = sizeof(MT_TYPE);
    sys_mem_set(mallco_dev.memmap[memx], 0U, memtblsize[memx] * mttsize);
    mallco_dev.memrdy[memx] = 1U;
    sys_mem_unlock(lock_state);
}

u16 sys_mem_perused(u8 memx)
{
    u32 used = 0U;
    u32 i;

    if (memx >= SRAMBANK || memtblsize[memx] == 0U)
    {
        return 1000U;
    }

    for (i = 0U; i < memtblsize[memx]; i++)
    {
        if (mallco_dev.memmap[memx][i] != 0U)
        {
            used++;
        }
    }

    return (u16)((used * 1000U) / memtblsize[memx]);
}

static u32 my_mem_malloc(u8 memx, u32 size)
{
    signed long offset;
    u32 nmemb;
    u32 cmemb = 0U;
    u32 i;

    if (memx >= SRAMBANK)
    {
        return 0xFFFFFFFFU;
    }

    if (mallco_dev.memrdy[memx] == 0U)
    {
        mallco_dev.init(memx);
    }

    if (size == 0U)
    {
        return 0xFFFFFFFFU;
    }

    nmemb = size / memblksize[memx];
    if ((size % memblksize[memx]) != 0U)
    {
        nmemb++;
    }

    for (offset = (signed long)memtblsize[memx] - 1L; offset >= 0L; offset--)
    {
        if (mallco_dev.memmap[memx][offset] == 0U)
        {
            cmemb++;
        }
        else
        {
            cmemb = 0U;
        }

        if (cmemb == nmemb)
        {
            mallco_dev.memmap[memx][offset] =
                (MT_TYPE)(SYS_MEM_BLOCK_START_FLAG | (MT_TYPE)nmemb);
            for (i = 1U; i < nmemb; i++)
            {
                mallco_dev.memmap[memx][offset + (signed long)i] = SYS_MEM_BLOCK_CONTINUE;
            }

            return (u32)offset * memblksize[memx];
        }
    }

    return 0xFFFFFFFFU;
}

static u8 my_mem_free(u8 memx, u32 offset)
{
    u32 i;
    u32 index;
    u32 nmemb;
    MT_TYPE entry;

    if (memx >= SRAMBANK)
    {
        return 2U;
    }

    if (mallco_dev.memrdy[memx] == 0U)
    {
        mallco_dev.init(memx);
        return 1U;
    }

    if (offset >= memsize[memx])
    {
        return 2U;
    }

    if ((offset % memblksize[memx]) != 0U)
    {
        return 2U;
    }

    index = offset / memblksize[memx];
    entry = mallco_dev.memmap[memx][index];
    if (entry == 0U || entry == SYS_MEM_BLOCK_CONTINUE ||
        (entry & SYS_MEM_BLOCK_START_FLAG) == 0U)
    {
        return 1U;
    }

    nmemb = (u32)(entry & SYS_MEM_BLOCK_COUNT_MASK);
    if (nmemb == 0U || nmemb > (memtblsize[memx] - index))
    {
        return 2U;
    }

    for (i = 1U; i < nmemb; i++)
    {
        if (mallco_dev.memmap[memx][index + i] != SYS_MEM_BLOCK_CONTINUE)
        {
            return 2U;
        }
    }

    for (i = 0U; i < nmemb; i++)
    {
        mallco_dev.memmap[memx][index + i] = 0U;
    }

    return 0U;
}

void sys_free(u8 memx, void *ptr)
{
    s32 lock_state;
    u32 offset;

    if (ptr == NULL || memx >= SRAMBANK)
    {
        return;
    }

    lock_state = sys_mem_lock();
    offset = (u32)ptr - (u32)mallco_dev.membase[memx];
    (void)my_mem_free(memx, offset);
    sys_mem_unlock(lock_state);
}

void *sys_malloc(u8 memx, u32 size)
{
    s32 lock_state;
    u32 offset;
    void *ptr = NULL;

    if (memx >= SRAMBANK)
    {
        return NULL;
    }

    lock_state = sys_mem_lock();
    offset = my_mem_malloc(memx, size);
    if (offset != 0xFFFFFFFFU)
    {
        ptr = (void *)((u32)mallco_dev.membase[memx] + offset);
    }
    sys_mem_unlock(lock_state);

    return ptr;
}

void *myrealloc(u8 memx, void *ptr, u32 size)
{
    s32 lock_state;
    u32 offset;
    u32 old_offset;
    u32 copy_size;
    void *new_ptr;

    if (ptr == NULL)
    {
        return sys_malloc(memx, size);
    }

    if (sys_memx_from_ptr(ptr) != memx)
    {
        return NULL;
    }

    if (size == 0U)
    {
        sys_free(memx, ptr);
        return NULL;
    }

    lock_state = sys_mem_lock();
    copy_size = sys_mem_used_size(memx, ptr);
    if (copy_size == 0U)
    {
        sys_mem_unlock(lock_state);
        return NULL;
    }

    offset = my_mem_malloc(memx, size);
    if (offset == 0xFFFFFFFFU)
    {
        sys_mem_unlock(lock_state);
        return NULL;
    }

    new_ptr = (void *)((u32)mallco_dev.membase[memx] + offset);
    if (copy_size > size)
    {
        copy_size = size;
    }
    if (copy_size != 0U)
    {
        sys_mem_copy(new_ptr, ptr, copy_size);
    }

    old_offset = (u32)ptr - (u32)mallco_dev.membase[memx];
    (void)my_mem_free(memx, old_offset);
    sys_mem_unlock(lock_state);

    return new_ptr;
}

void *sys_realloc(u8 memx, void *ptr, u32 size)
{
    return myrealloc(memx, ptr, size);
}
void *sys_malloc_aligned(u8 memx, u32 size, u32 alignment)
{
    void *raw;
    u32 raw_addr;
    u32 aligned_addr;
    u32 overhead;

    if (memx >= SRAMBANK || size == 0U)
    {
        return NULL;
    }

    if (alignment < sizeof(void *))
    {
        alignment = sizeof(void *);
    }
    if ((alignment & (alignment - 1U)) != 0U)
    {
        return NULL;
    }

    overhead = alignment + sizeof(void *);
    if (overhead < alignment || size > (0xFFFFFFFFU - overhead))
    {
        return NULL;
    }

    raw = sys_malloc(memx, size + overhead);
    if (raw == NULL)
    {
        return NULL;
    }

    raw_addr = (u32)raw;
    aligned_addr = (raw_addr + sizeof(void *) + alignment - 1U) & ~(alignment - 1U);
    *((void **)(aligned_addr - sizeof(void *))) = raw;

    return (void *)aligned_addr;
}

void sys_free_aligned(u8 memx, void *ptr)
{
    void *raw;
    u32 addr;
    u32 base;
    u32 end;

    if (ptr == NULL || memx >= SRAMBANK)
    {
        return;
    }

    addr = (u32)ptr;
    base = (u32)mallco_dev.membase[memx];
    end = base + memsize[memx];
    if (addr < (base + sizeof(void *)) || addr >= end)
    {
        return;
    }

    raw = *((void **)(addr - sizeof(void *)));
    if ((u32)raw < base || (u32)raw >= addr || (u32)raw >= end)
    {
        return;
    }

    sys_free(memx, raw);
}

void *sys_thread_stack_alloc(u32 stack_size)
{
    void *stack;
    u32 alloc_size;

    if (stack_size == 0U)
    {
        return NULL;
    }

    alloc_size = SYS_ALIGN8(stack_size);
    stack = sys_malloc_aligned(SRAMSTACK, alloc_size, 8U);
    if (stack != NULL)
    {
        sys_mem_set(stack, 0U, alloc_size);
    }

    return stack;
}

void sys_thread_stack_free(void *ptr)
{
    sys_free_aligned(SRAMSTACK, ptr);
}

void sys_thread_stack_get_stats(thread_stack_stats_t *stats)
{
    mem_frag_info_t frag;

    if (stats == NULL)
    {
        return;
    }

    sys_mem_frag_info(SRAMSTACK, &frag);
    stats->region_size = THREAD_STACK_REGION_SIZE;
    stats->fixed_bytes = THREAD_STACK_FIXED_SIZE;
    stats->dynamic_pool_size = THREAD_STACK_POOL_SIZE;
    stats->dynamic_used_bytes = frag.used_blocks * frag.block_size;
    stats->free_bytes = frag.free_blocks * frag.block_size;
    stats->largest_free_block = frag.largest_free_blocks * frag.block_size;
}

void *lv_mem_malloc(u32 size)
{
    return sys_malloc(LVGL_MEM_POOL, size);
}

void lv__mem_free(void *ptr)
{
    u8 memx;

    if (ptr == NULL)
    {
        return;
    }

    memx = sys_memx_from_ptr(ptr);
    if (memx == SYS_MEM_INVALID)
    {
        return;
    }

    sys_free(memx, ptr);
}

void *lv__mem_realloc(void *ptr, u32 size)
{
    u8 memx;

    if (ptr == NULL)
    {
        return lv_mem_malloc(size);
    }

    if (size == 0U)
    {
        lv__mem_free(ptr);
        return NULL;
    }

    memx = sys_memx_from_ptr(ptr);
    if (memx == SYS_MEM_INVALID)
    {
        return NULL;
    }

    return myrealloc(memx, ptr, size);
}

/* ---- Memory fragmentation analysis ---- */
void sys_mem_frag_info(u8 memx, mem_frag_info_t *info)
{
    u32 i;
    u32 in_hole;
    u32 hole_start;
    u32 current_free;

    if (info == NULL || memx >= SRAMBANK)
    {
        return;
    }

    /* Zero out the struct */
    sys_mem_set(info, 0U, sizeof(mem_frag_info_t));

    if (mallco_dev.memrdy[memx] == 0U)
    {
        return;
    }

    info->block_size = memblksize[memx];
    info->total_blocks = memtblsize[memx];

    /* Count used blocks */
    for (i = 0U; i < info->total_blocks; i++)
    {
        if (mallco_dev.memmap[memx][i] != 0U)
        {
            info->used_blocks++;
        }
    }
    info->free_blocks = info->total_blocks - info->used_blocks;

    /* Scan for free holes and fragments */
    in_hole = 0U;
    hole_start = 0U;
    current_free = 0U;

    for (i = 0U; i < info->total_blocks; i++)
    {
        if (mallco_dev.memmap[memx][i] == 0U)
        {
            if (in_hole == 0U)
            {
                /* Entering a new hole */
                in_hole = 1U;
                hole_start = i;
                current_free = 0U;
            }
            current_free++;
        }
        else
        {
            if (in_hole != 0U)
            {
                /* Exiting a hole */
                info->hole_count++;

                /* Fragment: hole has used blocks on BOTH sides */
                if (hole_start > 0U)
                {
                    info->frag_count++;
                }

                if (current_free > info->largest_free_blocks)
                {
                    info->largest_free_blocks = current_free;
                }

                in_hole = 0U;
            }
        }
    }

    /* Handle trailing hole (not a fragment, but count it) */
    if (in_hole != 0U)
    {
        info->hole_count++;
        /* Trailing hole is not a fragment (no used block after it) */

        if (current_free > info->largest_free_blocks)
        {
            info->largest_free_blocks = current_free;
        }
    }
}
