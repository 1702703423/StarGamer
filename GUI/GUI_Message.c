#include "GUI_Message.h"
#include "GUI_Theme.h"
#include "GUI_Font.h"
#include "System_Status.h"
#include "cmsis_os2.h"
#include <string.h>

extern osThreadId_t lvglThreadId;

/* --- toast layout ------------------------------------------------------- */
#define GUI_MESSAGE_TOAST_W           296
#define GUI_MESSAGE_TOAST_X           12
#define GUI_MESSAGE_TOAST_GAP_TOP     8
#define GUI_MESSAGE_TOAST_PAD_HOR     12
#define GUI_MESSAGE_TOAST_PAD_VER     10
#define GUI_MESSAGE_TOAST_ICON_X      GUI_MESSAGE_TOAST_PAD_HOR
#define GUI_MESSAGE_TOAST_TEXT_X      38
#define GUI_MESSAGE_TOAST_TEXT_W      (GUI_MESSAGE_TOAST_W - GUI_MESSAGE_TOAST_TEXT_X - GUI_MESSAGE_TOAST_PAD_HOR)
#define GUI_MESSAGE_TOAST_MIN_H       48
#define GUI_MESSAGE_TOAST_SLIDE_MS    150U

/* --- dialog layout ------------------------------------------------------- */
#define GUI_MESSAGE_DIALOG_W          260
#define GUI_MESSAGE_DIALOG_PAD        14
#define GUI_MESSAGE_DIALOG_TEXT_W     (GUI_MESSAGE_DIALOG_W - 2 * GUI_MESSAGE_DIALOG_PAD)
#define GUI_MESSAGE_DIALOG_BTN_H      30
#define GUI_MESSAGE_DIALOG_BTN_MIN_W  72
#define GUI_MESSAGE_SCRM_OPA          LV_OPA_50
#define GUI_MESSAGE_FLUSH_PERIOD_MS   20U

typedef struct
{
	u8 pending;
	u8 kind;
	char title[GUI_MESSAGE_TITLE_MAX];
	char text[GUI_MESSAGE_TEXT_MAX];
	GUI_Message_Callback_t callback;
	void *user_data;
} GUI_Message_Request_t;

typedef struct
{
	lv_obj_t *obj;
	lv_obj_t *btn_primary;
	lv_obj_t *btn_secondary;
	lv_indev_t *indev;
	lv_group_t *group;
	lv_group_t *saved_group;
	lv_obj_t *focus_prev;
	lv_timer_t *timer;			/* toast hold timer only */
	GUI_Message_Callback_t callback;
	void *user_data;
	u8 kind;
} GUI_Message_Active_t;

static int GUI_Message_Enqueue(GUI_Message_Kind_t kind, const char *title, const char *text,
                               GUI_Message_Callback_t callback, void *user_data);
static void GUI_Message_FlushTimerCb(lv_timer_t *timer);
static u8 GUI_Message_TakeRequest(GUI_Message_Request_t *out);
static void GUI_Message_NotifyDropped(const GUI_Message_Request_t *request);
static void GUI_Message_CopyText(char *dst, u32 size, const char *src);
static const lv_font_t *GUI_Message_TextFont(void);
static lv_indev_t *GUI_Message_FindKeypadIndev(void);
static void GUI_Message_ToastCreate(const GUI_Message_Request_t *request);
static void GUI_Message_ToastDiscard(void);
static void GUI_Message_ToastSlideOut(void);
static void GUI_Message_ToastHoldCb(lv_timer_t *timer);
static void GUI_Message_ToastAnimReadyCb(lv_anim_t *anim);
static void GUI_Message_DialogCreate(const GUI_Message_Request_t *request);
static void GUI_Message_DialogClose(GUI_Message_Result_t result);
static void GUI_Message_DialogDeleteAsync(void *obj);
static void GUI_Message_DialogBtnEvent(lv_event_t *event);
static void GUI_Message_DialogScrimEvent(lv_event_t *event);

/* Single-slot request mailbox; guarded by osKernelLock(), consumed by the
 * LVGL flush timer. */
static GUI_Message_Request_t s_request;
static lv_timer_t *s_flush_timer;

/* Active overlays; only touched from the LVGL thread. */
static GUI_Message_Active_t s_toast;
static GUI_Message_Active_t s_dialog;

