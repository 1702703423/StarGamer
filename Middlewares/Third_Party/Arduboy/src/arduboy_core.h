#ifndef ARDUBOY_CORE_H
#define ARDUBOY_CORE_H

#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARDUBOY_WIDTH              128U
#define ARDUBOY_HEIGHT             64U
#define ARDUBOY_BUFFER_SIZE        (ARDUBOY_WIDTH * ARDUBOY_HEIGHT / 8U)

#define LEFT_BUTTON                (1U << 5)
#define RIGHT_BUTTON               (1U << 6)
#define UP_BUTTON                  (1U << 7)
#define DOWN_BUTTON                (1U << 4)
#define A_BUTTON                   (1U << 3)
#define B_BUTTON                   (1U << 2)

#define ARDUBOY_BLACK              0U
#define ARDUBOY_WHITE              1U
#define ARDUBOY_INVERT             2U

typedef struct ArduboyCore_Platform
{
	void (*paint)(const u8 *image);
	u8 (*buttons)(void);
	u32 (*millis)(void);
	void (*delay)(u32 milliseconds);
	u32 (*random_word)(void);
	u8 (*should_stop)(void);
} ArduboyCore_Platform_t;

void ArduboyCore_SetPlatform(const ArduboyCore_Platform_t *platform);
void ArduboyCore_Paint(const u8 *image);
u8 ArduboyCore_Buttons(void);
u32 ArduboyCore_Millis(void);
void ArduboyCore_Delay(u32 milliseconds);
s32 ArduboyCore_Random(s32 minimum, s32 maximum);
u8 ArduboyCore_ShouldStop(void);

u8 buttonsState(void);
void paintScreen(const u8 *image);
u8 width(void);
u8 height(void);

#ifdef __cplusplus
}
#endif

#endif
