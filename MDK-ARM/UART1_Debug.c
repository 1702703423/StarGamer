#include "stm32f4xx.h"                  // Device header
#include "sys.h"
#include "System_Status.h"
#include <stdarg.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "shell.h"
#include "shell_support.h"

extern Shell shell;

//printf�ض���UART1

/******************************************************************************************/
/* �������´���, ֧��printf����, ������Ҫѡ��use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)                    /* ʹ��AC6������ʱ */
__asm(".global __use_no_semihosting\n\t");          /* ������ʹ�ð�����ģʽ */
__asm(".global __ARM_use_no_argv \n\t");            /* AC6����Ҫ����main����Ϊ�޲�����ʽ�����򲿷����̿��ܳ��ְ�����ģʽ */

#else
/* ʹ��AC5������ʱ, Ҫ�����ﶨ��__FILE �� ��ʹ�ð�����ģʽ */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using printf() for debugging, no file handling */
    /* is required. */
};

#endif

/* ��ʹ�ð�����ģʽ��������Ҫ�ض���_ttywrch\_sys_exit\_sys_command_string����,��ͬʱ����AC6��AC5ģʽ */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* ����_sys_exit()�Ա���ʹ�ð�����ģʽ */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

/* FILE �� stdio.h���涨��. */

struct __FILE
{
  int handle;
};

FILE __stdout;

/* �ض���fputc����, printf�������ջ�ͨ������fputc����ַ��������� */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);               /* �ȴ���һ���ַ�������� */

    USART1->DR = (uint8_t)ch;                       /* ��Ҫ���͵��ַ� ch д�뵽DR�Ĵ��� */
    return ch;
}

//int SysDebugInfoPushToUart(const char* string,...)
//{
//    osKernelLock();

//	if(SCI.Debug)
//	{
//		char String[100];				//�����ַ�����
//		va_list arg;					//����ɱ�����б��������͵ı���arg
//		va_start(arg, string);			//��format��ʼ�����ղ����б���arg����
//		vsprintf(String, string, arg);	//ʹ��vsprintf��ӡ��ʽ���ַ����Ͳ����б����ַ�������
//		va_end(arg);					//��������arg
//		printf("%s",String);
//		return SYSTEM_STATUS_OK;
//	}
//	return DEBUG_OPTION_DISABLE;

//    osKernelUnlock();
//}

int AxfDebugInfoPushToUart(const char* string, va_list arg)
{
    if(SCI.Debug)
    {
        char buf[160];
        int len = snprintf(buf, sizeof(buf), "[Axf]:");
        len += vsnprintf(buf + len, sizeof(buf) - len, string, arg);
        len += snprintf(buf + len, sizeof(buf) - len, "\n");
        if (len > 0)
        {
            shellWriteEndLine(&shell, buf, len);
        }
        return SYSTEM_STATUS_OK;
    }
		
    return DEBUG_OPTION_DISABLE;
}

static const char *shell_task_state_str(osThreadState_t state)
{
	switch (state)
	{
	case osThreadInactive:   return "Inactive";
	case osThreadReady:      return "Ready";
	case osThreadRunning:    return "Running";
	case osThreadBlocked:    return "Blocked";
	case osThreadTerminated: return "Terminated";
	default:                 return "Unknown";
	}
}

void shellTaskList(void)
{
#define MAX_TASKS 16
	osThreadId_t threads[MAX_TASKS];
	uint32_t count;
	uint32_t i;

	count = osThreadEnumerate(threads, MAX_TASKS);

	shellPrintEndLine(&shell, "\n===== RTOS Task List (%lu tasks) =====\n", count);
	shellPrintEndLine(&shell, "%-16s %-8s %-12s %-8s %-10s\n",
	           "Name", "Type", "State", "Prio", "StackFree");

	for (i = 0; i < count && i < MAX_TASKS; i++)
	{
		const char *name = osThreadGetName(threads[i]);
		osThreadState_t state = osThreadGetState(threads[i]);
		osPriority_t prio = osThreadGetPriority(threads[i]);
		uint32_t stack_free = osThreadGetStackSpace(threads[i]);
		const char *type = "SYSTEM";

		/* Check if this thread was created via SysCreatThread → USER */
		{
			adt_chain_t *node = SRT.task_table->next;
			while (node)
			{
				SYS_Task_Control_Table_t *tct = (SYS_Task_Control_Table_t *)node->data;
				if (tct && tct->tid == threads[i])
				{
					type = "USER";
					break;
				}
				node = node->next;
			}
		}

		shellPrintEndLine(&shell, "%-16s %-8s %-12s %-8d %-10lu\n",
		           name ? name : "(null)",
		           type,
		           shell_task_state_str(state),
		           (int)prio,
		           stack_free);
	}

	shellPrintEndLine(&shell, "======================================\n");
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
tasks, shellTaskList, list all RTOS tasks);

