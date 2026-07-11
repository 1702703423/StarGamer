#include "GUI_Start.h"
#include "System_Status.h"
#include "lvgl.h"

#define GUI_START_DELAY_MS       400U
#define GUI_START_SPINNER_TIME   900U
#define GUI_START_SPINNER_ARC    72U

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
	lv_obj_t *title;
	lv_obj_t *accent;

	if (ready_callback == NULL || s_start_screen != NULL || s_start_timer != NULL)
	{
		return OPERATE_FAILED;
	}

	s_start_screen = lv_obj_create(NULL);
	if (s_start_screen == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	lv_obj_clear_flag(s_start_screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(s_start_screen, lv_color_hex(0x101418U), 0);
	lv_obj_set_style_bg_opa(s_start_screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(s_start_screen, 0, 0);

	title = lv_label_create(s_start_screen);
	if (title == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_label_set_text(title, "Star Gamer");
	lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -54);

	accent = lv_obj_create(s_start_screen);
	if (accent == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(accent, 64, 2);
	lv_obj_set_style_radius(accent, 0, 0);
	lv_obj_set_style_border_width(accent, 0, 0);
	lv_obj_set_style_bg_color(accent, lv_color_hex(0x26A69AU), 0);
	lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
	lv_obj_align(accent, LV_ALIGN_CENTER, 0, -31);

	s_start_spinner = lv_spinner_create(s_start_screen,
	                                    GUI_START_SPINNER_TIME,
	                                    GUI_START_SPINNER_ARC);
	if (s_start_spinner == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_obj_set_size(s_start_spinner, 46, 46);
	lv_obj_set_style_arc_width(s_start_spinner, 4, LV_PART_MAIN);
	lv_obj_set_style_arc_color(s_start_spinner, lv_color_hex(0x344047U), LV_PART_MAIN);
	lv_obj_set_style_arc_width(s_start_spinner, 4, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(s_start_spinner, lv_color_hex(0x26A69AU), LV_PART_INDICATOR);
	lv_obj_center(s_start_spinner);

	s_start_status = lv_label_create(s_start_screen);
	if (s_start_status == NULL)
	{
		GUI_Start_Reset();
		return OUT_OF_HEAP_MEMORY;
	}
	lv_label_set_text(s_start_status, "Starting GUI...");
	lv_obj_set_style_text_color(s_start_status, lv_color_hex(0xAEB7C2U), 0);
	lv_obj_align(s_start_status, LV_ALIGN_CENTER, 0, 46);

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
		lv_obj_set_style_text_color(s_start_status, lv_color_hex(0xE84E40U), 0);
		lv_obj_invalidate(s_start_status);
	}
}
