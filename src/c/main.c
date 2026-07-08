#include <pebble.h>

// ── Layout (emery 200x228) ────────────────────────────────────────────────────
//
//  Y=  0 ┌────────────────────────────────────┐  outer border
//        │ 5/22 Fri    ┌──────────────────┐   │  h=44  s_top_layer
//        │             │  ♥  138          │   │        HR box
//  Y= 44 ├─────────────└──────────────────┘───┤
//        │                                    │
//        │           7:17   (Russo 62)        │  h=112 s_time_layer
//        │                                    │
//  Y=156 ├────────────────────────────────────┤
//        │   87%       ↑ 4:32                 │  h=36  s_bot_layer (row A)
//        │  ══════╢    ↓ 18:43               │  h=36            (row B)
//  Y=228 └────────────────────────────────────┘

#define SUN_SETTINGS_KEY 1
#define THEME_SETTINGS_KEY 2
#define APP_KEY_COLOR_THEME 10004
#define FACE_PADDING 8  // inner padding between outer border and content

typedef enum {
  COLOR_THEME_GRAPHITE = 0,
  COLOR_THEME_BLUEBERRY,
  COLOR_THEME_GRAPE,
  COLOR_THEME_TANGERINE,
  COLOR_THEME_LIME,
  COLOR_THEME_STRAWBERRY,
  COLOR_THEME_COUNT
} ColorThemeId;

typedef struct {
  uint16_t sunrise_today;
  uint16_t sunset_today;
  uint16_t sunrise_tomorrow;
  uint16_t sunset_tomorrow;
} SunTimes;

typedef struct {
  GColor background;
  GColor primary;
  GColor secondary;
  GColor accent;
} ColorTheme;

// ── Globals ───────────────────────────────────────────────────────────────────
static Window *s_window;
static Layer  *s_top_layer;
static Layer  *s_time_layer;
static Layer  *s_bot_layer;
static Layer  *s_border_layer;

static GFont   s_font_time;
static GFont   s_font_num;
static GFont   s_font_date;
static GPath  *s_heart_path;

static int      s_battery_level = 100;
static int      s_heart_rate    = 0;
static bool     s_is_24h        = true;
static SunTimes s_sun_times;
static ColorThemeId s_color_theme_id = COLOR_THEME_GRAPHITE;

static char s_time_buf[8];
static char s_ampm_buf[3];
static char s_date_buf[8];
static char s_day_buf[4];
static char s_hr_buf[6];
static char s_bat_buf[5];
static char s_rise_today_buf[8];
static char s_set_today_buf[8];

static const GPathInfo HEART_PATH_INFO = {
  .num_points = 10,
  .points = (GPoint[]) {
    {0, 8}, {-8, 2}, {-10, -2}, {-7, -7}, {-3, -8},
    {0, -5}, {3, -8}, {7, -7}, {10, -2}, {8, 2},
  }
};

static const ColorTheme COLOR_THEMES[COLOR_THEME_COUNT] = {
  [COLOR_THEME_GRAPHITE] = {
    .background = GColorBlack,
    .primary = GColorWhite,
    .secondary = GColorLightGray,
    .accent = GColorWhite,
  },
  [COLOR_THEME_BLUEBERRY] = {
    .background = GColorBlack,
    .primary = GColorFromHEX(0x00AAFF),
    .secondary = GColorWhite,
    .accent = GColorFromHEX(0x55FFFF),
  },
  [COLOR_THEME_GRAPE] = {
    .background = GColorBlack,
    .primary = GColorFromHEX(0xAA00FF),
    .secondary = GColorWhite,
    .accent = GColorFromHEX(0xFF55FF),
  },
  [COLOR_THEME_TANGERINE] = {
    .background = GColorBlack,
    .primary = GColorFromHEX(0xFFAA00),
    .secondary = GColorWhite,
    .accent = GColorFromHEX(0xFFFF55),
  },
  [COLOR_THEME_LIME] = {
    .background = GColorBlack,
    .primary = GColorFromHEX(0xAAFF00),
    .secondary = GColorWhite,
    .accent = GColorFromHEX(0x55FF00),
  },
  [COLOR_THEME_STRAWBERRY] = {
    .background = GColorBlack,
    .primary = GColorFromHEX(0xFF0055),
    .secondary = GColorWhite,
    .accent = GColorFromHEX(0xFF55AA),
  },
};

