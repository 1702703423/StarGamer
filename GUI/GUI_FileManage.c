#include "GUI_FileManage.h"
#include "GUI_Font.h"
#include "GUI_Theme.h"

#define GUI_FM_SCREEN_W             320
#define GUI_FM_SCREEN_H             240
#define GUI_FM_LIST_X               10
#define GUI_FM_LIST_Y               62
#define GUI_FM_LIST_W               300
#define GUI_FM_LIST_H               146
#define GUI_FM_ROW_H                GUI_M3_LIST_ROW_H
#define GUI_FM_STATUS_LEN           48U
#define GUI_FM_NAME_UTF8_SIZE       (((GUI_FILEMANAGE_NAME_SIZE - 1U) * 3U / 2U) + 2U)
#define GUI_FM_PATH_UTF8_SIZE       (((GUI_FILEMANAGE_PATH_SIZE - 1U) * 3U / 2U) + 2U)

static void GUI_FM_ApplyScreenBase(lv_obj_t *screen);
static void GUI_FM_CreateHeader(GUI_FileManage_t *manager);
static void GUI_FM_CreateList(GUI_FileManage_t *manager);
static void GUI_FM_BackEvent(lv_event_t *event);
static void GUI_FM_RefreshEvent(lv_event_t *event);
static void GUI_FM_ItemEvent(lv_event_t *event);
static u8 GUI_FM_HandleCommonKey(GUI_FileManage_t *manager, u32 key);
static void GUI_FM_WaitInputRelease(void);
static int GUI_FM_ScheduleAction(GUI_FileManage_t *manager, GUI_FileManagePending_t action, const char *path);
static void GUI_FM_AsyncAction(void *user_data);
static int GUI_FM_OpenNextPage(GUI_FileManage_t *manager);
static int GUI_FM_ReadDir(GUI_FileManage_t *manager);
static int GUI_FM_ScheduleItemPath(GUI_FileManageItem_t *item);
static int GUI_FM_AddParent(GUI_FileManage_t *manager);
static int GUI_FM_AddPageNav(GUI_FileManage_t *manager, GUI_FileManageItemType_t type, const char *name);
static int GUI_FM_AddInfo(GUI_FileManage_t *manager, const FILINFO *info);
static void GUI_FM_RebuildList(GUI_FileManage_t *manager);
static void GUI_FM_AddListItem(GUI_FileManage_t *manager, GUI_FileManageItem_t *item);
static int GUI_FM_OpenItem(GUI_FileManage_t *manager, GUI_FileManageItem_t *item);
static void GUI_FM_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj);
static void GUI_FM_RemoveChildrenFromGroup(lv_obj_t *parent);
static void GUI_FM_CloseDir(GUI_FileManage_t *manager);
static u8 GUI_FM_IsRootPath(const GUI_FileManage_t *manager);
static u8 GUI_FM_PathJoin(char *dst, u32 dst_size, const char *dir, const char *name);
static u8 GUI_FM_CopyText(char *dst, u32 dst_size, const char *src);
static void GUI_FM_ApplyTextFont(lv_obj_t *obj);
static void GUI_FM_ApplyIconFont(lv_obj_t *row);
static u8 GUI_FM_ExtensionAccepted(GUI_FileManage_t *manager, const FILINFO *info);
static int GUI_FM_StrCaseCmp(const char *a, const char *b);
static const char *GUI_FM_GetName(const FILINFO *info);
static const char *GUI_FM_GetExt(const char *name);
static void GUI_FM_SetDefaultStatus(GUI_FileManage_t *manager);

int GUI_FileManage_Create(GUI_FileManage_t *manager, Page_Struct_t *page, const GUI_FileManageConfig_t *config)
{
	const char *root;

	if (manager == NULL || page == NULL || page->screen == NULL || config == NULL)
	{
		return OPERATE_FAILED;
	}

	memset(manager, 0, sizeof(GUI_FileManage_t));
	manager->page = page;
	manager->screen = page->screen;
	manager->config = *config;

	root = (config->root_path != NULL) ? config->root_path : "0:";
	if (GUI_FM_CopyText(manager->root_path, GUI_FILEMANAGE_PATH_SIZE, root) == 0U)
	{
		return OPERATE_FAILED;
	}
	if (GUI_FM_CopyText(manager->current_path, GUI_FILEMANAGE_PATH_SIZE, manager->root_path) == 0U)
	{
		return OPERATE_FAILED;
	}

	GUI_FM_ApplyScreenBase(page->screen);
	GUI_FM_CreateHeader(manager);
	GUI_FM_CreateList(manager);
	GUI_FM_SetDefaultStatus(manager);

	return GUI_FileManage_Refresh(manager);
}

