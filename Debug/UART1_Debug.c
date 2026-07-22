#include "stm32f4xx.h"                  // Device header
#include "sys.h"
#include "System_Status.h"
#include <stdarg.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "Player.h"
#include "shell.h"
#include "shell_support.h"
#include "ccm_memory.h"
#include "Malloc.h"

extern Shell shell;

//printf锟截讹拷锟斤拷UART1

/******************************************************************************************/
/* 锟斤拷锟斤拷锟斤拷锟铰达拷锟斤拷, 支锟斤拷printf锟斤拷锟斤拷, 锟斤拷锟斤拷锟斤拷要选锟斤拷use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)                    /* 使锟斤拷AC6锟斤拷锟斤拷锟斤拷时 */
__asm(".global __use_no_semihosting\n\t");          /* 锟斤拷锟斤拷锟斤拷使锟矫帮拷锟斤拷锟斤拷模式 */
__asm(".global __ARM_use_no_argv \n\t");            /* AC6锟斤拷锟斤拷要锟斤拷锟斤拷main锟斤拷锟斤拷为锟睫诧拷锟斤拷锟斤拷式锟斤拷锟斤拷锟津部凤拷锟斤拷锟教匡拷锟杰筹拷锟街帮拷锟斤拷锟斤拷模式 */

#else
/* 使锟斤拷AC5锟斤拷锟斤拷锟斤拷时, 要锟斤拷锟斤拷锟斤定锟斤拷__FILE 锟斤拷 锟斤拷使锟矫帮拷锟斤拷锟斤拷模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using shellPrintEndLine() for debugging, no file handling */
    /* is required. */
};

#endif

/* 锟斤拷使锟矫帮拷锟斤拷锟斤拷模式锟斤拷锟斤拷锟斤拷锟斤拷要锟截讹拷锟斤拷_ttywrch\_sys_exit\_sys_command_string锟斤拷锟斤拷,锟斤拷同时锟斤拷锟斤拷AC6锟斤拷AC5模式 */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* 锟斤拷锟斤拷_sys_exit()锟皆憋拷锟斤拷使锟矫帮拷锟斤拷锟斤拷模式 */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

/* FILE 锟斤拷 stdio.h锟斤拷锟芥定锟斤拷. */

struct __FILE
{
  int handle;
};

FILE __stdout;

/* 锟截讹拷锟斤拷fputc锟斤拷锟斤拷, printf锟斤拷锟斤拷锟斤拷锟秸伙拷通锟斤拷锟斤拷锟斤拷fputc锟斤拷锟斤拷址锟斤拷锟斤拷锟斤拷锟斤拷锟� */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);               /* 锟饺达拷锟斤拷一锟斤拷锟街凤拷锟斤拷锟斤拷锟斤拷锟� */

    USART1->DR = (uint8_t)ch;                       /* 锟斤拷要锟斤拷锟酵碉拷锟街凤拷 ch 写锟诫到DR锟侥达拷锟斤拷 */
    return ch;
}

//int SysDebugInfoPushToUart(const char* string,...)
//{
//    osKernelLock();

//	if(SCI.Debug)
//	{
//		char String[100];				//锟斤拷锟斤拷锟街凤拷锟斤拷锟斤拷
//		va_list arg;					//锟斤拷锟斤拷杀锟斤拷锟斤拷锟叫憋拷锟斤拷锟斤拷锟斤拷锟酵的憋拷锟斤拷arg
//		va_start(arg, string);			//锟斤拷format锟斤拷始锟斤拷锟斤拷锟秸诧拷锟斤拷锟叫憋拷锟斤拷arg锟斤拷锟斤拷
//		vsprintf(String, string, arg);	//使锟斤拷vsprintf锟斤拷印锟斤拷式锟斤拷锟街凤拷锟斤拷锟酵诧拷锟斤拷锟叫憋拷锟斤拷锟街凤拷锟斤拷锟斤拷锟斤拷
//		va_end(arg);					//锟斤拷锟斤拷锟斤拷锟斤拷arg
//		shellPrintEndLine("%s",String);
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

static const char *shell_task_state_str(eTaskState state)
{
	switch (state)
	{
	case eRunning:   return "Running";
	case eReady:     return "Ready";
	case eBlocked:   return "Blocked";
	case eSuspended: return "Suspended";
	case eDeleted:   return "Terminated";
	default:                 return "Unknown";
	}
}

