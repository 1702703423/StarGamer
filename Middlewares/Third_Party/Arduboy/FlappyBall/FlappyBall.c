#include "FlappyBall.h"
#include "bitmaps.h"

#include <stdio.h>
#include <string.h>

#define FLAPPY_FRAMES_PER_SECOND     30U
#define FLAPPY_FRAC_BITS             6
#define FLAPPY_BALL_ACCELERATION     16
#define FLAPPY_BALL_JUMP_VELOCITY    (-144)
#define FLAPPY_NEG_OFFSET            64

#define FLAPPY_PIPE_COUNT            4U
#define FLAPPY_PIPE_MOVE_DISTANCE    2
#define FLAPPY_PIPE_GAP_MAX          30U
#define FLAPPY_PIPE_GAP_MIN          18U
#define FLAPPY_PIPE_GAP_REDUCE       5U
#define FLAPPY_PIPE_WIDTH            12
#define FLAPPY_PIPE_CAP_WIDTH        2
#define FLAPPY_PIPE_CAP_HEIGHT       3
#define FLAPPY_PIPE_MIN_HEIGHT       6
#define FLAPPY_PIPE_GEN_FRAMES       32U
#define FLAPPY_PIPE_INACTIVE_X       256

#define FLAPPY_BALL_RADIUS           4
#define FLAPPY_BALL_Y_START          ((s16)(ARDUBOY_HEIGHT / 2U) - 1)
#define FLAPPY_BALL_X                32
#define FLAPPY_RESTART_DELAY_MS      500U

typedef enum
{
	FLAPPY_STATE_WAIT_BEGIN = 0,
	FLAPPY_STATE_PLAYING,
	FLAPPY_STATE_FALLING,
	FLAPPY_STATE_GAME_OVER,
	FLAPPY_STATE_WAIT_RELEASE
} Flappy_State_t;

static Flappy_State_t s_game_state;
static u16 s_game_score;
static u16 s_game_high_score;
static s16 s_pipe_x[FLAPPY_PIPE_COUNT];
static s16 s_pipe_gap_y[FLAPPY_PIPE_COUNT];
static u8 s_pipe_gap;
static u8 s_pipe_reduce_count;
static s16 s_ball_y;
static s16 s_ball_y_previous;
static s16 s_ball_y_initial;
static s32 s_ball_velocity;
static u16 s_ball_frame;
static s16 s_ball_flapper;
static s16 s_score_x;
static s16 s_score_y;
static u8 s_score_riser;
static u32 s_game_over_tick;
static char s_message[32];

static void Flappy_ResetRound(u8 reset_high_score);
static void Flappy_DrawInfo(void);
static void Flappy_DrawFloor(void);
static void Flappy_DrawBall(void);
static void Flappy_DrawPipes(void);
static void Flappy_DrawGameOver(void);
static void Flappy_GeneratePipe(void);
static u8 Flappy_CheckPipe(u8 index);
static u8 Flappy_JumpPressed(void);
static void Flappy_BeginJump(void);
static void Flappy_StartFalling(void);
static void Flappy_MoveBall(void);
static u8 Flappy_WaitForAnyButton(void);
static u8 Flappy_WaitForButtonRelease(void);
static u8 Flappy_GetScoreOffset(u16 score);
static void Flappy_StartGameOver(void);

static void Flappy_CopyNumber(u16 number)
{
	(void)snprintf(s_message, sizeof(s_message), "%u", (unsigned int)number);
}

