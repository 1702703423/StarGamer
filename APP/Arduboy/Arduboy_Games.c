#include "Arduboy_Games.h"

#include "ArduboyButtons.h"
#include "Dino.h"
#include "FlappyBall.h"

static const Arduboy_Game_t s_games[] =
{
	{"dino", "Dino", dino_init, dino_loop, 0U},
	{"flappy-ball", "Flappy Ball", flappyball_init, flappyball_loop, 0U},
	{"buttons-test", "Buttons Test", arduboy_btn_init, arduboy_btn_loop, 1U},
};

u8 Arduboy_Games_Count(void)
{
	return (u8)(sizeof(s_games) / sizeof(s_games[0]));
}

const Arduboy_Game_t *Arduboy_Games_Get(u8 index)
{
	if (index >= Arduboy_Games_Count())
	{
		return NULL;
	}

	return &s_games[index];
}