int GUI_FileManage_OpenPath(GUI_FileManage_t *manager, const char *path)
{
	if (manager == NULL || path == NULL || manager->locked != 0U)
	{
		return OPERATE_FAILED;
	}

	if (GUI_FM_CopyText(manager->current_path, GUI_FILEMANAGE_PATH_SIZE, path) == 0U)
	{
		return OPERATE_FAILED;
	}

	GUI_FM_CloseDir(manager);
	return GUI_FileManage_Refresh(manager);
}

int GUI_FileManage_OpenParent(GUI_FileManage_t *manager)
{
	char *slash;
	char *backslash;
	char *cut;

	if (manager == NULL || manager->locked != 0U)
	{
		return OPERATE_FAILED;
	}

	if (GUI_FM_IsRootPath(manager) != 0U)
	{
		GUI_FM_CloseDir(manager);
		if (manager->config.back != NULL)
		{
			return manager->config.back(manager, manager->config.user_data);
		}
		return Page_Back();
	}

	slash = strrchr(manager->current_path, '/');
	backslash = strrchr(manager->current_path, '\\');
	cut = slash;
	if (backslash != NULL && (cut == NULL || backslash > cut))
	{
		cut = backslash;
	}

	if (cut == NULL || cut <= (manager->current_path + 1))
	{
		(void)GUI_FM_CopyText(manager->current_path, GUI_FILEMANAGE_PATH_SIZE, manager->root_path);
	}
	else
	{
		*cut = '\0';
		if (strlen(manager->current_path) < strlen(manager->root_path))
		{
			(void)GUI_FM_CopyText(manager->current_path, GUI_FILEMANAGE_PATH_SIZE, manager->root_path);
		}
	}

	GUI_FM_CloseDir(manager);
	return GUI_FileManage_Refresh(manager);
}

int GUI_FileManage_Refresh(GUI_FileManage_t *manager)
{
	int code;

	if (manager == NULL || manager->locked != 0U)
	{
		return OPERATE_FAILED;
	}

	GUI_FM_CloseDir(manager);
	code = GUI_FM_ReadDir(manager);
	GUI_FM_RebuildList(manager);

	return code;
}

