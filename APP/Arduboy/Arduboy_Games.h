#ifndef APP_ARDUBOY_GAMES_H
#define APP_ARDUBOY_GAMES_H

#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Arduboy_GameFunction_t)(void);

typedef struct Arduboy_Game
{
	const char *id;
	const char *name;
	Arduboy_GameFunction_t init;
	Arduboy_GameFunction_t loop;
	u8 diagnostic;
} Arduboy_Game_t;

u8 Arduboy_Games_Count(void);
const Arduboy_Game_t *Arduboy_Games_Get(u8 index);

#ifdef __cplusplus
}
#endif

#endif
