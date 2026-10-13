#include "System_Status.h"
#include "IAP_Protocol.h"
#include "LCD.h"
#include "main.h"
#include "sys.h"
#include "at24c02.h"
#include "w25q64.h"
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"
#include "shell_support.h"
#include <stdio.h>
#include <string.h>

#define SYSTEM_SETTINGS_EEPROM_ADDR       0U
#define SYSTEM_SETTINGS_MAGIC             0x53544753UL
#define SYSTEM_SETTINGS_VERSION           7U
#define SYSTEM_SETTINGS_VERSION_V6        6U
#define SYSTEM_SETTINGS_VERSION_V5        5U
#define SYSTEM_SETTINGS_VERSION_V4        4U
#define SYSTEM_SETTINGS_VERSION_V3        3U
#define SYSTEM_SETTINGS_VERSION_V2        2U
#define SYSTEM_SETTINGS_VERSION_V1        1U
#define SYSTEM_SETTINGS_DEFAULT_VOLUME    70UL
#define SYSTEM_SETTINGS_DEFAULT_LIGHT     100U
#define SYSTEM_SETTINGS_DEFAULT_FONT_LINE 21U
#define SYSTEM_SETTINGS_DEFAULT_AXF_STACK SYSTEM_AXF_STACK_16KB
#define SYSTEM_SETTINGS_DEFAULT_CLOCK      SYSTEM_CLOCK_240MHZ
#define SYSTEM_SETTINGS_DEFAULT_TOAST_DURATION SYSTEM_TOAST_DURATION_DEFAULT
#define SYSTEM_DEBUG_LOG_BUFFER_SIZE       192U

static const u32 s_system_axf_stack_sizes[SYSTEM_AXF_STACK_LEVEL_COUNT] =
{
	4UL * 1024UL,
	8UL * 1024UL,
	12UL * 1024UL,
	16UL * 1024UL,
	24UL * 1024UL
};

typedef struct System_SettingsV1
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
} System_SettingsV1_t;

typedef struct System_SettingsV2
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
	u8 DebugShellEnabled;
	u8 DebugAudioLogEnabled;
	u8 DebugVideoLogEnabled;
	u8 DebugLibjpegLogEnabled;
	u8 DebugFatfsLogEnabled;
	u8 MemoryDumpEnabled;
	u8 MemoryDumpCcmEnabled;
	u8 MemoryDumpMainSramEnabled;
	u8 MemoryDumpSramexEnabled;
} System_SettingsV2_t;

typedef struct System_SettingsV3
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
	u8 DebugShellEnabled;
	u8 DebugAudioLogEnabled;
	u8 DebugVideoLogEnabled;
	u8 DebugLibjpegLogEnabled;
	u8 DebugFatfsLogEnabled;
	u8 MemoryDumpEnabled;
	u8 MemoryDumpCcmEnabled;
	u8 MemoryDumpMainSramEnabled;
	u8 MemoryDumpSramexEnabled;
	u8 ThemeLightEnabled;
} System_SettingsV3_t;

typedef struct System_SettingsV4
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
	u8 DebugShellEnabled;
	u8 DebugAudioLogEnabled;
	u8 DebugVideoLogEnabled;
	u8 DebugLibjpegLogEnabled;
	u8 DebugFatfsLogEnabled;
	u8 MemoryDumpEnabled;
	u8 MemoryDumpCcmEnabled;
	u8 MemoryDumpMainSramEnabled;
	u8 MemoryDumpSramexEnabled;
	u8 ThemeLightEnabled;
	u8 AxfStackLevel;
} System_SettingsV4_t;

typedef struct System_SettingsV6
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
	u8 DebugShellEnabled;
	u8 DebugAudioLogEnabled;
	u8 DebugVideoLogEnabled;
	u8 DebugLibjpegLogEnabled;
	u8 DebugFatfsLogEnabled;
	u8 MemoryDumpEnabled;
	u8 MemoryDumpCcmEnabled;
	u8 MemoryDumpMainSramEnabled;
	u8 MemoryDumpSramexEnabled;
	u8 ThemeLightEnabled;
	u8 AxfStackLevel;
	u8 ClockProfile;
} System_SettingsV6_t;

typedef struct System_Settings_Record
{
	u32 magic;
	u16 version;
	u16 size;
	u32 sequence;
	System_Settings_t settings;
	u32 checksum;
} System_Settings_Record_t;

typedef struct System_Settings_RecordV6
{
	u32 magic;
	u16 version;
	u16 size;
	u32 sequence;
	System_SettingsV6_t settings;
	u32 checksum;
} System_Settings_RecordV6_t;

typedef struct System_Settings_RecordV5
{
	u32 magic;
	u16 version;
	u16 size;
	System_Settings_t settings;
	u32 checksum;
} System_Settings_RecordV5_t;

typedef struct System_Settings_RecordV2
{
	u32 magic;
	u16 version;
	u16 size;
	System_SettingsV2_t settings;
	u32 checksum;
} System_Settings_RecordV2_t;

typedef struct System_Settings_RecordV3
{
	u32 magic;
	u16 version;
	u16 size;
	System_SettingsV3_t settings;
	u32 checksum;
} System_Settings_RecordV3_t;

typedef struct System_Settings_RecordV4
{
	u32 magic;
	u16 version;
	u16 size;
	System_SettingsV4_t settings;
	u32 checksum;
} System_Settings_RecordV4_t;

typedef struct System_Settings_RecordV1
{
	u32 magic;
	u16 version;
	u16 size;
	System_SettingsV1_t settings;
	u32 checksum;
} System_Settings_RecordV1_t;

typedef union System_Settings_Storage
{
	System_Settings_Record_t current;
	System_Settings_RecordV6_t v6;
	System_Settings_RecordV5_t v5;
	System_Settings_RecordV4_t v4;
	System_Settings_RecordV3_t v3;
	System_Settings_RecordV2_t v2;
	System_Settings_RecordV1_t legacy;
} System_Settings_Storage_t;

typedef struct System_Settings_Candidate
{
	u8 valid;
	u8 version;
	u32 sequence;
	System_Settings_t settings;
} System_Settings_Candidate_t;

typedef char System_Settings_RecordSizeCheck[(sizeof(System_Settings_Record_t) <= 128U) ? 1 : -1];

static u32 System_Settings_Checksum(const u8* data, u16 length);
static void System_Settings_ConsiderCandidate(System_Settings_Candidate_t* best, u8 valid,
	u8 version, u32 sequence, const System_Settings_t* settings);
static u8 System_Settings_LoadFlashSlot(u32 address, System_Settings_Record_t* record);
static u8 System_Settings_SaveFlashSlot(u32 address, const System_Settings_Record_t* record);
static void System_Settings_Normalize(System_Settings_t* settings);
static u32 System_Settings_ClockFrequencyForProfile(u8 profile);
static u32 System_Settings_ClockPllNForProfile(u8 profile);
static u32 System_Settings_ClockPllQForProfile(u8 profile);

static u8 s_system_settings_initialized;
static u32 s_system_settings_sequence;

