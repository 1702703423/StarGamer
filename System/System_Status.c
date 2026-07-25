#include "System_Status.h"
#include "LCD.h"
#include "main.h"
#include "sys.h"
#include "at24c02.h"
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"
#include "shell_support.h"
#include <stdio.h>
#include <string.h>

#define SYSTEM_SETTINGS_EEPROM_ADDR       0U
#define SYSTEM_SETTINGS_MAGIC             0x53544753UL
#define SYSTEM_SETTINGS_VERSION           3U
#define SYSTEM_SETTINGS_VERSION_V2        2U
#define SYSTEM_SETTINGS_VERSION_V1        1U
#define SYSTEM_SETTINGS_DEFAULT_VOLUME    70UL
#define SYSTEM_SETTINGS_DEFAULT_LIGHT     100U
#define SYSTEM_SETTINGS_DEFAULT_FONT_LINE 21U
#define SYSTEM_DEBUG_LOG_BUFFER_SIZE       192U

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

typedef struct System_Settings_Record
{
	u32 magic;
	u16 version;
	u16 size;
	System_Settings_t settings;
	u32 checksum;
}System_Settings_Record_t;

typedef struct System_Settings_RecordV2
{
	u32 magic;
	u16 version;
	u16 size;
	System_SettingsV2_t settings;
	u32 checksum;
} System_Settings_RecordV2_t;

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
	System_Settings_RecordV2_t v2;
	System_Settings_RecordV1_t legacy;
} System_Settings_Storage_t;

typedef char System_Settings_RecordSizeCheck[(sizeof(System_Settings_Record_t) <= 128U) ? 1 : -1];

static u32 System_Settings_Checksum(const u8* data, u16 length);
static void System_Settings_Normalize(System_Settings_t* settings);

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
	if (s_lvgl_display_owner != current || s_lvgl_pause_requested == 0U || s_lvgl_paused == 0U)
	{
		(void)osKernelRestoreLock(lock_state);
		return OPERATE_FAILED;
	}

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
	if (s_lvgl_display_owner != NULL || s_lvgl_pause_requested != 0U || s_lvgl_paused != 0U)
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

u8 lvgl_ui_pause_point(void)
{
	osThreadId_t owner;
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
		if (s_lvgl_display_owner == owner)
		{
			(void)osThreadFlagsSet(owner, LVGL_UI_FLAG_RESUMED);
			s_lvgl_display_owner = NULL;
		}
		(void)osKernelRestoreLock(lock_state);
	}

	return paused;
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
	settings->ScreenLight = SYSTEM_SETTINGS_DEFAULT_LIGHT;
	settings->FontFlashAddress = SYSTEM_FONT_FLASH_ADDR;
	settings->FontLineHeight = SYSTEM_SETTINGS_DEFAULT_FONT_LINE;
}

int System_Settings_Init(void)
{
	AT24C02_StatusTypeDef at_status;
	int code;

	System_Settings_SetDefaults(&System_Settings);

	at_status = AT24C02_Init();
	if (at_status != AT24C02_OK)
	{
		return OPERATE_FAILED;
	}

	code = System_Settings_Load();
	if (code == SYSTEM_STATUS_OK)
	{
		return SYSTEM_STATUS_OK;
	}

	System_Settings_SetDefaults(&System_Settings);
	return System_Settings_Save();
}

