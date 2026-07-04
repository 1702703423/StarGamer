#include "System_Status.h"
#include "sys.h"
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"
#include "shell_support.h"

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
	
	uint8_t* ST7789s_ScreenBuffer = (uint8_t*)sys_malloc(1,ST7789s_ScreenBufferSize); //����֡�����ڴ�
	
	if(ST7789s_ScreenBuffer == NULL)
	{
		*code = OUT_OF_HEAP_MEMORY;
		return NULL;
	}
	
	Frame_Buffer_Status_t* FBST_data = (Frame_Buffer_Status_t*)sys_malloc(0,sizeof(Frame_Buffer_Status_t)); //����֡����������ڴ�?
	
	if(FBST_data == NULL)
	{
		sys_free(1,(void*)ST7789s_ScreenBuffer);
		*code = OUT_OF_HEAP_MEMORY;
		return NULL;
	}
	
	FBST_data->pointer = ST7789s_ScreenBuffer; //��¼֡������?
	FBST_data->state = 0;
	
	if(insert_head_chain(SRT.frame_buffer_table,FBST_data,&_code) == NULL)
	{
		sys_free(1,(void*)ST7789s_ScreenBuffer);
		sys_free(0,(void*)FBST_data);
		*code = OPERATE_FAILED;
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

	sys_free(1, fb->pointer);	//�ͷ��ڴ�
	sys_free(0, fb);			
	
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
	
	SYS_Task_Control_Table_t* SYS_TCT = (SYS_Task_Control_Table_t*)sys_malloc(0,sizeof(SYS_Task_Control_Table_t));

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
		sys_free(0,(void*)SYS_TCT);
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
	sys_free(0, (void*)found);
	
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
		sys_free(0, (void*)found);
	}

	osKernelUnlock();

	return (_code == SYSTEM_STATUS_OK) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}


