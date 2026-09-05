#ifndef GUI_MESSAGE_H
#define GUI_MESSAGE_H

/*
 * Star Gamer GUI message box service.
 *
 * Global overlay boxes rendered on the LVGL top layer, independent of the
 * active page:
 *   - GUI_MESSAGE_KIND_TOAST   : rounded notification that slides down from
 *                                the screen top edge (linear animation) and
 *                                auto-dismisses after the duration configured
 *                                in GUI_Settings (ToastDuration setting).
 *   - GUI_MESSAGE_KIND_CONFIRM : centered modal dialog with a single
 *                                Confirm button.
 *   - GUI_MESSAGE_KIND_YESNO   : centered modal dialog with Yes / No buttons.
 *
 * Screen ownership rules:
 *   - The service may only be used while the LVGL task owns the display.
 *     While another thread owns the screen (video playback, NES, ...) every
 *     entry point fails with OPERATE_FAILED - see lvgl_ui_is_active().
 *   - GUI_Message_* entry points are thread-safe. Requests are queued in a
 *     single-slot mailbox and executed by an LVGL timer, so all widget and
 *     callback code always runs in the LVGL thread. If LVGL loses the screen
 *     between enqueue and execution the request is dropped and the callback
 *     (if any) fires once with GUI_MESSAGE_RESULT_NONE.
 *   - GUI_Message_Init() must be called once from the LVGL thread during GUI
 *     startup; the flush timer is also created lazily on LVGL-thread calls.
 *
 * Only one centered dialog may exist at a time; a second request while one
 * is open fails with OPERATE_FAILED. A new toast replaces the previous one.
 */

#include "sys.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Requested strings are truncated to these limits (bytes, UTF-8). */
#define GUI_MESSAGE_TITLE_MAX   32U
#define GUI_MESSAGE_TEXT_MAX    96U

typedef enum
{
	GUI_MESSAGE_KIND_TOAST = 0,
	GUI_MESSAGE_KIND_CONFIRM,
	GUI_MESSAGE_KIND_YESNO
} GUI_Message_Kind_t;

typedef enum
{
	GUI_MESSAGE_RESULT_NONE = 0,	/* request dropped / no user action yet */
	GUI_MESSAGE_RESULT_CONFIRM,		/* Confirm dialog acknowledged (ESC counts too) */
	GUI_MESSAGE_RESULT_YES,
	GUI_MESSAGE_RESULT_NO,			/* No button, scrim click or ESC on Yes/No dialog */
	GUI_MESSAGE_RESULT_TIMEOUT		/* toast finished its configured display time */
} GUI_Message_Result_t;

typedef void (*GUI_Message_Callback_t)(GUI_Message_Result_t result, void *user_data);

/* Create the request flush timer. Must run in the LVGL thread. */
int GUI_Message_Init(void);

/* 1 while the LVGL task owns the display and message boxes may be shown. */
u8 GUI_Message_ScreenAvailable(void);

/* Generic entry point; title may be NULL. Returns OPERATE_FAILED when the
 * screen is owned by another thread, the mailbox is busy or a dialog is
 * already open (centered dialogs only). */
int GUI_Message_Show(GUI_Message_Kind_t kind, const char *title, const char *text,
                     GUI_Message_Callback_t callback, void *user_data);

int GUI_Message_Toast(const char *text, GUI_Message_Callback_t callback, void *user_data);
int GUI_Message_Confirm(const char *text, GUI_Message_Callback_t callback, void *user_data);
int GUI_Message_YesNo(const char *text, GUI_Message_Callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif
