#ifndef GUI_FONT_H
#define GUI_FONT_H

#include "GUI_Manage.h"

#ifdef __cplusplus
extern "C" {
#endif

int GUI_Font_Register(void);
int GUI_Font_Show(void);
int GUI_Font_Uninstall(void);
u8 GUI_Font_IsInstalled(void);
const lv_font_t* GUI_Font_GetXbfFont(void);

#ifdef __cplusplus
}
#endif

#endif
