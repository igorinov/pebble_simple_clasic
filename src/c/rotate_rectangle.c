#include <pebble.h>

#include "rotate_rectangle.h"

// blend two ARGB8 colors (alpha = 0 .. 65536)
static uint8_t blend(int32_t alpha, uint8_t c0, uint8_t c1)
{
  uint32_t beta = (1 << 16) - alpha;
  uint32_t r0 = (c0 & 0x30) >> 4;
  uint32_t g0 = (c0 & 0x0C) >> 2;
  uint32_t b0 = (c0 & 0x03);
  uint32_t r1 = (c1 & 0x30) >> 4;
  uint32_t g1 = (c1 & 0x0C) >> 2;
  uint32_t b1 = (c1 & 0x03);
  uint32_t x;
  uint8_t c = 3;

  c <<= 2;
  x = r1 * alpha + r0 * beta + (1 << 15);
  x >>= 16;
  c |= x;
  c <<= 2;
  x = g1 * alpha + g0 * beta + (1 << 15);
  x >>= 16;
  c |= x;
  c <<= 2;
  x = b1 * alpha + b0 * beta + (1 << 15);
  x >>= 16;
  c |= x;
  
  return c;
}

int rotate_rectangle(GContext *ctx, GPoint pt1, GPoint pt2, GPoint center, int32_t angle, uint8_t color)
{
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  int32_t sin_a = sin_lookup(angle);
  int32_t cos_a = cos_lookup(angle);
  int32_t d_min;
  uint8_t c;
  int32_t x, y;
  int32_t x_min = 32767, x_max = -32767;
  int32_t y_min = 32767, y_max = -32767;
  int16_t i, j;
  int16_t j_min, j_max;

  x = pt1.x * cos_a - pt1.y * sin_a;
  y = pt1.y * cos_a + pt1.x * sin_a;
  if (x < x_min)
    x_min = x;
  if (x > x_max)
    x_max = x;
  if (y < y_min)
    y_min = y;
  if (y > y_max)
    y_max = y;

  x = pt2.x * cos_a - pt2.y * sin_a;
  y = pt2.y * cos_a + pt2.x * sin_a;
  if (x < x_min)
    x_min = x;
  if (x > x_max)
    x_max = x;
  if (y < y_min)
    y_min = y;
  if (y > y_max)
    y_max = y;

  x = pt1.x * cos_a - pt2.y * sin_a;
  y = pt2.y * cos_a + pt1.x * sin_a;
  if (x < x_min)
    x_min = x;
  if (x > x_max)
    x_max = x;
  if (y < y_min)
    y_min = y;
  if (y > y_max)
    y_max = y;

  x = pt2.x * cos_a - pt1.y * sin_a;
  y = pt1.y * cos_a + pt2.x * sin_a;
  if (x < x_min)
    x_min = x;
  if (x > x_max)
    x_max = x;
  if (y < y_min)
    y_min = y;
  if (y > y_max)
    y_max = y;

  x_min >>= 16;
  x_min += center.x;
  x_max += 0x7FFF;
  x_max >>= 16;
  x_max += center.x;

  y_min >>= 16;
  y_min += center.y;
  y_max += 0x7FFF;
  y_max >>= 16;
  y_max += center.y;

  // Iterate over all rows
  for (y = y_min; y <= y_max; y += 1) {
    // Get this row's range and data
    GBitmapDataRowInfo info = gbitmap_get_data_row_info(fb, y);
    if (info.data == NULL)
      continue;

    j_min = x_min;
    j_max = x_max;

    if (j_min < info.min_x)
      j_min = info.min_x;
    if (j_max > info.max_x)
      j_max = info.max_x;

    // Iterate over all visible columns
    for (j = j_min; j <= j_max; j += 1) {
      int32_t dx = j - center.x;
      int32_t dy = y - center.y;
      
      // Map current pixel into rotating needle's plane
      int32_t rx = dx * cos_a + dy * sin_a;
      int32_t ry = dy * cos_a - dx * sin_a;

      int32_t dx1 = rx - pt1.x * TRIG_MAX_RATIO;
      int32_t dx2 = pt2.x  * TRIG_MAX_RATIO - rx;
      int32_t dy1 = ry - pt1.y * TRIG_MAX_RATIO;
      int32_t dy2 = pt2.y * TRIG_MAX_RATIO - ry;

      // Out of the rectangle area
      if (dx1 < 0 || dx2 < 0 || dy1 < 0 || dy2 < 0)
        continue;

      d_min = TRIG_MAX_RATIO + 1;
      if (dx1 < d_min)
        d_min = dx1;
      if (dx2 < d_min)
        d_min = dx2;
      if (dy1 < d_min)
        d_min = dy1;
      if (dy2 < d_min)
        d_min = dy2;
      
      // Outline
/*
      if (ld >= 0) {
        if  (ld < L)
          c = GColorWhiteARGB8;
        else {
          alpha = ratio_mul(ld - L, RL);
          c = blend(alpha, GColorWhiteARGB8, info.data[x]);
        }
        info.data[j] = c;
        continue;
      }
*/      
      if (d_min <= TRIG_MAX_RATIO) {
        // On the border
        c = info.data[j];
        c = blend(d_min, c, color);
      } else {
        // Deep inside
        c = color;
      }
      info.data[j] = c;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
  
  return 0;
}