#if ((W25Q64_SETTINGS_SLOT0_ADDRESS % W25Q64_SECTOR_SIZE) != 0U) || \
    ((W25Q64_SETTINGS_SLOT1_ADDRESS % W25Q64_SECTOR_SIZE) != 0U)
#error "System settings flash slots must be sector aligned"
#endif

#if ((W25Q64_SETTINGS_SIZE < (2UL * W25Q64_SECTOR_SIZE)) || \
     ((W25Q64_SETTINGS_SLOT0_ADDRESS + (2UL * W25Q64_SECTOR_SIZE)) > \
      (W25Q64_SETTINGS_ADDRESS + W25Q64_SETTINGS_SIZE)))
#error "System settings flash partition is too small"
#endif

extern SRAM_HandleTypeDef hsram3;
extern SD_HandleTypeDef hsd;
extern Shell shell;

int GetSdCardInfo()
{
	uint64_t CardCap;
	HAL_SD_CardInfoTypeDef  SDCardInfo; 
	HAL_SD_CardCIDTypeDef SDCard_CID;
	
	HAL_SD_GetCardCID(&hsd,&SDCard_CID);
	HAL_SD_GetCardInfo(&hsd,&SDCardInfo);
	CardCap=(uint64_t)(SDCardInfo.LogBlockNbr)*(uint64_t)(SDCardInfo.LogBlockSize);
	shellPrintEndLine(&shell,"====================\r\n");
	shellPrintEndLine(&shell,"Card ManufacturerID: %d \r\n",SDCard_CID.ManufacturerID);				//??????ID	
 	shellPrintEndLine(&shell,"CardVersion:         %d \r\n",(uint32_t)(SDCardInfo.CardVersion));		//???��??
	shellPrintEndLine(&shell,"Class:               %d \r\n",(uint32_t)(SDCardInfo.Class));		    //
 	shellPrintEndLine(&shell,"Card RCA(RelCardAdd):%d \r\n",SDCardInfo.RelCardAdd);					//????????
	shellPrintEndLine(&shell,"Card BlockNbr:       %d \r\n",SDCardInfo.BlockNbr);						//??????
 	shellPrintEndLine(&shell,"Card BlockSize:      %d \r\n",SDCardInfo.BlockSize);					//?????
	shellPrintEndLine(&shell,"LogBlockNbr:         %d \r\n",(uint32_t)(SDCardInfo.LogBlockNbr));		//?????????
	shellPrintEndLine(&shell,"LogBlockSize:        %d \r\n",(uint32_t)(SDCardInfo.LogBlockSize));		//????????
	shellPrintEndLine(&shell,"Card Capacity:       %d MB\r\n",(uint32_t)(CardCap>>20));				//??????
  shellPrintEndLine(&shell,"====================\r\n");
	
	return SYSTEM_STATUS_OK;
}

uint8_t* creat_frame_buffer(int* code)
{
	osKernelLock();
	
	int _code;
	
	uint8_t* ST7789s_ScreenBuffer = (uint8_t*)sys_malloc(SRAMEX,ST7789s_ScreenBufferSize); //����֡�����ڴ�
	
	if(ST7789s_ScreenBuffer == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}
	
	Frame_Buffer_Status_t* FBST_data = (Frame_Buffer_Status_t*)sys_malloc(SRAMEX,sizeof(Frame_Buffer_Status_t)); //����֡����������ڴ�?
	
	if(FBST_data == NULL)
	{
		sys_free(SRAMEX,(void*)ST7789s_ScreenBuffer);
		*code = OUT_OF_HEAP_MEMORY;
		osKernelUnlock();
		return NULL;
	}
	
	FBST_data->pointer = ST7789s_ScreenBuffer; //��¼֡������?
	FBST_data->state = 0;
	
	if(insert_head_chain(SRT.frame_buffer_table,FBST_data,&_code) == NULL)
	{
		sys_free(SRAMEX,(void*)ST7789s_ScreenBuffer);
		sys_free(SRAMEX,(void*)FBST_data);
		*code = OPERATE_FAILED;
		osKernelUnlock();
		return NULL;
	};//����֡���������?
	
	osKernelUnlock();
	
	*code = SYSTEM_STATUS_OK;
	return ST7789s_ScreenBuffer;
	
}

uint8_t* destroy_frame_buffer(uint8_t* pointer, int* code)
{
	if(code==NULL)return NULL;
	
	osKernelLock();
	
	int _code;
	adt_chain_t* curr = NULL;
	Frame_Buffer_Status_t* fb = NULL;

	if (pointer == NULL || code == NULL)
	{
		*code = OPERATE_FAILED;
		osKernelUnlock();
		return NULL;
	}

	curr = SRT.frame_buffer_table->next;

	while (curr != NULL) //��������
	{
		fb = (Frame_Buffer_Status_t*)curr->data;
		
		if (fb->pointer == pointer)
		{
			break;
		}
		
		curr = curr->next;
	}

	if (curr == NULL)
	{
		*code = OPERATE_FAILED;
		osKernelUnlock();
		return NULL;
	}
	
	delete_node(SRT.frame_buffer_table, fb, &_code);
	
	if (_code != SYSTEM_STATUS_OK) //ɾ���ڵ�
	{
		*code = OPERATE_FAILED;
		osKernelUnlock();
		return NULL;
	}

	sys_free(SRAMEX, fb->pointer);	//�ͷ��ڴ�
	sys_free(SRAMEX, fb);
	
	osKernelUnlock();

	*code = SYSTEM_STATUS_OK;
	return NULL; 
}

osThreadId_t SysCreateThread(osThreadFunc_t func,void* argument,const osThreadAttr_t* attr,uint8_t ui)
{
	int _code;
	
	if (func == NULL || attr == NULL) 
	{
    return NULL;
  }
	
	osKernelLock();
	
	osThreadId_t tid = osThreadNew(func,argument,attr);
	
	if(tid == NULL)
	{
		osKernelUnlock();
		return NULL;
	}
	
	SYS_Task_Control_Table_t* SYS_TCT = (SYS_Task_Control_Table_t*)sys_malloc(SRAMEX,sizeof(SYS_Task_Control_Table_t));

	if(SYS_TCT == NULL)
	{
		osThreadTerminate(tid);
		osKernelUnlock();
		return NULL;
	}
	
	SYS_TCT->attr = *attr;
	SYS_TCT->tid = tid;
	SYS_TCT->ui_enable = ui;
	
	if(insert_head_chain(SRT.task_table,SYS_TCT,&_code) == NULL)
	{
		osThreadTerminate(tid);
		sys_free(SRAMEX,(void*)SYS_TCT);
		osKernelUnlock();
		return NULL;
	}
	
	osKernelUnlock();
	
	return tid;
}

