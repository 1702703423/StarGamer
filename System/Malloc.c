#include "Malloc.h"
#include "System_Status.h"

#define SYS_MEM_INVALID 0xFFU
#define SYS_ALIGN8(size) (((size) + 7U) & ~7U)

static MT_TYPE mem2mapbase[MEM2_ALLOC_TABLE_SIZE];
#define mem3mapbase ((MT_TYPE *)MEM3_MAP_BASE)

const u32 memtblsize[SRAMBANK] =
{
    MEM2_ALLOC_TABLE_SIZE,
    MEM3_ALLOC_TABLE_SIZE,
};

const u32 memblksize[SRAMBANK] =
{
    MEM2_BLOCK_SIZE,
    MEM3_BLOCK_SIZE,
};

const u32 memsize[SRAMBANK] =
{
    MEM2_MAX_SIZE,
    MEM3_MAX_SIZE,
};

struct _m_mallco_dev mallco_dev =
{
    sys_mem_init,
    sys_mem_perused,
    {
        (u8 *)CCM_HEAP_BASE,
        (u8 *)SRAMEX_BASE,
    },
    {
        mem2mapbase,
        mem3mapbase,
    },
    {
        0U,
        0U,
    },
};

static u8 sys_mem_lock(void)
{
    if (osKernelGetState() == osKernelRunning)
    {
        if (osKernelLock() >= 0)
        {
            return 1U;
        }
    }

    return 0U;
}

static void sys_mem_unlock(u8 locked)
{
    if (locked != 0U)
    {
        (void)osKernelUnlock();
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
    index = offset / memblksize[memx];
    if (index >= memtblsize[memx])
    {
        return 0U;
    }

    blocks = mallco_dev.memmap[memx][index];
    if (blocks == 0U)
    {
        return 0U;
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
    u8 locked;
    u32 mttsize;

    if (memx >= SRAMBANK)
    {
        return;
    }

    locked = sys_mem_lock();
    mttsize = sizeof(MT_TYPE);
    sys_mem_set(mallco_dev.memmap[memx], 0U, memtblsize[memx] * mttsize);
    mallco_dev.memrdy[memx] = 1U;
    sys_mem_unlock(locked);
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
            for (i = 0U; i < nmemb; i++)
            {
                mallco_dev.memmap[memx][offset + (signed long)i] = (MT_TYPE)nmemb;
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

    index = offset / memblksize[memx];
    nmemb = mallco_dev.memmap[memx][index];
    if (nmemb == 0U)
    {
        return 1U;
    }

    for (i = 0U; i < nmemb && (index + i) < memtblsize[memx]; i++)
    {
        mallco_dev.memmap[memx][index + i] = 0U;
    }

    return 0U;
}

void sys_free(u8 memx, void *ptr)
{
    u8 locked;
    u32 offset;

    if (ptr == NULL || memx >= SRAMBANK)
    {
        return;
    }

    locked = sys_mem_lock();
    offset = (u32)ptr - (u32)mallco_dev.membase[memx];
    (void)my_mem_free(memx, offset);
    sys_mem_unlock(locked);
}

void *sys_malloc(u8 memx, u32 size)
{
    u8 locked;
    u32 offset;
    void *ptr = NULL;

    if (memx >= SRAMBANK)
    {
        return NULL;
    }

    locked = sys_mem_lock();
    offset = my_mem_malloc(memx, size);
    if (offset != 0xFFFFFFFFU)
    {
        ptr = (void *)((u32)mallco_dev.membase[memx] + offset);
    }
    sys_mem_unlock(locked);

    return ptr;
}

void *myrealloc(u8 memx, void *ptr, u32 size)
{
    u8 locked;
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

    locked = sys_mem_lock();
    offset = my_mem_malloc(memx, size);
    if (offset == 0xFFFFFFFFU)
    {
        sys_mem_unlock(locked);
        return NULL;
    }

    new_ptr = (void *)((u32)mallco_dev.membase[memx] + offset);
    copy_size = sys_mem_used_size(memx, ptr);
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
    sys_mem_unlock(locked);

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

    if (alignment < 4U)
    {
        alignment = 4U;
    }
    if ((alignment & (alignment - 1U)) != 0U)
    {
        return NULL;
    }

    raw = sys_malloc(memx, size + alignment + sizeof(void *));
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

    if (ptr == NULL)
    {
        return;
    }

    raw = *((void **)((u32)ptr - sizeof(void *)));
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
    stack = sys_malloc_aligned(SRAMCCM, alloc_size, 8U);
    if (stack != NULL)
    {
        sys_mem_set(stack, 0U, alloc_size);
    }

    return stack;
}

void sys_thread_stack_free(void *ptr)
{
    sys_free_aligned(SRAMCCM, ptr);
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
