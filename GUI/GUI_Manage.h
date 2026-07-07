#ifndef GUI_MANAGE_H
#define GUI_MANAGE_H

#include "sys.h"
#include "System_Status.h"
#include "lvgl.h"
#include "adt_chain.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	GUI_PAGE_MAIN_MENU = 0,
	GUI_PAGE_NES,
	GUI_PAGE_ARDUBOY,
	GUI_PAGE_MUSIC,
	GUI_PAGE_IMAGE,
	GUI_PAGE_TEXT,
	GUI_PAGE_SETTINGS,
	GUI_PAGE_MAX,
	GUI_PAGE_NONE = 0xFF
} Page_ID_t;

typedef struct Page_Struct Page_Struct_t;

typedef int (*PageInit)(Page_Struct_t *page);
typedef int (*PageCallBack)(Page_Struct_t *page, void *user_data);

struct Page_Struct
{
	Page_ID_t id;
	const char *name;
	lv_obj_t *screen;
	PageInit Page_init;
	PageCallBack Page_Callback;
	u8 created;
};

int Page_Manager_Init(void);
int Page_Create(Page_ID_t id, const char *name, PageInit init, PageCallBack callback);
int Page_Delete(Page_ID_t id);
int Page_DeleteAll(void);
int Page_Show(Page_ID_t id);
int Page_Show_Anim(Page_ID_t id, lv_scr_load_anim_t anim_type, u32 time, u32 delay);
int Page_Back(void);
int Page_SetCallBack(Page_ID_t id, PageCallBack callback);
Page_Struct_t *Page_Find(Page_ID_t id);
lv_obj_t *Page_GetObject(Page_ID_t id);
Page_ID_t Page_GetCurrent(void);
void Page_ClearHistory(void);

#ifdef __cplusplus
}
#endif

#endif
