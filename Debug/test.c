/**
 * @file test.c
 * @brief Application test code — System init, LCD, bsp, mount_fs, HardFault handler
 */
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "ff.h"
#include "stdio.h"
#include "string.h"
#include "Player.h"
#include "System_Status.h"
#include "Malloc.h"
#include "LCD.h"
#include "dl_lib.h"
#include "nes_main.h"
#include "shell.h"
#include "shell_support.h"
#include "test.h"
#include "Player.h"

/* External globals */
extern Shell shell;
extern System_Resource_Table_t SRT;
extern System_Status_Information_t SSI;
extern System_Core_Information_t SCI;
extern SPI_HandleTypeDef hspi2;
extern int AxfDebugInfoPushToUart(const char *string, va_list arg);
extern void shell_fs_init(void);

/* ---- System Init ---- */
void System_init()
{
	int code;

	sys_mem_init(0);
	sys_mem_init(1);

	SCI.Debug = 1;

	shell.read = shell_read;
	shell.write = shell_write;

	if (SCI.Debug) {
		shell_lock_create();
		shellInit(&shell, shell_buffer, SHELL_BUFFER_SIZE);
		shell_uart_rx_start();
		osThreadNew(shellTask, &shell, &shellTask_attributes);
		shell_fs_init();
	}

	SRT.frame_buffer_table = creat_chain(0, &code);
	SRT.task_table = creat_chain(0, &code);

	creat_frame_buffer(&code);

	shellPrintEndLine(&shell, "ScreenFrameNum:%d\n",
	                  get_chain_length(SRT.frame_buffer_table, &code));
	shellPrintEndLine(&shell, "MemPresureExMem:%d\n", sys_mem_perused(1));
}

/* ---- HardFault Handler (C part) ---- */
void HardFault_Handler_C(uint32_t *stack_pointer, uint32_t lr_value, uint32_t dummy)
{
	uint32_t cfsr = SCB->CFSR;
	uint32_t hfsr = SCB->HFSR;

	printf("HardFault!\r\n");
	printf("LR: 0x%08lX\r\n", lr_value);
	printf("CFSR: 0x%08lX\r\n", cfsr);
	printf("HFSR: 0x%08lX\r\n", hfsr);

	printf("R0: 0x%08lX\r\n", stack_pointer[0]);
	printf("R1: 0x%08lX\r\n", stack_pointer[1]);
	printf("R2: 0x%08lX\r\n", stack_pointer[2]);
	printf("R3: 0x%08lX\r\n", stack_pointer[3]);
	printf("R12: 0x%08lX\r\n", stack_pointer[4]);
	printf("LR: 0x%08lX\r\n", stack_pointer[5]);
	printf("PC: 0x%08lX\r\n", stack_pointer[6]);
	printf("PSR: 0x%08lX\r\n", stack_pointer[7]);

	printf("Please Contact to Us\r\n");

	while (1);
}

/* ---- LCD test ---- */
void lcd_test()
{
	uint8_t *p = ((Frame_Buffer_Status_t *)(SRT.frame_buffer_table->next->data))->pointer;

	LCD_Fill(0, 0, 320, 120, BLUE, p);
	LCD_Fill(0, 120, 320, 240, RED, p);
	LCD_DrawBox(1, 1, 120, 60, YELLOW, GRAY, p);
	LCD_ShowFloat(10, 10, 18.372f, 2, YELLOW, GRAY, 16, 0, p);
	LCD_ShowString(1, 220, (const u8 *)"HelloWorld", YELLOW, RED, 16, 0, p);
	LCD_DrawFillCircle(160, 20, 20, YELLOW, p);
	LCD_DrawCircle(220, 20, 20, GREEN, p);
	LCD_Flush(p);
}

/* ---- Board support init ---- */
static void bsp_init()
{
	LCD_Init(&hspi2);
}

/* ---- Mount FATFS ---- */
int mount_fs()
{
	FRESULT fr;

	fr = f_mount(&SDFatFS, "0:", 1);

	if (fr == FR_OK) {
		SSI.fs_mount_status = 1;
	}

	return SSI.fs_mount_status;
}

/* ---- Main application entry (called from StartDefaultTask) ---- */

FIL file;

void test_run(void)
{
	System_init();
	Wave_Infomation WI;
	uint8_t *p = ((Frame_Buffer_Status_t *)(SRT.frame_buffer_table->next->data))->pointer;
	int res;

	bsp_init();
	LCD_Fill(1, 1, 320, 240, BLACK, p);
	LCD_Flush(p);

	res = mount_fs();

	if (res)
	{
		DL_Handler DL;
		int (*enterFunc)(int argc, char *argv[]);
		DL_Err_Type err;

		osKernelLock();
		err = dl_load_file(&DL, "0:/debug.axf");
		osKernelUnlock();

		shellPrintEndLine(&shell, "err:%d\n", err);

		if (err) {
			while (1);
		}

		enterFunc = dl_get_entry(&DL);
		shellPrintEndLine(&shell, "StartAddressAt:%p\n", enterFunc);

		if (enterFunc == NULL) {
			shellPrintEndLine(&shell, "cant find entry\n");
		}

		shellPrintEndLine(&shell, "NeedExecute:%p\n", AxfDebugInfoPushToUart);
		shellPrintEndLine(&shell, "res:%d\n", enterFunc(0, NULL));
		dl_destroy_lib(&DL);
	}
	
	f_open(&file,"0:/33.wav",FA_READ);
	PlayerStart(&file);

	if (res) {
		while (1) {
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);
			osDelay(pdMS_TO_TICKS(1000));
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET);
			osDelay(pdMS_TO_TICKS(1000));
		}
	} else {
		while (1) {
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);
			osDelay(pdMS_TO_TICKS(200));
			HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET);
			osDelay(pdMS_TO_TICKS(200));
		}
	}
}
