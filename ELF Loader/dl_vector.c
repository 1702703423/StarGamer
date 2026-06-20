#include "dl_lib.h"
#include <stdarg.h>
#include "dl_user_api.h"
#include "System_Status.h"
#include "shell.h"
#include "shell_support.h"

extern Shell shell;

#include "/common/dl_extern_lib.h"
#include "/common/dl_string_lib.h"

int UserAdd(int a, int b);

#define STRING_LIB_DECLARE(func,index) [index+DL_STRING_LIB_BASE]=(void*)func,
	
#define USER_LIB_DECLARE(func,index) [index + DL_STRING_LIB_NUM	]=(void*)func,
	
#define TESTFUNC	0
#define APPDEBUG	1

struct __FILE {
    int dummy;
};

static void TestFunc(int a,int b,int* res)
{
	*res = a + b;
}

//用于共享给子程序的内存
static __attribute__((used)) 
	   void* DL_Vector_Table[] =
{
	STRING_LIB_DECLARE(memchr	,	MEMCHR)
	STRING_LIB_DECLARE(memcmp	,	MEMCMP)
	STRING_LIB_DECLARE(memcpy	,	MEMCPY)
	STRING_LIB_DECLARE(memmove	,	MEMMOVE)
	STRING_LIB_DECLARE(memset	,	MEMSET)
	STRING_LIB_DECLARE(strcat	,	STRCAT)
	STRING_LIB_DECLARE(strncat	,	STRNCAT)
	STRING_LIB_DECLARE(strchr	,	STRCHR)
	STRING_LIB_DECLARE(strcmp	,	STRCMP)
	STRING_LIB_DECLARE(strncmp	,	STRNCMP)
	STRING_LIB_DECLARE(strcoll	,	STRCOLL)
	STRING_LIB_DECLARE(strcpy	,	STRCPY)
	STRING_LIB_DECLARE(strncpy	,	STRNCPY)
	STRING_LIB_DECLARE(strcspn	,	STRCSPN)
	STRING_LIB_DECLARE(strerror	,	STRERROR)
	STRING_LIB_DECLARE(strlen	,	STRLEN)
	STRING_LIB_DECLARE(strpbrk	,	STRPBRK)
	STRING_LIB_DECLARE(strrchr	,	STRRCHR)
	STRING_LIB_DECLARE(strspn	,	STRSPN)
	STRING_LIB_DECLARE(strstr	,	STRSTR)
	STRING_LIB_DECLARE(strtok	,	STRTOK)
	STRING_LIB_DECLARE(strxfrm	,	STRXFRM)
	USER_LIB_DECLARE(UserAdd ,  TESTFUNC)
	USER_LIB_DECLARE(AxfDebugInfoPushToUart, APPDEBUG)
};

int UserAdd(int a, int b)
{
	shellPrintEndLine(&shell, "UserAdd:%d,%d", a, b);
  return a + b;
}

void* svc_dl_vector_get_lib_func_entry(size_t index)
{
    if(index < sizeof(DL_Vector_Table)/sizeof(DL_Vector_Table[0]))
    {
        void* func = DL_Vector_Table[index];
        shellPrintEndLine(&shell, "SVC_CALLBACK:%p\n", func);
        return (void*)((uintptr_t)func | 1); // 确保Thumb bit
    }
    return NULL;
}
