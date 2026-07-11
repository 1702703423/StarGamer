#include "Dino.h"

#include <stdio.h>

#define DINO_X                 0
#define DINO_GROUND_Y          40
#define DINO_TOP_WIDTH         20U
#define DINO_TOP_HEIGHT        18U
#define DINO_LEG_WIDTH         20U
#define DINO_LEG_HEIGHT        5U
#define DINO_CACTUS_Y          40
#define DINO_CACTUS_WIDTH      12U
#define DINO_CACTUS_HEIGHT     24U

// frame counter
static u16 s_frame;

// general use string buffer
static char text[16];

static const u8 cactus_1[] =
// w:  12  h:  24
{ 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xfe, 0x00, 0xc0, 0xc0, 0x80, 0xfe, 0xff, 0xfe, 0x00, 0xff, 0xff, 0xff, 0xff, 0xc0, 0xff, 0xff, 0x7f, 0x01, 0x03, 0x03, 0x83, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00 };

// cloud_1 w:  17  h:  7
static const u8 cloud_1[] =
{ 0x1c, 0x22, 0x22, 0x22, 0x24, 0x10, 0x12, 0x2a, 0x21, 0x41, 0x41, 0x41, 0x42, 0x4a, 0x24, 0x24, 0x18 };
 

static const u8 dino_up[] ={
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0xfe, 0xf6, 0xfe, 0xfe, 0x7e, 0x7e, 0x7e, 0x7e, 0x7c, 0x00, 0x00, 0xfc, 0xf0, 0xe0, 0xc0, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x09, 0x19, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x07, 0x0f, 0xff, 0xbf, 0x1f, 0x0f, 0x1f, 0xff, 0x87, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// w:  30  h:  18
static const u8 dino_tumble[] =
{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0xf8, 0xf0, 0xe0, 0xe0, 0xf0, 0xf0, 0xf8, 0xf8, 0xf8, 0xf8, 0xf0, 0xf0, 0xf0, 0xe0, 0xe0, 0xc0, 0xc0, 0x80, 0xc0, 0xf0, 0xa8, 0xd8, 0xa8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf0, 0x00, 0x00, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x01 };



// top w:  20  h:  18
static const u8 dino_top[] =
{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xfb, 0xff, 0xff, 0xbf, 0xbf, 0xbf, 0x3f, 0x3e, 0x7e, 0xf8, 0xf0, 0xe0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x04, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// leg_0 w:  20  h:  5
static const u8 dino_leg_0[] =
{ 0x00, 0x00, 0x00, 0x00, 0x01, 0x1f, 0x17, 0x03, 0x01, 0x03, 0x1f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// leg_1 w:  20  h:  5
static const u8 dino_leg_1[] =
{ 0x00, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x0b, 0x01, 0x01, 0x03, 0x1f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// leg_2 w:  20  h:  5
static const u8 dino_leg_2[] =
{ 0x00, 0x00, 0x00, 0x00, 0x01, 0x1f, 0x17, 0x03, 0x01, 0x03, 0x0f, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static void dino_intro(void)
{
  s16 i;

  for(i = -8; i < 28; i = (s16)(i + 2))
  {
    if (ArduboyCore_ShouldStop() != 0U)
      return;

    arduboy.clear();
    arduboy.setCursor(46, i);
    arduboy.print("ARDUBOY");
    arduboy.display();
    delay(16);
  }

//  arduboy.tunes.tone(987, 120);
//  delay(120);
//  arduboy.tunes.tone(1318, 400);

  arduboy.setCursor(42, 48);
  arduboy.print("presents");
  arduboy.display();

  delay(2000);

//  arduboy.tunes.tone(1318, 1);
}



// distance ran
static s16 s_distance;
static s16 s_delta;

static s16 s_cloud_y;

static s16 s_jump;
static s16 s_jump_time;

static s16 s_running;
static s16 s_tumble_time;

static s16 s_obstacle_x;

static u8 dino_bitmap_pixel(const u8 *bitmap, u8 width, s16 x, s16 y)
{
  u32 index;

  if (bitmap == NULL || x < 0 || y < 0) return 0U;

  index = ((u32)y / 8U) * (u32)width + (u32)x;
  return ((bitmap[index] & (u8)(1U << ((u32)y & 7U))) != 0U) ? 1U : 0U;
}

static u8 dino_bitmaps_overlap(s16 first_x, s16 first_y,
                               const u8 *first, u8 first_width, u8 first_height,
                               s16 second_x, s16 second_y,
                               const u8 *second, u8 second_width, u8 second_height)
{
  s16 left = (first_x > second_x) ? first_x : second_x;
  s16 top = (first_y > second_y) ? first_y : second_y;
  s16 first_right = (s16)(first_x + (s16)first_width);
  s16 second_right = (s16)(second_x + (s16)second_width);
  s16 first_bottom = (s16)(first_y + (s16)first_height);
  s16 second_bottom = (s16)(second_y + (s16)second_height);
  s16 right = (first_right < second_right) ? first_right : second_right;
  s16 bottom = (first_bottom < second_bottom) ? first_bottom : second_bottom;
  s16 x;
  s16 y;

  if (left >= right || top >= bottom) return 0U;

  for (y = top; y < bottom; y = (s16)(y + 1)) {
    for (x = left; x < right; x = (s16)(x + 1)) {
      if (dino_bitmap_pixel(first, first_width,
                            (s16)(x - first_x), (s16)(y - first_y)) != 0U &&
          dino_bitmap_pixel(second, second_width,
                            (s16)(x - second_x), (s16)(y - second_y)) != 0U) {
        return 1U;
      }
    }
  }

  return 0U;
}

static u8 dino_collision_detected(void)
{
  const u8 *leg_bitmap = dino_leg_0;
  s16 dino_y = (s16)(DINO_GROUND_Y - s_jump);

  if (s_running && !s_jump) {
    leg_bitmap = (((s_frame % 8U) / 4U) != 0U) ? dino_leg_1 : dino_leg_2;
  }

  if (dino_bitmaps_overlap(DINO_X, dino_y,
                           dino_top, DINO_TOP_WIDTH, DINO_TOP_HEIGHT,
                           s_obstacle_x, DINO_CACTUS_Y,
                           cactus_1, DINO_CACTUS_WIDTH, DINO_CACTUS_HEIGHT) != 0U) {
    return 1U;
  }

  return dino_bitmaps_overlap(DINO_X, (s16)(dino_y + (s16)DINO_TOP_HEIGHT),
                              leg_bitmap, DINO_LEG_WIDTH, DINO_LEG_HEIGHT,
                              s_obstacle_x, DINO_CACTUS_Y,
                              cactus_1, DINO_CACTUS_WIDTH, DINO_CACTUS_HEIGHT);
}

void dino_init(void)
{
  arduboy.begin();
  arduboy.setFrameRate(60);
  s_frame = 0U;
  dino_intro();
  if (ArduboyCore_ShouldStop() != 0U)
    return;

  s_distance = 0;
  s_delta = 0;

  s_cloud_y = 2;

  s_jump = 0;
  s_jump_time = 0;

  s_tumble_time = 0;
  s_running = 0;

  s_obstacle_x = 130;
}

void dino_loop(void)
{
  s16 dino_y;

  if (ArduboyCore_ShouldStop() != 0U)
    return;

  if (arduboy.nextFrame() == 0U)
    return;

  if (!s_running && arduboy.pressed(A_BUTTON)) {
    s_running = 1;
  }

  if (s_tumble_time && arduboy.pressed(A_BUTTON)) {
    dino_init();
    return;
  }

  ++s_frame;
  if (s_frame > 16000U) s_frame = 0U;

  // increase distance whilst running
  if (s_running && (++s_delta > 4)) {
    s_delta = 0; ++s_distance;
  }

  // obstacles
  if (s_running) {
    s_obstacle_x = (s16)(s_obstacle_x - (s16)((s_frame % 2U) * (u16)(s_distance / 100) + 2U));
    if (s_obstacle_x < -15) s_obstacle_x = (s16)(s_obstacle_x + 140 + random(0,60));
  }

  // jump!
  if (!s_jump_time && arduboy.pressed(A_BUTTON)) {
    s_jump_time = 1;
    s_jump = 5;

//    arduboy.tunes.tone(440, 40);

  } else if (s_jump_time) {
//    if (d_jump_t == 3) arduboy.tunes.tone(880, 80);

    ++s_jump_time;

    if (s_jump_time<6) {
      s_jump +=6;
    } else if (s_jump_time<9) {
      s_jump +=2;
    } else if (s_jump_time<13) {
      s_jump +=1;
    } else if (s_jump_time == 16 || s_jump_time == 18) {
      s_jump +=1;
    } else if (s_jump_time == 20 || s_jump_time == 22) {
      s_jump -=1;
    } else if (s_jump_time>38) {
      s_jump = 0;
      s_jump_time = 0;
    } else if (s_jump_time>32) {
      s_jump -=6;
    } else if (s_jump_time>29) {
      s_jump -=2;
    } else if (s_jump_time>25) {
      s_jump -=1;
    }
  }

  // Match collision to the visible sprite pixels instead of jump timing.
  if (!s_tumble_time && dino_collision_detected() != 0U) {
    s_tumble_time = 1;
  }

  if (s_tumble_time) {
    if (s_tumble_time == 1) {
//      arduboy.tunes.tone(246, 80);
    } else if (s_tumble_time == 6) {
//      arduboy.tunes.tone(174, 200);
    }

    ++s_tumble_time;
    if (s_jump > -4) {
      s_jump -= 1;
      s_obstacle_x -= 1;
    } else {
      s_running = 0;
    }
  }

  arduboy.clear();


  // hud
  arduboy.setCursor(100, 0);
  (void)snprintf(text, sizeof(text), "%d", (int)s_distance);
  arduboy.print(text);


  // parallax clouds
  arduboy.drawBitmap((s16)(128 - (s_distance % 128)),s_cloud_y,cloud_1,17,7,WHITE);

  if (s_distance % 128 == 0) {
    s_cloud_y = (s16)random(0,10);
  }

  // terrain
  if (s_jump > 4) {
    arduboy.drawLine(0,60,127,60,WHITE);
  } else {
    arduboy.drawLine(0,60,3,60,WHITE);
    arduboy.drawLine(12,60,127,60,WHITE); 
  }

  // obstacles
  arduboy.drawBitmap(s_obstacle_x,DINO_CACTUS_Y,cactus_1,
                     DINO_CACTUS_WIDTH,DINO_CACTUS_HEIGHT,WHITE);

  // dino
  dino_y = (s16)(DINO_GROUND_Y - s_jump);

  // tumbles!
  if (s_tumble_time) {
    arduboy.drawBitmap(0,dino_y,dino_tumble,30,18,WHITE);

  // runs!
  } else {
    arduboy.drawBitmap(DINO_X,dino_y,dino_top,DINO_TOP_WIDTH,DINO_TOP_HEIGHT,WHITE);
  
    // Run, Dino, Run!
    if (s_running && !s_jump) {
      if ((s_frame%8U)/4U) {
        arduboy.drawBitmap(DINO_X,(s16)(dino_y+DINO_TOP_HEIGHT),
                           dino_leg_1,DINO_LEG_WIDTH,DINO_LEG_HEIGHT,WHITE);
      } else {
        arduboy.drawBitmap(DINO_X,(s16)(dino_y+DINO_TOP_HEIGHT),
                           dino_leg_2,DINO_LEG_WIDTH,DINO_LEG_HEIGHT,WHITE);
      }
    } else {
      arduboy.drawBitmap(DINO_X,(s16)(dino_y+DINO_TOP_HEIGHT),
                         dino_leg_0,DINO_LEG_WIDTH,DINO_LEG_HEIGHT,WHITE);
    }
  }

  arduboy.display();
}
