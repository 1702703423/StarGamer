#ifndef GUI_FILEMANAGE_H
#define GUI_FILEMANAGE_H

#include "GUI_Manage.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GUI_FILEMANAGE_PATH_SIZE
#define GUI_FILEMANAGE_PATH_SIZE        256U
#endif

#ifndef GUI_FILEMANAGE_NAME_SIZE
#define GUI_FILEMANAGE_NAME_SIZE        96U
#endif

#ifndef GUI_FILEMANAGE_MAX_ITEMS
#define GUI_FILEMANAGE_MAX_ITEMS        32U
#endif

#ifndef GUI_FILEMANAGE_SCAN_LIMIT
#define GUI_FILEMANAGE_SCAN_LIMIT       (GUI_FILEMANAGE_MAX_ITEMS * 4U)
#endif

typedef struct GUI_FileManage GUI_FileManage_t;

typedef enum
{
	GUI_FILEMANAGE_ITEM_PARENT = 0,
	GUI_FILEMANAGE_ITEM_DIR,
	GUI_FILEMANAGE_ITEM_FILE,
	GUI_FILEMANAGE_ITEM_NEXT
} GUI_FileManageItemType_t;

typedef enum
{
	GUI_FILEMANAGE_PENDING_NONE = 0,
	GUI_FILEMANAGE_PENDING_REFRESH,
	GUI_FILEMANAGE_PENDING_PARENT,
	GUI_FILEMANAGE_PENDING_PATH,
	GUI_FILEMANAGE_PENDING_NEXT
} GUI_FileManagePending_t;

typedef struct
{
	GUI_FileManage_t *manager;
	GUI_FileManageItemType_t type;
	char name[GUI_FILEMANAGE_NAME_SIZE];
	char path[GUI_FILEMANAGE_PATH_SIZE];
	u32 size;
	u8 attr;
} GUI_FileManageItem_t;

typedef u8 (*GUI_FileManageFilter)(GUI_FileManage_t *manager, const FILINFO *info, const char *path, void *user_data);
typedef int (*GUI_FileManageOpen)(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
typedef int (*GUI_FileManageBack)(GUI_FileManage_t *manager, void *user_data);

typedef struct
{
	const char *title;
	const char *root_path;
	const char *empty_text;
	const char *status_text;
	const char * const *extensions;
	u8 extension_count;
	u32 accent_color;
	GUI_FileManageFilter filter;
	GUI_FileManageOpen open;
	GUI_FileManageBack back;
	void *user_data;
} GUI_FileManageConfig_t;

struct GUI_FileManage
{
	/* Embed as the first member of a page-specific context for C-style inheritance. */
	Page_Struct_t *page;
	GUI_FileManageConfig_t config;
	lv_obj_t *screen;
	lv_obj_t *path_label;
	lv_obj_t *list;
	lv_obj_t *status_label;
	lv_obj_t *back_btn;
	lv_obj_t *refresh_btn;
	GUI_FileManageItem_t items[GUI_FILEMANAGE_MAX_ITEMS];
	DIR dir;
	char root_path[GUI_FILEMANAGE_PATH_SIZE];
	char current_path[GUI_FILEMANAGE_PATH_SIZE];
	char pending_path[GUI_FILEMANAGE_PATH_SIZE];
	char dir_path[GUI_FILEMANAGE_PATH_SIZE];
	u8 pending_action;
	u8 pending_scheduled;
	u8 item_count;
	u8 dir_open;
	u8 dir_eof;
	u8 has_more;
	u8 locked;
};

int GUI_FileManage_Create(GUI_FileManage_t *manager, Page_Struct_t *page, const GUI_FileManageConfig_t *config);
int GUI_FileManage_OpenPath(GUI_FileManage_t *manager, const char *path);
int GUI_FileManage_OpenParent(GUI_FileManage_t *manager);
int GUI_FileManage_Refresh(GUI_FileManage_t *manager);
int GUI_FileManage_PageLoaded(GUI_FileManage_t *manager);
void GUI_FileManage_SetStatus(GUI_FileManage_t *manager, const char *text);
void GUI_FileManage_SetLocked(GUI_FileManage_t *manager, u8 locked);
const char *GUI_FileManage_GetCurrentPath(const GUI_FileManage_t *manager);
void *GUI_FileManage_GetUserData(const GUI_FileManage_t *manager);

#ifdef __cplusplus
}
#endif

#endif
