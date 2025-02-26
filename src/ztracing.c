#include "src/ztracing.h"

#include <stdarg.h>
#include <stdlib.h>

#include "src/assert.h"
#include "src/draw.h"
#include "src/hash_trie.h"
#include "src/json.h"
#include "src/json_trace_profile.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/string.h"
#include "src/types.h"
#include "src/ui.h"

typedef enum ZtracingProfileItemType {
  ZTRACING_PROFILE_ITEM_HEADER,
  ZTRACING_PROFILE_ITEM_COUNTER,
} ZtracingProfileItemType;

typedef struct ZtracingProfileItemHeader {
  ZtracingProfileItemType type;  // ZTRACING_PROFILE_ITEM_HEADER
  Str8 name;
} ZtracingProfileItemHeader;

typedef struct ZtracingProfileItemCounterSeriesSample {
  i64 time;
  f64 value;
} ZtracingProfileItemCounterSeriesSample;

typedef struct ZtracingProfileItemCounterSeries {
  Str8 name;
  usize sample_count;
  ZtracingProfileItemCounterSeriesSample *samples;
} ZtracingProfileItemCounterSeries;

typedef struct ZtracingProfileItemCounter {
  ZtracingProfileItemType type;  // ZTRACING_PROFILE_ITEM_COUNTER
  Str8 name;
  usize series_count;
  ZtracingProfileItemCounterSeries *series;
  f64 min_value;
  f64 max_value;
} ZtracingProfileItemCounter;

typedef union ZtracingProfileItem {
  ZtracingProfileItemType type;
  ZtracingProfileItemHeader header;
  ZtracingProfileItemCounter counter;
} ZtracingProfileItem;