/* Persistent keypad group for centered dialogs. Created once - groups are
 * never created/deleted at dialog open/close time: lv_group_create/del
 * modify the global group list (_lv_group_ll) and free its node memory,
 * and the freed node blocks get reused by other allocations (e.g. label
 * text buffers), which corrupted the list under stress testing. */
static lv_group_t *s_msg_group;

int GUI_Message_Init(void)
{
	if (s_flush_timer != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	s_flush_timer = lv_timer_create(GUI_Message_FlushTimerCb, GUI_MESSAGE_FLUSH_PERIOD_MS, NULL);
	if (s_flush_timer == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	if (s_msg_group == NULL)
	{
		s_msg_group = lv_group_create();
		if (s_msg_group != NULL)
		{
			lv_group_set_wrap(s_msg_group, true);
		}
		/* NULL is non-fatal: dialogs still render, only keypad navigation
		 * inside them is unavailable. */
	}

	return SYSTEM_STATUS_OK;
}

u8 GUI_Message_ScreenAvailable(void)
{
	return lvgl_ui_is_active();
}

int GUI_Message_Toast(const char *text, GUI_Message_Callback_t callback, void *user_data)
{
	return GUI_Message_Enqueue(GUI_MESSAGE_KIND_TOAST, NULL, text, callback, user_data);
}

int GUI_Message_Confirm(const char *text, GUI_Message_Callback_t callback, void *user_data)
{
	return GUI_Message_Enqueue(GUI_MESSAGE_KIND_CONFIRM, NULL, text, callback, user_data);
}

int GUI_Message_YesNo(const char *text, GUI_Message_Callback_t callback, void *user_data)
{
	return GUI_Message_Enqueue(GUI_MESSAGE_KIND_YESNO, NULL, text, callback, user_data);
}

int GUI_Message_Show(GUI_Message_Kind_t kind, const char *title, const char *text,
                     GUI_Message_Callback_t callback, void *user_data)
{
	return GUI_Message_Enqueue(kind, title, text, callback, user_data);
}

/* ------------------------------------------------------------------------- */
/* request queue                                                             */
/* ------------------------------------------------------------------------- */

static int GUI_Message_Enqueue(GUI_Message_Kind_t kind, const char *title, const char *text,
                               GUI_Message_Callback_t callback, void *user_data)
{
	s32 lock_state;

	if ((u32)kind > (u32)GUI_MESSAGE_KIND_YESNO)
	{
		return OPERATE_FAILED;
	}

	/* Rule: message boxes only work while the LVGL task owns the screen. */
	if (GUI_Message_ScreenAvailable() == 0U)
	{
		return OPERATE_FAILED;
	}

	if (s_flush_timer == NULL)
	{
		/* Creating LVGL objects/timers is only legal in the LVGL thread. */
		if (lvglThreadId == NULL || osThreadGetId() != lvglThreadId)
		{
			return OPERATE_FAILED;
		}
		if (GUI_Message_Init() != SYSTEM_STATUS_OK)
		{
			return OUT_OF_HEAP_MEMORY;
		}
	}

	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_request.pending != 0U)
	{
		(void)osKernelRestoreLock(lock_state);
		return OPERATE_FAILED;
	}

	s_request.kind = (u8)kind;
	GUI_Message_CopyText(s_request.title, sizeof(s_request.title), title);
	GUI_Message_CopyText(s_request.text, sizeof(s_request.text), text);
	s_request.callback = callback;
	s_request.user_data = user_data;
	s_request.pending = 1U;
	(void)osKernelRestoreLock(lock_state);

	return SYSTEM_STATUS_OK;
}

static u8 GUI_Message_TakeRequest(GUI_Message_Request_t *out)
{
	s32 lock_state;
	u8 taken = 0U;

	if (out == NULL)
	{
		return 0U;
	}

	lock_state = osKernelLock();
	if (lock_state >= 0)
	{
		if (s_request.pending != 0U)
		{
			*out = s_request;
			s_request.pending = 0U;
			taken = 1U;
		}
		(void)osKernelRestoreLock(lock_state);
	}

	return taken;
}

static void GUI_Message_FlushTimerCb(lv_timer_t *timer)
{
	GUI_Message_Request_t request;

	(void)timer;

	if (s_request.pending == 0U)
	{
		return;
	}

	if (GUI_Message_ScreenAvailable() == 0U)
	{
		/* The screen changed hands between enqueue and flush; drop the
		 * request instead of touching widgets we no longer own. */
		if (GUI_Message_TakeRequest(&request) != 0U)
		{
			GUI_Message_NotifyDropped(&request);
		}
		return;
	}

	if (GUI_Message_TakeRequest(&request) == 0U)
	{
		return;
	}

	if (request.kind == (u8)GUI_MESSAGE_KIND_TOAST)
	{
		GUI_Message_ToastCreate(&request);
	}
	else
	{
		GUI_Message_DialogCreate(&request);
	}
}

static void GUI_Message_NotifyDropped(const GUI_Message_Request_t *request)
{
	if (request != NULL && request->callback != NULL)
	{
		request->callback(GUI_MESSAGE_RESULT_NONE, request->user_data);
	}
}

static void GUI_Message_CopyText(char *dst, u32 size, const char *src)
{
	u32 i = 0U;

	if (dst == NULL || size == 0U)
	{
		return;
	}

	if (src != NULL)
	{
		while (i + 1U < size && src[i] != '\0')
		{
			dst[i] = src[i];
			i++;
		}
	}
	dst[i] = '\0';
}

/* ------------------------------------------------------------------------- */
/* shared helpers                                                            */
/* ------------------------------------------------------------------------- */

static const lv_font_t *GUI_Message_TextFont(void)
{
	const lv_font_t *font;

	if (GUI_Font_IsInstalled() != 0U)
	{
		font = GUI_Font_GetXbfFont();
		if (font != NULL)
		{
			return font;
		}
	}

	return GUI_M3_Font(GUI_M3_TYPE_BODY);
}

static lv_indev_t *GUI_Message_FindKeypadIndev(void)
{
	lv_indev_t *indev = lv_indev_get_next(NULL);

	while (indev != NULL)
	{
		if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD)
		{
			return indev;
		}
		indev = lv_indev_get_next(indev);
	}

	return NULL;
}

