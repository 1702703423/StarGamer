#include "Malloc.h"
#include "System_Status.h"

static __ALIGNED(64) uint8_t CCMRAM[CCMRAMSize] __attribute__((section(".bss.ARM.__at_0x10000000")));
static __ALIGNED(64) uint8_t mem3base[MEM3_MAX_SIZE] __attribute__((section(".bss.ARM.__at_0x68000000")));      /* 外部SRAM内存池 */ 

/* 内存管理参数 */
const uint32_t memtblsize[SRAMBANK] = {MEM2_ALLOC_TABLE_SIZE, MEM3_ALLOC_TABLE_SIZE};    /* 内存表大小 */
const uint32_t memblksize[SRAMBANK] = {MEM2_BLOCK_SIZE, MEM3_BLOCK_SIZE};                      /* 内存分块大小 */
const uint32_t memsize[SRAMBANK] = {MEM2_MAX_SIZE, MEM3_MAX_SIZE};                               /* 内存总大小 */

/* 内存管理表 */                                                            /* 内部SRAM内存池MAP */
static MT_TYPE mem2mapbase[MEM2_ALLOC_TABLE_SIZE] __attribute__((section(".bss.ARM.__at_0x1000F000")));         /* 内部CCM内存池MAP */
static MT_TYPE mem3mapbase[MEM3_ALLOC_TABLE_SIZE] __attribute__((section(".bss.ARM.__at_0x680F0C00")));         /* 外部SRAM内存池MAP */

/* 内存管理控制器 */
struct _m_mallco_dev mallco_dev =
{
    sys_mem_init,                                /* 内存初始化 */
    sys_mem_perused,                             /* 内存使用率 */
    CCMRAM, mem3base,               /* 内存池 */
    mem2mapbase, mem3mapbase,      /* 内存管理状态表 */
    0, 0,                                    /* 内存管理未就绪 */
};

/**
 * @brief       复制内存
 * @param       *des : 目的地址
 * @param       *src : 源地址
 * @param       n    : 需要复制的内存长度(字节为单位)
 * @retval      无
 */
void sys_mem_copy(void *des, void *src, uint32_t n)
{
    uint8_t *xdes = des;
    uint8_t *xsrc = src;

    while (n--) *xdes++ = *xsrc++;
}

/**
 * @brief       设置内存值
 * @param       *s    : 内存首地址
 * @param       c     : 要设置的值
 * @param       count : 需要设置的内存大小(字节为单位)
 * @retval      无
 */
void sys_mem_set(void *s, uint8_t c, uint32_t count)
{
    uint8_t *xs = s;

    while (count--) *xs++ = c;
}

/**
 * @brief       内存管理初始化
 * @param       memx : 所属内存块
 * @retval      无
 */
void sys_mem_init(uint8_t memx)
{
	osKernelLock();
    uint8_t mttsize = sizeof(MT_TYPE);  /* 获取memmap数组的类型长度(uint16_t /uint32_t)*/
    sys_mem_set(mallco_dev.memmap[memx], 0, memtblsize[memx] * mttsize); /* 内存状态表数据清零 */
    mallco_dev.memrdy[memx] = 1;        /* 内存管理初始化OK */
	osKernelUnlock();
}

/**
 * @brief       获取内存使用率
 * @param       memx : 所属内存块
 * @retval      使用率(扩大了10倍,0~1000,代表0.0%~100.0%)
 */
uint16_t sys_mem_perused(uint8_t memx)
{
    uint32_t used = 0;
    uint32_t i;

    for (i = 0; i < memtblsize[memx]; i++)
    {
        if (mallco_dev.memmap[memx][i]) used++;
    }

    return (used * 1000) / (memtblsize[memx]);
}

/**
 * @brief       内存分配(内部调用)
 * @param       memx : 所属内存块
 * @param       size : 要分配的内存大小(字节)
 * @retval      内存偏移地址
 *   @arg       0 ~ 0xFFFFFFFE : 有效的内存偏移地址
 *   @arg       0xFFFFFFFF     : 无效的内存偏移地址
 */
static uint32_t my_mem_malloc(uint8_t memx, uint32_t size)
{
    signed long offset = 0;
    uint32_t nmemb;         /* 需要的内存块数 */
    uint32_t cmemb = 0;     /* 连续空内存块数 */
    uint32_t i;

    if (!mallco_dev.memrdy[memx])
    {
        mallco_dev.init(memx);          /* 未初始化,先执行初始化 */
    }
    
    if (size == 0) return 0xFFFFFFFF;   /* 不需要分配 */

    nmemb = size / memblksize[memx];    /* 获取需要分配的连续内存块数 */

    if (size % memblksize[memx]) nmemb++;

    for (offset = memtblsize[memx] - 1; offset >= 0; offset--)  /* 搜索整个内存控制区 */
    {
        if (!mallco_dev.memmap[memx][offset])
        {
            cmemb++;                    /* 连续空内存块数增加 */
        }
        else 
        {
            cmemb = 0;                  /* 连续内存块清零 */
        }
        
        if (cmemb == nmemb)             /* 找到了连续nmemb个空内存块 */
        {
            for (i = 0; i < nmemb; i++) /* 标注内存块非空 */
            {
                mallco_dev.memmap[memx][offset + i] = nmemb;
            }

            return (offset * memblksize[memx]); /* 返回偏移地址 */
        }
    }

    return 0xFFFFFFFF;                  /* 未找到符合分配条件的内存块 */
}