typedef struct ZtracingProfileViewer {
  Arena arena;
  Str8 name;
  Str8 error;
  i64 min_time_ns;
  i64 max_time_ns;
  i64 begin_time_ns;
  i64 end_time_ns;
  usize item_count;
  ZtracingProfileItem *items;
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

static int ztracing_file_loader__compare_samples(const void *a, const void *b) {
  const ZtracingProfileItemCounterSeriesSample *sa = a;
  const ZtracingProfileItemCounterSeriesSample *sb = b;
  i64 result = sa->time - sb->time;
  if (result == 0) {
    return 0;
  }
  return result < 0 ? -1 : 1;
}

static void ztracing_file_loader__collect_series(Arena *arena,
                                                 ZtracingProfileItemCounter *c,
                                                 JsonTraceCounter *counter) {
  Scratch scratch = scratch_begin(&arena, 1);
  HashTrieIter series_iter = hash_trie_iter(scratch.arena, counter->series);
  HashTrie *series_slot;
  usize series_index = 0;
  while ((series_slot = hash_trie_iter_next(&series_iter))) {
    JsonTraceSeries *series = series_slot->value;
    ZtracingProfileItemCounterSeries *s = &c->series[series_index++];

    s->name = str8_dup(arena, series->name);
    s->sample_count = series->sample_len;
    s->samples = arena_push_array(arena, ZtracingProfileItemCounterSeriesSample,
                                  s->sample_count);

    usize sample_index = 0;
    for (JsonTraceSample *sample = series->first; sample;
         sample = sample->next) {
      s->samples[sample_index++] = (ZtracingProfileItemCounterSeriesSample){
          .time = sample->time,
          .value = sample->value,
      };
    }

    qsort(s->samples, s->sample_count, sizeof(*s->samples),
          ztracing_file_loader__compare_samples);
  }
  scratch_end(scratch);
}

static int ztracing_file_loader__compare_counter(const void *a, const void *b) {
  Scratch scratch = scratch_begin(0, 0);
  JsonTraceCounter *const *ca = a;
  JsonTraceCounter *const *cb = b;
  int result = str8_cmp(str8_to_uppercase((*ca)->name, scratch.arena),
                        str8_to_uppercase((*cb)->name, scratch.arena));
  scratch_end(scratch);
  return result;
}

static void ztracing_file_loader__collect_counters(Arena *arena,
                                                   ZtracingProfileItem *items,
                                                   usize *item_index,
                                                   JsonTraceProcess *process) {
  Scratch scratch = scratch_begin(&arena, 1);

  JsonTraceCounter **sorted_counters =
      arena_push_array(scratch.arena, JsonTraceCounter *, process->counter_len);
  {
    usize counter_index = 0;
    HashTrieIter counter_iter =
        hash_trie_iter(scratch.arena, process->counters);
    HashTrie *counter_slot;
    while ((counter_slot = hash_trie_iter_next(&counter_iter))) {
      sorted_counters[counter_index++] = counter_slot->value;
    }
  }
  qsort(sorted_counters, process->counter_len, sizeof(*sorted_counters),
        ztracing_file_loader__compare_counter);

  for (usize counter_index = 0; counter_index < process->counter_len;
       ++counter_index) {
    JsonTraceCounter *counter = sorted_counters[counter_index];

    items[(*item_index)++] = (ZtracingProfileItem){
        .header =
            {
                .type = ZTRACING_PROFILE_ITEM_HEADER,
                .name = str8_dup(arena, counter->name),
            },
    };

    ZtracingProfileItemCounter c = {
        .type = ZTRACING_PROFILE_ITEM_COUNTER,
    };
    c.name = str8_dup(arena, counter->name);
    c.series_count = counter->series_len;
    c.series = arena_push_array(arena, ZtracingProfileItemCounterSeries,
                                c.series_count);
    c.min_value = counter->min_value;
    c.max_value = counter->max_value;

    ztracing_file_loader__collect_series(arena, &c, counter);

    items[(*item_index)++] = (ZtracingProfileItem){
        .counter = c,
    };
  }
  scratch_end(scratch);
}

static void ztracing_file_loader__collect_items(Arena *arena,
                                                ZtracingProfileItem *items,
                                                JsonTraceProfile *profile) {
  Scratch scratch = scratch_begin(&arena, 1);
  usize item_index = 0;
  HashTrieIter process_iter = hash_trie_iter(scratch.arena, profile->processes);
  HashTrie *process_slot;
  while ((process_slot = hash_trie_iter_next(&process_iter))) {
    JsonTraceProcess *process = process_slot->value;
    ztracing_file_loader__collect_counters(arena, items, &item_index, process);
  }
  scratch_end(scratch);
}

static usize ztracing_file_loader__count_items(JsonTraceProfile *profile) {
  usize item_count = 0;
  Scratch scratch = scratch_begin(0, 0);
  HashTrieIter process_iter = hash_trie_iter(scratch.arena, profile->processes);
  HashTrie *process_slot;
  while ((process_slot = hash_trie_iter_next(&process_iter))) {
    JsonTraceProcess *process = process_slot->value;
    item_count += 2 * process->counter_len;
  }
  scratch_end(scratch);
  return item_count;
}

static void ztracing_file_loader__process_profile(ZtracingProfileViewer *viewer,
                                                  JsonTraceProfile *profile) {
  viewer->item_count = ztracing_file_loader__count_items(profile);
  viewer->items =
      arena_push_array(&viewer->arena, ZtracingProfileItem, viewer->item_count);
  ztracing_file_loader__collect_items(&viewer->arena, viewer->items, profile);
}

static int ztracing_file_loader__thread(void *self_) {
  ZtracingFileLoader *self = self_;
  ZtracingFile *file = self->file;

  ZtracingProfileViewer *viewer = ztracing_profile_viewer_alloc();
  viewer->name = str8_dup(&viewer->arena, file->name);

  Scratch scratch = scratch_begin(0, 0);
  JsonParser parser = json_parser(ztracing_file_get_input, file);
  u64 before = platform_get_perf_counter();
  JsonTraceProfile *profile = json_trace_profile_parse(scratch.arena, &parser);
  u64 after = platform_get_perf_counter();

  if (!file->interrupted) {
    f64 mb = (f64)file->nread / 1024.0 / 1024.0;
    f64 secs = (f64)(after - before) / (f64)platform_get_perf_freq();
    INFO("Loaded %.1f MiB over %.1f seconds, %.1f MiB / s", mb, secs,
         mb / secs);

    if (str8_is_empty(profile->error)) {
      ztracing_file_loader__process_profile(viewer, profile);
    } else {
      viewer->error = str8_dup(&viewer->arena, profile->error);
    }

    viewer->min_time_ns = profile->min_time_ns;
    viewer->max_time_ns = profile->max_time_ns;

    i64 duration = (profile->max_time_ns - profile->min_time_ns);
    i64 offset = duration * 0.1;
    viewer->begin_time_ns = profile->min_time_ns - offset;
    viewer->end_time_ns = profile->max_time_ns + offset;

    self->viewer = viewer;
  }

  scratch_end(scratch);
  scratch_free_all();

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
  f32 begin_time_ns;
  f32 end_time_ns;
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
    while (tmp > 0 && (time_unit_index + 1) < ARRAY_COUNT(TIME_UNITS)) {
      tmp /= 1000;
      t /= 1000.0;
      time_unit_index++;
    }
  } else {
    while (f64_abs(t) >= 1000.0 &&
           (time_unit_index + 1) < ARRAY_COUNT(TIME_UNITS)) {
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

#define PROFILE_ITEM_HEIGHT 20

static void timeline_layout(UIWidget *widget, UIBoxConstraints constraints) {
  widget->size = ui_box_constraints_constrain(
      constraints, vec2(F32_INFINITY, PROFILE_ITEM_HEIGHT));
}

static void timeline_paint(UIWidget *widget, UIPaintingContext *context,
                           Vec2 offset) {
  (void)context;

  TimelineProps *props = ui_widget_get_props(widget, TimelineProps);

  i64 begin = props->begin_time_ns;
  i64 end = props->end_time_ns;

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

  f32 point_per_ns = (f32)size.x / (f32)(duration);
  f32 large_block_width = large_block_duration * point_per_ns;

  i64 t = begin / block_duration * block_duration;
  // Truncate away from 0
  if (t < 0) {
    t -= block_duration;
  }
  f32 bottom = offset.y + size.y;
  while (t <= end) {
    f32 x = offset.x + (t - begin) * point_per_ns;
    bool is_large_block = t % large_block_duration == 0;
    f32 height = is_large_block ? size.y * 0.4f : size.y * 0.2f;
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

static UIWidgetClass timeline_class = {
    .name = "Timeline",
    .props_size = sizeof(TimelineProps),
    .layout = timeline_layout,
    .paint = timeline_paint,
};

static void timeline(const TimelineProps *props) {
  ui_widget_begin(&timeline_class, props);
  ui_widget_end(&timeline_class);
}

typedef struct ProfileCounterProps {
  UIKey key;
  ZtracingProfileItemCounter *counter;
  f32 begin_time_ns;
  f32 end_time_ns;
} ProfileCounterProps;

static void profile_counter_layout(UIWidget *widget,
                                   UIBoxConstraints constraints) {
  widget->size = ui_box_constraints_constrain(
      constraints, vec2(F32_INFINITY, PROFILE_ITEM_HEIGHT));
}

static usize profile_counter__samples_lower_bound(
    ZtracingProfileItemCounterSeriesSample *samples, usize count, i64 time) {
  usize low = 0;
  usize high = count;

  while (low < high) {
    usize mid = low + (high - low) / 2;
    i64 mid_t = samples[mid].time;
    if (mid_t < time) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }

  return low;
}

static void profile_counter__paint_sample(
    UIWidget *widget, ProfileCounterProps *props, Vec2 offset, f32o *prev_x,
    f32o *prev_h, f32 d, f32 point_per_ns, ZtracingProfileItemCounter *counter,
    ZtracingProfileItemCounterSeriesSample *sample) {
  f32 x = offset.x + (sample->time - props->begin_time_ns) * point_per_ns;
  f32 height =
      f32_max(1, (sample->value - counter->min_value) / d * widget->size.y);
  if (prev_x->present) {
    f32 bottom = offset.y + widget->size.y;
    f32 px = prev_x->value;
    f32 ph = prev_h->value;
    f32 width = f32_max(1, x - px);
    Vec2 min = vec2(px, bottom - ph);
    Vec2 max = vec2(px + width, bottom);
    fill_rect(min, max, ui_color(0.3, 0.3, 0.3, 0.7));
  }
  *prev_x = f32_some(x);
  *prev_h = f32_some(height);
}

static void profile_counter_paint(UIWidget *widget, UIPaintingContext *context,
                                  Vec2 offset) {
  (void)context;

  ProfileCounterProps *props = ui_widget_get_props(widget, ProfileCounterProps);

  ZtracingProfileItemCounter *counter = props->counter;
  f64 d = (counter->max_value - counter->min_value);
  f32 point_per_ns =
      (f32)widget->size.x / (f32)(props->end_time_ns - props->begin_time_ns);

  f32 bin_width = 2.0f;
  f32 ns_per_point = 1.0f / point_per_ns;
  i64 bin_duration = f32_round(ns_per_point * bin_width);

  for (usize series_index = 0; series_index < counter->series_count;
       ++series_index) {
    ZtracingProfileItemCounterSeries *series = counter->series + series_index;

    f32o prev_x = f32_none();
    f32o prev_h = f32_none();

    i64 bin_begin = props->begin_time_ns / bin_duration;
    i64 bin_end = props->end_time_ns / bin_duration + 1;

    {
      usize first_sample_index = profile_counter__samples_lower_bound(
          series->samples, series->sample_count, bin_begin * bin_duration);
      if (first_sample_index > 0) {
        ZtracingProfileItemCounterSeriesSample *sample =
            series->samples + (first_sample_index - 1);
        profile_counter__paint_sample(widget, props, offset, &prev_x, &prev_h,
                                      d, point_per_ns, counter, sample);
      }
    }

    for (i64 bin_index = bin_begin; bin_index < bin_end; ++bin_index) {
      i64 bin_begin_time_ms = bin_index * bin_duration;
      i64 bin_end_time_ms = bin_begin_time_ms + bin_duration;
      usize sample_index = profile_counter__samples_lower_bound(
          series->samples, series->sample_count, bin_begin_time_ms);
      ZtracingProfileItemCounterSeriesSample *sample =
          series->samples + sample_index;

      if (sample->time < bin_end_time_ms) {
        profile_counter__paint_sample(widget, props, offset, &prev_x, &prev_h,
                                      d, point_per_ns, counter, sample);
      }
    }

    {
      usize last_sample_index = profile_counter__samples_lower_bound(
          series->samples, series->sample_count, bin_end * bin_duration);
      if (last_sample_index < series->sample_count) {
        ZtracingProfileItemCounterSeriesSample *sample =
            series->samples + (last_sample_index);
        profile_counter__paint_sample(widget, props, offset, &prev_x, &prev_h,
                                      d, point_per_ns, counter, sample);
      }
    }
  }
}

static UIWidgetClass profile_counter_class = {
    .name = "ProfileCounter",
    .props_size = sizeof(ProfileCounterProps),
    .layout = profile_counter_layout,
    .paint = profile_counter_paint,
};

static void profile_counter(const ProfileCounterProps *props) {
  ui_widget_begin(&profile_counter_class, props);
  ui_widget_end(&profile_counter_class);
}

static void profile_screen(ZtracingState *state) {
  ZtracingProfileViewer *viewer = state->viewer;
  if (!str8_is_empty(viewer->error)) {
    ui_center_begin(&(UICenterProps){0});
    ui_text(&(UITextProps){
        .text = ui_push_str8f("error: %.*s", (int)viewer->error.len,
                              viewer->error.ptr),
        .style = text_style_default(),
    });
    ui_center_end();
    return;
  }

  UIGestureDetailO drag_update;
  ui_gesture_detector_begin(&(UIGestureDetectorProps){
      .behaviour = UI_HIT_TEST_BEHAVIOUR_OPAQUE,
      .drag_update = &drag_update,
  });
  UIPointerEventO scroll;
  ui_pointer_listener_begin(&(UIPointerListenerProps){
      .scroll = &scroll,
  });
  if (drag_update.present) {
    UIWidget *widget = ui_widget_get_current();
    Vec2 delta = drag_update.value.delta;
    i64 duration = viewer->end_time_ns - viewer->begin_time_ns;
    f64 ns_per_point = (f64)duration / (f64)widget->size.x;
    i64 offset = (i64)(ns_per_point * (f64)delta.x);
    viewer->begin_time_ns -= offset;
    viewer->end_time_ns = viewer->begin_time_ns + duration;
  }

  if (scroll.present /* && ctrl */) {
    UIWidget *widget = ui_widget_get_current();
    f32 pivot = scroll.value.local_position.x / widget->size.x;
    i64 duration = viewer->end_time_ns - viewer->begin_time_ns;
    i64 pivot_time = viewer->begin_time_ns + pivot * duration;

    Vec2 delta = scroll.value.scroll_delta;
    if (delta.y < 0) {
      duration = i64_max(duration * 0.8f, 1000000);
    } else {
      i64 max_duration = (viewer->max_time_ns - viewer->min_time_ns) * 2.0;
      duration = i64_min(duration * 1.25f, max_duration);
    }

    viewer->begin_time_ns = pivot_time - pivot * duration;
    viewer->end_time_ns = viewer->begin_time_ns + duration;
  }

  ui_column_begin(&(UIColumnProps){0});
  timeline(&(TimelineProps){
      .begin_time_ns = viewer->begin_time_ns,
      .end_time_ns = viewer->end_time_ns,
  });

  ui_expanded_begin(&(UIExpandedProps){
      .flex = 1,
  });
  UIListBuilder builder;
  ui_list_view_begin(&(UIListViewProps){
      .item_extent = PROFILE_ITEM_HEIGHT,
      .item_count = viewer->item_count,
      .builder = &builder,
  });
  for (i32 item_index = builder.first_index; item_index <= builder.last_index;
       ++item_index) {
    ZtracingProfileItem *item = viewer->items + item_index;
    switch (item->type) {
      case ZTRACING_PROFILE_ITEM_HEADER: {
        ui_row_begin(&(UIRowProps){0});
        ui_padding_begin(&(UIPaddingProps){
            .padding = ui_edge_insets_symmetric(6, 0),
        });
        ui_text(&(UITextProps){
            .text = item->header.name,
            .style = text_style_default(),
        });
        ui_padding_end();
        ui_row_end();
      } break;

      case ZTRACING_PROFILE_ITEM_COUNTER: {
        profile_counter(&(ProfileCounterProps){
            .counter = &item->counter,
            .begin_time_ns = viewer->begin_time_ns,
            .end_time_ns = viewer->end_time_ns,
        });
      } break;

      default: {
        UNREACHABLE;
      } break;
    }
  }
  ui_list_view_end();
  ui_expanded_end();

  ui_column_end();

  ui_pointer_listener_end();
  ui_gesture_detector_end();
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
