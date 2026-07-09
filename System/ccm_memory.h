#ifndef __CCM_MEMORY_H
#define __CCM_MEMORY_H

#define CCM_TOTAL_SIZE              (64U * 1024U)
#define CCM_HEAP_BASE               0x10000000UL
#define CCM_HEAP_SIZE               CCM_TOTAL_SIZE
#define CCM_HEAP_END                (CCM_HEAP_BASE + CCM_HEAP_SIZE)
#define CCM_HEAP_SIZE_KB            (CCM_HEAP_SIZE / 1024U)

#if (CCM_HEAP_END > 0x10010000UL)
#error "CCM heap exceeds CCM SRAM size"
#endif

#endif
