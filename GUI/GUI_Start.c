#include "GUI_Start.h"
#include "GUI_Theme.h"
#include "System_Status.h"
#include "lvgl.h"

#define GUI_START_DELAY_MS       400U
#define GUI_START_SPINNER_TIME   900U
#define GUI_START_SPINNER_ARC    72U
#define GUI_START_LOGO_SIZE      72

static lv_obj_t *s_start_screen = NULL;
static lv_obj_t *s_start_spinner = NULL;
static lv_obj_t *s_start_status = NULL;
static lv_timer_t *s_start_timer = NULL;
static GUI_StartReadyCallback_t s_ready_callback = NULL;

static void GUI_Start_TimerCallback(lv_timer_t *timer);
static void GUI_Start_Reset(void);
static void GUI_Start_ShowFailure(void);

int GUI_Start_Show(GUI_StartReadyCallback_t ready_callback)
{
	lv_obj_t *logo;
	lv_obj_t *title;

	if (ready_callback == NULL || s_start_screen != NULL || s_start_timer != NULL)
	{
		return OPERATE_FAILED;
	}

	s_start_screen = lv_obj_create(NULL);
	if (s_start_screen == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	GUI_M3_ApplyScreen(s_start_screen);

	logo = GUI_M3_CreateIconContainer(s_start_screen, GUI_ICON_GAMEPAD, GUI_START_LOGO_SIZE,
	                                  GUI_THEME_COLOR_PRIMARY_CONTAINER,
	                                  GUI_THEME_COLOR_ON_PRIMARY_CONTAINER, 1U);
	if (logo == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_obj_align(logo, LV_ALIGN_CENTER, 0, -56);

	title = GUI_M3_CreateLabel(s_start_screen, "Star Gamer", GUI_M3_TYPE_HEADLINE,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (title == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_obj_align(title, LV_ALIGN_CENTER, 0, 4);

	s_start_spinner = lv_spinner_create(s_start_screen,
	                                    GUI_START_SPINNER_TIME,
	                                    GUI_START_SPINNER_ARC);
	if (s_start_spinner == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_obj_set_size(s_start_spinner, 34, 34);
	GUI_M3_StyleSpinner(s_start_spinner);
	lv_obj_align(s_start_spinner, LV_ALIGN_CENTER, 0, 50);

	s_start_status = GUI_M3_CreateLabel(s_start_screen, "Starting GUI...", GUI_M3_TYPE_BODY,
	                                    GUI_THEME_COLOR_ON_SURFACE_VARIANT);
	if (s_start_status == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_obj_align(s_start_status, LV_ALIGN_CENTER, 0, 88);

	s_start_timer = lv_timer_create(GUI_Start_TimerCallback, GUI_START_DELAY_MS, NULL);
	if (s_start_timer == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}

	s_ready_callback = ready_callback;
	lv_timer_set_repeat_count(s_start_timer, 1);
	lv_scr_load(s_start_screen);
	lv_obj_invalidate(s_start_screen);

	return SYSTEM_STATUS_OK;
}

static void GUI_Start_TimerCallback(lv_timer_t *timer)
{
	GUI_StartReadyCallback_t ready_callback;
	lv_obj_t *start_screen;
	int code;

	(void)timer;

	start_screen = s_start_screen;
	ready_callback = s_ready_callback;
	s_start_timer = NULL;
	s_ready_callback = NULL;

	if (ready_callback == NULL)
	{
		GUI_Start_ShowFailure();
		return;
	}

	code = ready_callback();
	if (code != SYSTEM_STATUS_OK)
	{
		GUI_Start_ShowFailure();
		return;
	}

	if (start_screen != NULL && lv_scr_act() != start_screen)
	{
		lv_obj_del(start_screen);
	}

	s_start_screen = NULL;
	s_start_spinner = NULL;
	s_start_status = NULL;
}

static void GUI_Start_Reset(void)
{
	if (s_start_timer != NULL)
	{
		lv_timer_del(s_start_timer);
	}
	if (s_start_screen != NULL)
	{
		lv_obj_del(s_start_screen);
	}

	s_start_screen = NULL;
	s_start_spinner = NULL;
	s_start_status = NULL;
	s_start_timer = NULL;
	s_ready_callback = NULL;
}

static void GUI_Start_ShowFailure(void)
{
	if (s_start_spinner != NULL)
	{
		lv_obj_add_flag(s_start_spinner, LV_OBJ_FLAG_HIDDEN);
	}
	if (s_start_status != NULL)
	{
		lv_label_set_text(s_start_status, "GUI start failed");
		lv_obj_set_style_text_color(s_start_status, GUI_Theme_LvColor(GUI_THEME_COLOR_ERROR), 0);
		lv_obj_invalidate(s_start_status);
	}
}