/* ------------------------------------------------------------------------- */
/* toast                                                                     */
/* ------------------------------------------------------------------------- */

static void GUI_Message_ToastCreate(const GUI_Message_Request_t *request)
{
	lv_obj_t *toast;
	lv_obj_t *icon;
	lv_obj_t *label;
	lv_anim_t anim;
	lv_coord_t height;

	/* A new toast replaces the previous one; no timeout callback fires. */
	GUI_Message_ToastDiscard();

	toast = lv_obj_create(lv_layer_top());
	if (toast == NULL)
	{
		GUI_Message_NotifyDropped(request);
		return;
	}

	lv_obj_clear_flag(toast, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(toast, GUI_MESSAGE_TOAST_W, LV_SIZE_CONTENT);
	lv_obj_set_style_min_height(toast, GUI_MESSAGE_TOAST_MIN_H, 0);
	lv_obj_set_style_radius(toast, GUI_M3_RADIUS_LG, 0);
	lv_obj_set_style_border_width(toast, 0, 0);
	lv_obj_set_style_pad_left(toast, GUI_MESSAGE_TOAST_PAD_HOR, 0);
	lv_obj_set_style_pad_right(toast, GUI_MESSAGE_TOAST_PAD_HOR, 0);
	lv_obj_set_style_pad_top(toast, GUI_MESSAGE_TOAST_PAD_VER, 0);
	lv_obj_set_style_pad_bottom(toast, GUI_MESSAGE_TOAST_PAD_VER, 0);
	lv_obj_set_style_bg_color(toast, GUI_Theme_LvColor(GUI_THEME_COLOR_INVERSE_SURFACE), 0);
	lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(toast, 24, 0);
	lv_obj_set_style_shadow_opa(toast, (lv_opa_t)30, 0);
	lv_obj_set_style_shadow_ofs_y(toast, 4, 0);
	lv_obj_set_x(toast, GUI_MESSAGE_TOAST_X);

	icon = lv_label_create(toast);
	if (icon != NULL)
	{
		lv_label_set_text(icon, GUI_ICON_INFO);
		lv_obj_set_style_text_font(icon, GUI_M3_IconFont(), 0);
		lv_obj_set_style_text_color(icon, GUI_Theme_LvColor(GUI_THEME_COLOR_INVERSE_ON_SURFACE), 0);
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, GUI_MESSAGE_TOAST_ICON_X, 0);
	}

	label = lv_label_create(toast);
	if (label == NULL)
	{
		lv_obj_del(toast);
		GUI_Message_NotifyDropped(request);
		return;
	}
	lv_label_set_text(label, request->text);
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(label, GUI_MESSAGE_TOAST_TEXT_W);
	lv_obj_set_style_text_font(label, GUI_Message_TextFont(), 0);
	lv_obj_set_style_text_color(label, GUI_Theme_LvColor(GUI_THEME_COLOR_INVERSE_ON_SURFACE), 0);
	lv_obj_align(label, LV_ALIGN_LEFT_MID, GUI_MESSAGE_TOAST_TEXT_X, 0);

	s_toast.obj = toast;
	s_toast.kind = (u8)GUI_MESSAGE_KIND_TOAST;
	s_toast.callback = request->callback;
	s_toast.user_data = request->user_data;

	/* Slide down from above the screen edge; final y keeps a small gap. */
	lv_obj_update_layout(toast);
	height = lv_obj_get_height(toast);
	lv_obj_set_y(toast, -(height + GUI_MESSAGE_TOAST_GAP_TOP));

	lv_anim_init(&anim);
	lv_anim_set_var(&anim, toast);
	lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
	lv_anim_set_values(&anim, -(height + GUI_MESSAGE_TOAST_GAP_TOP), GUI_MESSAGE_TOAST_GAP_TOP);
	lv_anim_set_time(&anim, GUI_MESSAGE_TOAST_SLIDE_MS);
	lv_anim_set_path_cb(&anim, lv_anim_path_linear);
	lv_anim_start(&anim);

	if (System_Settings_ToastDurationSec() > 0U)
	{
		s_toast.timer = lv_timer_create(GUI_Message_ToastHoldCb,
		                                (u32)System_Settings_ToastDurationSec() * 1000U, NULL);
	}
	if (s_toast.timer == NULL)
	{
		/* No hold timer (allocation failed / duration zero): leave at once. */
		GUI_Message_ToastSlideOut();
	}
}