int System_Settings_Load(void)
{
	System_Settings_Storage_t storage;
	System_Settings_Record_t* record;
	System_Settings_RecordV2_t* v2;
	System_Settings_RecordV1_t* legacy;
	u32 checksum;
	AT24C02_StatusTypeDef status;

	memset(&storage, 0, sizeof(storage));
	status = AT24C02_Read(SYSTEM_SETTINGS_EEPROM_ADDR, (u8*)&storage, (u16)sizeof(storage));
	if (status != AT24C02_OK)
	{
		return OPERATE_FAILED;
	}

	record = &storage.current;
	checksum = System_Settings_Checksum((const u8*)record,
		(u16)(sizeof(*record) - sizeof(record->checksum)));
	if (record->magic == SYSTEM_SETTINGS_MAGIC &&
	    record->version == SYSTEM_SETTINGS_VERSION &&
	    record->size == (u16)sizeof(System_Settings_t) &&
	    record->checksum == checksum)
	{
		System_Settings = record->settings;
		System_Settings_Normalize(&System_Settings);
		return SYSTEM_STATUS_OK;
	}

	v2 = &storage.v2;
	checksum = System_Settings_Checksum((const u8*)v2,
		(u16)(sizeof(*v2) - sizeof(v2->checksum)));
	if (v2->magic == SYSTEM_SETTINGS_MAGIC &&
	    v2->version == SYSTEM_SETTINGS_VERSION_V2 &&
	    v2->size == (u16)sizeof(System_SettingsV2_t) &&
	    v2->checksum == checksum)
	{
		System_Settings_SetDefaults(&System_Settings);
		System_Settings.volume = v2->settings.volume;
		System_Settings.Debug = v2->settings.Debug;
		System_Settings.UsbMscEnabled = v2->settings.UsbMscEnabled;
		System_Settings.ScreenLight = v2->settings.ScreenLight;
		System_Settings.FontInstalled = v2->settings.FontInstalled;
		System_Settings.FontBpp = v2->settings.FontBpp;
		System_Settings.FontLineHeight = v2->settings.FontLineHeight;
		System_Settings.FontBaseLine = v2->settings.FontBaseLine;
		System_Settings.FontFlashAddress = v2->settings.FontFlashAddress;
		System_Settings.FontSize = v2->settings.FontSize;
		System_Settings.FontMinUnicode = v2->settings.FontMinUnicode;
		System_Settings.FontMaxUnicode = v2->settings.FontMaxUnicode;
		System_Settings.DebugShellEnabled = v2->settings.DebugShellEnabled;
		System_Settings.DebugAudioLogEnabled = v2->settings.DebugAudioLogEnabled;
		System_Settings.DebugVideoLogEnabled = v2->settings.DebugVideoLogEnabled;
		System_Settings.DebugLibjpegLogEnabled = v2->settings.DebugLibjpegLogEnabled;
		System_Settings.DebugFatfsLogEnabled = v2->settings.DebugFatfsLogEnabled;
		System_Settings.MemoryDumpEnabled = v2->settings.MemoryDumpEnabled;
		System_Settings.MemoryDumpCcmEnabled = v2->settings.MemoryDumpCcmEnabled;
		System_Settings.MemoryDumpMainSramEnabled = v2->settings.MemoryDumpMainSramEnabled;
		System_Settings.MemoryDumpSramexEnabled = v2->settings.MemoryDumpSramexEnabled;
		System_Settings_Normalize(&System_Settings);
		return System_Settings_Save();
	}

	legacy = &storage.legacy;
	checksum = System_Settings_Checksum((const u8*)legacy,
		(u16)(sizeof(*legacy) - sizeof(legacy->checksum)));
	if (legacy->magic != SYSTEM_SETTINGS_MAGIC ||
	    legacy->version != SYSTEM_SETTINGS_VERSION_V1 ||
	    legacy->size != (u16)sizeof(System_SettingsV1_t) ||
	    legacy->checksum != checksum)
	{
		return OPERATE_FAILED;
	}

	System_Settings_SetDefaults(&System_Settings);
	System_Settings.volume = legacy->settings.volume;
	System_Settings.Debug = legacy->settings.Debug;
	System_Settings.UsbMscEnabled = legacy->settings.UsbMscEnabled;
	System_Settings.ScreenLight = legacy->settings.ScreenLight;
	System_Settings.FontInstalled = legacy->settings.FontInstalled;
	System_Settings.FontBpp = legacy->settings.FontBpp;
	System_Settings.FontLineHeight = legacy->settings.FontLineHeight;
	System_Settings.FontBaseLine = legacy->settings.FontBaseLine;
	System_Settings.FontFlashAddress = legacy->settings.FontFlashAddress;
	System_Settings.FontSize = legacy->settings.FontSize;
	System_Settings.FontMinUnicode = legacy->settings.FontMinUnicode;
	System_Settings.FontMaxUnicode = legacy->settings.FontMaxUnicode;
	System_Settings_Normalize(&System_Settings);
	return System_Settings_Save();
}

int System_Settings_Save(void)
{
	System_Settings_Record_t record;
	AT24C02_StatusTypeDef status;

	System_Settings_Normalize(&System_Settings);

	memset(&record, 0, sizeof(record));
	record.magic = SYSTEM_SETTINGS_MAGIC;
	record.version = SYSTEM_SETTINGS_VERSION;
	record.size = (u16)sizeof(System_Settings_t);
	record.settings = System_Settings;
	record.checksum = System_Settings_Checksum((const u8*)&record, (u16)(sizeof(record) - sizeof(record.checksum)));

	status = AT24C02_Write(SYSTEM_SETTINGS_EEPROM_ADDR, (const u8*)&record, (u16)sizeof(record));
	return (status == AT24C02_OK) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
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


