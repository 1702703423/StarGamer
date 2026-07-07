#ifndef __KEY_H__
#define __KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os.h"
#include "sys.h"

#define KEY_OK                  0
#define KEY_ERROR              (-1)

#define KEY_COUNT               8U

typedef enum
{
	KEY_ID_1 = 0,
	KEY_ID_2,
	KEY_ID_3,
	KEY_ID_4,
	KEY_ID_5,
	KEY_ID_6,
	KEY_ID_7,
	KEY_ID_8,
	KEY_ID_MAX
} KEY_Id_t;

#define KEY_MASK_1              (1UL << KEY_ID_1)
#define KEY_MASK_2              (1UL << KEY_ID_2)
#define KEY_MASK_3              (1UL << KEY_ID_3)
#define KEY_MASK_4              (1UL << KEY_ID_4)
#define KEY_MASK_5              (1UL << KEY_ID_5)
#define KEY_MASK_6              (1UL << KEY_ID_6)
#define KEY_MASK_7              (1UL << KEY_ID_7)
#define KEY_MASK_8              (1UL << KEY_ID_8)
#define KEY_MASK_ALL            (KEY_MASK_1 | KEY_MASK_2 | KEY_MASK_3 | KEY_MASK_4 | \
                                 KEY_MASK_5 | KEY_MASK_6 | KEY_MASK_7 | KEY_MASK_8)

/* Default game/LVGL meaning. Adjust these aliases if the PCB silkscreen differs. */
#define KEY_GAME_UP             KEY_MASK_1      /* PB1  */
#define KEY_GAME_DOWN           KEY_MASK_4      /* PA1  */
#define KEY_GAME_LEFT           KEY_MASK_2      /* PC1  */
#define KEY_GAME_RIGHT          KEY_MASK_3      /* PA2  */
#define KEY_GAME_OK             KEY_MASK_5      /* PE2  */
#define KEY_GAME_A              KEY_MASK_6      /* PE3  */
#define KEY_GAME_B              KEY_MASK_8      /* PG14 */
#define KEY_GAME_SELECT         KEY_MASK_7      /* PB7  */
#define KEY_GAME_START          KEY_MASK_5      /* PE2  */

#define KEY_NES_UP              KEY_MASK_1      /* PB1  */
#define KEY_NES_DOWN            KEY_MASK_4      /* PA1  */
#define KEY_NES_LEFT            KEY_MASK_2      /* PC1  */
#define KEY_NES_RIGHT           KEY_MASK_3      /* PA2  */
#define KEY_NES_START           KEY_MASK_5      /* PE2  */
#define KEY_NES_A               KEY_MASK_6      /* PE3  */
#define KEY_NES_B               KEY_MASK_8      /* PG14 */
#define KEY_NES_EXIT            KEY_MASK_7      /* PB7  */

typedef enum
{
	KEY_EVENT_RELEASE = 0,
	KEY_EVENT_PRESS = 1
} KEY_EventType_t;

typedef struct
{
	KEY_Id_t id;
	KEY_EventType_t type;
	u32 mask;
	u32 tick;
} KEY_Event_t;

int KEY_Init(void);
u32 KEY_GetState(void);
u32 KEY_GetRawState(void);
u8 KEY_IsPressed(KEY_Id_t id);
osStatus_t KEY_GetEvent(KEY_Event_t *event, u32 timeout);
osMessageQueueId_t KEY_GetEventQueue(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
