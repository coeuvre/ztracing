#include "src/ztracing.h"

#include <stdarg.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/json.h"
#include "src/json_trace_profile.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

typedef struct ZtracingProfileViewer {
  Arena arena;
  Str8 name;
  JsonTraceProfile *profile;
  i64 begin_time_ns;
  i64 end_time_ns;
} ZtracingProfileViewer;

static ZtracingProfileViewer *ztracing_profile_viewer_alloc(void) {
  Arena arena_ = {0};
  ZtracingProfileViewer *viewer =
      arena_push_struct(&arena_, ZtracingProfileViewer);
  viewer->arena = arena_;
  return viewer;
}

static void ztracing_profile_viewer_free(ZtracingProfileViewer *self) {
  arena_free(&self->arena);
}

typedef struct ZtracingFileLoader {
  Arena arena;
  ZtracingFile *file;
  PlatformThread *thread;

  volatile ZtracingProfileViewer *viewer;
} ZtracingFileLoader;

static ZtracingFileLoader *ztracing_file_loader_alloc(ZtracingFile *file) {
  Arena arena_ = {0};
  ZtracingFileLoader *state = arena_push_struct(&arena_, ZtracingFileLoader);
  state->arena = arena_;
  state->file = file;
  return state;
}

static Str8 ztracing_file_get_input(void *c) {
  ZtracingFile *self = c;
  Str8 buf = self->read(self);
  self->nread += buf.len;
  return buf;
}

static int ztracing_file_loader__thread(void *self_) {
  ZtracingFileLoader *self = self_;
  ZtracingFile *file = self->file;

  ZtracingProfileViewer *viewer = ztracing_profile_viewer_alloc();
  viewer->name = arena_dup_str8(&viewer->arena, file->name);

  JsonParser parser = json_parser(ztracing_file_get_input, file);
  u64 before = platform_get_perf_counter();
  viewer->profile = json_trace_profile_parse(&viewer->arena, &parser);
  u64 after = platform_get_perf_counter();

  if (!file->interrupted) {
    f64 mb = (f64)file->nread / 1024.0 / 1024.0;
    f64 secs = (f64)(after - before) / (f64)platform_get_perf_freq();
    INFO("Loaded %.1f MiB over %.1f seconds, %.1f MiB / s", mb, secs,
         mb / secs);
  }

  scratch_free();

  i64 duration = (viewer->profile->max_time_ns - viewer->profile->min_time_ns);
  i64 offset = duration * 0.1;
  viewer->begin_time_ns = viewer->profile->min_time_ns - offset;
  viewer->end_time_ns = viewer->profile->max_time_ns + offset;

  self->viewer = viewer;

  return 0;
}

static void ztracing_file_loader_start(ZtracingFileLoader *self) {
  self->thread =
      platform_thread_start(ztracing_file_loader__thread, "FileLoader", self);
}

static bool ztracing_file_loader_is_done(ZtracingFileLoader *self) {
  return self->viewer;
}

static void ztracing_file_loader_free(ZtracingFileLoader *self) {
  self->file->interrupted = true;
  platform_thread_wait(self->thread);

  self->file->close(self->file);
  arena_free(&self->arena);
}

typedef struct ZtracingState {
  f32 dt;
  f32 frame_time;
  ZtracingFileLoader *loader;
  ZtracingProfileViewer *viewer;
} ZtracingState;

static UITextStyleO text_style_default(void) {
#define FONT_SIZE_DEFAULT 13
  return ui_text_style_some((UITextStyle){
      .color = ui_color_some(ui_color(0, 0, 0, 1)),
      .font_size = f32_some(FONT_SIZE_DEFAULT),
  });
}

typedef enum ButtonType {
  BUTTON_PRIMARY,
  BUTTON_SECONDARY,
} ButtonType;

static bool do_button(Str8 text, ButtonType type) {
  bool pressed;
  ui_button(&(UIButtonProps){
      .text = text,
      .text_style = text_style_default(),

      .pressed = &pressed,
      .fill_color = (type == BUTTON_PRIMARY)
                        ? ui_color_some(ui_color(0.73, 0.83, 0.95, 1))
                        : ui_color_none(),
      .hover_color = ui_color_some(ui_color(0.29, 0.49, 0.72, 1)),
      .splash_color = ui_color_some(ui_color(0.29, 0.44, 0.62, 1)),
      .padding = ui_edge_insets_some(ui_edge_insets_symmetric(6, 3)),
  });
  return pressed;
}

