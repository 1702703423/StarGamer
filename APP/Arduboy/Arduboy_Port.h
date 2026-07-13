#ifndef APP_ARDUBOY_PORT_H
#define APP_ARDUBOY_PORT_H

#include "arduboy_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARDUBOY_PORT_SCALE             2U
#define ARDUBOY_PORT_OUTPUT_WIDTH      (ARDUBOY_WIDTH * ARDUBOY_PORT_SCALE)
#define ARDUBOY_PORT_OUTPUT_HEIGHT     (ARDUBOY_HEIGHT * ARDUBOY_PORT_SCALE)
#define ARDUBOY_PORT_OUTPUT_X          32U
#define ARDUBOY_PORT_OUTPUT_Y          56U
#define ARDUBOY_PORT_RGB_BUFFER_SIZE   (ARDUBOY_PORT_OUTPUT_WIDTH * ARDUBOY_PORT_OUTPUT_HEIGHT * 2U)

typedef struct Arduboy_PortStats
{
	u32 frame_count;
	u32 last_convert_ms;
	u32 last_flush_ms;
	u32 max_convert_ms;
	u32 max_flush_ms;
	u8 max_cpu_load;
} Arduboy_PortStats_t;

int Arduboy_Port_Begin(void);
void Arduboy_Port_End(void);
void Arduboy_Port_ResetStop(void);
void Arduboy_Port_RequestStop(void);
u8 Arduboy_Port_ShouldStop(void);
void Arduboy_Port_WaitForExitRelease(void);
void Arduboy_Port_RecordCpuLoad(u8 cpu_load);
void Arduboy_Port_GetStats(Arduboy_PortStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