int shellTaskKill(int argc, char *argv[])
{
	osThreadId_t target = NULL;
	const char *target_name;
	adt_chain_t *node;
	SYS_Task_Control_Table_t *tct;
	int found = 0;

	if (argc < 2 || argc > 3)
	{
		shellPrintEndLine(&shell, "Usage: taskkill <name> [force]\n");
		shellPrintEndLine(&shell, "       taskkill <name> force  -- confirm kill\n");
		return -1;
	}

	target_name = argv[1];

	/* Find the thread in SRT.task_table */
	node = SRT.task_table->next;
	while (node)
	{
		tct = (SYS_Task_Control_Table_t *)node->data;
		if (tct && tct->tid)
		{
			const char *name = osThreadGetName(tct->tid);
			if (name && strcmp(name, target_name) == 0)
			{
				target = tct->tid;
				found = 1;
				break;
			}
		}
		node = node->next;
	}

	if (!found)
	{
		/* Check if it's a SYSTEM thread (not in our table) */
		shellPrintEndLine(&shell, "Error: thread '%s' not found or is a SYSTEM thread.\n", target_name);
		shellPrintEndLine(&shell, "       SYSTEM threads cannot be killed.\n");
		return -1;
	}

	/* Warn about consequences */
	shellPrintEndLine(&shell, "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	shellPrintEndLine(&shell, "WARNING: Killing thread '%s'\n", target_name);
	shellPrintEndLine(&shell, "Consequences may include:\n");
	shellPrintEndLine(&shell, "  - Memory leaks (stack, TCB, resources)\n");
	shellPrintEndLine(&shell, "  - Unreleased mutexes / semaphores\n");
	shellPrintEndLine(&shell, "  - Unpredictable system crashes\n");
	shellPrintEndLine(&shell, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

	if (argc != 3 || strcmp(argv[2], "force") != 0)
	{
		shellPrintEndLine(&shell, "\nTo proceed, re-run: taskkill %s force\n", target_name);
		return -1;
	}

	/* Remove from SRT.task_table chain (frees chain node + TCT data) */
	{
		int _code;
		delete_node(SRT.task_table, tct, &_code);
	}
	sys_free(0, tct);   /* free the tracking struct data */

	/* Terminate the thread */
	osThreadTerminate(target);
	
	shellPrintEndLine(&shell, "Thread '%s' terminated.\n", target_name);
	return 0;
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
taskkill, shellTaskKill, kill a USER thread (taskkill <name> force));

void shellResetReason(void)
{
	uint32_t csr = RCC->CSR;
	const char *reason = "Unknown";

	shellPrintEndLine(&shell, "\n===== Last Reset Reason =====\n");

	if (csr & RCC_CSR_PORRSTF)       reason = "POR/PDR (Power-on Reset)";
	else if (csr & RCC_CSR_PINRSTF)  reason = "NRST Pin Reset";
	else if (csr & RCC_CSR_BORRSTF)  reason = "BOR (Brown-out Reset)";
	else if (csr & RCC_CSR_SFTRSTF)  reason = "Software Reset";
	else if (csr & RCC_CSR_IWDGRSTF) reason = "IWDG (Independent Watchdog)";
	else if (csr & RCC_CSR_WWDGRSTF) reason = "WWDG (Window Watchdog)";
	else if (csr & RCC_CSR_LPWRRSTF) reason = "Low-power Reset";

	shellPrintEndLine(&shell, "  Reason: %s\n", reason);
	shellPrintEndLine(&shell, "  CSR:    0x%08lX\n", csr);

	/* Clear all reset flags for next detection */
	__HAL_RCC_CLEAR_RESET_FLAGS();
	shellPrintEndLine(&shell, "  (flags cleared)\n");
	shellPrintEndLine(&shell, "==============================\n");
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_FUNC),
reset, shellResetReason, show last reset reason);

/* ---- Shell Filesystem Support (FATFS) ---- */

#include "ff.h"
#include "shell_fs.h"

static ShellFs shell_fs_obj;
static char shell_fs_path_buf[256];

static size_t shell_fs_getcwd(char *buf, size_t len)
{
	FRESULT res = f_getcwd((TCHAR *)buf, (UINT)len);
	if (res == FR_OK)
	{
		return strlen(buf);
	}
	buf[0] = '/';
	buf[1] = '\0';
	return 1;
}

static size_t shell_fs_chdir(char *path)
{
	FRESULT res = f_chdir((const TCHAR *)path);
	return (res == FR_OK) ? 0 : (size_t)-1;
}

static size_t shell_fs_listdir(char *dir, char *buffer, size_t maxLen)
{
	DIR dp;
	FILINFO fno;
	FRESULT res;
	size_t pos = 0;

	(void)dir;

	res = f_opendir(&dp, (const TCHAR *)"");
	if (res != FR_OK)
	{
		return snprintf(buffer, maxLen, "open dir failed (%d)\r\n", (int)res);
	}

	while (1)
	{
		res = f_readdir(&dp, &fno);
		if (res != FR_OK || fno.fname[0] == '\0') break;

		if (pos + strlen(fno.fname) + 16 > maxLen) break;

		if (fno.fattrib & AM_DIR)
		{
			pos += snprintf(buffer + pos, maxLen - pos,
			                "<DIR>  %-32s\r\n", fno.fname);
		}
		else
		{
			pos += snprintf(buffer + pos, maxLen - pos,
			                "       %-32s %10lu\r\n", fno.fname, fno.fsize);
		}
	}

	f_closedir(&dp);
	return pos;
}

void shell_fs_init(void)
{
	shell_fs_obj.getcwd  = shell_fs_getcwd;
	shell_fs_obj.chdir   = shell_fs_chdir;
	shell_fs_obj.listdir = shell_fs_listdir;

	shellFsInit(&shell_fs_obj, shell_fs_path_buf, sizeof(shell_fs_path_buf));
	shellCompanionAdd(&shell, SHELL_COMPANION_ID_FS, &shell_fs_obj);
}

int shellHexdump(int argc, char *argv[])
{
	uint32_t addr, len, offset;
	uint8_t *ptr;
	char buf[80];
	int pos, i;
	uint8_t c;

	if (argc != 3)
	{
		shellPrintEndLine(&shell, "Usage: hexdump <addr> <len>\n");
		return -1;
	}

	addr = (uint32_t)strtoul(argv[1], NULL, 0);
	len  = (uint32_t)strtoul(argv[2], NULL, 0);

	if (len == 0 || len > 65536)
	{
		shellPrintEndLine(&shell, "Invalid length (1-65536)\n");
		return -1;
	}

	ptr = (uint8_t *)addr;

	for (offset = 0; offset < len; offset += 16)
	{
		pos = snprintf(buf, sizeof(buf), "%08lX: ", addr + offset);

		for (i = 0; i < 16; i++)
		{
			if (offset + i < len)
				pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", ptr[offset + i]);
			else
				pos += snprintf(buf + pos, sizeof(buf) - pos, "   ");
		}

		pos += snprintf(buf + pos, sizeof(buf) - pos, " ");
		for (i = 0; i < 16 && offset + i < len; i++)
		{
			c = ptr[offset + i];
			buf[pos++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
		}
		buf[pos++] = '\n';
		buf[pos] = '\0';

		shellWriteEndLine(&shell, buf, pos);
	}

	return 0;
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
hexdump, shellHexdump, hexdump <addr> <len>);

extern uint32_t cpu_idle_ticks;

int shellSysStats(int argc, char *argv[])
{
	uint32_t idle_start, idle_end, cpu_used_x10;
	uint16_t ccm_used, sram_used;
	uint32_t heap_free, heap_total;
	TickType_t delay_ticks;
	(void)argc;
	(void)argv;

	/* ---- CPU usage via 200ms idle sampling ---- */
	delay_ticks = pdMS_TO_TICKS(200);
	idle_start = cpu_idle_ticks;
	vTaskDelay(delay_ticks);
	idle_end = cpu_idle_ticks;

	/* vTaskDelay includes one extra tick, so ticks passed ≈ delay_ticks */
	if (idle_end >= idle_start)
		cpu_used_x10 = 1000 - ((idle_end - idle_start) * 1000 / delay_ticks);
	else
		cpu_used_x10 = 0;

	if (cpu_used_x10 > 1000) cpu_used_x10 = 1000;

	/* ---- SRAM usage ---- */
	ccm_used  = sys_mem_perused(SRAMCCM);   /* 0-1000, 0.0%-100.0% */
	sram_used = sys_mem_perused(SRAMEX);    /* external 1MB SRAM */

	/* ---- FreeRTOS heap ---- */
	heap_free = xPortGetFreeHeapSize();
	heap_total = configTOTAL_HEAP_SIZE;

	shellPrintEndLine(&shell, "\n===== System Stats =====\n");
	shellPrintEndLine(&shell, "CPU Usage:       %lu.%lu%%\n",
	                  cpu_used_x10 / 10, cpu_used_x10 % 10);
	shellPrintEndLine(&shell, "\n--- SRAM ---\n");
	shellPrintEndLine(&shell, "CCM (60KB):      %u.%u%% used (%lu/%lu KB free)\n",
	                  ccm_used / 10, ccm_used % 10,
	                  (uint32_t)(60 - (60 * ccm_used / 1000)),
	                  (uint32_t)60);
	shellPrintEndLine(&shell, "Ext (963KB):     %u.%u%% used (%lu/%lu KB free)\n",
	                  sram_used / 10, sram_used % 10,
	                  (uint32_t)(963 - (963 * sram_used / 1000)),
	                  (uint32_t)963);
	shellPrintEndLine(&shell, "\n--- FreeRTOS Heap ---\n");
	shellPrintEndLine(&shell, "Heap:            %lu/%lu bytes free (%lu%% used)\n",
	                  heap_free, heap_total,
	                  (heap_total - heap_free) * 100 / heap_total);
	shellPrintEndLine(&shell, "========================\n");

	return 0;
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
sysstats, shellSysStats, show CPU and SRAM usage);

#endif
/***********************************************END*******************************************/