static void GUI_Message_ToastDiscard(void)
{
	if (s_toast.timer != NULL)
	{
		lv_timer_del(s_toast.timer);
		s_toast.timer = NULL;
	}
	if (s_toast.obj != NULL)
	{
		lv_anim_del(s_toast.obj, NULL);
		lv_obj_del(s_toast.obj);
		s_toast.obj = NULL;
	}
	s_toast.callback = NULL;
	s_toast.user_data = NULL;
}

static void GUI_Message_ToastHoldCb(lv_timer_t *timer)
{
	(void)timer;

	if (s_toast.timer != NULL)
	{
		lv_timer_del(s_toast.timer);
		s_toast.timer = NULL;
	}
	GUI_Message_ToastSlideOut();
}

static void GUI_Message_ToastSlideOut(void)
{
	lv_obj_t *toast = s_toast.obj;
	lv_anim_t anim;
	lv_coord_t height;

	if (toast == NULL)
	{
		return;
	}

	lv_obj_update_layout(toast);
	height = lv_obj_get_height(toast);

	lv_anim_init(&anim);
	lv_anim_set_var(&anim, toast);
	lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
	lv_anim_set_values(&anim, GUI_MESSAGE_TOAST_GAP_TOP, -(height + GUI_MESSAGE_TOAST_GAP_TOP));
	lv_anim_set_time(&anim, GUI_MESSAGE_TOAST_SLIDE_MS);
	lv_anim_set_path_cb(&anim, lv_anim_path_linear);
	lv_anim_set_ready_cb(&anim, GUI_Message_ToastAnimReadyCb);
	lv_anim_start(&anim);
}

static void GUI_Message_ToastAnimReadyCb(lv_anim_t *anim)
{
	GUI_Message_Callback_t callback;
	void *user_data;

	(void)anim;

	if (s_toast.obj == NULL)
	{
		return;
	}

	lv_obj_del(s_toast.obj);
	s_toast.obj = NULL;

	callback = s_toast.callback;
	user_data = s_toast.user_data;
	s_toast.callback = NULL;
	s_toast.user_data = NULL;
	if (callback != NULL)
	{
		callback(GUI_MESSAGE_RESULT_TIMEOUT, user_data);
	}
}