int SysTerminateThread(osThreadId_t tid)
{
	int _code;
	TaskHandle_t task;
	SYS_Task_Control_Table_t* found;

	if (tid == NULL) 
	{
		return OPERATE_FAILED;
	}

	task = (TaskHandle_t)tid;

	osKernelLock();

	if (eTaskGetState(task) == eDeleted)
	{
		osKernelUnlock();
		return OPERATE_FAILED;
	}

	// ���� SRT.task_table ����ƥ����߳�
	adt_chain_t* curr = SRT.task_table->next;
	found = NULL;
	
	while (curr != NULL)
	{
		SYS_Task_Control_Table_t* entry = (SYS_Task_Control_Table_t*)curr->data;
		if (entry->tid == tid)
		{
			found = entry;
			break;
		}
		curr = curr->next;
	}
	
	if (found == NULL)
	{
		// ���� SYS_Task_Control_Table �У���������ֹ
		osKernelUnlock();
		return OPERATE_FAILED;
	}
	
	// ���������Ƴ�
	delete_node(SRT.task_table, found, &_code);
	if (_code != SYSTEM_STATUS_OK)
	{
		osKernelUnlock();
		return OPERATE_FAILED;
	}

	// ��ֹ�߳�
	vTaskDelete(task);
	
	// �ͷ� SYS_Task_Control_Table �ڵ�
	sys_free(SRAMEX, (void*)found);
	
	osKernelUnlock();
	
	return SYSTEM_STATUS_OK;
}

int SysUnregisterThread(osThreadId_t tid)
{
	int _code;
	adt_chain_t* curr;
	SYS_Task_Control_Table_t* found;

	if (tid == NULL)
	{
		return OPERATE_FAILED;
	}

	osKernelLock();

	curr = SRT.task_table->next;
	found = NULL;

	while (curr != NULL)
	{
		SYS_Task_Control_Table_t* entry = (SYS_Task_Control_Table_t*)curr->data;
		if (entry->tid == tid)
		{
			found = entry;
			break;
		}
		curr = curr->next;
	}

	if (found == NULL)
	{
		osKernelUnlock();
		return OPERATE_FAILED;
	}

	delete_node(SRT.task_table, found, &_code);
	if (_code == SYSTEM_STATUS_OK)
	{
		sys_free(SRAMEX, (void*)found);
	}

	osKernelUnlock();

	return (_code == SYSTEM_STATUS_OK) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}

extern osThreadId_t lvglThreadId;

#define LVGL_UI_FLAG_RESUME         (1UL << 27)
#define LVGL_UI_FLAG_PAUSED         (1UL << 28)
#define LVGL_UI_FLAG_RESUMED        (1UL << 29)
#define LVGL_UI_HANDSHAKE_TIMEOUT   1000U

static volatile u8 s_lvgl_pause_requested;
static volatile u8 s_lvgl_paused;
static osThreadId_t s_lvgl_display_owner;
static osThreadId_t s_lvgl_resume_waiter;

int lvgl_start_ui(void)
{
	osThreadId_t current;
	u32 flags;
	s32 lock_state;

	current = osThreadGetId();
	if (lvglThreadId == NULL || current == NULL)
	{
		return OPERATE_FAILED;
	}

	(void)osThreadFlagsClear(LVGL_UI_FLAG_RESUMED);
	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_lvgl_display_owner != current || s_lvgl_pause_requested == 0U ||
	    s_lvgl_paused == 0U || s_lvgl_resume_waiter != NULL)
	{
		(void)osKernelRestoreLock(lock_state);
		return OPERATE_FAILED;
	}

	s_lvgl_resume_waiter = current;
	s_lvgl_pause_requested = 0U;
	__DMB();
	(void)osKernelRestoreLock(lock_state);
	flags = osThreadFlagsSet(lvglThreadId, LVGL_UI_FLAG_RESUME);
	if ((flags & osFlagsError) != 0U)
	{
		return OPERATE_FAILED;
	}

	flags = osThreadFlagsWait(LVGL_UI_FLAG_RESUMED, osFlagsWaitAny, LVGL_UI_HANDSHAKE_TIMEOUT);
	return ((flags & osFlagsError) == 0U) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}

int lvgl_stop_ui(void)
{
	osThreadId_t current;
	u32 flags;
	s32 lock_state;

	current = osThreadGetId();
	if (lvglThreadId == NULL || current == NULL || current == lvglThreadId)
	{
		return OPERATE_FAILED;
	}

	(void)osThreadFlagsClear(LVGL_UI_FLAG_PAUSED | LVGL_UI_FLAG_RESUMED);
	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_lvgl_display_owner != NULL || s_lvgl_resume_waiter != NULL ||
	    s_lvgl_pause_requested != 0U || s_lvgl_paused != 0U)
	{
		(void)osKernelRestoreLock(lock_state);
		return OPERATE_FAILED;
	}

	s_lvgl_display_owner = current;
	s_lvgl_pause_requested = 1U;
	__DMB();
	(void)osKernelRestoreLock(lock_state);

	flags = osThreadFlagsWait(LVGL_UI_FLAG_PAUSED, osFlagsWaitAny, LVGL_UI_HANDSHAKE_TIMEOUT);
	if ((flags & osFlagsError) == 0U)
	{
		return SYSTEM_STATUS_OK;
	}

	lock_state = osKernelLock();
	if (lock_state >= 0)
	{
		if (s_lvgl_display_owner == current)
		{
			s_lvgl_pause_requested = 0U;
			s_lvgl_display_owner = NULL;
		}
		(void)osKernelRestoreLock(lock_state);
	}
	(void)osThreadFlagsSet(lvglThreadId, LVGL_UI_FLAG_RESUME);
	return OPERATE_FAILED;
}

int lvgl_recover_ui(osThreadId_t owner)
{
	osThreadId_t current;
	u32 flags;
	s32 lock_state;
	u8 already_recovered;

	current = osThreadGetId();
	if (lvglThreadId == NULL || current == NULL || owner == NULL ||
	    current == lvglThreadId)
	{
		return OPERATE_FAILED;
	}

	(void)osThreadFlagsClear(LVGL_UI_FLAG_RESUMED);
	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_lvgl_display_owner != owner)
	{
		already_recovered = (s_lvgl_display_owner == NULL &&
		                     s_lvgl_paused == 0U) ? 1U : 0U;
		(void)osKernelRestoreLock(lock_state);
		return (already_recovered != 0U) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
	}

	if (s_lvgl_paused == 0U && s_lvgl_pause_requested != 0U)
	{
		/* Cancel an ownership request before LVGL enters its pause point. */
		s_lvgl_pause_requested = 0U;
		s_lvgl_resume_waiter = NULL;
		s_lvgl_display_owner = NULL;
		__DMB();
		(void)osKernelRestoreLock(lock_state);
		return SYSTEM_STATUS_OK;
	}

	s_lvgl_resume_waiter = current;
	s_lvgl_pause_requested = 0U;
	__DMB();
	(void)osKernelRestoreLock(lock_state);

	flags = osThreadFlagsSet(lvglThreadId, LVGL_UI_FLAG_RESUME);
	if ((flags & osFlagsError) != 0U)
	{
		return OPERATE_FAILED;
	}

	flags = osThreadFlagsWait(LVGL_UI_FLAG_RESUMED, osFlagsWaitAny,
	                          LVGL_UI_HANDSHAKE_TIMEOUT);
	return ((flags & osFlagsError) == 0U) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}

