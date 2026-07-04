/**
 * @file shell_support.c
 * @brief Shell infrastructure: mutex, read/write, ring buffer, RX ISR, idle hook, System_init
 */
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "stdio.h"
#include "string.h"
#include "System_Status.h"
#include "Malloc.h"
#include "shell.h"
#include "shell_support.h"
#include <stdarg.h>

/* Types and externs from main.c */
typedef StaticTask_t osStaticThreadDef_t;
extern UART_HandleTypeDef huart1;

/* ---- External globals (defined in main.c) ---- */
extern Shell shell;
extern System_Resource_Table_t SRT;
extern System_Status_Information_t SSI;
extern System_Core_Information_t SCI;

extern void shell_fs_init(void);
extern int AxfDebugInfoPushToUart(const char *string, va_list arg);

/* ---- Shell thread static resources ---- */
#define SHELL_THREAD_STACK_SIZE  1024
#define SHELL_BUFFER_SIZE        1024
char shell_buffer[SHELL_BUFFER_SIZE];
static uint32_t shellTaskBuffer[SHELL_THREAD_STACK_SIZE];
static osStaticThreadDef_t shellTaskControlBlock;
const osThreadAttr_t shellTask_attributes = {
  .name = "shellTask",
  .cb_mem = &shellTaskControlBlock,
  .cb_size = sizeof(shellTaskControlBlock),
  .stack_mem = &shellTaskBuffer[0],
  .stack_size = sizeof(shellTaskBuffer),
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* ---- CPU idle counter (counts OS ticks observed by the idle task) ---- */
volatile uint32_t cpu_idle_ticks = 0;

void vApplicationIdleHook(void)
{
	/*
	 * Count one idle tick per OS tick period. The idle hook runs in a tight
	 * loop inside the idle task and may be called many times per tick.
	 * Without this guard, cpu_idle_ticks would increment far faster than
	 * the OS tick, causing sysstats to always report 100% CPU usage.
	 */
	static TickType_t last_idle_tick = 0;
	TickType_t now = xTaskGetTickCount();

	if (now != last_idle_tick) {
		cpu_idle_ticks++;
		last_idle_tick = now;
	}
}

/* ---- Shell mutex ---- */
static osMutexId_t shell_mutex_id = NULL;

int shell_lock(Shell *shell)
{
	if (shell_mutex_id == NULL) return -1;
	osStatus_t status = osMutexAcquire(shell_mutex_id, osWaitForever);
	return (status != osOK) ? -1 : 0;
}

int shell_unlock(Shell *shell)
{
	if (shell_mutex_id == NULL) return -1;
	osStatus_t status = osMutexRelease(shell_mutex_id);
	return (status != osOK) ? -1 : 0;
}

int shell_lock_create()
{
	const osMutexAttr_t mutex_attr = {
		.attr_bits = osMutexRecursive,
		.name = "shell_mutex"
	};
	shell_mutex_id = osMutexNew(&mutex_attr);
	if (shell_mutex_id == NULL) return -1;
	shell.lock = shell_lock;
	shell.unlock = shell_unlock;
	return 0;
}

/* ---- shellPrintEndLine ---- */
void shellPrintEndLine(Shell *shell, const char *fmt, ...)
{
	char buf[SHELL_PRINT_BUFFER];
	va_list args;
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len > 0) {
		shellWriteEndLine(shell, buf, len);
	}
}

/* ---- UART RX ring buffer ---- */
#define SHELL_RX_BUF_SIZE  256
static char shell_rx_buf[SHELL_RX_BUF_SIZE];
static volatile uint16_t shell_rx_head = 0;
static volatile uint16_t shell_rx_tail = 0;
static uint8_t shell_rx_byte;

signed short shell_read(char *data, unsigned short len)
{
	unsigned short i;
	for (i = 0; i < len; i++) {
		while (shell_rx_head == shell_rx_tail) {
			osDelay(1);
		}
		data[i] = shell_rx_buf[shell_rx_tail];
		shell_rx_tail = (shell_rx_tail + 1) % SHELL_RX_BUF_SIZE;
	}
	return (signed short)len;
}

signed short shell_write(char *data, unsigned short len)
{
	unsigned short i;
	for (i = 0; i < len; i++) {
		if (data[i] == '\n') {
			while ((USART1->SR & 0x40) == 0);
			USART1->DR = '\r';
		}
		while ((USART1->SR & 0x40) == 0);
		USART1->DR = (uint8_t)data[i];
	}
	return (signed short)len;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART1) {
		uint16_t next_head = (shell_rx_head + 1) % SHELL_RX_BUF_SIZE;
		if (next_head != shell_rx_tail) {
			shell_rx_buf[shell_rx_head] = (char)shell_rx_byte;
			shell_rx_head = next_head;
		}
		HAL_UART_Receive_IT(&huart1, &shell_rx_byte, 1);
	}
}

void shell_uart_rx_start(void)
{
	HAL_UART_Receive_IT(&huart1, &shell_rx_byte, 1);
}

/* ---- System_init (moved to test.c) ---- */
/* System_init() is now in test.c; it calls shell_lock_create(),
   shellInit, shell_uart_rx_start, etc. from this module. */