/* ------------------------------------------------------------------------- */
/* centered dialog (Confirm / Yes-No)                                        */
/* ------------------------------------------------------------------------- */

static void GUI_Message_DialogCreate(const GUI_Message_Request_t *request)
{
	lv_obj_t *scrim;
	lv_obj_t *card;
	lv_obj_t *title;
	lv_obj_t *msg;
	lv_obj_t *row;
	lv_obj_t *btn;
	lv_indev_t *indev;
	lv_group_t *group;

	if (s_dialog.obj != NULL)
	{
		/* Only one centered dialog at a time. */
		GUI_Message_NotifyDropped(request);
		return;
	}

	/* Drop stale button/group pointers from any earlier failed attempt. */
	memset(&s_dialog, 0, sizeof(s_dialog));

	scrim = lv_obj_create(lv_layer_top());
	if (scrim == NULL)
	{
		GUI_Message_NotifyDropped(request);
		return;
	}

	lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(scrim, GUI_M3_SCREEN_W, GUI_M3_SCREEN_H);
	lv_obj_set_pos(scrim, 0, 0);
	lv_obj_set_style_radius(scrim, 0, 0);
	lv_obj_set_style_border_width(scrim, 0, 0);
	lv_obj_set_style_pad_all(scrim, 0, 0);
	lv_obj_set_style_bg_color(scrim, GUI_Theme_LvColor(GUI_THEME_COLOR_SCRIM), 0);
	lv_obj_set_style_bg_opa(scrim, GUI_MESSAGE_SCRM_OPA, 0);
	lv_obj_add_event_cb(scrim, GUI_Message_DialogScrimEvent, LV_EVENT_CLICKED, NULL);

	card = GUI_M3_CreateCard(scrim, GUI_M3_CARD_ELEVATED);
	if (card == NULL)
	{
		lv_obj_del(scrim);
		GUI_Message_NotifyDropped(request);
		return;
	}
	lv_obj_set_width(card, GUI_MESSAGE_DIALOG_W);
	lv_obj_set_height(card, LV_SIZE_CONTENT);
	lv_obj_set_style_radius(card, GUI_M3_RADIUS_XL, 0);
	lv_obj_set_style_pad_all(card, GUI_MESSAGE_DIALOG_PAD, 0);
	lv_obj_set_style_pad_row(card, 8, 0);
	lv_obj_set_style_bg_color(card, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_HIGH), 0);
	lv_obj_set_style_shadow_width(card, 24, 0);
	lv_obj_set_style_shadow_opa(card, (lv_opa_t)40, 0);
	lv_obj_set_style_shadow_ofs_y(card, 3, 0);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_center(card);

	if (request->title[0] != '\0')
	{
		title = GUI_M3_CreateLabel(card, request->title, GUI_M3_TYPE_TITLE,
		                           GUI_THEME_COLOR_ON_SURFACE);
		(void)title;
	}

	msg = lv_label_create(card);
	if (msg == NULL)
	{
		lv_obj_del(scrim);
		GUI_Message_NotifyDropped(request);
		return;
	}
	lv_label_set_text(msg, request->text);
	lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(msg, GUI_MESSAGE_DIALOG_TEXT_W);
	lv_obj_set_style_text_font(msg, GUI_Message_TextFont(), 0);
	lv_obj_set_style_text_color(msg, GUI_Theme_LvColor(
		(request->title[0] != '\0') ? GUI_THEME_COLOR_ON_SURFACE_VARIANT :
		                              GUI_THEME_COLOR_ON_SURFACE), 0);

	row = lv_obj_create(card);
	if (row == NULL)
	{
		lv_obj_del(scrim);
		GUI_Message_NotifyDropped(request);
		return;
	}
	lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_style_pad_column(row, 8, 0);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	s_dialog.kind = request->kind;
	if (request->kind == (u8)GUI_MESSAGE_KIND_YESNO)
	{
		btn = GUI_M3_CreateButton(row, GUI_M3_BUTTON_FILLED, NULL, "Yes");
		if (btn == NULL)
		{
			lv_obj_del(scrim);
			GUI_Message_NotifyDropped(request);
			return;
		}
		s_dialog.btn_primary = btn;

		btn = GUI_M3_CreateButton(row, GUI_M3_BUTTON_TONAL, NULL, "No");
		if (btn == NULL)
		{
			lv_obj_del(scrim);
			GUI_Message_NotifyDropped(request);
			return;
		}
		s_dialog.btn_secondary = btn;
	}
	else
	{
		btn = GUI_M3_CreateButton(row, GUI_M3_BUTTON_FILLED, NULL, "Confirm");
		if (btn == NULL)
		{
			lv_obj_del(scrim);
			GUI_Message_NotifyDropped(request);
			return;
		}
		s_dialog.btn_primary = btn;
		s_dialog.btn_secondary = NULL;
	}

	lv_obj_set_size(s_dialog.btn_primary, LV_SIZE_CONTENT, GUI_MESSAGE_DIALOG_BTN_H);
	lv_obj_set_style_min_width(s_dialog.btn_primary, GUI_MESSAGE_DIALOG_BTN_MIN_W, 0);
	lv_obj_add_event_cb(s_dialog.btn_primary, GUI_Message_DialogBtnEvent, LV_EVENT_ALL, NULL);
	if (s_dialog.btn_secondary != NULL)
	{
		lv_obj_set_size(s_dialog.btn_secondary, LV_SIZE_CONTENT, GUI_MESSAGE_DIALOG_BTN_H);
		lv_obj_set_style_min_width(s_dialog.btn_secondary, GUI_MESSAGE_DIALOG_BTN_MIN_W, 0);
		lv_obj_add_event_cb(s_dialog.btn_secondary, GUI_Message_DialogBtnEvent, LV_EVENT_ALL, NULL);
	}

	/* Capture input: point the keypad at the persistent message group which
	 * holds only the dialog buttons, so navigation cannot reach the page
	 * underneath. The group itself is created once (see GUI_Message_Init);
	 * only the button objects are added/removed per dialog. */
	indev = GUI_Message_FindKeypadIndev();
	group = s_msg_group;
	if (group != NULL)
	{
		lv_group_remove_all_objs(group);
	}

	s_dialog.focus_prev = (indev != NULL) ? lv_group_get_focused(indev->group) :
	                       lv_group_get_focused(lv_group_get_default());
	if (s_dialog.focus_prev != NULL)
	{
		/* lv_indev_set_group does not reset the old focus state; drop the
		 * stale focus ring on the page underneath the scrim. */
		lv_obj_clear_state(s_dialog.focus_prev, LV_STATE_FOCUSED);
	}

	if (group != NULL)
	{
		lv_group_add_obj(group, s_dialog.btn_primary);
		if (s_dialog.btn_secondary != NULL)
		{
			lv_group_add_obj(group, s_dialog.btn_secondary);
		}
		s_dialog.group = group;
	}
	else
	{
		/* Persistent group unavailable (creation failed): fall back to the
		 * shared default group so the buttons stay reachable. */
		group = lv_group_get_default();
		s_dialog.group = NULL;
		if (group != NULL)
		{
			lv_group_add_obj(group, s_dialog.btn_primary);
			if (s_dialog.btn_secondary != NULL)
			{
				lv_group_add_obj(group, s_dialog.btn_secondary);
			}
		}
	}

	if (indev != NULL && s_dialog.group != NULL)
	{
		s_dialog.indev = indev;
		s_dialog.saved_group = indev->group;
		lv_indev_set_group(indev, s_dialog.group);
	}
	if (s_dialog.group != NULL)
	{
		lv_group_focus_obj(s_dialog.btn_primary);
	}

	s_dialog.obj = scrim;
	s_dialog.callback = request->callback;
	s_dialog.user_data = request->user_data;
}