static void Flappy_ResetRound(u8 reset_high_score)
{
	u8 i;

	s_game_state = FLAPPY_STATE_WAIT_BEGIN;
	s_game_score = 0U;
	if (reset_high_score != 0U)
	{
		s_game_high_score = 0U;
	}

	for (i = 0U; i < FLAPPY_PIPE_COUNT; i++)
	{
		s_pipe_x[i] = FLAPPY_PIPE_INACTIVE_X;
		s_pipe_gap_y[i] = 0;
	}

	s_pipe_gap = FLAPPY_PIPE_GAP_MAX;
	s_pipe_reduce_count = FLAPPY_PIPE_GAP_REDUCE;
	s_ball_y = FLAPPY_BALL_Y_START;
	s_ball_y_previous = FLAPPY_BALL_Y_START;
	s_ball_y_initial = FLAPPY_BALL_Y_START;
	s_ball_velocity = 0;
	s_ball_frame = 0U;
	s_ball_flapper = FLAPPY_BALL_RADIUS;
	s_score_x = 0;
	s_score_y = 0;
	s_score_riser = 0U;
	s_game_over_tick = 0U;
	memset(s_message, 0, sizeof(s_message));
}

static void Flappy_DrawInfo(void)
{
	arduboy.setCursor(6, 3);
	arduboy.print("A/B/Up/Down: Jump");
	arduboy.setCursor(34, 51);
	arduboy.print("Flappy Ball");
}

static void Flappy_DrawFloor(void)
{
	arduboy.drawFastHLine(0, (s16)ARDUBOY_HEIGHT - 1, (u8)ARDUBOY_WIDTH, WHITE);
}

static void Flappy_DrawBall(void)
{
	s_ball_flapper--;
	if (s_ball_flapper < 0)
	{
		s_ball_flapper = FLAPPY_BALL_RADIUS;
	}

	arduboy.drawCircle(FLAPPY_BALL_X, s_ball_y, FLAPPY_BALL_RADIUS, BLACK);
	arduboy.drawCircle(FLAPPY_BALL_X, s_ball_y, FLAPPY_BALL_RADIUS, WHITE);
	arduboy.drawLine(FLAPPY_BALL_X, s_ball_y,
	                 FLAPPY_BALL_X - (FLAPPY_BALL_RADIUS + 1),
	                 (s16)(s_ball_y - s_ball_flapper), WHITE);
	arduboy.drawPixel(FLAPPY_BALL_X - (FLAPPY_BALL_RADIUS + 1),
	                  (s16)(s_ball_y - s_ball_flapper + 1), WHITE);
	arduboy.drawPixel(FLAPPY_BALL_X + 1, (s16)(s_ball_y - 2), WHITE);
}

static void Flappy_DrawPipes(void)
{
	u8 i;

	for (i = 0U; i < FLAPPY_PIPE_COUNT; i++)
	{
		if (s_pipe_x[i] == FLAPPY_PIPE_INACTIVE_X)
		{
			continue;
		}

		arduboy.drawRect(s_pipe_x[i], -1, FLAPPY_PIPE_WIDTH, (u8)s_pipe_gap_y[i], WHITE);
		arduboy.drawRect(s_pipe_x[i], (s16)(s_pipe_gap_y[i] + s_pipe_gap), FLAPPY_PIPE_WIDTH,
		                 (u8)((s16)ARDUBOY_HEIGHT - s_pipe_gap_y[i] - s_pipe_gap), WHITE);
		arduboy.drawRect((s16)(s_pipe_x[i] - FLAPPY_PIPE_CAP_WIDTH),
		                 (s16)(s_pipe_gap_y[i] - FLAPPY_PIPE_CAP_HEIGHT),
		                 FLAPPY_PIPE_WIDTH + FLAPPY_PIPE_CAP_WIDTH * 2U,
		                 FLAPPY_PIPE_CAP_HEIGHT, WHITE);
		arduboy.drawRect((s16)(s_pipe_x[i] - FLAPPY_PIPE_CAP_WIDTH),
		                 (s16)(s_pipe_gap_y[i] + s_pipe_gap),
		                 FLAPPY_PIPE_WIDTH + FLAPPY_PIPE_CAP_WIDTH * 2U,
		                 FLAPPY_PIPE_CAP_HEIGHT, WHITE);
		arduboy.drawLine((s16)(s_pipe_x[i] + 2), 0,
		                 (s16)(s_pipe_x[i] + 2), (s16)(s_pipe_gap_y[i] - 5), WHITE);
		arduboy.drawLine((s16)(s_pipe_x[i] + 2), (s16)(s_pipe_gap_y[i] + s_pipe_gap + 5),
		                 (s16)(s_pipe_x[i] + 2), (s16)ARDUBOY_HEIGHT - 3, WHITE);
	}
}