static void global_menu_bar(ZtracingState *state) {
  ui_padding_begin(&(UIPaddingProps){
      .padding = ui_edge_insets_symmetric(6, 4),
  });
  ui_row_begin(&(UIRowProps){0});
  {
    Str8 name = str8_zero();
    if (state->loader) {
      name = state->loader->file->name;
    } else if (state->viewer) {
      name = state->viewer->name;
    }

    ui_text(&(UITextProps){
        .text = name,
        .style = text_style_default(),
    });

    ui_expanded_begin(&(UIExpandedProps){.flex = 1});
    ui_expanded_end();

    ui_text(&(UITextProps){
        .text = ui_push_str8f(
            "%.0f %.1fMB %.1fms", 1.0f / state->dt,
            (f32)((f64)platform_memory_get_allocated_bytes() / 1024.0 / 1024.0),
            state->frame_time * 1000.0f),
        .style = text_style_default(),
    });
  }
  ui_row_end();
  ui_padding_end();
}

static void welcome_screen(void) {
  Str8 logo = STR8_LIT(
      // clang-format off
      " ________  _________  _______          _        ______  _____  ____  _____   ______\n"
      "|  __   _||  _   _  ||_   __ \\        / \\     .' ___  ||_   _||_   \\|_   _|.' ___  |\n"
      "|_/  / /  |_/ | | \\_|  | |__) |      / _ \\   / .'   \\_|  | |    |   \\ | | / .'   \\_|\n"
      "   .'.' _     | |      |  __ /      / ___ \\  | |         | |    | |\\ \\| | | |   ____\n"
      " _/ /__/ |   _| |_    _| |  \\ \\_  _/ /   \\ \\_\\ `.___.'\\ _| |_  _| |_\\   |_\\ `.___]  |\n"
      "|________|  |_____|  |____| |___||____| |____|`.____ .'|_____||_____|\\____|`._____.'"
      // clang-format on
  );
  ui_column_begin(&(UIColumnProps){
      .main_axis_alignment = UI_MAIN_AXIS_ALIGNMENT_CENTER,
  });
  ui_text(&(UITextProps){
      .text = logo,
      .style = text_style_default(),
  });
  ui_padding_begin(&(UIPaddingProps){
      .padding = ui_edge_insets_symmetric(0, 10),
  });
  ui_padding_end();
  ui_text(&(UITextProps){
      .text = STR8_LIT("Drag & Drop a json trace profile to start."),
      .style = text_style_default(),
  });
  ui_column_end();
}

static void loading_screen(ZtracingState *state) {
  ZtracingFileLoader *loader = state->loader;

  ui_center_begin(&(UICenterProps){0});
  ui_text(&(UITextProps){
      .text = ui_push_str8f("Loading (%.1f MiB) ...",
                            (f64)loader->file->nread / 1024.0 / 1024.0),
      .style = text_style_default(),
  });
  ui_center_end();

  if (ztracing_file_loader_is_done(loader)) {
    state->viewer = (ZtracingProfileViewer *)loader->viewer;
    state->loader = 0;
    ztracing_file_loader_free(loader);
  }
}

typedef struct TimelineProps {
  UIKey key;
  f32 min_time_ns;
  f32 max_time_ns;
} TimelineProps;

static i64 timeline__calc_block_duration(i64 duration, f32 width,
                                         f32 target_block_width) {
  f32 num_blocks = f32_floor(width / target_block_width);
  i64 block_duration = (f32)duration / (f32)num_blocks;
  i64 base = 1;
  while (base * 10 < block_duration) {
    base *= 10;
  }
  if (block_duration >= base * 4) {
    base *= 4;
  } else if (block_duration >= base * 2) {
    base *= 2;
  }
  block_duration = base;
  return block_duration;
}