void shellTaskList(void)
{
#define MAX_TASKS 16
#define TASK_LIST_LINE "==================================================================\n"
	TaskStatus_t tasks[MAX_TASKS];
	UBaseType_t count;
	uint32_t i;

	count = uxTaskGetSystemState(tasks, MAX_TASKS, NULL);

	shellPrintEndLine(&shell, "\n===== RTOS Task List (%lu tasks) =====\n", count);
	shellPrintEndLine(&shell, "%-*s %-8s %-12s %-8s %-10s\n",
	           configMAX_TASK_NAME_LEN, "Name", "Type", "State", "Prio", "StackFree");

	for (i = 0; i < count && i < MAX_TASKS; i++)
	{
		const char *name = tasks[i].pcTaskName;
		eTaskState state = tasks[i].eCurrentState;
		UBaseType_t prio = tasks[i].uxCurrentPriority;
		uint32_t stack_free = (uint32_t)tasks[i].usStackHighWaterMark * sizeof(StackType_t);
		const char *type = "SYSTEM";

		if (state == eDeleted)
		{
			continue;
		}

		/* Check if this thread was created via SysCreatThread 鈫� USER */
		{
			adt_chain_t *node = SRT.task_table->next;
			while (node)
			{
				SYS_Task_Control_Table_t *tct = (SYS_Task_Control_Table_t *)node->data;
				if (tct && tct->tid == (osThreadId_t)tasks[i].xHandle)
				{
					type = "USER";
					break;
				}
				node = node->next;
			}
		}

		shellPrintEndLine(&shell, "%-*s %-8s %-12s %-8d %-10lu\n",
		           configMAX_TASK_NAME_LEN, name ? name : "(null)",
		           type,
		           shell_task_state_str(state),
		           (int)prio,
		           stack_free);
	}

	shellPrintEndLine(&shell, TASK_LIST_LINE);
#undef TASK_LIST_LINE
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

	if (strcmp(target_name, "WaveDataTransmitTask") == 0) //对音频传输任务做了特殊处理，因为它持有声卡
	{
		WavePlayTaskStop(target);
		shellPrintEndLine(&shell, "Thread '%s' terminated.\n", target_name);
		return 0;
	}

	if (SysTerminateThread(target) != SYSTEM_STATUS_OK)
	{
		shellPrintEndLine(&shell, "Error: failed to terminate thread '%s'.\n", target_name);
		return -1;
	}

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
		pos = snprintf(buf, sizeof(buf), "%08X: ", addr + offset);

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

extern volatile uint32_t cpu_idle_ticks;

int shellSysStats(int argc, char *argv[])
{
	uint32_t idle_start, idle_end, idle_delta, elapsed_ticks, cpu_used_x10;
	uint16_t ccm_used, sram_used;
	uint32_t heap_free, heap_total;
	TickType_t delay_ticks;
	(void)argc;
	(void)argv;

	/* ---- CPU usage via 200ms idle sampling ---- */
	delay_ticks = pdMS_TO_TICKS(200);
	if (delay_ticks == 0) delay_ticks = 1;
	idle_start = cpu_idle_ticks;
	elapsed_ticks = xTaskGetTickCount();
	vTaskDelay(delay_ticks);
	idle_end = cpu_idle_ticks;
	elapsed_ticks = xTaskGetTickCount() - elapsed_ticks;

	if (elapsed_ticks == 0) elapsed_ticks = delay_ticks;
	idle_delta = idle_end - idle_start;
	if (idle_delta > elapsed_ticks) idle_delta = elapsed_ticks;
	cpu_used_x10 = 1000 - (idle_delta * 1000 / elapsed_ticks);

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
	shellPrintEndLine(&shell, "CCM heap (%luKB): %u.%u%% used (%lu/%lu KB free)\n",
	                  (uint32_t)CCM_HEAP_SIZE_KB,
	                  ccm_used / 10, ccm_used % 10,
	                  (uint32_t)(CCM_HEAP_SIZE_KB - (CCM_HEAP_SIZE_KB * ccm_used / 1000)),
	                  (uint32_t)CCM_HEAP_SIZE_KB);
	shellPrintEndLine(&shell, "Ext (%luKB):     %u.%u%% used (%lu/%lu KB free)\n",
	                  (uint32_t)(MEM3_MAX_SIZE / 1024U),
	                  sram_used / 10, sram_used % 10,
	                  (uint32_t)((MEM3_MAX_SIZE / 1024U) - ((MEM3_MAX_SIZE / 1024U) * sram_used / 1000U)),
	                  (uint32_t)(MEM3_MAX_SIZE / 1024U));
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

/* ---- memfrag: memory fragmentation scanner ---- */
static void shell_print_frag_info(const char *name, u8 memx)
{
    mem_frag_info_t info;
    u32 used_pct_x10;
    u32 frag_pct_x10;
    u32 largest_bytes;
    u32 total_bytes;
    u32 i;
    u32 chars_per_line = 64U;
    u32 blocks_per_char;
    u32 pos;
    u32 used_in_group;
    u32 group;

    sys_mem_frag_info(memx, &info);

    if (info.total_blocks == 0U)
    {
        shellPrintEndLine(&shell, "\n===== %s Pool =====\n", name);
        shellPrintEndLine(&shell, "  (not initialized)\n");
        return;
    }

    total_bytes = info.total_blocks * info.block_size;
    largest_bytes = info.largest_free_blocks * info.block_size;

    used_pct_x10 = (info.total_blocks > 0U)
        ? (info.used_blocks * 1000U / info.total_blocks) : 0U;

    /* Fragmentation ratio: 0 = no frag, 1000 = fully fragmented */
    frag_pct_x10 = (info.free_blocks > 0U)
        ? (1000U - (info.largest_free_blocks * 1000U / info.free_blocks)) : 0U;

    shellPrintEndLine(&shell, "\n===== %s Pool =====\n", name);
    shellPrintEndLine(&shell,
        "Blocks:       %lu total, %lu used, %lu free (%lu.%lu%% used)\n",
        info.total_blocks, info.used_blocks, info.free_blocks,
        used_pct_x10 / 10U, used_pct_x10 % 10U);
    shellPrintEndLine(&shell,
        "Block size:   %lu B  |  Pool size: %lu KB\n",
        info.block_size, total_bytes / 1024U);
    shellPrintEndLine(&shell,
        "Free holes:   %lu (%lu fragments, i.e. sandwiched free regions)\n",
        info.hole_count, info.frag_count);
    shellPrintEndLine(&shell,
        "Largest free: %lu blocks (%lu.%02lu KB)\n",
        info.largest_free_blocks,
        largest_bytes / 1024U,
        (largest_bytes % 1024U) * 100U / 1024U);
    shellPrintEndLine(&shell,
        "Frag ratio:   %lu.%lu%% (0%%=perfect, 100%%=fully scattered)\n",
        frag_pct_x10 / 10U, frag_pct_x10 % 10U);

    /* Visual fragmentation map (compressed to fit terminal width) */
    blocks_per_char = (info.total_blocks + chars_per_line - 1U) / chars_per_line;
    if (blocks_per_char < 1U) blocks_per_char = 1U;

    shellPrintEndLine(&shell, "Map [.%lu blocks/char, #=used .=free -=mixed]:\n  ",
                      blocks_per_char);

    pos = 0U;
    for (i = 0U; i < info.total_blocks; i += blocks_per_char)
    {
        used_in_group = 0U;
        for (group = 0U; group < blocks_per_char && (i + group) < info.total_blocks; group++)
        {
            if (mallco_dev.memmap[memx][i + group] != 0U)
            {
                used_in_group++;
            }
        }

        if (used_in_group == 0U)
            shellWriteString(&shell, ".");
        else if (used_in_group == group)
            shellWriteString(&shell, "#");
        else
            shellWriteString(&shell, "-");

        pos++;
        if (pos >= chars_per_line)
        {
            shellWriteString(&shell, "\n  ");
            pos = 0U;
        }
    }
    shellWriteString(&shell, "\n");
}

int shellMemFrag(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* CCM pool (SRAMCCM) */
    shell_print_frag_info("CCM (SRAMCCM)", SRAMCCM);

    /* External SRAM pool (SRAMEX) */
    shell_print_frag_info("External SRAM (SRAMEX)", SRAMEX);

    shellPrintEndLine(&shell,
        "\nLegend: # = fully-used group, . = fully-free, - = mixed (fragmented)\n");
    shellPrintEndLine(&shell,
        "Frag ratio = 1 - (largest_free / total_free). Higher = more fragmented.\n");
    shellPrintEndLine(&shell,
        "Fragments = free holes trapped between used blocks (pattern: 101).\n");

    return 0;
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0)|SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
memfrag, shellMemFrag, scan memory fragmentation (101 pattern));

#endif
/***********************************************END*******************************************/