int GUI_FileManage_PageLoaded(GUI_FileManage_t *manager)
{
	lv_group_t *group;
	lv_obj_t *first_obj = NULL;

	if (manager == NULL || manager->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	group = lv_group_get_default();
	if (group == NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	lv_group_remove_all_objs(group);
	lv_group_set_editing(group, false);
	lv_group_set_wrap(group, true);

	GUI_FM_AddFocusableChildren(group, manager->screen, &first_obj);
	if (first_obj != NULL)
	{
		lv_group_focus_obj(first_obj);
	}

	lv_obj_invalidate(manager->screen);

	return SYSTEM_STATUS_OK;
}

void GUI_FileManage_SetStatus(GUI_FileManage_t *manager, const char *text)
{
	if (manager == NULL || manager->status_label == NULL)
	{
		return;
	}

	lv_label_set_text(manager->status_label, (text != NULL) ? text : "");
}

void GUI_FileManage_SetLocked(GUI_FileManage_t *manager, u8 locked)
{
	if (manager == NULL)
	{
		return;
	}

	manager->locked = (locked != 0U) ? 1U : 0U;
	if (manager->list != NULL)
	{
		if (manager->locked != 0U)
		{
			lv_obj_add_state(manager->list, LV_STATE_DISABLED);
		}
		else
		{
			lv_obj_clear_state(manager->list, LV_STATE_DISABLED);
		}
	}
}

const char *GUI_FileManage_GetCurrentPath(const GUI_FileManage_t *manager)
{
	return (manager != NULL) ? manager->current_path : NULL;
}

void *GUI_FileManage_GetUserData(const GUI_FileManage_t *manager)
{
	return (manager != NULL) ? manager->config.user_data : NULL;
}

u8 GUI_FileManage_BuildItemPath(const GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, char *dst, u32 dst_size)
{
	if (manager == NULL || item == NULL || dst == NULL || dst_size == 0U)
	{
		return 0U;
	}

	if (item->type != GUI_FILEMANAGE_ITEM_DIR && item->type != GUI_FILEMANAGE_ITEM_FILE)
	{
		return 0U;
	}

	return GUI_FM_PathJoin(dst, dst_size, manager->current_path, item->name);
}

static void GUI_FM_ApplyScreenBase(lv_obj_t *screen)
{
	GUI_M3_ApplyScreen(screen);
}

static void GUI_FM_CreateHeader(GUI_FileManage_t *manager)
{
	const char *title;
	lv_obj_t *bar;

	title = (manager->config.title != NULL) ? manager->config.title : "File";

	bar = GUI_M3_CreateTopAppBar(manager->screen, title);
	manager->back_btn = GUI_M3_AppBarSetNav(bar, GUI_ICON_ARROW_BACK, GUI_FM_BackEvent, manager);
	manager->refresh_btn = GUI_M3_AppBarAddAction(bar, GUI_ICON_REFRESH, GUI_FM_RefreshEvent, manager);

	manager->path_label = lv_label_create(manager->screen);
	lv_label_set_long_mode(manager->path_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(manager->path_label, GUI_FM_SCREEN_W - 28);
	lv_obj_set_pos(manager->path_label, 14, GUI_M3_APPBAR_H + 4);
	lv_obj_set_style_text_color(manager->path_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
}

static void GUI_FM_CreateList(GUI_FileManage_t *manager)
{
	manager->list = lv_list_create(manager->screen);
	lv_obj_set_size(manager->list, GUI_FM_LIST_W, GUI_FM_LIST_H);
	lv_obj_set_pos(manager->list, GUI_FM_LIST_X, GUI_FM_LIST_Y);
	GUI_M3_StyleList(manager->list);
	lv_obj_set_scrollbar_mode(manager->list, LV_SCROLLBAR_MODE_AUTO);

	manager->status_label = lv_label_create(manager->screen);
	lv_label_set_long_mode(manager->status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(manager->status_label, GUI_FM_SCREEN_W - 28);
	lv_obj_set_pos(manager->status_label, 14, 216);
	lv_obj_set_style_text_color(manager->status_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
}

static void GUI_FM_BackEvent(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);
	GUI_FileManage_t *manager = (GUI_FileManage_t *)lv_event_get_user_data(event);

	if (code == LV_EVENT_CLICKED)
	{
		(void)GUI_FM_ScheduleAction(manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
	}
	else if (code == LV_EVENT_KEY && lv_event_get_key(event) == LV_KEY_ESC)
	{
		GUI_FM_WaitInputRelease();
		(void)GUI_FM_ScheduleAction(manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_FM_HandleCommonKey(manager, lv_event_get_key(event));
	}
}

static void GUI_FM_RefreshEvent(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);
	GUI_FileManage_t *manager = (GUI_FileManage_t *)lv_event_get_user_data(event);

	if (code == LV_EVENT_CLICKED || (code == LV_EVENT_KEY && lv_event_get_key(event) == LV_KEY_ENTER))
	{
		if (code == LV_EVENT_KEY)
		{
			GUI_FM_WaitInputRelease();
		}
		(void)GUI_FM_ScheduleAction(manager, GUI_FILEMANAGE_PENDING_REFRESH, NULL);
	}
	else if (code == LV_EVENT_KEY && lv_event_get_key(event) == LV_KEY_ESC)
	{
		GUI_FM_WaitInputRelease();
		(void)GUI_FM_ScheduleAction(manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_FM_HandleCommonKey(manager, lv_event_get_key(event));
	}
}

static void GUI_FM_ItemEvent(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);
	GUI_FileManageItem_t *item = (GUI_FileManageItem_t *)lv_event_get_user_data(event);
	u32 key = 0U;

	if (item == NULL || item->manager == NULL)
	{
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
	}

	if (code == LV_EVENT_CLICKED || (code == LV_EVENT_KEY && key == LV_KEY_ENTER))
	{
		if (code == LV_EVENT_KEY)
		{
			GUI_FM_WaitInputRelease();
		}
		if (item->type == GUI_FILEMANAGE_ITEM_PARENT)
		{
			(void)GUI_FM_ScheduleAction(item->manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
		}
		else if (item->type == GUI_FILEMANAGE_ITEM_NEXT)
		{
			(void)GUI_FM_ScheduleAction(item->manager, GUI_FILEMANAGE_PENDING_NEXT, NULL);
		}
		else if (item->type == GUI_FILEMANAGE_ITEM_DIR)
		{
			(void)GUI_FM_ScheduleItemPath(item);
		}
		else
		{
			(void)GUI_FM_OpenItem(item->manager, item);
		}
	}
	else if (code == LV_EVENT_KEY && key == LV_KEY_RIGHT)
	{
		GUI_FM_WaitInputRelease();
		if (item->type == GUI_FILEMANAGE_ITEM_PARENT)
		{
			(void)GUI_FM_ScheduleAction(item->manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
		}
		else if (item->type == GUI_FILEMANAGE_ITEM_NEXT)
		{
			(void)GUI_FM_ScheduleAction(item->manager, GUI_FILEMANAGE_PENDING_NEXT, NULL);
		}
		else if (item->type == GUI_FILEMANAGE_ITEM_DIR)
		{
			(void)GUI_FM_ScheduleItemPath(item);
		}
		else
		{
			(void)GUI_FM_OpenItem(item->manager, item);
		}
	}
	else if (code == LV_EVENT_KEY && key == LV_KEY_ESC)
	{
		GUI_FM_WaitInputRelease();
		(void)GUI_FM_ScheduleAction(item->manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_FM_HandleCommonKey(item->manager, key);
	}
}

static u8 GUI_FM_HandleCommonKey(GUI_FileManage_t *manager, u32 key)
{
	lv_group_t *group;

	if (manager == NULL)
	{
		return 0U;
	}

	if (key == LV_KEY_ESC || key == LV_KEY_LEFT)
	{
		GUI_FM_WaitInputRelease();
		(void)GUI_FM_ScheduleAction(manager, GUI_FILEMANAGE_PENDING_PARENT, NULL);
		return 1U;
	}

	group = lv_group_get_default();
	if (group == NULL)
	{
		return 0U;
	}

	if (key == LV_KEY_UP || key == LV_KEY_PREV)
	{
		lv_group_focus_prev(group);
		return 1U;
	}

	if (key == LV_KEY_DOWN || key == LV_KEY_NEXT)
	{
		lv_group_focus_next(group);
		return 1U;
	}

	return 0U;
}

static void GUI_FM_WaitInputRelease(void)
{
	lv_indev_t *indev;

	indev = lv_indev_get_act();
	if (indev != NULL)
	{
		lv_indev_wait_release(indev);
	}
}

static int GUI_FM_ScheduleAction(GUI_FileManage_t *manager, GUI_FileManagePending_t action, const char *path)
{
	lv_res_t res;

	if (manager == NULL || manager->locked != 0U || action == GUI_FILEMANAGE_PENDING_NONE)
	{
		return OPERATE_FAILED;
	}

	if (action == GUI_FILEMANAGE_PENDING_PATH)
	{
		if (path != manager->pending_path &&
		    GUI_FM_CopyText(manager->pending_path, GUI_FILEMANAGE_PATH_SIZE, path) == 0U)
		{
			GUI_FileManage_SetStatus(manager, "Path too long");
			return OPERATE_FAILED;
		}
	}
	else
	{
		manager->pending_path[0] = '\0';
	}

	manager->pending_action = (u8)action;
	if (manager->pending_scheduled != 0U)
	{
		return SYSTEM_STATUS_OK;
	}

	res = lv_async_call(GUI_FM_AsyncAction, manager);
	if (res != LV_RES_OK)
	{
		manager->pending_action = (u8)GUI_FILEMANAGE_PENDING_NONE;
		manager->pending_path[0] = '\0';
		return OPERATE_FAILED;
	}

	manager->pending_scheduled = 1U;
	return SYSTEM_STATUS_OK;
}

static void GUI_FM_AsyncAction(void *user_data)
{
	GUI_FileManage_t *manager = (GUI_FileManage_t *)user_data;
	u8 action;

	if (manager == NULL)
	{
		return;
	}

	action = manager->pending_action;
	manager->pending_action = (u8)GUI_FILEMANAGE_PENDING_NONE;
	manager->pending_scheduled = 0U;

	if (action == (u8)GUI_FILEMANAGE_PENDING_REFRESH)
	{
		(void)GUI_FileManage_Refresh(manager);
	}
	else if (action == (u8)GUI_FILEMANAGE_PENDING_PARENT)
	{
		(void)GUI_FileManage_OpenParent(manager);
	}
	else if (action == (u8)GUI_FILEMANAGE_PENDING_PATH)
	{
		(void)GUI_FileManage_OpenPath(manager, manager->pending_path);
	}
	else if (action == (u8)GUI_FILEMANAGE_PENDING_NEXT)
	{
		(void)GUI_FM_OpenNextPage(manager);
	}
}

static int GUI_FM_OpenNextPage(GUI_FileManage_t *manager)
{
	int code;

	if (manager == NULL || manager->locked != 0U || manager->has_more == 0U)
	{
		return OPERATE_FAILED;
	}

	code = GUI_FM_ReadDir(manager);
	GUI_FM_RebuildList(manager);

	return code;
}

static int GUI_FM_ScheduleItemPath(GUI_FileManageItem_t *item)
{
	GUI_FileManage_t *manager;

	if (item == NULL || item->manager == NULL)
	{
		return OPERATE_FAILED;
	}

	manager = item->manager;
	if (GUI_FileManage_BuildItemPath(manager, item, manager->pending_path, GUI_FILEMANAGE_PATH_SIZE) == 0U)
	{
		GUI_FileManage_SetStatus(manager, "Path too long");
		return OPERATE_FAILED;
	}

	return GUI_FM_ScheduleAction(manager, GUI_FILEMANAGE_PENDING_PATH, manager->pending_path);
}

static int GUI_FM_ReadDir(GUI_FileManage_t *manager)
{
	FILINFO info;
	FRESULT fr;
	int code = SYSTEM_STATUS_OK;
	u32 scanned = 0U;
	u32 scan_limit = GUI_FILEMANAGE_SCAN_LIMIT;
	u8 first_page = 0U;
	u8 real_item_limit;

	if (manager == NULL)
	{
		return OPERATE_FAILED;
	}

	manager->item_count = 0U;
	manager->has_more = 0U;
	if (manager->dir_open == 0U || strcmp(manager->dir_path, manager->current_path) != 0)
	{
		GUI_FM_CloseDir(manager);

		fr = f_opendir(&manager->dir, manager->current_path);
		if (fr != FR_OK)
		{
			GUI_FileManage_SetStatus(manager, "Open directory failed");
			return OPERATE_FAILED;
		}

		if (GUI_FM_CopyText(manager->dir_path, GUI_FILEMANAGE_PATH_SIZE, manager->current_path) == 0U)
		{
			(void)f_closedir(&manager->dir);
			manager->dir_path[0] = '\0';
			return OPERATE_FAILED;
		}

		manager->dir_open = 1U;
		manager->dir_eof = 0U;
		first_page = 1U;
	}

	if (scan_limit == 0U)
	{
		scan_limit = 1U;
	}

	if (first_page != 0U)
	{
		code = GUI_FM_AddParent(manager);
	}

	real_item_limit = GUI_FILEMANAGE_MAX_ITEMS;
	if (real_item_limit > manager->item_count)
	{
		real_item_limit--;
	}

	while (code == SYSTEM_STATUS_OK && manager->dir_eof == 0U &&
	       manager->item_count < real_item_limit && scanned < scan_limit)
	{
		memset(&info, 0, sizeof(info));
		fr = f_readdir(&manager->dir, &info);
		if (fr != FR_OK)
		{
			code = OPERATE_FAILED;
			GUI_FileManage_SetStatus(manager, "Read directory failed");
			break;
		}

		if (info.fname[0] == '\0')
		{
			manager->dir_eof = 1U;
			break;
		}

		scanned++;

		code = GUI_FM_AddInfo(manager, &info);
	}

	if (code == SYSTEM_STATUS_OK && manager->dir_eof == 0U)
	{
		manager->has_more = 1U;
		code = GUI_FM_AddPageNav(manager, GUI_FILEMANAGE_ITEM_NEXT, "More...");
	}

	if (code != SYSTEM_STATUS_OK || manager->dir_eof != 0U)
	{
		GUI_FM_CloseDir(manager);
	}

	if (code == SYSTEM_STATUS_OK)
	{
		GUI_FM_SetDefaultStatus(manager);
	}

	return code;
}

static int GUI_FM_AddParent(GUI_FileManage_t *manager)
{
	GUI_FileManageItem_t *item;

	if (GUI_FM_IsRootPath(manager) != 0U)
	{
		return SYSTEM_STATUS_OK;
	}

	if (manager->item_count >= GUI_FILEMANAGE_MAX_ITEMS)
	{
		return SYSTEM_STATUS_OK;
	}

	item = &manager->items[manager->item_count];
	memset(item, 0, sizeof(GUI_FileManageItem_t));
	item->manager = manager;
	item->type = GUI_FILEMANAGE_ITEM_PARENT;
	(void)GUI_FM_CopyText(item->name, GUI_FILEMANAGE_NAME_SIZE, "..");
	manager->item_count++;

	return SYSTEM_STATUS_OK;
}

static int GUI_FM_AddPageNav(GUI_FileManage_t *manager, GUI_FileManageItemType_t type, const char *name)
{
	GUI_FileManageItem_t *item;

	if (manager == NULL || name == NULL)
	{
		return OPERATE_FAILED;
	}

	if (manager->item_count >= GUI_FILEMANAGE_MAX_ITEMS)
	{
		return SYSTEM_STATUS_OK;
	}

	item = &manager->items[manager->item_count];
	memset(item, 0, sizeof(GUI_FileManageItem_t));
	item->manager = manager;
	item->type = type;
	(void)GUI_FM_CopyText(item->name, GUI_FILEMANAGE_NAME_SIZE, name);
	manager->item_count++;

	return SYSTEM_STATUS_OK;
}

static int GUI_FM_AddInfo(GUI_FileManage_t *manager, const FILINFO *info)
{
	GUI_FileManageItem_t *item;
	const char *name;
	char path[GUI_FILEMANAGE_PATH_SIZE];

	name = GUI_FM_GetName(info);
	if (name == NULL || name[0] == '\0' || name[0] == '.')
	{
		return SYSTEM_STATUS_OK;
	}

	if ((info->fattrib & AM_DIR) == 0U && GUI_FM_ExtensionAccepted(manager, info) == 0U)
	{
		return SYSTEM_STATUS_OK;
	}

	if (manager->config.filter != NULL)
	{
		if (GUI_FM_PathJoin(path, GUI_FILEMANAGE_PATH_SIZE, manager->current_path, name) == 0U)
		{
			return OPERATE_FAILED;
		}
		if (manager->config.filter(manager, info, path, manager->config.user_data) == 0U)
		{
			return SYSTEM_STATUS_OK;
		}
	}

	if (manager->item_count >= GUI_FILEMANAGE_MAX_ITEMS)
	{
		GUI_FileManage_SetStatus(manager, "More files omitted");
		return SYSTEM_STATUS_OK;
	}

	if (strlen(name) >= GUI_FILEMANAGE_NAME_SIZE)
	{
		GUI_FileManage_SetStatus(manager, "Name too long");
		return SYSTEM_STATUS_OK;
	}

	item = &manager->items[manager->item_count];
	memset(item, 0, sizeof(GUI_FileManageItem_t));
	item->manager = manager;
	item->type = ((info->fattrib & AM_DIR) != 0U) ? GUI_FILEMANAGE_ITEM_DIR : GUI_FILEMANAGE_ITEM_FILE;
	item->size = (u32)info->fsize;
	item->attr = info->fattrib;
	(void)GUI_FM_CopyText(item->name, GUI_FILEMANAGE_NAME_SIZE, name);
	manager->item_count++;

	return SYSTEM_STATUS_OK;
}

static void GUI_FM_RebuildList(GUI_FileManage_t *manager)
{
	u8 i;
	lv_group_t *group;
	lv_obj_t *first_obj = NULL;
	u8 page_active;
	char display_path[GUI_FM_PATH_UTF8_SIZE];

	if (manager == NULL || manager->list == NULL)
	{
		return;
	}

	page_active = (Page_GetCurrent() == manager->page->id) ? 1U : 0U;
	group = lv_group_get_default();
	if (group != NULL && page_active != 0U)
	{
		lv_group_remove_all_objs(group);
		lv_group_set_editing(group, false);
		lv_group_set_wrap(group, true);
	}

	if (GUI_FileManage_OemToUtf8(display_path, sizeof(display_path), manager->current_path) != 0U)
	{
		lv_label_set_text(manager->path_label, display_path);
	}
	else
	{
		lv_label_set_text(manager->path_label, "Path encoding error");
	}
	GUI_FM_ApplyTextFont(manager->path_label);
	GUI_FM_RemoveChildrenFromGroup(manager->list);
	lv_obj_clean(manager->list);

	for (i = 0U; i < manager->item_count; i++)
	{
		GUI_FM_AddListItem(manager, &manager->items[i]);
	}

	if (manager->item_count == 0U)
	{
		const char *text = (manager->config.empty_text != NULL) ? manager->config.empty_text : "No files";
		lv_obj_t *empty = lv_list_add_text(manager->list, text);

		if (empty != NULL)
		{
			lv_obj_set_style_bg_opa(empty, LV_OPA_TRANSP, 0);
			lv_obj_set_style_text_color(empty, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
			lv_obj_set_style_pad_hor(empty, 12, 0);
		}
	}

	if (group != NULL && page_active != 0U)
	{
		GUI_FM_AddFocusableChildren(group, manager->screen, &first_obj);
		if (first_obj != NULL)
		{
			lv_group_focus_obj(first_obj);
		}
	}
}

static void GUI_FM_AddListItem(GUI_FileManage_t *manager, GUI_FileManageItem_t *item)
{
	const char *icon;
	lv_obj_t *btn;
	char display_name[GUI_FM_NAME_UTF8_SIZE];

	if (item->type == GUI_FILEMANAGE_ITEM_PARENT)
	{
		icon = GUI_ICON_ARROW_UPWARD;
	}
	else if (item->type == GUI_FILEMANAGE_ITEM_DIR)
	{
		icon = GUI_ICON_FOLDER;
	}
	else if (item->type == GUI_FILEMANAGE_ITEM_NEXT)
	{
		icon = GUI_ICON_MORE_HORIZ;
	}
	else
	{
		icon = GUI_ICON_DESCRIPTION;
	}

	if (GUI_FileManage_OemToUtf8(display_name, sizeof(display_name), item->name) == 0U)
	{
		(void)GUI_FM_CopyText(display_name, sizeof(display_name), "Name encoding error");
	}

	btn = lv_list_add_btn(manager->list, icon, display_name);
	lv_obj_set_height(btn, GUI_FM_ROW_H);
	GUI_M3_StyleListItem(btn);
	GUI_FM_ApplyTextFont(btn);
	GUI_FM_ApplyIconFont(btn);
	lv_obj_add_event_cb(btn, GUI_FM_ItemEvent, LV_EVENT_CLICKED, item);
	lv_obj_add_event_cb(btn, GUI_FM_ItemEvent, LV_EVENT_KEY, item);
}

static int GUI_FM_OpenItem(GUI_FileManage_t *manager, GUI_FileManageItem_t *item)
{
	if (manager == NULL || item == NULL || manager->locked != 0U)
	{
		return OPERATE_FAILED;
	}

	if (item->type == GUI_FILEMANAGE_ITEM_PARENT)
	{
		return GUI_FileManage_OpenParent(manager);
	}

	if (item->type == GUI_FILEMANAGE_ITEM_DIR)
	{
		if (GUI_FileManage_BuildItemPath(manager, item, manager->pending_path, GUI_FILEMANAGE_PATH_SIZE) == 0U)
		{
			GUI_FileManage_SetStatus(manager, "Path too long");
			return OPERATE_FAILED;
		}
		return GUI_FileManage_OpenPath(manager, manager->pending_path);
	}

	GUI_FM_CloseDir(manager);
	if (manager->config.open != NULL)
	{
		return manager->config.open(manager, item, manager->config.user_data);
	}

	return SYSTEM_STATUS_OK;
}

static void GUI_FM_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj)
{
	u32 i;
	u32 child_count;

	if (group == NULL || parent == NULL)
	{
		return;
	}

	child_count = lv_obj_get_child_cnt(parent);
	for (i = 0U; i < child_count; i++)
	{
		lv_obj_t *child = lv_obj_get_child(parent, (s32)i);

		if (lv_obj_is_group_def(child) && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN))
		{
			lv_group_add_obj(group, child);
			if (first_obj != NULL && *first_obj == NULL)
			{
				*first_obj = child;
			}
		}

		GUI_FM_AddFocusableChildren(group, child, first_obj);
	}
}

static void GUI_FM_RemoveChildrenFromGroup(lv_obj_t *parent)
{
	u32 i;
	u32 child_count;

	if (parent == NULL)
	{
		return;
	}

	child_count = lv_obj_get_child_cnt(parent);
	for (i = 0U; i < child_count; i++)
	{
		lv_obj_t *child = lv_obj_get_child(parent, (s32)i);

		GUI_FM_RemoveChildrenFromGroup(child);
		if (lv_obj_get_group(child) != NULL)
		{
			lv_group_remove_obj(child);
		}
	}
}

static void GUI_FM_CloseDir(GUI_FileManage_t *manager)
{
	if (manager == NULL || manager->dir_open == 0U)
	{
		return;
	}

	(void)f_closedir(&manager->dir);
	manager->dir_open = 0U;
	manager->dir_eof = 0U;
	manager->has_more = 0U;
	manager->dir_path[0] = '\0';
}

static u8 GUI_FM_IsRootPath(const GUI_FileManage_t *manager)
{
	if (manager == NULL)
	{
		return 1U;
	}

	return (strcmp(manager->current_path, manager->root_path) == 0) ? 1U : 0U;
}

static u8 GUI_FM_PathJoin(char *dst, u32 dst_size, const char *dir, const char *name)
{
	u32 dir_len;
	u32 name_len;
	u8 need_sep;

	if (dst == NULL || dst_size == 0U || dir == NULL || name == NULL)
	{
		return 0U;
	}

	dir_len = (u32)strlen(dir);
	name_len = (u32)strlen(name);
	need_sep = (dir_len > 0U && dir[dir_len - 1U] != '/' && dir[dir_len - 1U] != '\\') ? 1U : 0U;
	if ((dir_len + name_len + (u32)need_sep + 1U) > dst_size)
	{
		return 0U;
	}

	memcpy(dst, dir, dir_len);
	if (need_sep != 0U)
	{
		dst[dir_len] = '/';
		dir_len++;
	}
	memcpy(&dst[dir_len], name, name_len);
	dst[dir_len + name_len] = '\0';

	return 1U;
}

static u8 GUI_FM_CopyText(char *dst, u32 dst_size, const char *src)
{
	u32 len;

	if (dst == NULL || dst_size == 0U || src == NULL)
	{
		return 0U;
	}

	len = (u32)strlen(src);
	if (len >= dst_size)
	{
		return 0U;
	}

	memcpy(dst, src, len + 1U);
	return 1U;
}

u8 GUI_FileManage_OemToUtf8(char *dst, u32 dst_size, const char *src)
{
	const u8 *input;
	u32 output_index;
	u32 output_size;
	u16 oem;
	u16 unicode;
	u8 lead;
	u8 trail;

	if (dst == NULL || dst_size == 0U || src == NULL)
	{
		return 0U;
	}

	input = (const u8 *)src;
	output_index = 0U;
	while (*input != 0U)
	{
		lead = *input;
		if (lead < 0x80U)
		{
			unicode = (u16)lead;
			input++;
		}
		else
		{
			trail = input[1];
			if (lead >= 0x81U && lead <= 0xFEU &&
			    trail >= 0x40U && trail <= 0xFEU && trail != 0x7FU)
			{
				oem = (u16)(((u16)lead << 8U) | (u16)trail);
				unicode = (u16)ff_convert((WCHAR)oem, 1U);
				input += 2;
			}
			else
			{
				unicode = 0U;
				input++;
			}

			if (unicode == 0U)
			{
				unicode = (u16)'?';
			}
		}

		if (unicode < 0x80U)
		{
			output_size = 1U;
		}
		else if (unicode < 0x800U)
		{
			output_size = 2U;
		}
		else
		{
			output_size = 3U;
		}

		if ((output_index + output_size) >= dst_size)
		{
			dst[0] = '\0';
			return 0U;
		}

		if (output_size == 1U)
		{
			dst[output_index++] = (char)unicode;
		}
		else if (output_size == 2U)
		{
			dst[output_index++] = (char)(0xC0U | (unicode >> 6U));
			dst[output_index++] = (char)(0x80U | (unicode & 0x3FU));
		}
		else
		{
			dst[output_index++] = (char)(0xE0U | (unicode >> 12U));
			dst[output_index++] = (char)(0x80U | ((unicode >> 6U) & 0x3FU));
			dst[output_index++] = (char)(0x80U | (unicode & 0x3FU));
		}
	}

	dst[output_index] = '\0';
	return 1U;
}

static void GUI_FM_ApplyTextFont(lv_obj_t *obj)
{
	const lv_font_t *font;

	if (obj == NULL)
	{
		return;
	}

	font = GUI_Font_GetXbfFont();
	lv_obj_set_style_text_font(obj, (font != NULL) ? font : LV_FONT_DEFAULT, 0);
}

/* The leading glyph of a list row is rendered by an lv_img symbol source, so it
 * needs the Material Symbols font instead of the inherited text font. */
static void GUI_FM_ApplyIconFont(lv_obj_t *row)
{
	lv_obj_t *icon;

	if (row == NULL || lv_obj_get_child_cnt(row) == 0U)
	{
		return;
	}

	icon = lv_obj_get_child(row, 0);
	if (icon != NULL)
	{
		lv_obj_set_style_text_font(icon, GUI_M3_IconFont(), 0);
		lv_obj_set_style_text_color(icon, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
		lv_obj_set_style_text_color(icon, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SECONDARY_CONTAINER),
		                            LV_STATE_FOCUSED);
	}
}

static u8 GUI_FM_ExtensionAccepted(GUI_FileManage_t *manager, const FILINFO *info)
{
	const char *ext;
	u8 i;

	if (manager == NULL || info == NULL)
	{
		return 0U;
	}

	if (manager->config.extensions == NULL || manager->config.extension_count == 0U)
	{
		return 1U;
	}

	ext = GUI_FM_GetExt(GUI_FM_GetName(info));
	if (ext == NULL)
	{
		return 0U;
	}

	for (i = 0U; i < manager->config.extension_count; i++)
	{
		if (manager->config.extensions[i] != NULL && GUI_FM_StrCaseCmp(ext, manager->config.extensions[i]) == 0)
		{
			return 1U;
		}
	}

	return 0U;
}

static int GUI_FM_StrCaseCmp(const char *a, const char *b)
{
	char ca;
	char cb;

	if (a == NULL || b == NULL)
	{
		return (a == b) ? 0 : 1;
	}

	while (*a != '\0' && *b != '\0')
	{
		ca = *a;
		cb = *b;
		if (ca >= 'A' && ca <= 'Z')
		{
			ca = (char)(ca - 'A' + 'a');
		}
		if (cb >= 'A' && cb <= 'Z')
		{
			cb = (char)(cb - 'A' + 'a');
		}
		if (ca != cb)
		{
			return (int)((unsigned char)ca - (unsigned char)cb);
		}
		a++;
		b++;
	}

	return (int)((unsigned char)*a - (unsigned char)*b);
}

static const char *GUI_FM_GetName(const FILINFO *info)
{
	if (info == NULL)
	{
		return NULL;
	}

#if _USE_LFN != 0
	if (info->fname[0] != '\0')
	{
		return info->fname;
	}
	if (info->altname[0] != '\0')
	{
		return info->altname;
	}
#endif
	return info->fname;
}

static const char *GUI_FM_GetExt(const char *name)
{
	const char *dot;

	if (name == NULL)
	{
		return NULL;
	}

	dot = strrchr(name, '.');
	if (dot == NULL || dot[1] == '\0')
	{
		return NULL;
	}

	return dot;
}

static void GUI_FM_SetDefaultStatus(GUI_FileManage_t *manager)
{
	char text[GUI_FM_STATUS_LEN];
	const char *prefix;
	u32 len;
	u32 count;

	if (manager == NULL || manager->status_label == NULL)
	{
		return;
	}
	if (manager->config.hide_default_status != 0U)
	{
		GUI_FileManage_SetStatus(manager, "");
		return;
	}

	prefix = (manager->config.status_text != NULL) ? manager->config.status_text : "Items";
	len = (u32)strlen(prefix);
	if (len >= (GUI_FM_STATUS_LEN - 8U))
	{
		len = GUI_FM_STATUS_LEN - 8U;
	}

	memcpy(text, prefix, len);
	text[len++] = ':';
	text[len++] = ' ';

	count = manager->item_count;
	if (count >= 100U)
	{
		text[len++] = (char)('0' + ((count / 100U) % 10U));
	}
	if (count >= 10U)
	{
		text[len++] = (char)('0' + ((count / 10U) % 10U));
	}
	text[len++] = (char)('0' + (count % 10U));
	text[len] = '\0';

	GUI_FileManage_SetStatus(manager, text);
}

/* MD3 keeps one primary accent per scheme, so the legacy per-page accent_color
 * field in GUI_FileManageConfig_t is intentionally unused: every browser page now
 * shares the same tonal palette from GUI_Theme. */
