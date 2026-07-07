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
#include "w25q64.h"
#include "key.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "../GUI/GUI_Main.h"

/* External globals */
extern Shell shell;
extern System_Resource_Table_t SRT;
extern System_Status_Information_t SSI;
extern System_Core_Information_t SCI;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi1;
extern int AxfDebugInfoPushToUart(const char *string, va_list arg);
extern void shell_fs_init(void);
extern void lv_port_indev_init(void);

/* ---- LVGL timer handler thread ---- */
#define LVGL_THREAD_STACK_SIZE  1024
typedef StaticTask_t osStaticThreadDef_t;
static uint32_t lvglTaskBuffer[LVGL_THREAD_STACK_SIZE];
static osStaticThreadDef_t lvglTaskControlBlock;
static const osThreadAttr_t lvglTask_attributes = {
	.name = "LVGL",
	.cb_mem = &lvglTaskControlBlock,
	.cb_size = sizeof(lvglTaskControlBlock),
	.stack_mem = &lvglTaskBuffer[0],
	.stack_size = sizeof(lvglTaskBuffer),
	.priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t lvglThreadId = NULL;

/* ---- LVGL timer handler task ---- */
static void lvgl_timer_task(void *arg)
{
	(void)arg;
	while (1) {
		lv_timer_handler();
		osDelay(10);
	}
}

static int key_init()
{
	return KEY_Init();
}

/* ---- System Init ---- */
void System_init()
{
	int code;
	
	//内存池初始化
	sys_mem_init(0);
	sys_mem_init(1);
	
	//开发阶段，调试模式常开
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
	SRT.task_table = creat_chain(0,&code);
	
	creat_frame_buffer(&code);/*本项目架构中默认第一缓冲区为LVGL缓冲*/
	
}

/* ---- HardFault Handler (C part) ---- */
void HardFault_Handler_C(uint32_t *stack_pointer, uint32_t lr_value, uint32_t dummy)
{
	uint32_t cfsr = SCB->CFSR;
	uint32_t hfsr = SCB->HFSR;

	printf("HardFault!\r\n");
	printf("LR: 0x%08X\r\n", lr_value);
	printf("CFSR: 0x%08X\r\n", cfsr);
	printf("HFSR: 0x%08X\r\n", hfsr);

	printf("R0: 0x%08X\r\n", stack_pointer[0]);
	printf("R1: 0x%08X\r\n", stack_pointer[1]);
	printf("R2: 0x%08X\r\n", stack_pointer[2]);
	printf("R3: 0x%08X\r\n", stack_pointer[3]);
	printf("R12: 0x%08X\r\n", stack_pointer[4]);
	printf("LR: 0x%08X\r\n", stack_pointer[5]);
	printf("PC: 0x%08X\r\n", stack_pointer[6]);
	printf("PSR: 0x%08X\r\n", stack_pointer[7]);

	printf("Please Contact to Us\r\n");

	while (1);
}

/* ---- LCD test ---- */
void lcd_test()
{
	uint8_t *p = ((Frame_Buffer_Status_t *)(SRT.frame_buffer_table->next->next->data))->pointer;

	LCD_Fill(0, 0, 320, 120, BLUE, p);
	LCD_Fill(0, 120, 320, 240, RED, p);
	LCD_DrawBox(1, 1, 120, 60, YELLOW, GRAY, p);
	LCD_ShowFloat(10, 10, 18.372f, 2, YELLOW, GRAY, 16, 0, p);
	LCD_ShowString(1, 220, (const u8 *)"HelloWorld", YELLOW, RED, 16, 0, p);
	LCD_DrawFillCircle(160, 20, 20, YELLOW, p);
	LCD_DrawCircle(220, 20, 20, GREEN, p);
	LCD_Flush(p);
}

/* ---- GPIO init ---- */
static void gpio_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOE_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_6;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	/* Deselect W25Q64 (CS high) */
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_SET);
}

/* ---- Board support init ---- */
static void bsp_init()
{
	uint8 mid;
	uint16_t did;
	
	gpio_init();
	LCD_Init(&hspi1);
	key_init();
	if(W25Q64_Init() == W25Q64_OK)
	{
		W25Q64_ReadID(&mid,&did);
		shellPrintEndLine(&shell,"W25Q64InitSuccess,MID,DID:%X,%X\r\n",mid,did);
	}else
	{
		shellPrintEndLine(&shell,"W25Q64Failed\r\n");
	}
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

static void LVGL_shellLog(const char* log)
{
	shellPrintEndLine(&shell,log);
}

static void lvgl_init()
{
	lv_init();
	lv_port_disp_init();
	lv_port_indev_init();
	if(SCI.Debug)
	{
		lv_log_register_print_cb(LVGL_shellLog);
	}
	lvglThreadId = osThreadNew(lvgl_timer_task, NULL, &lvglTask_attributes);
}

static void GUI_Init(void)
{
	(void)GUI_Main_Init();
}

void init(void)
{
	int res;
	res = mount_fs();
	System_init();
	bsp_init();
		/* LVGL 初始化 */
	lvgl_init();
//	nes_load((u8*)"0://SICHUAN.NES");
	GUI_Init();
//	LCD_Fill(1, 1, 320, 240, BLACK, p);
//	LCD_Flush(p);lcd_test();

//	if (res)
//	{
//		DL_Handler DL;
//		int (*enterFunc)(int argc, char *argv[]);
//		DL_Err_Type err;

//		osKernelLock();
//		err = dl_load_file(&DL, "0:/debug.axf");
//		osKernelUnlock();

//		shellPrintEndLine(&shell, "err:%d\n", err);

//		if (err) {
//			while (1);
//		}

//		enterFunc = dl_get_entry(&DL);
//		shellPrintEndLine(&shell, "StartAddressAt:%p\n", enterFunc);

//		if (enterFunc == NULL) {
//			shellPrintEndLine(&shell, "cant find entry\n");
//		}

//		shellPrintEndLine(&shell, "NeedExecute:%p\n", AxfDebugInfoPushToUart);
//		shellPrintEndLine(&shell, "res:%d\n", enterFunc(0, NULL));
//		dl_destroy_lib(&DL);
//	}

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