u8 lvgl_ui_pause_point(void)
{
	osThreadId_t owner;
	osThreadId_t waiter;
	u32 flags;
	u32 acknowledge_flags;
	s32 lock_state;
	u8 paused = 0U;

	if (s_lvgl_pause_requested == 0U)
	{
		return 0U;
	}

	(void)osThreadFlagsClear(LVGL_UI_FLAG_RESUME);
	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return 0U;
	}
	owner = s_lvgl_display_owner;
	if (s_lvgl_pause_requested == 0U || owner == NULL)
	{
		(void)osKernelRestoreLock(lock_state);
		return 0U;
	}

	s_lvgl_paused = 1U;
	__DMB();
	acknowledge_flags = osThreadFlagsSet(owner, LVGL_UI_FLAG_PAUSED);
	(void)osKernelRestoreLock(lock_state);
	if ((acknowledge_flags & osFlagsError) != 0U)
	{
		lock_state = osKernelLock();
		if (lock_state >= 0)
		{
			s_lvgl_paused = 0U;
			s_lvgl_pause_requested = 0U;
			if (s_lvgl_display_owner == owner)
			{
				s_lvgl_display_owner = NULL;
			}
			(void)osKernelRestoreLock(lock_state);
		}
		return 0U;
	}
	paused = 1U;

	while (s_lvgl_pause_requested != 0U)
	{
		flags = osThreadFlagsWait(LVGL_UI_FLAG_RESUME, osFlagsWaitAny, osWaitForever);
		if ((flags & osFlagsError) != 0U)
		{
			(void)osDelay(1U);
		}
	}

	lock_state = osKernelLock();
	if (lock_state >= 0)
	{
		s_lvgl_paused = 0U;
		__DMB();
		waiter = s_lvgl_resume_waiter;
		s_lvgl_resume_waiter = NULL;
		if (s_lvgl_display_owner == owner)
		{
			s_lvgl_display_owner = NULL;
		}
		(void)osKernelRestoreLock(lock_state);
		if (waiter != NULL)
		{
			(void)osThreadFlagsSet(waiter, LVGL_UI_FLAG_RESUMED);
		}
	}

	return paused;
}

u8 lvgl_ui_is_active(void)
{
	/* The LVGL task owns the display while no other thread holds it and no
	 * handover has been requested. */
	return (lvglThreadId != NULL && s_lvgl_display_owner == NULL &&
	        s_lvgl_pause_requested == 0U) ? 1U : 0U;
}

void System_Settings_SetDefaults(System_Settings_t* settings)
{
	if (settings == NULL)
	{
		return;
	}

	memset(settings, 0, sizeof(System_Settings_t));
	settings->volume = SYSTEM_SETTINGS_DEFAULT_VOLUME;
	settings->Debug = 1U;
	settings->DebugShellEnabled = 1U;
	settings->DebugAudioLogEnabled = 1U;
	settings->DebugVideoLogEnabled = 0U;
	settings->DebugLibjpegLogEnabled = 0U;
	settings->DebugFatfsLogEnabled = 0U;
	settings->MemoryDumpEnabled = 0U;
	settings->MemoryDumpCcmEnabled = 1U;
	settings->MemoryDumpMainSramEnabled = 1U;
	settings->MemoryDumpSramexEnabled = 1U;
	settings->UsbMscEnabled = 0U;
	settings->ThemeLightEnabled = 0U;
	settings->AxfStackLevel = SYSTEM_SETTINGS_DEFAULT_AXF_STACK;
	settings->ClockProfile = SYSTEM_SETTINGS_DEFAULT_CLOCK;
	settings->ToastDuration = SYSTEM_SETTINGS_DEFAULT_TOAST_DURATION;
	settings->ScreenLight = SYSTEM_SETTINGS_DEFAULT_LIGHT;
	settings->FontFlashAddress = SYSTEM_FONT_FLASH_ADDR;
	settings->FontLineHeight = SYSTEM_SETTINGS_DEFAULT_FONT_LINE;
}

int System_Settings_Init(void)
{
	int code;

	if (s_system_settings_initialized != 0U)
	{
		/* MX_GPIO_Init may have restored PB8/PB9 to their CubeMX mode. */
		(void)AT24C02_Init();
		(void)W25Q64_Init();
		return SYSTEM_STATUS_OK;
	}

	System_Settings_SetDefaults(&System_Settings);
	s_system_settings_sequence = 0U;
	(void)AT24C02_Init();
	(void)W25Q64_Init();

	code = System_Settings_Load();
	if (code == SYSTEM_STATUS_OK)
	{
		s_system_settings_initialized = 1U;
		return SYSTEM_STATUS_OK;
	}

	System_Settings_SetDefaults(&System_Settings);
	code = System_Settings_Save();
	if (code == SYSTEM_STATUS_OK)
	{
		s_system_settings_initialized = 1U;
	}
	return code;
}

static void System_Settings_ConsiderCandidate(System_Settings_Candidate_t* best, u8 valid,
	u8 version, u32 sequence, const System_Settings_t* settings)
{
	if (best == NULL || valid == 0U || settings == NULL)
	{
		return;
	}

	if (best->valid == 0U || sequence > best->sequence)
	{
		best->valid = 1U;
		best->version = version;
		best->sequence = sequence;
		best->settings = *settings;
	}
}

static u8 System_Settings_LoadFlashSlot(u32 address, System_Settings_Record_t* record)
{
	u32 checksum;

	if (record == NULL)
	{
		return 0U;
	}

	memset(record, 0, sizeof(*record));
	if (W25Q64_Init() != W25Q64_OK ||
	    W25Q64_ReadData(address, (u8*)record, (u32)sizeof(*record)) != W25Q64_OK)
	{
		return 0U;
	}

	checksum = System_Settings_Checksum((const u8*)record,
		(u16)(sizeof(*record) - sizeof(record->checksum)));
	if (record->magic != SYSTEM_SETTINGS_MAGIC ||
	    record->version != SYSTEM_SETTINGS_VERSION ||
	    record->size != (u16)sizeof(System_Settings_t) ||
	    record->checksum != checksum)
	{
		return 0U;
	}

	return 1U;
}

static u8 System_Settings_SaveFlashSlot(u32 address, const System_Settings_Record_t* record)
{
	System_Settings_Record_t verify;

	if (record == NULL)
	{
		return 0U;
	}

	memset(&verify, 0, sizeof(verify));
	if (W25Q64_Init() != W25Q64_OK ||
	    W25Q64_SectorErase(address) != W25Q64_OK ||
	    W25Q64_WriteData(address, (const u8*)record, (u32)sizeof(*record)) != W25Q64_OK ||
	    W25Q64_ReadData(address, (u8*)&verify, (u32)sizeof(verify)) != W25Q64_OK)
	{
		return 0U;
	}

	return (memcmp(&verify, record, sizeof(*record)) == 0) ? 1U : 0U;
}