static Str8 timeline__format_time(Arena *arena, i64 time, i64 duration) {
  static const char *TIME_UNITS[] = {"ns", "us", "ms", "s"};

  if (time == 0) {
    return STR8_LIT("0");
  }

  f64 t = time;
  usize time_unit_index = 0;

  if (duration > 0) {
    i64 tmp = duration / 1000;
    while (tmp > 0 && time_unit_index < ARRAY_COUNT(TIME_UNITS)) {
      tmp /= 1000;
      t /= 1000.0;
      time_unit_index++;
    }
  } else {
    while (f64_abs(t) >= 1000.0 && time_unit_index < ARRAY_COUNT(TIME_UNITS)) {
      t /= 1000.0;
      time_unit_index++;
    }
  }

  Str8 buf = arena_push_str8f(arena, "%.1lf%s", t, TIME_UNITS[time_unit_index]);
  u8 *period = buf.ptr + buf.len - 1;
  while (*period != '.') {
    period--;
  }
  if (*(period + 1) == '0') {
    memory_move(period, period + 2, buf.ptr + buf.len - period - 1);
    buf.len -= 2;
  }
  return buf;
}

static void timeline__paint(UIWidget *widget, TimelineProps *props,
                            UIPaintingContext *context, Vec2 offset) {
  (void)context;

#define TIMELINE_HEIGHT 20

  i64 begin = props->min_time_ns;
  i64 end = props->max_time_ns;

  UIColor color = ui_color(0, 0, 0, 1);

  Vec2 size = widget->size;
  f32 font_size = text_style_default().value.font_size.value;

  i64 duration = end - begin;
  i64 block_duration =
      timeline__calc_block_duration(duration, size.x, size.y * 1.5f);
  i64 large_block_duration = block_duration * 5;

  f32 line_thickness = 1.0f;
  fill_rect(vec2(offset.x, offset.y + size.y - line_thickness),
            vec2(offset.x + size.x, offset.y + size.y), color);

  f32 ns_per_point = (f32)((f64)(duration) / (f64)size.x);
  f32 point_per_ns = 1.0f / ns_per_point;
  f32 large_block_width = large_block_duration * point_per_ns;

  i64 t = begin / block_duration * block_duration;
  f32 bottom = offset.y + size.y;
  while (t <= end) {
    f32 x = offset.x + (t - begin) * point_per_ns;
    bool is_large_block = t % large_block_duration == 0;
    f32 height =
        is_large_block ? TIMELINE_HEIGHT * 0.4f : TIMELINE_HEIGHT * 0.2f;
    fill_rect(vec2(x, bottom - height), vec2(x + line_thickness, bottom),
              color);

    if (is_large_block) {
      Scratch scratch = scratch_begin(0, 0);
      draw_text_str8(vec2(x + 4, bottom - 2 - font_size),
                     timeline__format_time(scratch.arena, t, duration),
                     font_size, 0, large_block_width - 4, color);
      scratch_end(scratch);
    }

    t += block_duration;
  }
}

static i32 timeline_callback(UIWidget *widget, UIMessage *message) {
  ASSERT(!widget->first);

  i32 result = 0;
  switch (message->type) {
    case UI_MESSAGE_LAYOUT_BOX: {
      UIBoxConstraints constraints = message->layout_box.constraints;
      widget->size = ui_box_constraints_constrain(
          constraints, vec2(F32_INFINITY, TIMELINE_HEIGHT));
    } break;

    case UI_MESSAGE_PAINT: {
      timeline__paint(widget, ui_widget_get_props(widget, TimelineProps),
                      message->paint.context, message->paint.offset);
    } break;

    default: {
      result = ui_widget_callback_default(widget, message);
    } break;
  }
  return result;
}

static UIWidgetClass timeline_class = {
    .name = "Timeline",
    .props_size = sizeof(TimelineProps),
    .callback = timeline_callback,
};

static void timeline(const TimelineProps *props) {
  ui_widget_begin(&timeline_class, props);
  ui_widget_end(&timeline_class);
}

