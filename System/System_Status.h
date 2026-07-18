//AllStar Gamer�ں���ز���

#ifndef System_Status_H
#define System_Status_H

#include "sys.h"
#include "stdarg.h"
#include "adt_chain.h"

#define SYSTEM_FONT_FLASH_ADDR   (0UL)
#define SYSTEM_FONT_FLASH_SIZE   (2UL * 1024UL * 1024UL)

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

}System_Status_Information_t;

typedef struct System_Core_Information
{
	u8 SetPasswd;
	u8 isSystemBackup;
	u8 AvilableUpdateReady;
	u8 NeedRecovery;
	u8 Debug;
	u32 AppBinLength;
}System_Core_Information_t;

typedef struct IAP_Info
{
	u8 NeedBackUpSystem;
	u8 AvilableUpdateReady;
	u8 NeedRecovery;
	u32 AppBinLength;
}IAP_Info_t;

typedef struct System_Settings
{
	u32 volume;
	u8 Debug;
	u8 UsbMscEnabled;
	u16 ScreenLight;
	u8 FontInstalled;
	u8 FontBpp;
	u8 FontLineHeight;
	u8 FontBaseLine;
	u32 FontFlashAddress;
	u32 FontSize;
	u16 FontMinUnicode;
	u16 FontMaxUnicode;
}System_Settings_t;

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
extern System_Settings_t System_Settings;

uint8_t* creat_frame_buffer(int* code);
int AxfDebugInfoPushToUart(const char* string, va_list arg);
osThreadId_t SysCreateThread(osThreadFunc_t func,void* argument,const osThreadAttr_t* attr,uint8_t ui);
int SysUnregisterThread(osThreadId_t tid);
int SysTerminateThread(osThreadId_t tid);
int lvgl_start_ui(void);
int lvgl_stop_ui(void);
u8 lvgl_ui_pause_point(void);
void System_Settings_SetDefaults(System_Settings_t* settings);
int System_Settings_Init(void);
int System_Settings_Load(void);
int System_Settings_Save(void);
u8 System_Settings_FontInstalled(void);
u8 System_Settings_UsbMscEnabled(void);
int System_Settings_SetUsbMscEnabled(u8 enabled);

#endif
