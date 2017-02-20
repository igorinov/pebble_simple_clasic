#include <pebble.h>

#include "simple_classic.h"
#include "rotate_rectangle.h"

static Window *s_window;
static Layer *s_back_layer;
static Layer *s_date_layer;
static Layer *s_hand_layer;
static Layer *s_batt_layer;
static Layer *s_conn_layer;

static char s_date_buffer[16] = "";
static char s_wday_buffer[16] = "";
static int g_wday = -1;
static BatteryChargeState g_charge;
static bool g_connected = false;
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
int32_t fixed_round(int32_t a)
{
  if (a > 0)
    a += (1 << 15);

  if (a < 0)
    a -= (1 << 15);

  return (int16_t)(a >> 16);
}

static void back_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  graphics_context_set_antialiased(ctx, true);
                                   
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  const int32_t tick_span = 66;

  for (int i = 0; i < 12; i += 1) {
    int32_t tick_angle = DEG_TO_TRIGANGLE(i * 30);
    GPoint tick_center = {
      .x = center.x + fixed_round(sin_lookup(tick_angle) * tick_span),
      .y = center.y - fixed_round(cos_lookup(tick_angle) * tick_span),
    };
    graphics_fill_circle(ctx, tick_center, 3);
    graphics_draw_circle(ctx, tick_center, 3);
  }
}

static void conn_update_proc(Layer *layer, GContext *ctx) {
#ifdef PBL_ROUND
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  GPoint indicator_center = GPoint(center.x + 80, center.y);
#else
  GPoint indicator_center = GPoint(136, 8);
#endif
  
  graphics_context_set_stroke_color(ctx, GColorWhite);
  if (g_connected)
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
  else
    graphics_context_set_fill_color(ctx, GColorDarkGray);

    graphics_fill_circle(ctx, indicator_center, 6);
    graphics_draw_circle(ctx, indicator_center, 6);
}

static void batt_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  int c = g_charge.charge_percent;
  GRect charge_frame = {
#ifdef PBL_ROUND
    .origin = { center.x - 21, center.x + 74 },
    .size = { 42, 8 },
#else
    .origin = { center.x - 31, bounds.size.h - 8},
    .size = { 62, 8 },
#endif
  };

  GRect charge_level = {
#ifdef PBL_ROUND
    .origin = { center.x - 20, center.y + 75  },
    .size = { (c / 5) * 2, 6 },
#else
    .origin = { center.x - 30, bounds.size.h - 7 },
    .size = { (c / 5) * 3, 6 },
#endif
  };

  GColor color = get_charging_color(g_charge.charge_percent);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, charge_level, 0, GCornerNone);
  graphics_draw_rect(ctx, charge_frame);
}

static void date_update_proc(Layer *layer, GContext *ctx) {
#ifdef PBL_ROUND
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);
  GRect date_rect = GRect(center.x - 35, center.y - 85, 64, 24);
  GRect wday_rect = GRect(center.x + 40, center.y - 57, 48, 24);
#else
  GRect date_rect = GRect(0, 0, 64, 24);
  GRect wday_rect = GRect(80, 0, 48, 24);
#endif
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%b %d", t);
  strftime(s_wday_buffer, sizeof(s_wday_buffer), "%a", t);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_date_buffer, s_font, date_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, s_wday_buffer, s_font, wday_rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  
  g_wday = t->tm_wday;
}

static void hand_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int32_t hour_angle = minute_index(((((t->tm_hour % 12) * 60 + t->tm_min) * 60) + t->tm_sec) / 2);
  int32_t minute_angle = minute_index((t->tm_min * 60 + t->tm_sec) * 6);
  int32_t second_angle = minute_index(t->tm_sec * 360);

  // hour hand
  rotate_rectangle(ctx, GPoint(-3, -39), GPoint(3, 13), center, hour_angle, GColorRajahARGB8);

  // minute hand
  rotate_rectangle(ctx, GPoint(-2, -60), GPoint(2, 20), center, minute_angle, GColorWhiteARGB8);

  // second hand
  rotate_rectangle(ctx, GPoint(-1, -68), GPoint(1, 17), center, second_angle, GColorOrangeARGB8);
  rotate_rectangle(ctx, GPoint(-2, 16), GPoint(2, 24), center, second_angle, GColorOrangeARGB8);

  // dot in the middle
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, GColorWindsorTan);
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
  graphics_fill_circle(ctx, center, 5);
  
  if (t->tm_wday != g_wday)
    layer_mark_dirty(s_date_layer);
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_hand_layer);
}

static void charge_handler(BatteryChargeState charge)
{
  g_charge = charge;
  layer_mark_dirty(s_batt_layer);
}

static void app_connection_handler(bool connected)
{
  g_connected = connected;
  layer_mark_dirty(s_conn_layer);
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

  s_conn_layer = layer_create(bounds);
  layer_set_update_proc(s_conn_layer, conn_update_proc);
  layer_add_child(window_layer, s_conn_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_back_layer);
  layer_destroy(s_date_layer);
  layer_destroy(s_hand_layer);
  layer_destroy(s_batt_layer);
  layer_destroy(s_conn_layer);
}

static void init() {
  s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_PERFECT_16));
  g_charge = battery_state_service_peek();
  g_connected = connection_service_peek_pebble_app_connection();
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  s_date_buffer[0] = '\0';
  s_wday_buffer[0] = '\0';

  ConnectionHandlers connection_handlers = {
    .pebble_app_connection_handler = app_connection_handler,
    .pebblekit_connection_handler = NULL
  };

  connection_service_subscribe(connection_handlers);
  battery_state_service_subscribe(charge_handler);
  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
}

static void deinit() {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main() {
  init();
  app_event_loop();
  deinit();
}
