#include "stm32f4xx.h"                  // Device header
#include "sys.h"
#include "System_Status.h"
#include <stdarg.h>
#include "FreeRTOS.h"

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

/* �ض���fputc����, printf�������ջ�ͨ������fputc����ַ���������? */
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0);               /* �ȴ���һ���ַ��������? */

    USART1->DR = (uint8_t)ch;                       /* ��Ҫ���͵��ַ� ch д�뵽DR�Ĵ��� */
    return ch;
}

int SysDebugInfoPushToUart(const char* string,...)
{
    osKernelLock();

	if(SCI.Debug)
	{
		char String[100];				//�����ַ�����
		va_list arg;					//����ɱ�����б��������͵ı���arg
		va_start(arg, string);			//��format��ʼ�����ղ����б���arg����
		vsprintf(String, string, arg);	//ʹ��vsprintf��ӡ��ʽ���ַ����Ͳ����б����ַ�������
		va_end(arg);					//��������arg
		printf("%s",String);
		return SYSTEM_STATUS_OK;
	}
	return DEBUG_OPTION_DISABLE;

    osKernelUnlock();
}

int AxfDebugInfoPushToUart(const char* string, va_list arg)
{
    if(SCI.Debug)
    {
        printf("[Axf]:");
        vprintf(string, arg);  // vprintf ԭ��֧�� va_list
        printf("\n");
        return SYSTEM_STATUS_OK;
    }
    return DEBUG_OPTION_DISABLE;
}
#endif
/***********************************************END*******************************************/