int System_Settings_Load(void)
{
	System_Settings_Storage_t storage;
	System_Settings_Record_t flash_slot0;
	System_Settings_Record_t flash_slot1;
	System_Settings_Record_t* record;
	System_Settings_RecordV6_t* v6;
	System_Settings_RecordV5_t* v5;
	System_Settings_RecordV4_t* v4;
	System_Settings_RecordV3_t* v3;
	System_Settings_RecordV2_t* v2;
	System_Settings_RecordV1_t* legacy;
	System_Settings_Candidate_t best;
	System_Settings_t settings;
	u32 checksum;

	memset(&storage, 0, sizeof(storage));
	memset(&flash_slot0, 0, sizeof(flash_slot0));
	memset(&flash_slot1, 0, sizeof(flash_slot1));
	memset(&best, 0, sizeof(best));

	(void)AT24C02_Read(SYSTEM_SETTINGS_EEPROM_ADDR, (u8*)&storage, (u16)sizeof(storage));

	record = &storage.current;
	checksum = System_Settings_Checksum((const u8*)record,
		(u16)(sizeof(*record) - sizeof(record->checksum)));
	if (record->magic == SYSTEM_SETTINGS_MAGIC &&
	    record->version == SYSTEM_SETTINGS_VERSION &&
	    record->size == (u16)sizeof(System_Settings_t) &&
	    record->checksum == checksum)
	{
		System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION,
			record->sequence, &record->settings);
	}
	else
	{
		v6 = &storage.v6;
		checksum = System_Settings_Checksum((const u8*)v6,
			(u16)(sizeof(*v6) - sizeof(v6->checksum)));
		if (v6->magic == SYSTEM_SETTINGS_MAGIC &&
		    v6->version == SYSTEM_SETTINGS_VERSION_V6 &&
		    v6->size == (u16)sizeof(System_SettingsV6_t) &&
		    v6->checksum == checksum)
		{
			System_Settings_SetDefaults(&settings);
			settings.volume = v6->settings.volume;
			settings.Debug = v6->settings.Debug;
			settings.UsbMscEnabled = v6->settings.UsbMscEnabled;
			settings.ScreenLight = v6->settings.ScreenLight;
			settings.FontInstalled = v6->settings.FontInstalled;
			settings.FontBpp = v6->settings.FontBpp;
			settings.FontLineHeight = v6->settings.FontLineHeight;
			settings.FontBaseLine = v6->settings.FontBaseLine;
			settings.FontFlashAddress = v6->settings.FontFlashAddress;
			settings.FontSize = v6->settings.FontSize;
			settings.FontMinUnicode = v6->settings.FontMinUnicode;
			settings.FontMaxUnicode = v6->settings.FontMaxUnicode;
			settings.DebugShellEnabled = v6->settings.DebugShellEnabled;
			settings.DebugAudioLogEnabled = v6->settings.DebugAudioLogEnabled;
			settings.DebugVideoLogEnabled = v6->settings.DebugVideoLogEnabled;
			settings.DebugLibjpegLogEnabled = v6->settings.DebugLibjpegLogEnabled;
			settings.DebugFatfsLogEnabled = v6->settings.DebugFatfsLogEnabled;
			settings.MemoryDumpEnabled = v6->settings.MemoryDumpEnabled;
			settings.MemoryDumpCcmEnabled = v6->settings.MemoryDumpCcmEnabled;
			settings.MemoryDumpMainSramEnabled = v6->settings.MemoryDumpMainSramEnabled;
			settings.MemoryDumpSramexEnabled = v6->settings.MemoryDumpSramexEnabled;
			settings.ThemeLightEnabled = v6->settings.ThemeLightEnabled;
			settings.AxfStackLevel = v6->settings.AxfStackLevel;
			settings.ClockProfile = v6->settings.ClockProfile;
			System_Settings_Normalize(&settings);
			System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION_V6,
				v6->sequence, &settings);
		}
		else
		{
			v5 = &storage.v5;
			checksum = System_Settings_Checksum((const u8*)v5,
				(u16)(sizeof(*v5) - sizeof(v5->checksum)));
			if (v5->magic == SYSTEM_SETTINGS_MAGIC &&
			    v5->version == SYSTEM_SETTINGS_VERSION_V5 &&
			    v5->size == (u16)sizeof(System_Settings_t) &&
			    v5->checksum == checksum)
			{
				System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION_V5,
					0U, &v5->settings);
			}
			else
			{
				v4 = &storage.v4;
				checksum = System_Settings_Checksum((const u8*)v4,
					(u16)(sizeof(*v4) - sizeof(v4->checksum)));
				if (v4->magic == SYSTEM_SETTINGS_MAGIC &&
				    v4->version == SYSTEM_SETTINGS_VERSION_V4 &&
				    v4->size == (u16)sizeof(System_SettingsV4_t) &&
				    v4->checksum == checksum)
				{
					System_Settings_SetDefaults(&settings);
					settings.volume = v4->settings.volume;
					settings.Debug = v4->settings.Debug;
					settings.UsbMscEnabled = v4->settings.UsbMscEnabled;
					settings.ScreenLight = v4->settings.ScreenLight;
					settings.FontInstalled = v4->settings.FontInstalled;
					settings.FontBpp = v4->settings.FontBpp;
					settings.FontLineHeight = v4->settings.FontLineHeight;
					settings.FontBaseLine = v4->settings.FontBaseLine;
					settings.FontFlashAddress = v4->settings.FontFlashAddress;
					settings.FontSize = v4->settings.FontSize;
					settings.FontMinUnicode = v4->settings.FontMinUnicode;
					settings.FontMaxUnicode = v4->settings.FontMaxUnicode;
					settings.DebugShellEnabled = v4->settings.DebugShellEnabled;
					settings.DebugAudioLogEnabled = v4->settings.DebugAudioLogEnabled;
					settings.DebugVideoLogEnabled = v4->settings.DebugVideoLogEnabled;
					settings.DebugLibjpegLogEnabled = v4->settings.DebugLibjpegLogEnabled;
					settings.DebugFatfsLogEnabled = v4->settings.DebugFatfsLogEnabled;
					settings.MemoryDumpEnabled = v4->settings.MemoryDumpEnabled;
					settings.MemoryDumpCcmEnabled = v4->settings.MemoryDumpCcmEnabled;
					settings.MemoryDumpMainSramEnabled = v4->settings.MemoryDumpMainSramEnabled;
					settings.MemoryDumpSramexEnabled = v4->settings.MemoryDumpSramexEnabled;
					settings.ThemeLightEnabled = v4->settings.ThemeLightEnabled;
					settings.AxfStackLevel = v4->settings.AxfStackLevel;
					System_Settings_Normalize(&settings);
					System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION_V4,
						0U, &settings);
				}
				else
				{
					v3 = &storage.v3;
					checksum = System_Settings_Checksum((const u8*)v3,
						(u16)(sizeof(*v3) - sizeof(v3->checksum)));
					if (v3->magic == SYSTEM_SETTINGS_MAGIC &&
					    v3->version == SYSTEM_SETTINGS_VERSION_V3 &&
					    v3->size == (u16)sizeof(System_SettingsV3_t) &&
					    v3->checksum == checksum)
					{
						System_Settings_SetDefaults(&settings);
						settings.volume = v3->settings.volume;
						settings.Debug = v3->settings.Debug;
						settings.UsbMscEnabled = v3->settings.UsbMscEnabled;
						settings.ScreenLight = v3->settings.ScreenLight;
						settings.FontInstalled = v3->settings.FontInstalled;
						settings.FontBpp = v3->settings.FontBpp;
						settings.FontLineHeight = v3->settings.FontLineHeight;
						settings.FontBaseLine = v3->settings.FontBaseLine;
						settings.FontFlashAddress = v3->settings.FontFlashAddress;
						settings.FontSize = v3->settings.FontSize;
						settings.FontMinUnicode = v3->settings.FontMinUnicode;
						settings.FontMaxUnicode = v3->settings.FontMaxUnicode;
						settings.DebugShellEnabled = v3->settings.DebugShellEnabled;
						settings.DebugAudioLogEnabled = v3->settings.DebugAudioLogEnabled;
						settings.DebugVideoLogEnabled = v3->settings.DebugVideoLogEnabled;
						settings.DebugLibjpegLogEnabled = v3->settings.DebugLibjpegLogEnabled;
						settings.DebugFatfsLogEnabled = v3->settings.DebugFatfsLogEnabled;
						settings.MemoryDumpEnabled = v3->settings.MemoryDumpEnabled;
						settings.MemoryDumpCcmEnabled = v3->settings.MemoryDumpCcmEnabled;
						settings.MemoryDumpMainSramEnabled = v3->settings.MemoryDumpMainSramEnabled;
						settings.MemoryDumpSramexEnabled = v3->settings.MemoryDumpSramexEnabled;
						settings.ThemeLightEnabled = v3->settings.ThemeLightEnabled;
						System_Settings_Normalize(&settings);
						System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION_V3,
							0U, &settings);
					}
					else
					{
						v2 = &storage.v2;
						checksum = System_Settings_Checksum((const u8*)v2,
							(u16)(sizeof(*v2) - sizeof(v2->checksum)));
						if (v2->magic == SYSTEM_SETTINGS_MAGIC &&
						    v2->version == SYSTEM_SETTINGS_VERSION_V2 &&
						    v2->size == (u16)sizeof(System_SettingsV2_t) &&
						    v2->checksum == checksum)
						{
							System_Settings_SetDefaults(&settings);
							settings.volume = v2->settings.volume;
							settings.Debug = v2->settings.Debug;
							settings.UsbMscEnabled = v2->settings.UsbMscEnabled;
							settings.ScreenLight = v2->settings.ScreenLight;
							settings.FontInstalled = v2->settings.FontInstalled;
							settings.FontBpp = v2->settings.FontBpp;
							settings.FontLineHeight = v2->settings.FontLineHeight;
							settings.FontBaseLine = v2->settings.FontBaseLine;
							settings.FontFlashAddress = v2->settings.FontFlashAddress;
							settings.FontSize = v2->settings.FontSize;
							settings.FontMinUnicode = v2->settings.FontMinUnicode;
							settings.FontMaxUnicode = v2->settings.FontMaxUnicode;
							settings.DebugShellEnabled = v2->settings.DebugShellEnabled;
							settings.DebugAudioLogEnabled = v2->settings.DebugAudioLogEnabled;
							settings.DebugVideoLogEnabled = v2->settings.DebugVideoLogEnabled;
							settings.DebugLibjpegLogEnabled = v2->settings.DebugLibjpegLogEnabled;
							settings.DebugFatfsLogEnabled = v2->settings.DebugFatfsLogEnabled;
							settings.MemoryDumpEnabled = v2->settings.MemoryDumpEnabled;
							settings.MemoryDumpCcmEnabled = v2->settings.MemoryDumpCcmEnabled;
							settings.MemoryDumpMainSramEnabled = v2->settings.MemoryDumpMainSramEnabled;
							settings.MemoryDumpSramexEnabled = v2->settings.MemoryDumpSramexEnabled;
							System_Settings_Normalize(&settings);
							System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION_V2,
								0U, &settings);
						}
						else
						{
							legacy = &storage.legacy;
							checksum = System_Settings_Checksum((const u8*)legacy,
								(u16)(sizeof(*legacy) - sizeof(legacy->checksum)));
							if (legacy->magic == SYSTEM_SETTINGS_MAGIC &&
							    legacy->version == SYSTEM_SETTINGS_VERSION_V1 &&
							    legacy->size == (u16)sizeof(System_SettingsV1_t) &&
							    legacy->checksum == checksum)
							{
								System_Settings_SetDefaults(&settings);
								settings.volume = legacy->settings.volume;
								settings.Debug = legacy->settings.Debug;
								settings.UsbMscEnabled = legacy->settings.UsbMscEnabled;
								settings.ScreenLight = legacy->settings.ScreenLight;
								settings.FontInstalled = legacy->settings.FontInstalled;
								settings.FontBpp = legacy->settings.FontBpp;
								settings.FontLineHeight = legacy->settings.FontLineHeight;
								settings.FontBaseLine = legacy->settings.FontBaseLine;
								settings.FontFlashAddress = legacy->settings.FontFlashAddress;
								settings.FontSize = legacy->settings.FontSize;
								settings.FontMinUnicode = legacy->settings.FontMinUnicode;
								settings.FontMaxUnicode = legacy->settings.FontMaxUnicode;
								System_Settings_Normalize(&settings);
								System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION_V1,
									0U, &settings);
							}
						}
					}
				}
			}
		}
	}

	if (System_Settings_LoadFlashSlot(W25Q64_SETTINGS_SLOT0_ADDRESS, &flash_slot0) != 0U)
	{
		System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION,
			flash_slot0.sequence, &flash_slot0.settings);
	}
	if (System_Settings_LoadFlashSlot(W25Q64_SETTINGS_SLOT1_ADDRESS, &flash_slot1) != 0U)
	{
		System_Settings_ConsiderCandidate(&best, 1U, SYSTEM_SETTINGS_VERSION,
			flash_slot1.sequence, &flash_slot1.settings);
	}

	if (best.valid == 0U)
	{
		return OPERATE_FAILED;
	}

	System_Settings = best.settings;
	System_Settings_Normalize(&System_Settings);
	s_system_settings_sequence = best.sequence;

	return (best.version == SYSTEM_SETTINGS_VERSION) ? SYSTEM_STATUS_OK :
	       System_Settings_Save();
}