static void GUI_Message_DialogClose(GUI_Message_Result_t result)
{
	GUI_Message_Callback_t callback;
	void *user_data;
	lv_obj_t *focus_prev;
	lv_obj_t *scrim;
	lv_group_t *group;
	lv_group_t *saved_group;
	lv_indev_t *indev;

	if (s_dialog.obj == NULL)
	{
		return;
	}

	callback = s_dialog.callback;
	user_data = s_dialog.user_data;
	focus_prev = s_dialog.focus_prev;
	scrim = s_dialog.obj;
	group = s_dialog.group;
	saved_group = s_dialog.saved_group;
	indev = s_dialog.indev;

	if (indev != NULL && group != NULL)
	{
		lv_indev_set_group(indev, saved_group);
	}

	/* Detach the buttons from whichever group holds them. Never delete the
	 * group itself: lv_group_del frees the group node on the global group
	 * list and the recycled memory corrupted the list under stress. */
	if (group != NULL)
	{
		if (s_dialog.btn_secondary != NULL &&
		    lv_obj_get_group(s_dialog.btn_secondary) == group)
		{
			lv_group_remove_obj(s_dialog.btn_secondary);
		}
		if (s_dialog.btn_primary != NULL &&
		    lv_obj_get_group(s_dialog.btn_primary) == group)
		{
			lv_group_remove_obj(s_dialog.btn_primary);
		}
	}
	else
	{
		/* Fallback path: buttons live in the shared default group. */
		if (s_dialog.btn_secondary != NULL &&
		    lv_obj_get_group(s_dialog.btn_secondary) != NULL)
		{
			lv_group_remove_obj(s_dialog.btn_secondary);
		}
		if (s_dialog.btn_primary != NULL &&
		    lv_obj_get_group(s_dialog.btn_primary) != NULL)
		{
			lv_group_remove_obj(s_dialog.btn_primary);
		}
	}

	/* DialogClose usually runs inside the keypad indev's event cycle:
	 * pressing ESC sends LV_EVENT_KEY followed by LV_EVENT_CANCEL to the
	 * focused button, ENTER release sends RELEASED/SHORT_CLICKED/CLICKED.
	 * Freeing the button here would leave the indev dispatching the next
	 * event of the cycle on a dangling pointer (observed as a precise bus
	 * HardFault inside event_send_core). Free the widget tree on the next
	 * timer pass instead - the buttons stay alive until then but belong to
	 * no group, so no further input events can reach them. */
	if (lv_async_call(GUI_Message_DialogDeleteAsync, scrim) != LV_RES_OK)
	{
		lv_obj_del(scrim);
	}

	memset(&s_dialog, 0, sizeof(s_dialog));

	if (focus_prev != NULL && saved_group != NULL &&
	    lv_obj_get_group(focus_prev) == saved_group)
	{
		lv_group_focus_obj(focus_prev);
	}

	if (callback != NULL)
	{
		callback(result, user_data);
	}
}

