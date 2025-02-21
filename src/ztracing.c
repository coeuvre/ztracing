#include "src/ztracing.h"

#include <stdarg.h>

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

typedef struct ZtracingFileLoader {
  Arena arena;
  ZtracingFile *file;
  PlatformThread *thread;

  Arena *profile_arena;
  volatile JsonTraceProfile *profile;
} ZtracingFileLoader;

static ZtracingFileLoader *ztracing_file_loader_alloc(ZtracingFile *file,
                                                      Arena *profile_arena) {
  Arena arena_ = {0};
  ZtracingFileLoader *state =
      arena_push_struct_no_zero(&arena_, ZtracingFileLoader);
  *state = (ZtracingFileLoader){
      .arena = arena_,
      .file = file,
      .profile_arena = profile_arena,
  };
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

  JsonParser parser = json_parser(ztracing_file_get_input, file);
  u64 before = platform_get_perf_counter();
  JsonTraceProfile *profile =
      json_trace_profile_parse(self->profile_arena, &parser);
  u64 after = platform_get_perf_counter();

  f64 mb = (f64)file->nread / 1024.0 / 1024.0;
  f64 secs = (f64)(after - before) / (f64)platform_get_perf_freq();
  INFO("Loaded %.1f MiB over %.1f seconds, %.1f MiB / s", mb, secs, mb / secs);

  scratch_free();

  self->profile = profile;

  return 0;
}

static void ztracing_file_loader_start(ZtracingFileLoader *self) {
  self->thread =
      platform_thread_start(ztracing_file_loader__thread, "FileLoader", self);
}

static bool ztracing_file_loader_is_done(ZtracingFileLoader *self) {
  return self->profile;
}

static void ztracing_file_loader_free(ZtracingFileLoader *self) {
  platform_thread_wait(self->thread);
  self->file->close(self->file);
  arena_free(&self->arena);
}

typedef struct ZtracingState {
  f32 dt;
  f32 frame_time;
  ZtracingFileLoader *file_loader;
  Arena profile_arena;
  JsonTraceProfile *profile;
} ZtracingState;

static UITextStyleO text_style_default(void) {
  return ui_text_style_some((UITextStyle){
      .color = ui_color_some(ui_color(0, 0, 0, 1)),
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
  ui_row_begin(&(UIRowProps){0});
  {
    if (do_button(STR8_LIT("Load"), BUTTON_PRIMARY)) {
      INFO("Load");
    }

    ui_expanded_begin(&(UIExpandedProps){.flex = 1});
    ui_expanded_end();

    ui_padding_begin(&(UIPaddingProps){
        .padding = ui_edge_insets_symmetric(6, 3),
    });
    ui_text(&(UITextProps){
        .text = ui_push_str8f(
            "%.0f %.1fMB %.1fms", 1.0f / state->dt,
            (f32)((f64)platform_memory_get_allocated_bytes() / 1024.0 / 1024.0),
            state->frame_time * 1000.0f),
        .style = text_style_default(),
    });
    ui_padding_end();
  }
  ui_row_end();
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
  ZtracingFileLoader *loader = state->file_loader;

  ui_center_begin(&(UICenterProps){0});
  ui_text(&(UITextProps){
      .text = ui_push_str8f("Loading (%.1f MiB) ...",
                            (f64)loader->file->nread / 1024.0 / 1024.0),
      .style = text_style_default(),
  });
  ui_center_end();

  if (ztracing_file_loader_is_done(loader)) {
    state->profile = (JsonTraceProfile *)loader->profile;
    state->file_loader = 0;
    ztracing_file_loader_free(loader);
  }
}

static void profile_screen(ZtracingState *state) {
  JsonTraceProfile *profile = state->profile;
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
  if (state->file_loader) {
    loading_screen(state);
  } else if (state->profile) {
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

  if (state->file_loader) {
    return;
  }

  arena_clear(&state->profile_arena);
  state->profile = 0;
  state->file_loader = ztracing_file_loader_alloc(file, &state->profile_arena);
  ztracing_file_loader_start(state->file_loader);
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