// ── Helpers ───────────────────────────────────────────────────────────────────
static void minutes_to_str(uint16_t min, char *buf, size_t sz) {
  if (min == 0) {
    snprintf(buf, sz, "--:--");
  } else {
    snprintf(buf, sz, "%d:%02d", (int)(min / 60), (int)(min % 60));
  }
}

static void update_sun_buffers(void) {
  minutes_to_str(s_sun_times.sunrise_today, s_rise_today_buf, sizeof(s_rise_today_buf));
  minutes_to_str(s_sun_times.sunset_today,  s_set_today_buf,  sizeof(s_set_today_buf));
}

static bool is_valid_theme_id(int theme_id) {
  return theme_id >= 0 && theme_id < COLOR_THEME_COUNT;
}

static ColorTheme theme(void) {
  if (!is_valid_theme_id((int)s_color_theme_id)) {
    return COLOR_THEMES[COLOR_THEME_GRAPHITE];
  }
  return COLOR_THEMES[s_color_theme_id];
}

static void mark_all_layers_dirty(void) {
  if (s_top_layer) layer_mark_dirty(s_top_layer);
  if (s_time_layer) layer_mark_dirty(s_time_layer);
  if (s_bot_layer) layer_mark_dirty(s_bot_layer);
  if (s_border_layer) layer_mark_dirty(s_border_layer);
}

static void apply_color_theme(int theme_id) {
  if (!is_valid_theme_id(theme_id)) {
    theme_id = (int)COLOR_THEME_GRAPHITE;
  }
  s_color_theme_id = (ColorThemeId)theme_id;
  persist_write_int(THEME_SETTINGS_KEY, s_color_theme_id);
  if (s_window) {
    window_set_background_color(s_window, theme().background);
  }
  mark_all_layers_dirty();
}

// Draw a solid triangle arrow at (x,y), pointing up or down. 9px wide, 5px tall.
static void draw_arrow(GContext *ctx, int x, int y, bool up) {
  for (int i = 0; i < 5; i++) {
    int row = up ? (4 - i) : i;
    int left  = x + row;
    int right = x + 8 - row;
    graphics_draw_line(ctx, GPoint(left, y + i), GPoint(right, y + i));
  }
}

// ── Draw procs ────────────────────────────────────────────────────────────────