static void profile_screen(ZtracingState *state) {
  ZtracingProfileViewer *viewer = state->viewer;
  JsonTraceProfile *profile = viewer->profile;
  if (!str8_is_empty(profile->error)) {
    ui_center_begin(&(UICenterProps){0});
    ui_text(&(UITextProps){
        .text = ui_push_str8f("error: %.*s", (int)profile->error.len,
                              profile->error.ptr),
        .style = text_style_default(),
    });
    ui_center_end();
    return;
  }

  ui_column_begin(&(UIColumnProps){0});
  timeline(&(TimelineProps){
      .min_time_ns = viewer->begin_time_ns,
      .max_time_ns = viewer->end_time_ns,
  });
  ui_column_end();

  // UIListBuilder builder;
  // ui_list_view_begin(&(UIListViewProps){
  //     .item_extent = 20,
  //     .item_count = 512,
  //     .builder = &builder,
  // });
  // for (i32 item_index = builder.first_index; item_index <=
  // builder.last_index;
  //      ++item_index) {
  //   ui_row_begin(&(UIRowProps){0});
  //   ui_container_begin(&(UIContainerProps){
  //       .width = f32_some(200.0f),
  //   });
  //   ui_text(&(UITextProps){
  //       .text = ui_push_str8f("Row %u", item_index),
  //       .style = text_style_default(),
  //   });
  //   ui_container_end();
  //
  //   ui_expanded_begin(&(UIExpandedProps){
  //       .flex = 1,
  //   });
  //   ui_container_begin(&(UIContainerProps){
  //       .color = ui_color_some(ui_color(0, (item_index % 255) / 255.0f, 0,
  //       1)),
  //   });
  //   ui_container_end();
  //   ui_expanded_end();
  //   ui_row_end();
  // }
  // ui_list_view_end();
}

static void main_screen(ZtracingState *state) {
  if (state->loader) {
    loading_screen(state);
  } else if (state->viewer) {
    profile_screen(state);
  } else {
    welcome_screen();
  }
}

static void build_ui(ZtracingState *state) {
  ui_colored_box_begin(&(UIColoredBoxProps){
      .color = ui_color(0.94, 0.94, 0.94, 1.0),
  });
  ui_column_begin(&(UIColumnProps){0});
  {
    global_menu_bar(state);

    // Simulate a bottom border.
    // TODO: Impl DecorationBox.
    ui_container_begin(&(UIContainerProps){
        .height = f32_some(1),
        .color = ui_color_some(ui_color(0.6, 0.6, 0.6, 1.0)),
    });
    ui_container_end();

    ui_expanded_begin(&(UIExpandedProps){
        .flex = 1,
    });
    main_screen(state);
    ui_expanded_end();
  }
  ui_column_end();
  ui_colored_box_end();
}

static ZtracingState g_ztracing_state;

void ztracing_load_file(ZtracingFile *file) {
  ZtracingState *state = &g_ztracing_state;

  if (state->loader) {
    ztracing_file_loader_free(state->loader);
    state->loader = 0;
  }

  if (state->viewer) {
    ztracing_profile_viewer_free(state->viewer);
    state->viewer = 0;
  }

  state->loader = ztracing_file_loader_alloc(file);
  ztracing_file_loader_start(state->loader);
}

void ztracing_update(void) {
  static u64 last_counter;
  static f32 last_frame_time;

  ZtracingState *state = &g_ztracing_state;

  f32 dt = 0.0f;
  u64 current_counter = platform_get_perf_counter();
  if (last_counter) {
    dt = (f32)((f64)(current_counter - last_counter) /
               (f64)platform_get_perf_freq());
  }
  last_counter = current_counter;

  state->dt = dt;
  state->frame_time = last_frame_time;

  clear_draw();

  ui_set_delta_time(dt);
  do {
    ui_begin_frame();
    build_ui(state);
    ui_end_frame();
  } while (ui_should_rebuild());
  ui_paint();

  last_frame_time = (f32)((f64)(platform_get_perf_counter() - last_counter) /
                          (f64)platform_get_perf_freq());

  present_draw();
}

void ztracing_quit(void) {
  ZtracingState *state = &g_ztracing_state;

  if (state->loader) {
    ztracing_file_loader_free(state->loader);
    state->loader = 0;
  }
}
