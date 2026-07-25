/*
*   @file  dl_port.c
*   @author  wzh
*   @version 1.0
*   @date 2021-12-3
*   @description 与硬件相关的底层函数接口
*   @detail	此处的函数原型需要自己提供
*/
#include "dl_port.h"
#include "ff.h"
#include <stdlib.h>
#include "FreeRTOS.h"

extern void *aligned_alloc(size_t align_byte,size_t size);

//以给定的地址对齐分配一块内存
//align_byte:给定的地址对齐字节
//size:内存大小
//返回值：void* 内存地址，分配失败时返回NULL
void* dl_port_aligned_alloc(size_t align_byte,size_t size){
	u32 alignment = (u32)align_byte;

	if(alignment < 4U){
		alignment = 4U;
	}
	if((alignment & (alignment - 1U)) != 0U){
		return NULL;
	}
	return sys_malloc_aligned(SRAMEX,(u32)size,alignment);
}

//释放对齐分配的内存
//ptr:对齐内存的地址
void dl_port_aligned_free(void* ptr){
	sys_free_aligned(SRAMEX,ptr);
}

//分配一块指定大小的内存
//size:内存大小
//返回值：void* 内存地址，分配失败时返回NULL
void* dl_port_malloc(size_t size){
	return sys_malloc(1,size);
}

//释放内存
//ptr:内存地址
void dl_port_free(void* ptr){
	sys_free(1,ptr);
}

//打开一个文件
//file_name:文件名
//返回值：void*文件句柄，打开失败时返回NULL
void* dl_port_open_file(const char* file_name){
	FIL* pfile;
	
	pfile=sys_malloc(1,sizeof(FIL));
	if(pfile==NULL){
		return NULL;
	}
	
	if(f_open(pfile,file_name,FA_READ)!=FR_OK){
		sys_free(1,pfile);
		return NULL;
	}
	
	return (void*)pfile;
}

//获取文件大小
//file_handle:文件句柄
//返回值：文件大小
size_t dl_port_get_file_size(void* file_handle){
	return f_size((FIL*)file_handle);
}

//读取文件
//file_handle:文件句柄
//buf:目标地址
//len:读取的数据长度
//返回值：读取成功返回true，否则返回false
bool dl_port_read_file(void* file_handle,void* buf,size_t len){
	UINT br;
	return f_read(file_handle,buf,len,&br)==FR_OK && len==br;
}

//关闭文件
//file_handle：文件句柄
void dl_port_close_file(void* file_handle){
	f_close(file_handle);
	sys_free(1,file_handle);
}


#if defined(DL_CACHE_USE)&& DL_CACHE_USE== 1
#include "stm32h7xx.h"
//无效化I_Cache，否则会有小概率在执行动态加载的函数时出现硬件错误
//addr：无效化的内存首地址
//size：无效化的内存大小
void dl_port_cache_process(void* addr,size_t size){
	SCB_CleanDCache_by_Addr(addr,size);
	SCB_InvalidateICache_by_Addr(addr,size);
}
#endif
