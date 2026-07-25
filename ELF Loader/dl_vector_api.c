#include "dl_user_api.h"
#include "Extern_API.h"
#include "System_Status.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DL_VECTOR_STRING(func, index) [index] = (void *)func,
#define DL_VECTOR_USER(func, index) [DL_USER_LIB_BASE + (index)] = (void *)func,

int UserAdd(int a, int b);

static __attribute__((used)) void * const DL_Vector_Table[
	DL_USER_LIB_BASE + DL_USER_LIB_NUM] =
{
	DL_VECTOR_STRING(memchr, 0)
	DL_VECTOR_STRING(memcmp, 1)
	DL_VECTOR_STRING(memcpy, 2)
	DL_VECTOR_STRING(memmove, 3)
	DL_VECTOR_STRING(memset, 4)
	DL_VECTOR_STRING(strcat, 5)
	DL_VECTOR_STRING(strncat, 6)
	DL_VECTOR_STRING(strchr, 7)
	DL_VECTOR_STRING(strcmp, 8)
	DL_VECTOR_STRING(strncmp, 9)
	DL_VECTOR_STRING(strcoll, 10)
	DL_VECTOR_STRING(strcpy, 11)
	DL_VECTOR_STRING(strncpy, 12)
	DL_VECTOR_STRING(strcspn, 13)
	DL_VECTOR_STRING(strerror, 14)
	DL_VECTOR_STRING(strlen, 15)
	DL_VECTOR_STRING(strpbrk, 16)
	DL_VECTOR_STRING(strrchr, 17)
	DL_VECTOR_STRING(strspn, 18)
	DL_VECTOR_STRING(strstr, 19)
	DL_VECTOR_STRING(strtok, 20)
	DL_VECTOR_STRING(strxfrm, 21)
	DL_VECTOR_USER(UserAdd, DL_USER_ADD)
	DL_VECTOR_USER(AxfDebugInfoPushToUart, DL_USER_DEBUG)
	DL_VECTOR_USER(ExternAPI_LCD_Acquire, DL_USER_LCD_ACQUIRE)
	DL_VECTOR_USER(ExternAPI_LCD_Release, DL_USER_LCD_RELEASE)
	DL_VECTOR_USER(ExternAPI_LCD_Clear, DL_USER_LCD_CLEAR)
	DL_VECTOR_USER(ExternAPI_LCD_DrawPixel, DL_USER_LCD_DRAW_PIXEL)
	DL_VECTOR_USER(ExternAPI_LCD_FillRect, DL_USER_LCD_FILL_RECT)
	DL_VECTOR_USER(ExternAPI_LCD_BlitRGB565, DL_USER_LCD_BLIT_RGB565)
	DL_VECTOR_USER(ExternAPI_LCD_GetWidth, DL_USER_LCD_GET_WIDTH)
	DL_VECTOR_USER(ExternAPI_LCD_GetHeight, DL_USER_LCD_GET_HEIGHT)
	DL_VECTOR_USER(ExternAPI_KeyGetState, DL_USER_KEY_GET_STATE)
	DL_VECTOR_USER(ExternAPI_KeyGetRawState, DL_USER_KEY_GET_RAW_STATE)
	DL_VECTOR_USER(ExternAPI_AXF_ShouldExit, DL_USER_SHOULD_EXIT)
};

int UserAdd(int a, int b)
{
	return a + b;
}

void *svc_dl_vector_get_lib_func_entry(size_t index)
{
	void *function;

	if (index >= (sizeof(DL_Vector_Table) / sizeof(DL_Vector_Table[0])))
	{
		return NULL;
	}

	function = DL_Vector_Table[index];
	if (function == NULL)
	{
		return NULL;
	}

	return (void *)((uintptr_t)function | 1UL);
}
