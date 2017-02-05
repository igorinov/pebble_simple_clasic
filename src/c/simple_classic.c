#include "pebble.h"
#include "simple_classic.h"

static Window *s_window;
static Layer *s_back_layer, *s_date_layer, *s_hand_layer, *s_batt_layer;

static char s_date_buffer[16] = "";
static char s_wday_buffer[16] = "";
static int g_wday = -1;
static BatteryChargeState g_charge;

static GFont s_font;

static struct color_map charge_colors[] = {
    { 100, 0x00FF55 },
    {  90, 0x00FF00 },
    {  80, 0x55FF00 },
    {  70, 0xAAFF00 },
    {  50, 0xFFFF00 },
    {  30, 0xFFAA00 },
    {  20, 0xFF5500 },
    {   0, 0xFF0000 },
    {  -1, 0xFFFFFF }
};

// get battery charge indicator color
GColor get_charging_color(int percent)
{
  struct color_map *cm = charge_colors;
  GColor c;

  if (percent < 0)
    percent = 0;
  
  do {
    c = GColorFromHEX(cm->color);
    cm += 1;
  } while (percent <= cm->key);

  return c;
}

// convert angle in minutes (1° = 60 min) to lookup table index
uint32_t minute_index(int16_t m)
{
  return (TRIG_MAX_ANGLE * m + 10800) / 21600;
}

// round fixed point number to nearest integer
int16_t fixed_round(uint32_t a)
{
  return (int16_t)((a + TRIG_MAX_RATIO / 2) / TRIG_MAX_RATIO);
}

static void back_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  graphics_context_set_antialiased(ctx, true);
                                   
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWindsorTan);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  const int32_t tick_span = 66;

  for (int i = 0; i < 12; i += 1) {
    int32_t tick_angle = TRIG_MAX_ANGLE * i / 12;
    GPoint tick_center = {
      .x = center.x + fixed_round(sin_lookup(tick_angle) * tick_span),
      .y = center.y - fixed_round(cos_lookup(tick_angle) * tick_span),
    };
    graphics_fill_circle(ctx, tick_center, 3);
    graphics_draw_circle(ctx, tick_center, 3);
  }
}

static void batt_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  GRect charge_frame = {
#ifdef PBL_ROUND
    .origin = { center.x - 26, center.x + 74 },
    .size = { 52, 8 },
#else
    .origin = { center.x - 51, bounds.size.h - 8},
    .size = { 102, 8 },
#endif
  };

  GRect charge_level = {
#ifdef PBL_ROUND
    .origin = { center.x - 25, center.y + 75 },
    .size = { g_charge.charge_percent / 2, 6 },
#else
    .origin = { center.x - 50, bounds.size.h - 7 },
    .size = { g_charge.charge_percent, 6 },
#endif
  };

  GColor c = get_charging_color(g_charge.charge_percent);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, charge_level, 0, GCornerNone);
  graphics_draw_rect(ctx, charge_frame);
}

static void date_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

#ifdef PBL_ROUND
  GRect date_rect = GRect(center.x - 35, center.y - 85, 64, 24);
  GRect wday_rect = GRect(center.x + 40, center.y - 57, 32, 24);
#else
  GRect date_rect = GRect(0, 0, 64, 24);
  GRect wday_rect = GRect(80, 0, 64, 24);
#endif
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%b %d", t);
  strftime(s_wday_buffer, sizeof(s_wday_buffer), "%a", t);

  graphics_context_set_text_color(ctx, GColorMediumAquamarine);
  graphics_draw_text(ctx, s_date_buffer, s_font, date_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, s_wday_buffer, s_font, wday_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  
  g_wday = t->tm_wday;
}

static void hand_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  const int32_t hour_tail_length = 14;
  const int32_t minute_tail_length = 20;
  const int32_t second_tail_length = 22;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int32_t hour_angle = minute_index(((((t->tm_hour % 12) * 60 + t->tm_min) * 60) + t->tm_sec) / 2);
  int32_t minute_angle = minute_index((t->tm_min * 60 + t->tm_sec) * 6);
  int32_t second_angle = minute_index(t->tm_sec * 360);

  int16_t hx = fixed_round(sin_lookup(hour_angle) * hour_tail_length);
  int16_t hy = fixed_round(cos_lookup(hour_angle) * (int32_t) hour_tail_length);
  GPoint hour_head = { center.x + hx * 3, center.y - hy * 3 };
  GPoint hour_tail = { center.x - hx, center.y + hy };

  int16_t mx = fixed_round(sin_lookup(minute_angle) * (int32_t) minute_tail_length);
  int16_t my = fixed_round(cos_lookup(minute_angle) * (int32_t) minute_tail_length);
  GPoint minute_head = { center.x + mx * 3, center.y - my * 3 };
  GPoint minute_tail = { center.x - mx, center.y + my };

  int16_t sx = fixed_round(sin_lookup(second_angle) * (int32_t) second_tail_length);
  int16_t sy = fixed_round(cos_lookup(second_angle) * (int32_t) second_tail_length);
  GPoint second_head = { center.x + sx * 3, center.y - sy * 3 };
  GPoint second_tail = { center.x - sx, center.y + sy };

  // hour hand
  graphics_context_set_stroke_color(ctx, GColorBrass);
  graphics_context_set_stroke_width(ctx, 6);
  graphics_draw_line(ctx, hour_tail, hour_head);
  graphics_context_set_stroke_color(ctx, GColorLimerick);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, hour_tail, hour_head);

  // minute hand
  graphics_context_set_stroke_color(ctx, GColorPastelYellow);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, minute_tail, minute_head);

  // second hand
  graphics_context_set_fill_color(ctx, GColorRajah);
  graphics_context_set_stroke_color(ctx, GColorRajah);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_line(ctx, second_tail, center);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, center, second_head);

  // dot in the middle
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, GColorWindsorTan);
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
  graphics_fill_circle(ctx, center, 5);
  
  if (t->tm_wday != g_wday)
    layer_mark_dirty(s_date_layer);
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(window_get_root_layer(s_window));
}

static void charge_handler(BatteryChargeState charge)
{
  g_charge = charge;
  layer_mark_dirty(s_batt_layer);
}

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_back_layer = layer_create(bounds);
  layer_set_update_proc(s_back_layer, back_update_proc);
  layer_add_child(window_layer, s_back_layer);

  s_hand_layer = layer_create(bounds);
  layer_set_update_proc(s_hand_layer, hand_update_proc);
  layer_add_child(window_layer, s_hand_layer);

  s_date_layer = layer_create(bounds);
  layer_set_update_proc(s_date_layer, date_update_proc);
  layer_add_child(window_layer, s_date_layer);

  s_batt_layer = layer_create(bounds);
  layer_set_update_proc(s_batt_layer, batt_update_proc);
  layer_add_child(window_layer, s_batt_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_back_layer);
  layer_destroy(s_date_layer);
  layer_destroy(s_hand_layer);
  layer_destroy(s_batt_layer);
}

static void init() {
  s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_PERFECT_16));
  g_charge = battery_state_service_peek();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  s_date_buffer[0] = '\0';
  s_wday_buffer[0] = '\0';

  battery_state_service_subscribe(charge_handler);
  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
}

static void deinit() {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
}

int main() {
  init();
  app_event_loop();
  deinit();
}