static void Flappy_DrawGameOver(void)
{
	arduboy.drawRect(16, 8, 96U, 48U, WHITE);
	arduboy.fillRect(17, 9, 94U, 46U, BLACK);
	arduboy.drawSlowXYBitmap(30, 12, gameover, 72U, 14U, WHITE);

	arduboy.setCursor((s16)(50 - Flappy_GetScoreOffset(s_game_score)), 30);
	Flappy_CopyNumber(s_game_score);
	arduboy.print(s_message);
	arduboy.setCursor(62, 30);
	arduboy.print("Score");

	arduboy.setCursor((s16)(50 - Flappy_GetScoreOffset(s_game_high_score)), 42);
	Flappy_CopyNumber(s_game_high_score);
	arduboy.print(s_message);
	arduboy.setCursor(62, 42);
	arduboy.print("High");
}

static void Flappy_GeneratePipe(void)
{
	u8 i;
	s16 maximum;

	for (i = 0U; i < FLAPPY_PIPE_COUNT; i++)
	{
		if (s_pipe_x[i] == FLAPPY_PIPE_INACTIVE_X)
		{
			maximum = (s16)ARDUBOY_HEIGHT - FLAPPY_PIPE_MIN_HEIGHT - s_pipe_gap;
			s_pipe_x[i] = (s16)ARDUBOY_WIDTH;
			s_pipe_gap_y[i] = (s16)random(FLAPPY_PIPE_MIN_HEIGHT, maximum);
			return;
		}
	}
}

static u8 Flappy_RectanglesOverlap(s16 ax1, s16 ay1, s16 ax2, s16 ay2,
	                               s16 bx1, s16 by1, s16 bx2, s16 by2)
{
	return (ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1) ? 1U : 0U;
}

static u8 Flappy_CheckPipe(u8 index)
{
	s16 ax1 = FLAPPY_BALL_X - (FLAPPY_BALL_RADIUS - 1);
	s16 ax2 = FLAPPY_BALL_X + (FLAPPY_BALL_RADIUS - 1);
	s16 ay1 = s_ball_y - (FLAPPY_BALL_RADIUS - 1);
	s16 ay2 = s_ball_y + (FLAPPY_BALL_RADIUS - 1);
	s16 pipe_x1 = s_pipe_x[index];
	s16 pipe_x2 = (s16)(pipe_x1 + FLAPPY_PIPE_WIDTH);
	s16 cap_x1 = (s16)(pipe_x1 - FLAPPY_PIPE_CAP_WIDTH);
	s16 cap_x2 = (s16)(pipe_x2 + FLAPPY_PIPE_CAP_WIDTH);
	s16 gap_top = s_pipe_gap_y[index];
	s16 gap_bottom = (s16)(gap_top + s_pipe_gap);

	if (Flappy_RectanglesOverlap(ax1, ay1, ax2, ay2, pipe_x1, 0, pipe_x2, gap_top) != 0U)
	{
		return 1U;
	}
	if (Flappy_RectanglesOverlap(ax1, ay1, ax2, ay2, cap_x1,
	                            (s16)(gap_top - FLAPPY_PIPE_CAP_HEIGHT), cap_x2, gap_top) != 0U)
	{
		return 1U;
	}
	if (Flappy_RectanglesOverlap(ax1, ay1, ax2, ay2, pipe_x1, gap_bottom,
	                            pipe_x2, (s16)ARDUBOY_HEIGHT - 1) != 0U)
	{
		return 1U;
	}
	return Flappy_RectanglesOverlap(ax1, ay1, ax2, ay2, cap_x1, gap_bottom,
	                                cap_x2, (s16)(gap_bottom + FLAPPY_PIPE_CAP_HEIGHT));
}

static u8 Flappy_JumpPressed(void)
{
	return ((buttonsState() & (UP_BUTTON | DOWN_BUTTON | A_BUTTON | B_BUTTON)) != 0U) ? 1U : 0U;
}

