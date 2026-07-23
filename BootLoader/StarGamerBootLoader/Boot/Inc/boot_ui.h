#ifndef BOOT_UI_H
#define BOOT_UI_H

#include "boot_platform.h"

#define BOOT_KEY_UP             (1UL << 0)
#define BOOT_KEY_DOWN           (1UL << 1)
#define BOOT_KEY_OK             (1UL << 2)
#define BOOT_KEY_CANCEL         (1UL << 3)

void Boot_TickInterrupt(void);
u32 Boot_TickMs(void);
void Boot_DelayMs(u32 delay_ms);

void Boot_UI_Init(void);
void Boot_UI_Clear(void);
void Boot_UI_Message(const char *title, const char *line1, const char *line2);
void Boot_UI_Progress(const char *title, const char *detail, u8 percent);
void Boot_UI_Menu(u8 selected);

u32 Boot_KeyRaw(void);
u8 Boot_KeyHeld(u32 key, u32 duration_ms);
u32 Boot_KeyWait(void);

#endif
