/**
 * @file shell_support.h
 * @brief Shell infrastructure support — mutex, read/write, idle hook, init
 */
#ifndef __SHELL_SUPPORT_H__
#define __SHELL_SUPPORT_H__

#include "cmsis_os.h"
#include "shell.h"

/* Buffer size (must match shell_support.c) */
#define SHELL_BUFFER_SIZE  1024

/* Global for cpu_idle_ticks (exported for sysstats command) */
extern volatile uint32_t cpu_idle_ticks;

/* Shell thread attributes (exported for System_init) */
extern osThreadAttr_t shellTask_attributes;

/* Core functions */
void System_init(void);
void shellPrintEndLine(Shell *shell, const char *fmt, ...);

/* Shell internal buffers (needed by System_init caller) */
extern char shell_buffer[];
void shell_uart_rx_start(void);
int shell_lock_create(void);

/* Shell read/write (assigned to shell struct in System_init) */
signed short shell_read(char *data, unsigned short len);
signed short shell_write(char *data, unsigned short len);

#endif /* __SHELL_SUPPORT_H__ */
