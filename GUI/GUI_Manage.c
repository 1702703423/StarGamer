#include "GUI_Manage.h"
#include "GUI_Theme.h"

#define PAGE_HISTORY_DEPTH 8U

static Page_Struct_t s_page_pool[GUI_PAGE_MAX];
static Page_ID_t s_page_history[PAGE_HISTORY_DEPTH];
static u8 s_history_depth = 0;
static Page_ID_t s_current_page = GUI_PAGE_NONE;
static lv_obj_t *s_fallback_screen = NULL;
static u8 s_manager_ready = 0;

static int Page_IsValidId(Page_ID_t id)
{
	return ((u32)id < (u32)GUI_PAGE_MAX);
}

static void Page_ResetSlot(Page_Struct_t *page)
{
	Page_ID_t id;

	if (page == NULL)
	{
		return;
	}

	id = page->id;
	memset(page, 0, sizeof(Page_Struct_t));
	page->id = id;
}

static void Page_RemoveFromHistory(Page_ID_t id)
{
	u8 read_index;
	u8 write_index = 0;

	for (read_index = 0; read_index < s_history_depth; read_index++)
	{
		if (s_page_history[read_index] != id)
		{
			s_page_history[write_index] = s_page_history[read_index];
			write_index++;
		}
	}

	while (write_index < s_history_depth)
	{
		s_page_history[write_index] = GUI_PAGE_NONE;
		write_index++;
	}

	s_history_depth = write_index;
}

static void Page_HistoryPush(Page_ID_t id)
{
	u8 i;

	if (!Page_IsValidId(id))
	{
		return;
	}

	if (s_history_depth > 0U && s_page_history[s_history_depth - 1U] == id)
	{
		return;
	}

	if (s_history_depth >= PAGE_HISTORY_DEPTH)
	{
		for (i = 1U; i < PAGE_HISTORY_DEPTH; i++)
		{
			s_page_history[i - 1U] = s_page_history[i];
		}
		s_history_depth = PAGE_HISTORY_DEPTH - 1U;
	}

	s_page_history[s_history_depth] = id;
	s_history_depth++;
}

static Page_ID_t Page_HistoryPopExisting(Page_ID_t skip_id)
{
	Page_ID_t id;

	while (s_history_depth > 0U)
	{
		s_history_depth--;
		id = s_page_history[s_history_depth];
		s_page_history[s_history_depth] = GUI_PAGE_NONE;

		if (id != skip_id && Page_Find(id) != NULL)
		{
			return id;
		}
	}

	return GUI_PAGE_NONE;
}

