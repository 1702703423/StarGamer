#include "dl_extern_lib.h"
#include "dl_User_api.h"
#include "stdarg.h"

__attribute__((naked)) void* dl_vector_get_lib_func_entry(unsigned int index) {
    __asm volatile(
        "svc 0x08\n"           // µ÷ÓÃ SVC #8
        "bx lr\n"              // ·µ»Ø
    );
}

int AxfDebug(const char* fmt, ...);

int AxfDebug(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    
    DL_DECLARE_EXTERN_LIB_FUNC(axfDebug,DL_USER_API_START_BASE,USER_FUNC_NUM,APPDEBUG,int,const char* ,va_list);
    int ret = DL_FUNC_NAME(axfDebug)(fmt, ap); 
    
    va_end(ap);
    return ret;
}
int Add(int a,int b)
{
	DL_DECLARE_EXTERN_LIB_FUNC(add,DL_USER_API_START_BASE,USER_FUNC_NUM,TestFunc,int,int a,int b);
	return DL_FUNC_NAME(add)(a,b);
}