static void Flappy_BeginJump(void)
{
	s_ball_velocity = FLAPPY_BALL_JUMP_VELOCITY;
	s_ball_frame = 0U;
	s_ball_y_initial = s_ball_y;
}

static void Flappy_StartFalling(void)
{
	s_ball_frame = 0U;
	s_ball_y_initial = s_ball_y;
	s_ball_velocity = 0;
}

static void Flappy_MoveBall(void)
{
	s32 distance;

	s_ball_y_previous = s_ball_y;
	s_ball_frame++;
	s_ball_velocity += FLAPPY_BALL_ACCELERATION / 2;
	distance = s_ball_velocity * (s32)s_ball_frame;
	distance += ((s32)FLAPPY_NEG_OFFSET << FLAPPY_FRAC_BITS);
	distance += (1L << (FLAPPY_FRAC_BITS - 1));
	distance >>= FLAPPY_FRAC_BITS;
	distance -= FLAPPY_NEG_OFFSET;
	s_ball_y = (s16)(distance + s_ball_y_initial);
}

static u8 Flappy_WaitForAnyButton(void)
{
	while (buttonsState() == 0U)
	{
		if (ArduboyCore_ShouldStop() != 0U)
		{
			return 0U;
		}
		delay(10U);
	}
	return 1U;
}

static u8 Flappy_WaitForButtonRelease(void)
{
	while (buttonsState() != 0U)
	{
		if (ArduboyCore_ShouldStop() != 0U)
		{
			return 0U;
		}
		delay(10U);
	}
	return 1U;
}

static u8 Flappy_GetScoreOffset(u16 score)
{
	if (score > 9999U) return 24U;
	if (score > 999U) return 18U;
	if (score > 99U) return 12U;
	if (score > 9U) return 6U;
	return 0U;
}

static void Flappy_StartGameOver(void)
{
	if (s_game_score > s_game_high_score)
	{
		s_game_high_score = s_game_score;
	}
	Flappy_StartFalling();
	s_game_state = FLAPPY_STATE_FALLING;
}

void flappyball_init(void)
{
	arduboy.begin();
	arduboy.setFrameRate(FLAPPY_FRAMES_PER_SECOND);
	Flappy_ResetRound(1U);

	arduboy.clear();
	arduboy.drawSlowXYBitmap(0, 0, floatyball, 128U, 64U, WHITE);
	arduboy.display();
	delay(300U);
	if (ArduboyCore_ShouldStop() != 0U)
	{
		return;
	}

	arduboy.setCursor(18, 55);
	arduboy.print("Press Any Button");
	arduboy.display();
	if (Flappy_WaitForAnyButton() == 0U || Flappy_WaitForButtonRelease() == 0U)
	{
		return;
	}

	arduboy.initRandomSeed();
}