int System_Settings_Save(void)
{
	System_Settings_Record_t record;
	AT24C02_StatusTypeDef eeprom_status;
	u8 eeprom_ok;
	u8 flash_ok = 0U;
	u32 next_sequence;

	System_Settings_Normalize(&System_Settings);

	next_sequence = s_system_settings_sequence + 1U;
	memset(&record, 0, sizeof(record));
	record.magic = SYSTEM_SETTINGS_MAGIC;
	record.version = SYSTEM_SETTINGS_VERSION;
	record.size = (u16)sizeof(System_Settings_t);
	record.sequence = next_sequence;
	record.settings = System_Settings;
	record.checksum = System_Settings_Checksum((const u8*)&record,
		(u16)(sizeof(record) - sizeof(record.checksum)));

	eeprom_status = AT24C02_Write(SYSTEM_SETTINGS_EEPROM_ADDR, (const u8*)&record,
		(u16)sizeof(record));
	eeprom_ok = (eeprom_status == AT24C02_OK) ? 1U : 0U;

	if (System_Settings_SaveFlashSlot(
		((next_sequence & 1U) != 0U) ? W25Q64_SETTINGS_SLOT1_ADDRESS :
		W25Q64_SETTINGS_SLOT0_ADDRESS, &record) != 0U)
	{
		flash_ok = 1U;
	}
	else if (System_Settings_SaveFlashSlot(
		((next_sequence & 1U) != 0U) ? W25Q64_SETTINGS_SLOT0_ADDRESS :
		W25Q64_SETTINGS_SLOT1_ADDRESS, &record) != 0U)
	{
		flash_ok = 1U;
	}

	if (eeprom_ok != 0U || flash_ok != 0U)
	{
		s_system_settings_sequence = next_sequence;
		return SYSTEM_STATUS_OK;
	}

	return OPERATE_FAILED;
}

