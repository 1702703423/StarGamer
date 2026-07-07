//AllStar Gamer�ں���ز���

#ifndef System_Status_H
#define System_Status_H

#include "sys.h"
#include "stdarg.h"
#include "adt_chain.h"

//paramenter

#define SRAM_ADDR_BEGIN 	(uint32_t)0x68000000
#define SRAM_ADDR_HALF		(uint32_t)0x68080000
#define SRAM_ADDR_END			(uint32_t)0x680FFFFF

//system error code start-------------------

#define SYSTEM_STATUS_OK			  0x00
#define RTOS_UNEXPECTION_FATAL	0x01
#define STACK_OVERFLOW					0x02
#define OUT_OF_HEAP_MEMORY			0x03
#define MEMORY_TEST_FAILED			0x04
#define DEBUG_OPTION_DISABLE		0x05
#define OPERATE_FAILED					0x06

//system error code end-------------------

//application error code------------------

#define operate_success 	0x0
#define operate_failed		0x1
#define io_fault					0x2

typedef struct System_Status_Information
{
	u8 fs_mount_status;
	u8 SysFatalError;
}System_Status_Information_t;

typedef struct System_Core_Information
{
	u8 SetPasswd;
	u8 BackUpSystem;
	u8 AvilableUpdateReady;
	u8 NeedRecovery;
	u8 Debug;
	u8 RecordErrorInfo;
	u32 AppBinLength;
}System_Core_Information_t;

typedef struct System_Settings
{
	u32 volume;
	u8 Debug;
	u16 ScreenLight;
}System_Settings_t;

typedef struct System_API
{
	
}System_API_t;

typedef struct Frame_Buffer_Status
{
	uint8_t* pointer;
	uint8_t state;
}Frame_Buffer_Status_t;

typedef struct SYS_Task_Control_Table
{
    osThreadId_t tid;        
    uint8_t ui_enable;  
    osThreadAttr_t attr;       
    struct SYS_Task_Control_Table* next;
} SYS_Task_Control_Table_t;;

typedef struct System_Resource_Table
{
	adt_chain_t* frame_buffer_table;
	adt_chain_t* task_table;
	adt_chain_t* page_table;
}System_Resource_Table_t;

extern System_Resource_Table_t SRT;
extern System_Status_Information_t SSI;
extern System_Core_Information_t SCI;

uint8_t* creat_frame_buffer(int* code);
int AxfDebugInfoPushToUart(const char* string, va_list arg);
osThreadId_t SysCreateThread(osThreadFunc_t func,void* argument,const osThreadAttr_t* attr,uint8_t ui);
int SysUnregisterThread(osThreadId_t tid);
int SysTerminateThread(osThreadId_t tid);
int lvgl_start_ui(void);
int lvgl_stop_ui(void);

#endif