// TOP: date (left, 2 lines) + boxed heart+HR (right)
static void top_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int line_h = b.size.h / 2;
  ColorTheme colors = theme();

  // Date line 1: m/d
  graphics_context_set_text_color(ctx, colors.primary);
  graphics_draw_text(ctx, s_date_buf, s_font_num,
      GRect(6, 0, 80, line_h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Date line 2: DAY (uppercase)
  graphics_draw_text(ctx, s_day_buf, s_font_date,
      GRect(6, line_h, 80, line_h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Box around heart+HR
  graphics_context_set_stroke_color(ctx, colors.primary);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(90, 4, 90, 50), 8);

  // Heart icon
  int hr_center_y = b.size.h / 2;
  gpath_move_to(s_heart_path, GPoint(106, hr_center_y + 1));
  graphics_context_set_fill_color(ctx, colors.accent);
  gpath_draw_filled(ctx, s_heart_path);

  // HR number (Russo One 28)
  graphics_context_set_text_color(ctx, colors.primary);
  graphics_draw_text(ctx, s_hr_buf, s_font_num,
      GRect(124, (b.size.h - 34) / 2, 48, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

// TIME: large Russo One centered
static void time_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GFont f = s_font_time ? s_font_time : fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  ColorTheme colors = theme();

  graphics_context_set_text_color(ctx, colors.primary);
  graphics_draw_text(ctx, s_time_buf, f,
      GRect(-10, 0, b.size.w + 20, b.size.h - 10),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  if (!s_is_24h && s_ampm_buf[0]) {
    GFont fa = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    graphics_context_set_text_color(ctx, colors.secondary);
    graphics_draw_text(ctx, s_ampm_buf, fa,
        GRect(2, b.size.h - 44, b.size.w, 22),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

// BOTTOM: battery (left) + sunrise/sunset (right)
static void bot_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int row_h = b.size.h / 2;
  ColorTheme colors = theme();

  // ── Battery column (left) ──
  // Row A: horizontal bar
  int bar_x = 8, bar_y = 8, bar_w = 58, bar_h = 26;
  graphics_context_set_stroke_color(ctx, colors.primary);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h), 2);
  graphics_context_set_fill_color(ctx, colors.primary);
  graphics_fill_rect(ctx, GRect(bar_x + bar_w, bar_y + bar_h / 2 - 3, 4, 6), 1, GCornersRight);
  GColor fill = (s_battery_level <= 20) ? GColorRed :
                (s_battery_level <= 40) ? GColorChromeYellow : GColorGreen;
  int fw = (s_battery_level * (bar_w - 8)) / 100;
  if (fw < 0) fw = 0;
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, GRect(bar_x + 4, bar_y + 4, fw, bar_h - 8), 0, GCornerNone);

  // Row B: percentage text (Russo One 28)
  graphics_context_set_text_color(ctx, colors.primary);
  graphics_draw_text(ctx, s_bat_buf, s_font_date,
      GRect(4, row_h + 4, 70, row_h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // ── Sun column (right) ──
  int sun_x = 95;
  int text_x = sun_x + 8;
  int text_w = b.size.w - text_x - 4;

  // Sunrise (row A)
  graphics_context_set_stroke_color(ctx, colors.accent);
  graphics_context_set_stroke_width(ctx, 1);
  draw_arrow(ctx, sun_x, row_h / 2, true);
  graphics_context_set_text_color(ctx, colors.primary);
  graphics_draw_text(ctx, s_rise_today_buf, s_font_num,
      GRect(text_x, 0, text_w, row_h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  // Sunset (row B)
  graphics_context_set_stroke_color(ctx, colors.accent);
  draw_arrow(ctx, sun_x, row_h + row_h / 2, false);
  graphics_context_set_text_color(ctx, colors.primary);
  graphics_draw_text(ctx, s_set_today_buf, s_font_num,
      GRect(text_x, row_h, text_w, row_h),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
}

// BORDER: full-screen outer frame
static void border_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, theme().primary);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_round_rect(ctx, GRect(1, 1, b.size.w - 2, b.size.h - 2), 16);
}

// ── Event handlers ────────────────────────────────────────────────────────────
static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  snprintf(s_bat_buf, sizeof(s_bat_buf), "%d%%", state.charge_percent);
  layer_mark_dirty(s_bot_layer);
}

static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    HealthMetric metric = HealthMetricHeartRateBPM;
    HealthServiceAccessibilityMask mask =
        health_service_metric_accessible(metric, time(NULL), time(NULL));
    if (mask & HealthServiceAccessibilityMaskAvailable) {
      s_heart_rate = (int)health_service_peek_current_value(metric);
    }
    if (s_heart_rate > 0) {
      snprintf(s_hr_buf, sizeof(s_hr_buf), "%d", s_heart_rate);
    } else {
      snprintf(s_hr_buf, sizeof(s_hr_buf), "--");
    }
    layer_mark_dirty(s_top_layer);
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  bool updated = false;
  Tuple *t;
  t = dict_find(iterator, MESSAGE_KEY_SUNRISE_TODAY);
  if (t) { s_sun_times.sunrise_today    = (uint16_t)t->value->int32; updated = true; }
  t = dict_find(iterator, MESSAGE_KEY_SUNSET_TODAY);
  if (t) { s_sun_times.sunset_today     = (uint16_t)t->value->int32; updated = true; }
  t = dict_find(iterator, MESSAGE_KEY_SUNRISE_TOMORROW);
  if (t) { s_sun_times.sunrise_tomorrow = (uint16_t)t->value->int32; updated = true; }
  t = dict_find(iterator, MESSAGE_KEY_SUNSET_TOMORROW);
  if (t) { s_sun_times.sunset_tomorrow  = (uint16_t)t->value->int32; updated = true; }
  t = dict_find(iterator, APP_KEY_COLOR_THEME);
  if (t) {
    apply_color_theme(t->value->int32);
  }

  if (updated) {
    persist_write_data(SUN_SETTINGS_KEY, &s_sun_times, sizeof(SunTimes));
    update_sun_buffers();
    layer_mark_dirty(s_bot_layer);
  }
}

static void update_time(struct tm *t) {
  s_is_24h = clock_is_24h_style();
  if (s_is_24h) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", t);
    if (s_time_buf[0] == '0') memmove(s_time_buf, s_time_buf + 1, sizeof(s_time_buf) - 1);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", t);
    if (s_time_buf[0] == '0') memmove(s_time_buf, s_time_buf + 1, sizeof(s_time_buf) - 1);
    strftime(s_ampm_buf, sizeof(s_ampm_buf), "%p", t);
    s_ampm_buf[1] = '\0';  // "AM"→"A", "PM"→"P"
  }
  layer_mark_dirty(s_time_layer);
}

static void update_date(struct tm *t) {
  strftime(s_date_buf, sizeof(s_date_buf), "%m/%d", t);
  if (s_date_buf[0] == '0') memmove(s_date_buf, s_date_buf + 1, strlen(s_date_buf));
  strftime(s_day_buf, sizeof(s_day_buf), "%a", t);
  for (int i = 0; s_day_buf[i]; i++) {
    if (s_day_buf[i] >= 'a' && s_day_buf[i] <= 'z') s_day_buf[i] -= 32;
  }
  layer_mark_dirty(s_top_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  update_date(tick_time);
  if (tick_time->tm_hour == 0 && tick_time->tm_min == 0) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_SUNRISE_TODAY, 1);
      app_message_outbox_send();
    }
  }
}

// ── Window ────────────────────────────────────────────────────────────────────
static void main_window_load(Window *window) {
  Layer *wl = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(wl);
  if (persist_exists(THEME_SETTINGS_KEY)) {
    s_color_theme_id = (ColorThemeId)persist_read_int(THEME_SETTINGS_KEY);
  }
  window_set_background_color(window, theme().background);

  s_font_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RUSSO_ONE_70));
  s_font_num  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RUSSO_ONE_28));
  s_font_date = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_RUSSO_ONE_20));
  s_heart_path = gpath_create(&HEART_PATH_INFO);

  int p      = FACE_PADDING;
  int cw     = bounds.size.w - 2 * p;
  int top_h  = 58;
  int bot_h  = 64;
  int time_y = p + top_h;
  int time_h = bounds.size.h - 2 * p - top_h - bot_h;
  int bot_y  = bounds.size.h - p - bot_h;

  s_top_layer    = layer_create(GRect(p, p,      cw, top_h));
  s_time_layer   = layer_create(GRect(p, time_y, cw, time_h));
  s_bot_layer    = layer_create(GRect(p, bot_y,  cw, bot_h));
  s_border_layer = layer_create(bounds);

  layer_set_update_proc(s_top_layer,    top_update_proc);
  layer_set_update_proc(s_time_layer,   time_update_proc);
  layer_set_update_proc(s_bot_layer,    bot_update_proc);
  layer_set_update_proc(s_border_layer, border_update_proc);

  layer_add_child(wl, s_top_layer);
  layer_add_child(wl, s_time_layer);
  layer_add_child(wl, s_bot_layer);
  layer_add_child(wl, s_border_layer);  // drawn on top

  // Initial values
  if (persist_exists(SUN_SETTINGS_KEY)) {
    persist_read_data(SUN_SETTINGS_KEY, &s_sun_times, sizeof(SunTimes));
  }
  update_sun_buffers();
  snprintf(s_hr_buf,  sizeof(s_hr_buf),  "--");
  snprintf(s_bat_buf, sizeof(s_bat_buf), "%d%%", s_battery_level);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t);
  update_date(t);

  HealthMetric metric = HealthMetricHeartRateBPM;
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(metric, time(NULL), time(NULL));
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_heart_rate = (int)health_service_peek_current_value(metric);
    if (s_heart_rate > 0) snprintf(s_hr_buf, sizeof(s_hr_buf), "%d", s_heart_rate);
  }
}

static void main_window_unload(Window *window) {
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_num);
  fonts_unload_custom_font(s_font_date);
  gpath_destroy(s_heart_path);
  layer_destroy(s_top_layer);
  layer_destroy(s_time_layer);
  layer_destroy(s_bot_layer);
  layer_destroy(s_border_layer);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_callback);
  battery_callback(battery_state_service_peek());
  health_service_events_subscribe(health_handler, NULL);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(64, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  health_service_events_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