u8 System_Settings_FontInstalled(void)
{
	return (System_Settings.FontInstalled != 0U &&
	        System_Settings.FontFlashAddress == SYSTEM_FONT_FLASH_ADDR &&
	        System_Settings.FontSize > 0U &&
	        System_Settings.FontSize <= SYSTEM_FONT_FLASH_SIZE &&
	        System_Settings.FontMinUnicode <= System_Settings.FontMaxUnicode &&
	        (System_Settings.FontBpp == 1U || System_Settings.FontBpp == 2U ||
	         System_Settings.FontBpp == 4U || System_Settings.FontBpp == 8U)) ? 1U : 0U;
}

u8 System_Settings_UsbMscEnabled(void)
{
	return (System_Settings.UsbMscEnabled != 0U) ? 1U : 0U;
}

int System_Settings_SetUsbMscEnabled(u8 enabled)
{
	u8 old_enabled;
	int code;

	old_enabled = System_Settings.UsbMscEnabled;
	System_Settings.UsbMscEnabled = (enabled != 0U) ? 1U : 0U;
	code = System_Settings_Save();
	if (code != SYSTEM_STATUS_OK)
	{
		System_Settings.UsbMscEnabled = old_enabled;
	}

	return code;
}

u8 System_Settings_ThemeLightEnabled(void)
{
	return (System_Settings.ThemeLightEnabled != 0U) ? 1U : 0U;
}

int System_Settings_SetThemeLightEnabled(u8 enabled)
{
	u8 old_enabled;
	int code;

	old_enabled = System_Settings.ThemeLightEnabled;
	System_Settings.ThemeLightEnabled = (enabled != 0U) ? 1U : 0U;
	code = System_Settings_Save();
	if (code != SYSTEM_STATUS_OK)
	{
		System_Settings.ThemeLightEnabled = old_enabled;
	}

	return code;
}

u8 System_Settings_AxfStackLevel(void)
{
	return System_Settings.AxfStackLevel;
}

u32 System_Settings_AxfStackSize(void)
{
	u8 level = System_Settings_AxfStackLevel();

	if (level >= SYSTEM_AXF_STACK_LEVEL_COUNT)
	{
		level = SYSTEM_SETTINGS_DEFAULT_AXF_STACK;
	}

	return s_system_axf_stack_sizes[level];
}

int System_Settings_SetAxfStackLevel(u8 level)
{
	u8 old_level;
	int code;

	if (level >= SYSTEM_AXF_STACK_LEVEL_COUNT)
	{
		return OPERATE_FAILED;
	}

	old_level = System_Settings.AxfStackLevel;
	System_Settings.AxfStackLevel = level;
	code = System_Settings_Save();
	if (code != SYSTEM_STATUS_OK)
	{
		System_Settings.AxfStackLevel = old_level;
	}

	return code;
}

static u32 System_Settings_ClockFrequencyForProfile(u8 profile)
{
	/* HSE=8 MHz, PLLM=4 and PLLP=2 make SYSCLK equal PLLN MHz. */
	switch (profile)
	{
		case SYSTEM_CLOCK_192MHZ:
			return 192UL;
		case SYSTEM_CLOCK_216MHZ:
			return 216UL;
		case SYSTEM_CLOCK_240MHZ:
		default:
			return 240UL;
	}
}

static u32 System_Settings_ClockPllNForProfile(u8 profile)
{
	return System_Settings_ClockFrequencyForProfile(profile);
}

static u32 System_Settings_ClockPllQForProfile(u8 profile)
{
	/* PLL48CLK = (8 / 4 * PLLN) / PLLQ = 48 MHz for every profile. */
	switch (profile)
	{
		case SYSTEM_CLOCK_192MHZ:
			return 8UL;
		case SYSTEM_CLOCK_216MHZ:
			return 9UL;
		case SYSTEM_CLOCK_240MHZ:
		default:
			return 5UL;
	}
}

u8 System_Settings_ClockProfile(void)
{
	u8 profile = System_Settings.ClockProfile;

	if (profile >= SYSTEM_CLOCK_PROFILE_COUNT)
	{
		profile = SYSTEM_SETTINGS_DEFAULT_CLOCK;
	}

	return profile;
}

u32 System_Settings_ClockFrequencyMHz(void)
{
	return System_Settings_ClockFrequencyForProfile(System_Settings_ClockProfile());
}

u32 System_Settings_ClockPllN(void)
{
	return System_Settings_ClockPllNForProfile(System_Settings_ClockProfile());
}

u32 System_Settings_ClockPllQ(void)
{
	return System_Settings_ClockPllQForProfile(System_Settings_ClockProfile());
}

int System_Settings_SetClockProfile(u8 profile)
{
	u8 old_profile;
	int code;

	if (profile >= SYSTEM_CLOCK_PROFILE_COUNT)
	{
		return OPERATE_FAILED;
	}

	old_profile = System_Settings.ClockProfile;
	System_Settings.ClockProfile = profile;
	code = System_Settings_Save();
	if (code != SYSTEM_STATUS_OK)
	{
		System_Settings.ClockProfile = old_profile;
	}

	return code;
}

u8 System_Settings_ToastDurationSec(void)
{
	return System_Settings.ToastDuration;
}

int System_Settings_SetToastDuration(u8 seconds)
{
	u8 old_duration;
	int code;

	if (seconds < SYSTEM_TOAST_DURATION_MIN)
	{
		seconds = SYSTEM_TOAST_DURATION_MIN;
	}
	if (seconds > SYSTEM_TOAST_DURATION_MAX)
	{
		seconds = SYSTEM_TOAST_DURATION_MAX;
	}

	old_duration = System_Settings.ToastDuration;
	System_Settings.ToastDuration = seconds;
	code = System_Settings_Save();
	if (code != SYSTEM_STATUS_OK)
	{
		System_Settings.ToastDuration = old_duration;
	}

	return code;
}

u8 System_DebugEnabled(void)
{
	return (System_Settings.Debug != 0U) ? 1U : 0U;
}