/**
 * @brief       释放内存(内部调用)
 * @param       memx   : 所属内存块
 * @param       offset : 内存地址偏移
 * @retval      释放结果
 *   @arg       0, 释放成功;
 *   @arg       1, 释放失败;
 *   @arg       2, 超区域了(失败);
 */
static uint8_t my_mem_free(uint8_t memx, uint32_t offset)
{
    int i;

    if (!mallco_dev.memrdy[memx])   /* 未初始化,先执行初始化 */
    {
        mallco_dev.init(memx);
        return 1;                   /* 未初始化 */
    }

    if (offset < memsize[memx])     /* 偏移在内存池内. */
    {
        int index = offset / memblksize[memx];      /* 偏移所在内存块号码 */
        int nmemb = mallco_dev.memmap[memx][index]; /* 内存块数量 */

        for (i = 0; i < nmemb; i++)                 /* 内存块清零 */
        {
            mallco_dev.memmap[memx][index + i] = 0;
        }

        return 0;
    }
    else
    {
        return 2;   /* 偏移超区了 */
    }
}

/**
 * @brief       释放内存(外部调用)
 * @param       memx : 所属内存块
 * @param       ptr  : 内存首地址
 * @retval      无
 */
void sys_free(uint8_t memx, void *ptr)
{
		osKernelLock();
	
    uint32_t offset;

    if (ptr == NULL) 
		{
			osKernelUnlock();
			return;
		}    /* 地址为0. */

    offset = (uint32_t)ptr - (uint32_t)mallco_dev.membase[memx];
    my_mem_free(memx, offset);  /* 释放内存 */
	
		osKernelUnlock();
}

/**
 * @brief       分配内存(外部调用)
 * @param       memx : 所属内存块
 * @param       size : 要分配的内存大小(字节)
 * @retval      分配到的内存首地址.
 */
void *sys_malloc(uint8_t memx, uint32_t size)
{
		osKernelLock();
	
    uint32_t offset;
    
    offset = my_mem_malloc(memx, size);
    if (offset == 0xFFFFFFFF)   /* 申请出错 */
    {
				osKernelUnlock();
        return NULL;            /* 返回空(0) */
    }
    else                        /* 申请没问题, 返回首地址 */
    {
				osKernelUnlock();
        return (void *)((uint32_t)mallco_dev.membase[memx] + offset);
    }
		
		osKernelUnlock();
}

/**
 * @brief       重新分配内存(外部调用)
 * @param       memx : 所属内存块
 * @param       *ptr : 旧内存首地址
 * @param       size : 要分配的内存大小(字节)
 * @retval      新分配到的内存首地址.
 */
void *myrealloc(uint8_t memx, void *ptr, uint32_t size)
{
    uint32_t offset;
    
    offset = my_mem_malloc(memx, size);
    if (offset == 0xFFFFFFFF)   /* 申请出错 */
    {
        return NULL;            /* 返回空(0) */
    }
    else                        /* 申请没问题, 返回首地址 */
    {
        sys_mem_copy((void *)((uint32_t)mallco_dev.membase[memx] + offset), ptr, size);  /* 拷贝旧内存内容到新内存 */
        sys_free(memx, ptr);      /* 释放旧内存 */
        return (void *)((uint32_t)mallco_dev.membase[memx] + offset);                   /* 返回新内存首地址 */
    }
}

/**
 * @brief       分配对齐内存(外部调用)
 * @param       memx      : 所属内存块
 * @param       size      : 要分配的内存大小(字节)
 * @param       alignment : 对齐字节数(必须是2的幂，且 >= 4)
 * @retval      对齐后的内存首地址, NULL表示失败
 */
void *sys_malloc_aligned(uint8_t memx, uint32_t size, uint32_t alignment)
{
    void *raw;
    uint32_t raw_addr;
    uint32_t aligned_addr;
    if (alignment < 4) alignment = 4;               /* 最小4字节对齐，保证能存放原始指针 */
    if (alignment & (alignment - 1)) return NULL;    /* 非2的幂，返回失败 */
    /* 多分配 alignment + sizeof(void*) 字节，确保能对齐且能存指针 */
    raw = sys_malloc(memx, size + alignment + sizeof(void *));
    if (raw == NULL) return NULL;
    raw_addr = (uint32_t)raw;
    /* 在 raw 之后留出存指针的空间，再向上对齐 */
    aligned_addr = (raw_addr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    /* 在对齐地址的前4字节存放原始指针，释放时取回 */
    *((void **)(aligned_addr - sizeof(void *))) = raw;
    return (void *)aligned_addr;
}
/**
 * @brief       释放对齐内存(外部调用)
 * @param       memx : 所属内存块
 * @param       ptr  : 对齐内存首地址(必须由sys_malloc_aligned返回)
 * @retval      无
 */
void sys_free_aligned(uint8_t memx, void *ptr)
{
    void *raw;
    if (ptr == NULL) return;
    /* 从对齐地址前4字节取回原始指针 */
    raw = *((void **)((uint32_t)ptr - sizeof(void *)));
    sys_free(memx, raw);
}

void *lv_mem_malloc(uint32_t size)
{
	return (void*)sys_malloc(0,size);
}

void lv__mem_free(void* ptr)
{
	sys_free(0,ptr);
}

void *lv__mem_realloc(void* ptr,uint32_t size)
{
	return (void*)myrealloc(0,ptr,size);
}
