#include "string.h"
#include "dl_User_api.h"
//使用DL_EXPORT导出函数
#define DL_EXPORT		__attribute__((visibility("default")))
#define DL_HIDDEN		__attribute__((visibility("hidden")))

int dl_main(void)
{
	AxfDebug("HelloWorld");
	AxfDebug("ThisIsDynamicApplication");
	return Add(12,5);
}