u8 System_DebugShellEnabled(void)
{
	return (System_Settings.Debug != 0U &&
	        System_Settings.DebugShellEnabled != 0U) ? 1U : 0U;
}

u8 System_DebugLogEnabled(System_DebugLog_t source)
{
	u8 enabled = 0U;

	if (System_DebugShellEnabled() == 0U || SCI.Debug == 0U)
	{
		return 0U;
	}

	switch (source)
	{
		case SYSTEM_DEBUG_LOG_AUDIO:
			enabled = System_Settings.DebugAudioLogEnabled;
			break;
		case SYSTEM_DEBUG_LOG_VIDEO:
			enabled = System_Settings.DebugVideoLogEnabled;
			break;
		case SYSTEM_DEBUG_LOG_LIBJPEG:
			enabled = System_Settings.DebugLibjpegLogEnabled;
			break;
		case SYSTEM_DEBUG_LOG_FATFS:
			enabled = System_Settings.DebugFatfsLogEnabled;
			break;
		default:
			break;
	}

	return (enabled != 0U) ? 1U : 0U;
}

void System_DebugLogPrint(System_DebugLog_t source, const char* fmt, ...)
{
	static const char* const prefix[] = {"AUDIO", "VIDEO", "LIBJPEG", "FATFS"};
	char buffer[SYSTEM_DEBUG_LOG_BUFFER_SIZE];
	va_list args;
	int length;

	if (fmt == NULL || source > SYSTEM_DEBUG_LOG_FATFS ||
	    System_DebugLogEnabled(source) == 0U ||
	    shell.write == NULL || shell.parser.buffer == NULL)
	{
		return;
	}
#if SHELL_USING_LOCK == 1
	if (shell.lock == NULL || shell.unlock == NULL)
	{
		return;
	}
#endif

	va_start(args, fmt);
	length = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	if (length <= 0)
	{
		return;
	}

	shellPrintEndLine(&shell, "[%s] %s", prefix[(u32)source], buffer);
}

u8 System_MemoryDumpEnabled(void)
{
	return (System_Settings.Debug != 0U &&
	        System_Settings.MemoryDumpEnabled != 0U &&
	        (System_Settings.MemoryDumpCcmEnabled != 0U ||
	         System_Settings.MemoryDumpMainSramEnabled != 0U ||
	         System_Settings.MemoryDumpSramexEnabled != 0U)) ? 1U : 0U;
}

u8 System_MemoryDumpRegionEnabled(System_MemoryDumpRegion_t region)
{
	if (System_MemoryDumpEnabled() == 0U)
	{
		return 0U;
	}

	switch (region)
	{
		case SYSTEM_MEMORY_DUMP_REGION_CCM:
			return (System_Settings.MemoryDumpCcmEnabled != 0U) ? 1U : 0U;
		case SYSTEM_MEMORY_DUMP_REGION_MAIN_SRAM:
			return (System_Settings.MemoryDumpMainSramEnabled != 0U) ? 1U : 0U;
		case SYSTEM_MEMORY_DUMP_REGION_SRAMEX:
			return (System_Settings.MemoryDumpSramexEnabled != 0U) ? 1U : 0U;
		default:
			return 0U;
	}
}

static u32 System_Settings_Checksum(const u8* data, u16 length)
{
	u16 i;
	u32 hash = 2166136261UL;

	if (data == NULL)
	{
		return 0U;
	}

	for (i = 0U; i < length; i++)
	{
		hash ^= data[i];
		hash *= 16777619UL;
	}

	return hash;
}

static void System_Settings_Normalize(System_Settings_t* settings)
{
	u8 valid_font;

	if (settings == NULL)
	{
		return;
	}

	settings->Debug = (settings->Debug != 0U) ? 1U : 0U;
	settings->DebugShellEnabled = (settings->DebugShellEnabled != 0U) ? 1U : 0U;
	settings->DebugAudioLogEnabled = (settings->DebugAudioLogEnabled != 0U) ? 1U : 0U;
	settings->DebugVideoLogEnabled = (settings->DebugVideoLogEnabled != 0U) ? 1U : 0U;
	settings->DebugLibjpegLogEnabled = (settings->DebugLibjpegLogEnabled != 0U) ? 1U : 0U;
	settings->DebugFatfsLogEnabled = (settings->DebugFatfsLogEnabled != 0U) ? 1U : 0U;
	settings->MemoryDumpEnabled = (settings->MemoryDumpEnabled != 0U) ? 1U : 0U;
	settings->MemoryDumpCcmEnabled = (settings->MemoryDumpCcmEnabled != 0U) ? 1U : 0U;
	settings->MemoryDumpMainSramEnabled = (settings->MemoryDumpMainSramEnabled != 0U) ? 1U : 0U;
	settings->MemoryDumpSramexEnabled = (settings->MemoryDumpSramexEnabled != 0U) ? 1U : 0U;
	settings->UsbMscEnabled = (settings->UsbMscEnabled != 0U) ? 1U : 0U;
	settings->ThemeLightEnabled = (settings->ThemeLightEnabled != 0U) ? 1U : 0U;
	if (settings->ClockProfile >= SYSTEM_CLOCK_PROFILE_COUNT)
	{
		settings->ClockProfile = SYSTEM_SETTINGS_DEFAULT_CLOCK;
	}
	if (settings->AxfStackLevel >= SYSTEM_AXF_STACK_LEVEL_COUNT)
	{
		settings->AxfStackLevel = SYSTEM_SETTINGS_DEFAULT_AXF_STACK;
	}
	if (settings->ToastDuration < SYSTEM_TOAST_DURATION_MIN ||
	    settings->ToastDuration > SYSTEM_TOAST_DURATION_MAX)
	{
		settings->ToastDuration = SYSTEM_SETTINGS_DEFAULT_TOAST_DURATION;
	}
	valid_font = (settings->FontInstalled != 0U &&
	              settings->FontFlashAddress == SYSTEM_FONT_FLASH_ADDR &&
	              settings->FontSize > 0U &&
	              settings->FontSize <= SYSTEM_FONT_FLASH_SIZE &&
	              settings->FontMinUnicode <= settings->FontMaxUnicode &&
	              (settings->FontBpp == 1U || settings->FontBpp == 2U ||
	               settings->FontBpp == 4U || settings->FontBpp == 8U)) ? 1U : 0U;

	if (valid_font == 0U)
	{
		settings->FontInstalled = 0U;
		settings->FontBpp = 0U;
		settings->FontLineHeight = SYSTEM_SETTINGS_DEFAULT_FONT_LINE;
		settings->FontBaseLine = 0U;
		settings->FontFlashAddress = SYSTEM_FONT_FLASH_ADDR;
		settings->FontSize = 0U;
		settings->FontMinUnicode = 0U;
		settings->FontMaxUnicode = 0U;
	}
	else if (settings->FontLineHeight == 0U)
	{
		settings->FontLineHeight = SYSTEM_SETTINGS_DEFAULT_FONT_LINE;
	}
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC)|SHELL_CMD_DISABLE_RETURN,
sdinfo, GetSdCardInfo, show SD card information);


