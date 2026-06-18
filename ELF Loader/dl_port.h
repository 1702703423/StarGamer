/*
*   @file  dl_port.h
*   @author  wzh
*   @version  1.0
*   @date  2021-12-3
*   @description  实现单片机程序动态加载，类似WIndows的dll
*   @detail	与硬件相关的底层函数接口
*/
#ifndef DL_PORT_H__
#define DL_PORT_H__

#include "dl_lib.h"
#include <stdio.h>

#define DL_ARCH_ARM			0

//定义当前芯片的架构，目前只支持arm
#define DL_USE_ARCH			DL_ARCH_ARM

//当前处理器开启了cache时，如STM32H7，开启宏定义
#define DL_CACHE_USE		0

#define DL_USE_FILE_SYSTEM	1

//调试日志输出开关
#define DL_DEBUG_LOG_USE	0
#define DL_ERROR_LOG_USE	0

#if defined(DL_DEBUG_LOG_USE) && DL_DEBUG_LOG_USE==1
#define DL_LOG_D(fmt,...)  logDebug("<<DL_DEBUG>>" fmt,##__VA_ARGS__)
#else
#define DL_LOG_D(...)
#endif

#if defined(DL_ERROR_LOG_USE) && DL_ERROR_LOG_USE==1
#define DL_LOG_E(fmt,...)  logError("<<DL_ERROR>>" fmt,##__VA_ARGS__)
#else
#define DL_LOG_E(...)  
#endif

#if defined(DL_CACHE_USE)&& DL_CACHE_USE==1
void dl_port_cache_process(void* addr,size_t size);
#endif
void* dl_port_aligned_alloc(size_t align_byte,size_t size);
void dl_port_aligned_free(void* ptr);
void* dl_port_malloc(size_t size);
void dl_port_free(void* ptr);

#if defined(DL_USE_FILE_SYSTEM)&&DL_USE_FILE_SYSTEM==1
void* dl_port_open_file(const char* file_name);
size_t dl_port_get_file_size(void* file_handle);
bool dl_port_read_file(void* file_handle,void* buf,size_t len);
void dl_port_close_file(void* file_handle);
#endif

#endif
