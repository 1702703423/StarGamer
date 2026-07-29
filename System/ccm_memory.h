#ifndef __CCM_MEMORY_H
#define __CCM_MEMORY_H

#define CCM_TOTAL_BASE              0x10000000UL
#define CCM_TOTAL_SIZE              (64UL * 1024UL)
#define CCM_HEAP_BASE               CCM_TOTAL_BASE
#define CCM_HEAP_SIZE               (60UL * 1024UL)
#define CCM_HEAP_END                (CCM_HEAP_BASE + CCM_HEAP_SIZE)
#define CCM_HEAP_SIZE_KB            (CCM_HEAP_SIZE / 1024U)
#define CCM_ALLOC_MAP_BASE          CCM_HEAP_END
#define CCM_ALLOC_MAP_SIZE          (4UL * 1024UL)
#define CCM_TOTAL_END               (CCM_TOTAL_BASE + CCM_TOTAL_SIZE)

#if ((CCM_ALLOC_MAP_BASE + CCM_ALLOC_MAP_SIZE) > CCM_TOTAL_END)
#error "CCM heap and allocator map exceed CCM SRAM size"
#endif

#endif
