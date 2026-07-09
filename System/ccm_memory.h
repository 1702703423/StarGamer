#ifndef __CCM_MEMORY_H
#define __CCM_MEMORY_H

#define CCM_TOTAL_SIZE              (64U * 1024U)
#define CCM_THREAD_STACK_SIZE       (40U * 1024U)
#define CCM_THREAD_STACK_BASE       0x10000000UL
#define CCM_THREAD_STACK_END        (CCM_THREAD_STACK_BASE + CCM_THREAD_STACK_SIZE)

#define CCM_HEAP_SIZE               (20U * 1024U)
#define CCM_HEAP_BASE               0x1000A000UL
#define CCM_HEAP_END                (CCM_HEAP_BASE + CCM_HEAP_SIZE)
#define CCM_HEAP_MAP_BASE           0x1000F000UL

#define CCM_HEAP_SECTION            ".bss.ARM.__at_0x1000A000"
#define CCM_HEAP_MAP_SECTION        ".bss.ARM.__at_0x1000F000"

#define CCM_THREAD_STACK_SIZE_KB    (CCM_THREAD_STACK_SIZE / 1024U)
#define CCM_HEAP_SIZE_KB            (CCM_HEAP_SIZE / 1024U)

#if (CCM_THREAD_STACK_END > CCM_HEAP_BASE)
#error "CCM thread stack region overlaps CCM heap"
#endif

#if (CCM_HEAP_END > CCM_HEAP_MAP_BASE)
#error "CCM heap overlaps CCM heap map"
#endif

#if defined(__CC_ARM) || defined(__ARMCC_VERSION) || defined(__GNUC__)
#define CCM_THREAD_STACK            __attribute__((section(".ccm_thread_stack"), aligned(8)))
#else
#define CCM_THREAD_STACK
#endif

#endif