static int Page_LoadFallback(void)
{
	if (s_fallback_screen == NULL)
	{
		s_fallback_screen = lv_obj_create(NULL);
		if (s_fallback_screen != NULL)
		{
			lv_obj_clear_flag(s_fallback_screen, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_style_bg_color(s_fallback_screen, GUI_Theme_LvColor(GUI_THEME_COLOR_SCREEN_BG), 0);
			lv_obj_set_style_bg_opa(s_fallback_screen, LV_OPA_COVER, 0);
		}
	}

	if (s_fallback_screen == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	if (s_fallback_screen != NULL)
	{
		lv_scr_load(s_fallback_screen);
	}

	s_current_page = GUI_PAGE_NONE;
	return SYSTEM_STATUS_OK;
}

static int Page_Load(Page_Struct_t *page, lv_scr_load_anim_t anim_type, u32 time, u32 delay, u8 use_anim, u8 save_history)
{
	if (page == NULL || page->screen == NULL || page->created == 0U)
	{
		return OPERATE_FAILED;
	}

	if (save_history != 0U && s_current_page != GUI_PAGE_NONE && s_current_page != page->id)
	{
		Page_HistoryPush(s_current_page);
	}

	if (use_anim != 0U)
	{
		lv_scr_load_anim(page->screen, anim_type, time, delay, false);
	}
	else
	{
		lv_scr_load(page->screen);
	}

	s_current_page = page->id;

	if (page->Page_Callback != NULL)
	{
		return page->Page_Callback(page, NULL);
	}

	return SYSTEM_STATUS_OK;
}

int Page_Manager_Init(void)
{
	int code;
	u8 i;

	if (SRT.page_table == NULL)
	{
		SRT.page_table = creat_chain(SRAMEX, &code);
		if (SRT.page_table == NULL || code != SYSTEM_STATUS_OK)
		{
			return (code == OUT_OF_HEAP_MEMORY) ? OUT_OF_HEAP_MEMORY : OPERATE_FAILED;
		}
	}

	if (s_manager_ready != 0U)
	{
		return SYSTEM_STATUS_OK;
	}

	memset(s_page_pool, 0, sizeof(s_page_pool));
	for (i = 0U; i < (u8)GUI_PAGE_MAX; i++)
	{
		s_page_pool[i].id = (Page_ID_t)i;
	}

	Page_ClearHistory();
	s_current_page = GUI_PAGE_NONE;
	s_manager_ready = 1U;

	return SYSTEM_STATUS_OK;
}

int Page_Create(Page_ID_t id, const char *name, PageInit init, PageCallBack callback)
{
	int code;
	Page_Struct_t *page;

	code = Page_Manager_Init();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	if (!Page_IsValidId(id))
	{
		return OPERATE_FAILED;
	}

	page = &s_page_pool[(u32)id];
	if (page->created != 0U)
	{
		return OPERATE_FAILED;
	}

	page->name = name;
	page->Page_init = init;
	page->Page_Callback = callback;
	page->screen = lv_obj_create(NULL);
	if (page->screen == NULL)
	{
		Page_ResetSlot(page);
		return OUT_OF_HEAP_MEMORY;
	}

	page->created = 1U;

	if (page->Page_init != NULL)
	{
		code = page->Page_init(page);
		if (code != SYSTEM_STATUS_OK)
		{
			lv_obj_del(page->screen);
			Page_ResetSlot(page);
			return code;
		}
	}

	if (insert_head_chain(SRT.page_table, page, &code) == NULL || code != SYSTEM_STATUS_OK)
	{
		lv_obj_del(page->screen);
		Page_ResetSlot(page);
		return (code == OUT_OF_HEAP_MEMORY) ? OUT_OF_HEAP_MEMORY : OPERATE_FAILED;
	}

	return SYSTEM_STATUS_OK;
}

int Page_Delete(Page_ID_t id)
{
	int code;
	Page_ID_t fallback_id;
	Page_Struct_t *page;
	Page_Struct_t *fallback_page;

	page = Page_Find(id);
	if (page == NULL)
	{
		return OPERATE_FAILED;
	}

	if (s_current_page == id)
	{
		fallback_id = Page_HistoryPopExisting(id);
		fallback_page = Page_Find(fallback_id);
		if (fallback_page != NULL)
		{
			(void)Page_Load(fallback_page, LV_SCR_LOAD_ANIM_NONE, 0U, 0U, 0U, 0U);
		}
		else
		{
			code = Page_LoadFallback();
			if (code != SYSTEM_STATUS_OK)
			{
				return code;
			}
		}
	}

	lv_obj_del(page->screen);
	(void)delete_node(SRT.page_table, page, &code);
	Page_RemoveFromHistory(id);
	Page_ResetSlot(page);

	if (s_current_page == id)
	{
		s_current_page = GUI_PAGE_NONE;
	}

	return SYSTEM_STATUS_OK;
}

int Page_DeleteAll(void)
{
	int code;
	int result = SYSTEM_STATUS_OK;
	u8 i;

	if (s_current_page != GUI_PAGE_NONE)
	{
		code = Page_LoadFallback();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	for (i = 0U; i < (u8)GUI_PAGE_MAX; i++)
	{
		if (s_page_pool[i].created != 0U)
		{
			code = Page_Delete((Page_ID_t)i);
			if (code != SYSTEM_STATUS_OK)
			{
				result = code;
			}
		}
	}

	Page_ClearHistory();
	s_current_page = GUI_PAGE_NONE;

	return result;
}

int Page_Show(Page_ID_t id)
{
	Page_Struct_t *page;

	page = Page_Find(id);
	if (page == NULL)
	{
		return OPERATE_FAILED;
	}

	if (s_current_page == id)
	{
		return SYSTEM_STATUS_OK;
	}

	return Page_Load(page, LV_SCR_LOAD_ANIM_NONE, 0U, 0U, 0U, 1U);
}

int Page_Show_Anim(Page_ID_t id, lv_scr_load_anim_t anim_type, u32 time, u32 delay)
{
	Page_Struct_t *page;

	page = Page_Find(id);
	if (page == NULL)
	{
		return OPERATE_FAILED;
	}

	if (s_current_page == id)
	{
		return SYSTEM_STATUS_OK;
	}

	return Page_Load(page, anim_type, time, delay, 1U, 1U);
}

int Page_Back(void)
{
	Page_ID_t fallback_id;
	Page_Struct_t *fallback_page;

	fallback_id = Page_HistoryPopExisting(s_current_page);
	if (fallback_id == GUI_PAGE_NONE && s_current_page != GUI_PAGE_MAIN_MENU)
	{
		fallback_id = GUI_PAGE_MAIN_MENU;
	}

	fallback_page = Page_Find(fallback_id);
	if (fallback_page == NULL)
	{
		return OPERATE_FAILED;
	}

	return Page_Load(fallback_page, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 120U, 0U, 1U, 0U);
}

int Page_SetCallBack(Page_ID_t id, PageCallBack callback)
{
	Page_Struct_t *page;

	page = Page_Find(id);
	if (page == NULL)
	{
		return OPERATE_FAILED;
	}

	page->Page_Callback = callback;
	return SYSTEM_STATUS_OK;
}

Page_Struct_t *Page_Find(Page_ID_t id)
{
	Page_Struct_t *page;

	if (!Page_IsValidId(id))
	{
		return NULL;
	}

	page = &s_page_pool[(u32)id];
	if (page->created == 0U)
	{
		return NULL;
	}

	return page;
}

lv_obj_t *Page_GetObject(Page_ID_t id)
{
	Page_Struct_t *page;

	page = Page_Find(id);
	if (page == NULL)
	{
		return NULL;
	}

	return page->screen;
}

Page_ID_t Page_GetCurrent(void)
{
	return s_current_page;
}

void Page_ClearHistory(void)
{
	u8 i;

	for (i = 0U; i < PAGE_HISTORY_DEPTH; i++)
	{
		s_page_history[i] = GUI_PAGE_NONE;
	}

	s_history_depth = 0U;
}
