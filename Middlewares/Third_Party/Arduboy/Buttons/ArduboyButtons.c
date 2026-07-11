#include "ArduboyButtons.h"

static const char s_text[] = "Press Buttons!";
static u8 s_x;
static u8 s_y;

#define CHAR_WIDTH 6
#define CHAR_HEIGHT 8

#define NUM_CHARS (sizeof(s_text) - 1U)
#define X_MAX (WIDTH - (NUM_CHARS * CHAR_WIDTH) + 1)

#define Y_MAX (HEIGHT - CHAR_HEIGHT)

void arduboy_btn_init(void)
{
  arduboy.begin();
  arduboy.setFrameRate(30);

  s_x = (u8)((arduboy.width() / 2U) - (NUM_CHARS * CHAR_WIDTH / 2U));
  s_y = (u8)((arduboy.height() / 2U) - (CHAR_HEIGHT / 2U));
}

void arduboy_btn_loop(void)
{
  if (ArduboyCore_ShouldStop() != 0U)
    return;

  if (!(arduboy.nextFrame()))
    return;
  if(arduboy.pressed(RIGHT_BUTTON) && (s_x < X_MAX)) {
    s_x++;
  }
  if(arduboy.pressed(LEFT_BUTTON) && (s_x > 0U)) {
    s_x--;
  }
  if((arduboy.pressed(UP_BUTTON) || arduboy.pressed(B_BUTTON)) && (s_y > 0U)) {
    s_y--;
  }
  if((arduboy.pressed(DOWN_BUTTON) || arduboy.pressed(A_BUTTON)) && (s_y < Y_MAX)) {
    s_y++;
  }
  arduboy.clear();
  arduboy.setCursor(s_x, s_y);
  arduboy.print(s_text);
  arduboy.display();
}