void flappyball_loop(void)
{
	u8 i;

	if (ArduboyCore_ShouldStop() != 0U || arduboy.nextFrame() == 0U)
	{
		return;
	}

	arduboy.clear();

	if (s_game_state == FLAPPY_STATE_WAIT_BEGIN)
	{
		Flappy_DrawFloor();
		Flappy_DrawBall();
		Flappy_DrawInfo();
		if (Flappy_JumpPressed() != 0U)
		{
			s_game_state = FLAPPY_STATE_PLAYING;
			Flappy_BeginJump();
		}
	}
	else if (s_game_state == FLAPPY_STATE_PLAYING)
	{
		if (s_ball_y_previous <= s_ball_y && Flappy_JumpPressed() != 0U)
		{
			Flappy_BeginJump();
		}

		Flappy_MoveBall();
		if (s_ball_y < FLAPPY_BALL_RADIUS)
		{
			s_ball_y = FLAPPY_BALL_RADIUS;
			Flappy_StartFalling();
		}

		if (arduboy.everyXFrames(FLAPPY_PIPE_GEN_FRAMES) != 0U)
		{
			Flappy_GeneratePipe();
		}

		for (i = 0U; i < FLAPPY_PIPE_COUNT; i++)
		{
			if (s_pipe_x[i] == FLAPPY_PIPE_INACTIVE_X)
			{
				continue;
			}

			s_pipe_x[i] = (s16)(s_pipe_x[i] - FLAPPY_PIPE_MOVE_DISTANCE);
			if (s_pipe_x[i] + FLAPPY_PIPE_WIDTH < 0)
			{
				s_pipe_x[i] = FLAPPY_PIPE_INACTIVE_X;
				continue;
			}
			if (s_pipe_x[i] + FLAPPY_PIPE_WIDTH == FLAPPY_BALL_X - FLAPPY_BALL_RADIUS)
			{
				s_game_score++;
				if (s_pipe_reduce_count > 0U) s_pipe_reduce_count--;
				s_score_x = FLAPPY_BALL_X;
				s_score_y = (s16)(s_ball_y - FLAPPY_BALL_RADIUS - 8);
				s_score_riser = 15U;
			}
		}

		if (s_score_riser > 0U)
		{
			s_score_y--;
			if (s_score_y >= 0)
			{
				arduboy.setCursor(s_score_x, s_score_y);
				Flappy_CopyNumber(s_game_score);
				arduboy.print(s_message);
				s_score_x = (s16)(s_score_x - 2);
				s_score_riser--;
			}
			else
			{
				s_score_riser = 0U;
			}
		}

		if (s_ball_y + FLAPPY_BALL_RADIUS > (s16)ARDUBOY_HEIGHT - 1)
		{
			s_ball_y = (s16)ARDUBOY_HEIGHT - 1 - FLAPPY_BALL_RADIUS;
			Flappy_StartGameOver();
		}
		else
		{
			for (i = 0U; i < FLAPPY_PIPE_COUNT; i++)
			{
				if (s_pipe_x[i] != FLAPPY_PIPE_INACTIVE_X && Flappy_CheckPipe(i) != 0U)
				{
					Flappy_StartGameOver();
					break;
				}
			}
		}

		Flappy_DrawPipes();
		Flappy_DrawFloor();
		Flappy_DrawBall();
		if (s_pipe_gap > FLAPPY_PIPE_GAP_MIN && s_pipe_reduce_count == 0U)
		{
			s_pipe_gap--;
			s_pipe_reduce_count = FLAPPY_PIPE_GAP_REDUCE;
		}
	}
	else if (s_game_state == FLAPPY_STATE_FALLING)
	{
		if (s_ball_y + FLAPPY_BALL_RADIUS < (s16)ARDUBOY_HEIGHT - 1)
		{
			Flappy_MoveBall();
		}
		if (s_ball_y + FLAPPY_BALL_RADIUS >= (s16)ARDUBOY_HEIGHT - 1)
		{
			s_ball_y = (s16)ARDUBOY_HEIGHT - 1 - FLAPPY_BALL_RADIUS;
			s_game_state = FLAPPY_STATE_GAME_OVER;
			s_game_over_tick = millis();
		}
		Flappy_DrawPipes();
		Flappy_DrawFloor();
		Flappy_DrawBall();
		if (s_game_state == FLAPPY_STATE_GAME_OVER)
		{
			Flappy_DrawGameOver();
		}
	}
	else if (s_game_state == FLAPPY_STATE_GAME_OVER)
	{
		Flappy_DrawPipes();
		Flappy_DrawFloor();
		Flappy_DrawBall();
		Flappy_DrawGameOver();
		if ((u32)(millis() - s_game_over_tick) >= FLAPPY_RESTART_DELAY_MS && Flappy_JumpPressed() != 0U)
		{
			s_game_state = FLAPPY_STATE_WAIT_RELEASE;
		}
	}
	else
	{
		Flappy_DrawPipes();
		Flappy_DrawFloor();
		Flappy_DrawBall();
		Flappy_DrawGameOver();
		if (buttonsState() == 0U)
		{
			Flappy_ResetRound(0U);
		}
	}

	arduboy.display();
}
