#ifndef __MALLOC_H
#define __MALLOC_H

#include "sys.h"
#include "cmsis_os.h"

#define CCMRAMSize (60 * 1024)
#define ST7789s_ScreenBufferSize (150*1024)

/* 定义内存管理表类型,当外扩SDRAM的时候，必须使用uint32_t类型，否则可以定义成uint16_t，以节省内存占用 */
#define MT_TYPE     uint16_t

/* 定义3个内存池 */
#define SRAMCCM                 0                               /* CCM内存池(此部分SRAM仅仅CPU可以访问!!!) */
#define SRAMEX                  1                               /* 外部内存池 */

#define SRAMBANK                2                               /* 定义支持的SRAM块数 */

/* mem3内存参数设定.mem3是外扩SRAM */
#define MEM3_BLOCK_SIZE         32                              /* 内存块大小为32字节 */
#define MEM3_MAX_SIZE           963 * 1024                      /* 最大管理内存963K */
#define MEM3_ALLOC_TABLE_SIZE   MEM3_MAX_SIZE/MEM3_BLOCK_SIZE   /* 内存表大小 */

//paramenter

#define SRAM_ADDR_BEGIN 	(uint32_t)0x68000000
#define SRAM_ADDR_HALF		(uint32_t)0x68080000
#define SRAM_ADDR_END			(uint32_t)0x680FFFFF

/* mem2内存参数设定.mem2处于CCM,用于管理CCM(特别注意,这部分SRAM,仅CPU可以访问!!) */
#define MEM2_BLOCK_SIZE         32                              /* 内存块大小为32字节 */
#define MEM2_MAX_SIZE           60 * 1024                       /* 最大管理内存60K */
#define MEM2_ALLOC_TABLE_SIZE   MEM2_MAX_SIZE/MEM2_BLOCK_SIZE   /* 内存表大小 */

/* 内存管理控制器 */
struct _m_mallco_dev
{
    void (*init)(uint8_t);              /* 初始化 */
    uint16_t (*perused)(uint8_t);       /* 内存使用率 */
    uint8_t *membase[SRAMBANK];         /* 内存池 管理SRAMBANK个区域的内存 */
    MT_TYPE *memmap[SRAMBANK];          /* 内存管理状态表 */
    uint8_t  memrdy[SRAMBANK];          /* 内存管理是否就绪 */
};

extern struct _m_mallco_dev mallco_dev; /* 在mallco.c里面定义 */

/* 函数声明 */
void sys_mem_init(uint8_t memx);                             /* 内存管理初始化函数(外/内部调用) */
uint16_t sys_mem_perused(uint8_t memx) ;                     /* 获得内存使用率(外/内部调用) */
void sys_mem_set(void *s, uint8_t c, uint32_t count);        /* 内存设置函数 */
void sys_mem_copy(void *des, void *src, uint32_t n);         /* 内存拷贝函数 */
void sys_free(uint8_t memx, void *ptr);                       /* 内存释放(外部调用) */
void *sys_malloc(uint8_t memx, uint32_t size);                /* 内存分配(外部调用) */
void *sys_realloc(uint8_t memx, void *ptr, uint32_t size);    /* 重新分配内存(外部调用) */
void *sys_malloc_align(uint8_t memx, uint32_t size, uint32_t align);
void sys_free_align(uint8_t memx, void *ptr);
void *lv_mem_malloc(uint32_t size);
void lv__mem_free(void* ptr);
void *lv__mem_realloc(void* ptr,uint32_t size);

#endif
