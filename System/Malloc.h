#ifndef __MALLOC_H
#define __MALLOC_H

#include "sys.h"
#include "cmsis_os.h"
#include "ccm_memory.h"

#define CCMRAMSize                 CCM_HEAP_SIZE

#define MT_TYPE                    u16
#define MT_TYPE_SIZE               2UL

#define SRAMCCM                    0U
#define SRAMEX                     1U
#define LVGL_MEM_POOL              SRAMEX

#define SRAMBANK                   2U

#define MEM2_BLOCK_SIZE            32U
#define MEM2_MAX_SIZE              CCMRAMSize
#define MEM2_ALLOC_TABLE_SIZE      (MEM2_MAX_SIZE / MEM2_BLOCK_SIZE)

#define SRAMEX_BASE                0x68000000UL
#define SRAMEX_TOTAL_SIZE          (1024UL * 1024UL)
#define MEM3_BLOCK_SIZE            32U
#define MEM3_MAX_SIZE              (960UL * 1024UL)
#define MEM3_ALLOC_TABLE_SIZE      (MEM3_MAX_SIZE / MEM3_BLOCK_SIZE)
#define MEM3_MAP_BASE              (SRAMEX_BASE + MEM3_MAX_SIZE)
#define MEM3_MAP_SIZE              (MEM3_ALLOC_TABLE_SIZE * MT_TYPE_SIZE)
#define MEM3_GUARD_BASE            (MEM3_MAP_BASE + MEM3_MAP_SIZE)
#define MEM3_GUARD_SIZE            (SRAMEX_TOTAL_SIZE - (MEM3_MAX_SIZE + MEM3_MAP_SIZE))

#define SRAM_ADDR_BEGIN            ((u32)SRAMEX_BASE)
#define SRAM_ADDR_HALF             ((u32)0x68080000UL)
#define SRAM_ADDR_END              ((u32)0x680FFFFFUL)

#if ((MEM3_MAX_SIZE + MEM3_MAP_SIZE) > SRAMEX_TOTAL_SIZE)
#error "External SRAM heap/map exceeds SRAM size"
#endif

#if (MEM3_GUARD_SIZE < (4UL * 1024UL))
#error "External SRAM guard region is too small"
#endif

struct _m_mallco_dev
{
    void (*init)(u8 memx);
    u16 (*perused)(u8 memx);
    u8 *membase[SRAMBANK];
    MT_TYPE *memmap[SRAMBANK];
    u8 memrdy[SRAMBANK];
};

extern struct _m_mallco_dev mallco_dev;

void sys_mem_init(u8 memx);
u16 sys_mem_perused(u8 memx);
void sys_mem_set(void *s, u8 c, u32 count);
void sys_mem_copy(void *des, void *src, u32 n);
void sys_free(u8 memx, void *ptr);
void *sys_malloc(u8 memx, u32 size);
void *myrealloc(u8 memx, void *ptr, u32 size);
void *sys_realloc(u8 memx, void *ptr, u32 size);
void *sys_malloc_aligned(u8 memx, u32 size, u32 alignment);
void sys_free_aligned(u8 memx, void *ptr);
#define sys_malloc_align sys_malloc_aligned
#define sys_free_align   sys_free_aligned
void *sys_thread_stack_alloc(u32 stack_size);
void sys_thread_stack_free(void *ptr);
void *lv_mem_malloc(u32 size);
void lv__mem_free(void *ptr);
void *lv__mem_realloc(void *ptr, u32 size);

#endif