static void GUI_Message_DialogDeleteAsync(void *obj)
{
	lv_obj_del((lv_obj_t *)obj);
}

static void GUI_Message_DialogBtnEvent(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);
	lv_obj_t *target = lv_event_get_target(event);
	GUI_Message_Result_t result;

	if (s_dialog.obj == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED)
	{
		if (target == s_dialog.btn_primary)
		{
			result = (s_dialog.kind == (u8)GUI_MESSAGE_KIND_YESNO) ?
			         GUI_MESSAGE_RESULT_YES : GUI_MESSAGE_RESULT_CONFIRM;
		}
		else
		{
			result = GUI_MESSAGE_RESULT_NO;
		}
		GUI_Message_DialogClose(result);
	}
	else if (code == LV_EVENT_CANCEL)
	{
		GUI_Message_DialogClose((s_dialog.kind == (u8)GUI_MESSAGE_KIND_YESNO) ?
		                        GUI_MESSAGE_RESULT_NO : GUI_MESSAGE_RESULT_CONFIRM);
	}
	else if (code == LV_EVENT_KEY)
	{
		u32 key = lv_event_get_key(event);

		if (key == LV_KEY_ESC)
		{
			GUI_Message_DialogClose((s_dialog.kind == (u8)GUI_MESSAGE_KIND_YESNO) ?
			                        GUI_MESSAGE_RESULT_NO : GUI_MESSAGE_RESULT_CONFIRM);
		}
		else
		{
			/* Arrow keys move focus inside the dialog group; the group only
			 * holds the dialog buttons, so focus cannot escape the dialog. */
			lv_group_t *group = lv_obj_get_group(target);

			if (group != NULL)
			{
				if (key == LV_KEY_UP || key == LV_KEY_LEFT)
				{
					lv_group_focus_prev(group);
				}
				else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT)
				{
					lv_group_focus_next(group);
				}
			}
		}
	}
}

static void GUI_Message_DialogScrimEvent(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CLICKED && s_dialog.obj != NULL)
	{
		GUI_Message_DialogClose((s_dialog.kind == (u8)GUI_MESSAGE_KIND_YESNO) ?
		                        GUI_MESSAGE_RESULT_NO : GUI_MESSAGE_RESULT_CONFIRM);
	}
}
