/*
*   @file dl_lib.h
*   @author wzh
*   @version 1.0
*   @date 2021-12-3
*   @description: 实现单片机程序动态加载，类似WIndows的dll
*   @detail	具体使用方法如下
*         1、首先确定好自己的硬件平台是否适配，目前本程序仅使用了Coretex-M7内核的
*            STMH743单片机进行测试，理论上arm的Coretex-M系列可以直接使用，其他的arm系列
*            芯片可能在重定位指令与Cache的支持的不够好，下面假设使用的是Coretex-M系列芯片
*         2、确定自己的芯片中是否使用了Cache，若使用了Cache，请将宏定义DL_CACHE_USE置为1
*         3、代码示例中使用了FATFS作为文件系统，C库函数printf作为日志输出，
*            C库函数malloc（注1）作为内存分配方法，移植时请注意
*         4、本程序实现的加载器加载的对象主要针对MDK5生成的elf文件，其他平台生成的elf文件
*            理论上应该也可以用，但必须保证生成的elf文件是可重定位的，最好使用本程序附带的
*            MDK5工程模板生成用于动态加载的elf文件
*         5、使用诸如fromelf等工具确定生成的elf文件没问题后，就可以愉快的进行动态加载了
*         6、首先使用DL_Handler定义一个句柄，类似Windows的dll的句柄
*         7、使用dl_load_lib将一个用于动态加载的elf文件加载到句柄上，同时确任返回值是否为DL_NO_ERR
*         8、加载成功后，可使用dl_get_func通过函数名获得库中对应函数的首地址
*         9、可使用dl_get_entry直接获得库中的main函数
*         10、动态加载的程序执行完成后，可使用dl_destroy_lib释放句柄
*		  【注1】自己实现的malloc函数要确保分配的地址至少是4字节对齐的
*/

#ifndef DL_LIB_H__
#define DL_LIB_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "Malloc.h"

#ifdef __cplusplus
 extern "C" {
#endif

typedef enum{
	DL_NO_ERR			= 0, //无错误
	DL_FILE_INVALID		= -1,//文件信息错误
	DL_MALLOC_FAIL 		= -2,//内存分配失败
	DL_MEMSIZE_ERR 		= -3,//分配内存信息错误
	DL_RELOCATE_ERR 	= -4,//重定向操作不支持
	DL_UNDEF_SYM_ERR 	= -5,//文件解析时出现未定义符号表
	DL_FILE_OPEN_ERR 	= -6,//文件读取失败
	DL_PLATFORM_ERR  	= -7,//elf文件不支持当前的硬件
	DL_ELF_NOT_SUPPRORT = -8,//当前elf文件格式不支持
	DL_ELF_NO_ENTRY		= -9
}DL_Err_Type;

typedef struct{
	const char* name;		//函数名称
	void* addr;				//函数地址
}DL_Symbol_Type;

typedef struct{
	void*         mem_ptr;		//程序加载区首地址
	uint32_t      mem_size;		//程序加载区内存大小
	uint32_t      vstart_addr;	//程序虚拟执行点
	void*         entry_addr;	//程序入口地址（main函数地址）
	uint32_t      nsym;			//动态符号个数
	DL_Symbol_Type* symtab;		//动态符号表
}DL_Handler;

DL_Err_Type dl_load_lib(DL_Handler* pHandle,const void* const lib_ptr);
void* dl_get_func(DL_Handler* pHandle,const char* sys_name);
int(*dl_get_entry(DL_Handler* pHandle))(int argc, char *argv[]);
void dl_destroy_lib(DL_Handler* pHandle);

DL_Err_Type dl_load_file(DL_Handler* pHandle,const char* file_name);
DL_Err_Type dl_exec_file(const char* file_name,int argc,char* argv[],int* ret);
#ifdef __cplusplus
}
#endif

#endif
