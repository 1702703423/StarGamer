#ifndef APP_ARDUBOY_RUNTIME_H
#define APP_ARDUBOY_RUNTIME_H

#include "Arduboy_Games.h"
#include "Arduboy_Port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	ARDUBOY_RUNTIME_IDLE = 0,
	ARDUBOY_RUNTIME_STARTING,
	ARDUBOY_RUNTIME_RUNNING,
	ARDUBOY_RUNTIME_STOPPING,
	ARDUBOY_RUNTIME_FINISHED,
	ARDUBOY_RUNTIME_ERROR
} Arduboy_RuntimeState_t;

typedef enum
{
	ARDUBOY_RUNTIME_RESULT_NONE = 0,
	ARDUBOY_RUNTIME_RESULT_EXITED,
	ARDUBOY_RUNTIME_RESULT_INVALID_GAME,
	ARDUBOY_RUNTIME_RESULT_CORE_TEST_FAILED,
	ARDUBOY_RUNTIME_RESULT_STACK_FAILED,
	ARDUBOY_RUNTIME_RESULT_THREAD_FAILED,
	ARDUBOY_RUNTIME_RESULT_DISPLAY_BUSY,
	ARDUBOY_RUNTIME_RESULT_FRAMEBUFFER_FAILED,
	ARDUBOY_RUNTIME_RESULT_DISPLAY_RESUME_FAILED
} Arduboy_RuntimeResult_t;

int Arduboy_Runtime_Start(u8 game_index);
void Arduboy_Runtime_RequestStop(void);
void Arduboy_Runtime_Cleanup(void);
Arduboy_RuntimeState_t Arduboy_Runtime_GetState(void);
Arduboy_RuntimeResult_t Arduboy_Runtime_GetResult(void);
u8 Arduboy_Runtime_GetGameIndex(void);
u8 Arduboy_Runtime_IsActive(void);
u32 Arduboy_Runtime_GetStackSpace(void);
const char *Arduboy_Runtime_ResultText(Arduboy_RuntimeResult_t result);

#ifdef __cplusplus
}
#endif

#endif
