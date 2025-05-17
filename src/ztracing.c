#include "src/ztracing.h"

#include <stdarg.h>
#include <stdlib.h>

#include "src/assert.h"
#include "src/flick.h"
#include "src/hash_trie.h"
#include "src/json.h"
#include "src/json_trace_profile.h"
#include "src/list.h"
#include "src/log.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/string.h"
#include "src/types.h"

#define RGB(r, g, b) {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}

static FL_Color COLORS[] = {
    RGB(38, 70, 83),   RGB(42, 157, 143), RGB(233, 196, 106),
    RGB(244, 162, 97), RGB(231, 111, 81),
};

typedef enum ZProfileItemType {
  Z_PROFILE_ITEM_HEADER,
  Z_PROFILE_ITEM_COUNTER,
  Z_PROFILE_ITEM_TRACK,
} ZProfileItemType;

typedef struct ZProfileItemHeader {
  ZProfileItemType type;  // Z_PROFILE_ITEM_HEADER
  Str8 name;
} ZProfileItemHeader;

typedef struct ZProfileSample {
  i64 time;
  f64 value;
} ZProfileSample;

typedef struct ZProfileSeries {
  Str8 name;
  u32 color_index;
  usize sample_count;
  ZProfileSample *samples;
} ZProfileSeries;

typedef struct ZProfileItemCounter {
  ZProfileItemType type;  // Z_PROFILE_ITEM_COUNTER
  Str8 name;
  usize series_count;
  ZProfileSeries *series;
  f64 min_value;
  f64 max_value;
} ZProfileItemCounter;

typedef struct ZProfileSpan {
  Str8 name;
  u32 color_index;
  i64 begin_time_ns;
  i64 end_time_ns;
  i64 self_duration_ns;
} ZProfileSpan;

typedef struct ZProfileItemTrack {
  ZProfileItemType type;  // Z_PROFILE_ITEM_TRACK
  usize span_count;
  ZProfileSpan *spans;
} ZProfileItemTrack;

typedef union ZProfileItem {
  ZProfileItemType type;
  ZProfileItemHeader header;
  ZProfileItemCounter counter;
  ZProfileItemTrack track;
} ZProfileItem;

typedef struct ZProfileViewer {
  Arena arena;
  Str8 name;
  Str8 error;
  i64 min_time_ns;
  i64 max_time_ns;
  i64 begin_time_ns;
  i64 end_time_ns;
  usize item_count;
  ZProfileItem *items;
  f32 scroll;
  FL_Widget *list_view;
} ZProfileViewer;

static ZProfileViewer *z_profile_viewer_alloc(void) {
  Arena arena_ = {0};
  ZProfileViewer *viewer = arena_push_struct(&arena_, ZProfileViewer);
  viewer->arena = arena_;
  return viewer;
}

static void z_profile_viewer_free(ZProfileViewer *self) {
  arena_free(&self->arena);
}

typedef struct ZFileLoader {
  Arena arena;
  ZFile *file;
  PlatformThread *thread;

  volatile ZProfileViewer *viewer;
} ZFileLoader;

static ZFileLoader *z_file_loader_alloc(ZFile *file) {
  Arena arena_ = {0};
  ZFileLoader *state = arena_push_struct(&arena_, ZFileLoader);
  state->arena = arena_;
  state->file = file;
  return state;
}

static Str8 z_file_get_input(void *c) {
  ZFile *self = c;
  Str8 buf = self->read(self);
  self->nread += buf.len;
  return buf;
}

static int z_file_loader_compare_json_trace_span(const void *a, const void *b) {
  JsonTraceSpan *sa = *(JsonTraceSpan *const *)a;
  JsonTraceSpan *sb = *(JsonTraceSpan *const *)b;
  int result = i64_cmp(sa->begin_time_ns, sb->begin_time_ns);
  if (result == 0) {
    result = i64_cmp(sa->end_time_ns, sb->end_time_ns);
  }
  return result;
}

typedef struct ZProfileSpanNode ZProfileSpanNode;
struct ZProfileSpanNode {
  ZProfileSpanNode *prev;
  ZProfileSpanNode *next;
  JsonTraceSpan *span;
  usize self_duration_ns;
};

typedef struct ZProfileTrackNode ZProfileTrackNode;
struct ZProfileTrackNode {
  ZProfileTrackNode *prev;
  ZProfileTrackNode *next;
  usize level;
  usize span_count;
  ZProfileSpanNode *first_span;
  ZProfileSpanNode *last_span;
};

static ZProfileSpanNode *z_profile_track_node_add_span(ZProfileTrackNode *self,
                                                       Arena *arena,
                                                       JsonTraceSpan *span) {
  ZProfileSpanNode *node = arena_push_struct(arena, ZProfileSpanNode);
  node->span = span;
  DLL_APPEND(self->first_span, self->last_span, node, prev, next);
  self->span_count += 1;
  return node;
}

typedef struct ZProfileThreadNode ZProfileThreadNode;
struct ZProfileThreadNode {
  ZProfileThreadNode *prev;
  ZProfileThreadNode *next;
  JsonTraceThread *thread;
  usize track_count;
  ZProfileTrackNode *first_track;
  ZProfileTrackNode *last_track;
};

static ZProfileTrackNode *z_profile_thread_node_upsert_track(
    ZProfileThreadNode *self, Arena *arena, usize level) {
  ZProfileTrackNode *track = self->first_track;
  isize i = level;
  while (track && i > 0) {
    i--;
    track = track->next;
  }

  while (i > 0 || !track) {
    track = arena_push_struct(arena, ZProfileTrackNode);
    track->level = level;
    DLL_APPEND(self->first_track, self->last_track, track, prev, next);
    self->track_count += 1;
    i--;
  }

  ASSERT(track->level == level);

  return track;
}

typedef struct ZProfileTrackBuilder {
  usize thread_count;
  ZProfileThreadNode *first_thread;
  ZProfileThreadNode *last_thread;
} ZProfileTrackBuilder;

static ZProfileThreadNode *z_profile_track_builder_add_thread(
    ZProfileTrackBuilder *self, JsonTraceThread *thread, Arena *arena) {
  ZProfileThreadNode *thread_node =
      arena_push_struct(arena, ZProfileThreadNode);
  thread_node->thread = thread;
  DLL_APPEND(self->first_thread, self->last_thread, thread_node, prev, next);
  self->thread_count += 1;
  return thread_node;
}

static usize z_file_loader_merge_spans(Arena *arena, ZProfileThreadNode *thread,
                                       usize level, i64 parent_begin_time_ns,
                                       i64 parent_end_time_ns,
                                       JsonTraceSpan **spans, usize span_count,
                                       usize span_index,
                                       i64 *total_duration_ns) {
  i64 total = 0;
  for (; span_index < span_count;) {
    JsonTraceSpan *span = spans[span_index];
    i64 span_duration_ns = span->end_time_ns - span->begin_time_ns;
    if (span->begin_time_ns >= parent_begin_time_ns &&
        span->end_time_ns <= parent_end_time_ns) {
      ZProfileTrackNode *track =
          z_profile_thread_node_upsert_track(thread, arena, level);
      ZProfileSpanNode *span_node =
          z_profile_track_node_add_span(track, arena, span);
      i64 children_duration_ns;
      span_index = z_file_loader_merge_spans(
          arena, thread, level + 1, span->begin_time_ns, span->end_time_ns,
          spans, span_count, span_index + 1, &children_duration_ns);
      span_node->self_duration_ns =
          i64_max(span_duration_ns - children_duration_ns, 0);

      total += span_duration_ns;
    } else {
      break;
    }
  }
  *total_duration_ns = total;
  return span_index;
}

static void z_file_loader_build_track_with_thread(
    JsonTraceThread *thread, Arena *arena, ZProfileTrackBuilder *builder) {
  ZProfileThreadNode *thread_node =
      z_profile_track_builder_add_thread(builder, thread, arena);

  Scratch scratch = scratch_begin(&arena, 1);
  if (thread->span_count > 0) {
    JsonTraceSpan **spans = arena_push_array_no_zero(
        scratch.arena, JsonTraceSpan *, thread->span_count);
    usize span_index = 0;
    for (JsonTraceSpan *span = thread->first_span; span; span = span->next) {
      spans[span_index++] = span;
    }
    qsort(spans, thread->span_count, sizeof(JsonTraceSpan *),
          z_file_loader_compare_json_trace_span);

    JsonTraceSpan *first_span = spans[0];
    i64 begin_time_ns = first_span->begin_time_ns;
    i64 end_time_ns = first_span->end_time_ns;
    for (span_index = 1; span_index < thread->span_count; ++span_index) {
      end_time_ns = i64_max(end_time_ns, spans[span_index]->end_time_ns);
    }

    i64 total_duration_ns;
    z_file_loader_merge_spans(arena, thread_node, 0, begin_time_ns, end_time_ns,
                              spans, thread->span_count, 0, &total_duration_ns);
  }
  scratch_end(scratch);
}

static int z_file_loader_compare_thread(const void *a, const void *b) {
  JsonTraceThread *ta = *(JsonTraceThread *const *)a;
  JsonTraceThread *tb = *(JsonTraceThread *const *)b;
  if (ta->sort_index.present) {
    if (tb->sort_index.present) {
      return i64_cmp(ta->sort_index.value, tb->sort_index.value);
    } else {
      return -1;
    }
  } else if (tb->sort_index.present) {
    return 1;
  }

  Scratch scratch = scratch_begin(0, 0);
  int result = str8_cmp(str8_to_uppercase(ta->name, scratch.arena),
                        str8_to_uppercase(tb->name, scratch.arena));
  scratch_end(scratch);
  return result;
}

static void z_file_loader_build_track_with_process(
    JsonTraceProcess *process, Arena *arena, ZProfileTrackBuilder *builder) {
  Scratch scratch = scratch_begin(&arena, 1);

  JsonTraceThread **threads = arena_push_array_no_zero(
      scratch.arena, JsonTraceThread *, process->thread_count);
  usize thread_index = 0;
  HashTrieIter thread_iter = hash_trie_iter(&process->threads, scratch.arena);
  JsonTraceThread *thread;
  while ((thread = hash_trie_iter_next(&thread_iter, JsonTraceThread))) {
    threads[thread_index++] = thread;
  }
  qsort(threads, process->thread_count, sizeof(*threads),
        z_file_loader_compare_thread);

  for (thread_index = 0; thread_index < process->thread_count; ++thread_index) {
    z_file_loader_build_track_with_thread(threads[thread_index], arena,
                                          builder);
  }

  scratch_end(scratch);
}

static void z_file_loader_build_track(JsonTraceProfile *profile, Arena *arena,
                                      ZProfileTrackBuilder *builder) {
  Scratch scratch = scratch_begin(&arena, 1);
  HashTrieIter process_iter =
      hash_trie_iter(&profile->processes, scratch.arena);
  JsonTraceProcess *process;
  while ((process = hash_trie_iter_next(&process_iter, JsonTraceProcess))) {
    z_file_loader_build_track_with_process(process, arena, builder);
  }
  scratch_end(scratch);
}

static int z_file_loader_compare_sample(const void *a, const void *b) {
  const ZProfileSample *sa = a;
  const ZProfileSample *sb = b;
  return i64_cmp(sa->time, sb->time);
}

static void z_file_loader_collect_series(Arena *arena, ZProfileItemCounter *c,
                                         JsonTraceCounter *counter) {
  Scratch scratch = scratch_begin(&arena, 1);
  HashTrieIter series_iter = hash_trie_iter(&counter->series, scratch.arena);
  usize series_index = 0;
  JsonTraceSeries *series;
  while ((series = hash_trie_iter_next(&series_iter, JsonTraceSeries))) {
    ZProfileSeries *s = &c->series[series_index++];

    s->name = str8_dup(arena, series->name);
    s->color_index = str8_hash(s->name) % ARRAY_COUNT(COLORS);
    s->sample_count = series->sample_count;
    s->samples = arena_push_array(arena, ZProfileSample, s->sample_count);

    usize sample_index = 0;
    for (JsonTraceSample *sample = series->first; sample;
         sample = sample->next) {
      s->samples[sample_index++] = (ZProfileSample){
          .time = sample->time,
          .value = sample->value,
      };
    }

    qsort(s->samples, s->sample_count, sizeof(*s->samples),
          z_file_loader_compare_sample);
  }
  scratch_end(scratch);
}

static int z_file_loader_compare_counter(const void *a, const void *b) {
  Scratch scratch = scratch_begin(0, 0);
  JsonTraceCounter *const *ca = a;
  JsonTraceCounter *const *cb = b;
  int result = str8_cmp(str8_to_uppercase((*ca)->name, scratch.arena),
                        str8_to_uppercase((*cb)->name, scratch.arena));
  scratch_end(scratch);
  return result;
}

static void z_file_loader_collect_counters(Arena *arena, ZProfileItem *items,
                                           usize *item_index,
                                           JsonTraceProcess *process) {
  Scratch scratch = scratch_begin(&arena, 1);

  JsonTraceCounter **sorted_counters = arena_push_array(
      scratch.arena, JsonTraceCounter *, process->counter_count);
  {
    usize counter_index = 0;
    HashTrieIter counter_iter =
        hash_trie_iter(&process->counters, scratch.arena);
    JsonTraceCounter *counter;
    while ((counter = hash_trie_iter_next(&counter_iter, JsonTraceCounter))) {
      sorted_counters[counter_index++] = counter;
    }
  }
  qsort(sorted_counters, process->counter_count, sizeof(*sorted_counters),
        z_file_loader_compare_counter);

  for (usize counter_index = 0; counter_index < process->counter_count;
       ++counter_index) {
    JsonTraceCounter *counter = sorted_counters[counter_index];

    items[(*item_index)++] = (ZProfileItem){
        .header =
            {
                .type = Z_PROFILE_ITEM_HEADER,
                .name = str8_dup(arena, counter->name),
            },
    };

    ZProfileItemCounter c = {
        .type = Z_PROFILE_ITEM_COUNTER,
    };
    c.name = str8_dup(arena, counter->name);
    c.series_count = counter->series_count;
    c.series = arena_push_array(arena, ZProfileSeries, c.series_count);
    c.min_value = counter->min_value;
    c.max_value = counter->max_value;

    z_file_loader_collect_series(arena, &c, counter);

    items[(*item_index)++] = (ZProfileItem){
        .counter = c,
    };
  }
  scratch_end(scratch);
}

static int z_file_loader_compare_profile_span(const void *a, const void *b) {
  const ZProfileSpan *sa = a;
  const ZProfileSpan *sb = b;
  int result = i64_cmp(sa->end_time_ns, sb->end_time_ns);
  if (result == 0) {
    result = i64_cmp(sa->begin_time_ns, sb->begin_time_ns);
  }
  return result;
}

static void z_file_loader_collect_tracks(Arena *arena, ZProfileItem *items,
                                         usize *item_index,
                                         ZProfileTrackBuilder *track_builder) {
  for (ZProfileThreadNode *thread = track_builder->first_thread; thread;
       thread = thread->next) {
    Str8 name;
    if (str8_is_empty(thread->thread->name)) {
      name = arena_push_str8f(arena, "Thread %lld", thread->thread->tid);
    } else {
      name = str8_dup(arena, thread->thread->name);
    }
    items[(*item_index)++] = (ZProfileItem){
        .header =
            {
                .type = Z_PROFILE_ITEM_HEADER,
                .name = name,
            },
    };

    for (ZProfileTrackNode *track = thread->first_track; track;
         track = track->next) {
      ZProfileSpan *spans =
          arena_push_array_no_zero(arena, ZProfileSpan, track->span_count);

      ZProfileSpanNode *span_node = track->first_span;
      for (usize span_index = 0; span_index < track->span_count; ++span_index) {
        ZProfileSpan *span = spans + span_index;
        span->name = str8_dup(arena, span_node->span->name);
        span->color_index = str8_hash(span->name) % ARRAY_COUNT(COLORS);
        span->begin_time_ns = span_node->span->begin_time_ns;
        span->end_time_ns = span_node->span->end_time_ns;
        span->self_duration_ns = span_node->self_duration_ns;
        span_node = span_node->next;
      }
      qsort(spans, track->span_count, sizeof(ZProfileSpan),
            z_file_loader_compare_profile_span);

      items[(*item_index)++] = (ZProfileItem){
          .track =
              {
                  .type = Z_PROFILE_ITEM_TRACK,
                  .span_count = track->span_count,
                  .spans = spans,
              },
      };
    }
  }
}

static void z_file_loader_collect_items(Arena *arena, ZProfileItem *items,
                                        JsonTraceProfile *profile,
                                        ZProfileTrackBuilder *track_builder) {
  Scratch scratch = scratch_begin(&arena, 1);
  usize item_index = 0;
  HashTrieIter process_iter =
      hash_trie_iter(&profile->processes, scratch.arena);
  JsonTraceProcess *process;
  while ((process = hash_trie_iter_next(&process_iter, JsonTraceProcess))) {
    z_file_loader_collect_counters(arena, items, &item_index, process);
  }

  z_file_loader_collect_tracks(arena, items, &item_index, track_builder);

  scratch_end(scratch);
}

static usize z_file_loader_count_items(JsonTraceProfile *profile,
                                       ZProfileTrackBuilder *track_builder) {
  usize item_count = 0;
  Scratch scratch = scratch_begin(0, 0);
  HashTrieIter process_iter =
      hash_trie_iter(&profile->processes, scratch.arena);
  JsonTraceProcess *process;
  while ((process = hash_trie_iter_next(&process_iter, JsonTraceProcess))) {
    item_count += 2 * process->counter_count;
  }

  item_count += track_builder->thread_count;
  for (ZProfileThreadNode *thread = track_builder->first_thread; thread;
       thread = thread->next) {
    item_count += thread->track_count;
  }

  scratch_end(scratch);
  return item_count;
}

static void z_file_loader_convert_profile(ZProfileViewer *viewer,
                                          JsonTraceProfile *profile) {
  Scratch scratch = scratch_begin(0, 0);
  ZProfileTrackBuilder track_builder = {0};
  z_file_loader_build_track(profile, scratch.arena, &track_builder);

  viewer->item_count = z_file_loader_count_items(profile, &track_builder);
  viewer->items =
      arena_push_array(&viewer->arena, ZProfileItem, viewer->item_count);
  z_file_loader_collect_items(&viewer->arena, viewer->items, profile,
                              &track_builder);

  viewer->min_time_ns = profile->min_time_ns;
  viewer->max_time_ns = profile->max_time_ns;

  i64 duration = (profile->max_time_ns - profile->min_time_ns);
  i64 offset = duration * 0.1;
  viewer->begin_time_ns = profile->min_time_ns - offset;
  viewer->end_time_ns = profile->max_time_ns + offset;
  scratch_end(scratch);
}

static int z_file_loader_thread(void *self_) {
  ZFileLoader *self = self_;
  ZFile *file = self->file;

  ZProfileViewer *viewer = z_profile_viewer_alloc();
  viewer->name = str8_dup(&viewer->arena, file->name);

  Scratch scratch = scratch_begin(0, 0);
  JsonParser parser = json_parser(z_file_get_input, file);
  u64 before = platform_get_perf_counter();
  JsonTraceProfile *profile = json_trace_profile_parse(scratch.arena, &parser);
  u64 after = platform_get_perf_counter();

  if (!file->interrupted) {
    f64 mb = (f64)file->nread / 1024.0 / 1024.0;
    f64 secs = (f64)(after - before) / (f64)platform_get_perf_freq();
    INFO("Loaded %.1f MiB over %.1f seconds, %.1f MiB / s", mb, secs,
         mb / secs);

    if (str8_is_empty(profile->error)) {
      z_file_loader_convert_profile(viewer, profile);
    } else {
      viewer->error = str8_dup(&viewer->arena, profile->error);
    }

    self->viewer = viewer;
  }

  scratch_end(scratch);
  scratch_free_all();

  return 0;
}

static void z_file_loader_start(ZFileLoader *self) {
  self->thread =
      platform_thread_start(z_file_loader_thread, "FileLoader", self);
}

static bool z_file_loader_is_done(ZFileLoader *self) { return self->viewer; }

static void z_file_loader_free(ZFileLoader *self) {
  self->file->interrupted = true;
  platform_thread_wait(self->thread);

  self->file->close(self->file);
  arena_free(&self->arena);
}

typedef struct ZState {
  f32 dt;
  f32 frame_time;
  ZFileLoader *loader;
  ZProfileViewer *viewer;
} ZState;

static FL_TextStyleO DefaultTextStyle(void) {
#define FONT_SIZE_DEFAULT 13
  return FL_TextStyle_Some((FL_TextStyle){
      .color = FL_Color_Some((FL_Color){0, 0, 0, 1}),
      .font_size = FL_f32_Some(FONT_SIZE_DEFAULT),
  });
}

typedef enum ButtonType {
  BUTTON_PRIMARY,
  BUTTON_SECONDARY,
} ButtonType;

// static bool do_button(Str8 text, ButtonType type) {
//   bool pressed;
//   ui_button(&(UIButtonProps){
//       .text = text,
//       .text_style = DefaultTextStyle(),
//
//       .pressed = &pressed,
//       .fill_color = (type == BUTTON_PRIMARY)
//                         ? ui_color_some(ui_color(0.73, 0.83, 0.95, 1))
//                         : ui_color_none(),
//       .hover_color = ui_color_some(ui_color(0.29, 0.49, 0.72, 1)),
//       .splash_color = ui_color_some(ui_color(0.29, 0.44, 0.62, 1)),
//       .padding = ui_edge_insets_some(ui_edge_insets_symmetric(6, 3)),
//   });
//   return pressed;
// }

static FL_Widget *UI_GlobalMenuBar(ZState *state) {
  Str8 name = {0};
  if (state->loader) {
    name = state->loader->file->name;
  } else if (state->viewer) {
    name = state->viewer->name;
  }

  return FL_Padding(&(FL_PaddingProps){
      .padding = FL_EdgeInsets_Symmetric(6, 4),
      .child = FL_Row(&(FL_RowProps){
          .children = FL_WidgetList_Make((FL_Widget *[]){
              FL_Text(&(FL_TextProps){
                  .text = name,
                  .style = DefaultTextStyle(),
              }),
              FL_Expanded(&(FL_ExpandedProps){.flex = 1}),
              FL_Text(&(FL_TextProps){
                  .text = FL_Format(
                      "%.1fMB %.1fms",
                      (f32)((f64)platform_memory_get_allocated_bytes() /
                            1024.0 / 1024.0),
                      state->frame_time * 1000.0f),
                  .style = DefaultTextStyle(),
              }),
              0,
          }),
      }),
  });
}

static FL_Widget *UI_WelcomeScreen(void) {
  Str8 logo = FL_STR_C(
      // clang-format off
      " ________  _________  _______          _        ______  _____  ____  _____   ______\n"
      "|  __   _||  _   _  ||_   __ \\        / \\     .' ___  ||_   _||_   \\|_   _|.' ___  |\n"
      "|_/  / /  |_/ | | \\_|  | |__) |      / _ \\   / .'   \\_|  | |    |   \\ | | / .'   \\_|\n"
      "   .'.' _     | |      |  __ /      / ___ \\  | |         | |    | |\\ \\| | | |   ____\n"
      " _/ /__/ |   _| |_    _| |  \\ \\_  _/ /   \\ \\_\\ `.___.'\\ _| |_  _| |_\\   |_\\ `.___]  |\n"
      "|________|  |_____|  |____| |___||____| |____|`.____ .'|_____||_____|\\____|`._____.'"
      // clang-format on
  );
  return FL_Column(&(FL_ColumnProps){
      .main_axis_alignment = FL_MainAxisAlignment_Center,
      .children = FL_WidgetList_Make((FL_Widget *[]){
          FL_Text(&(FL_TextProps){
              .text = logo,
              .style = DefaultTextStyle(),
          }),
          FL_Padding(&(FL_PaddingProps){
              .padding = FL_EdgeInsets_Symmetric(0, 10),
          }),
          FL_Text(&(FL_TextProps){
              .text = FL_STR_C("Drag & Drop a json trace profile to start."),
              .style = DefaultTextStyle(),
          }),
          0,
      }),
  });
}

static FL_Widget *UI_LoadingScreen(ZState *state) {
  ZFileLoader *loader = state->loader;

  FL_Widget *widget = FL_Center(&(FL_CenterProps){
      .child = FL_Text(&(FL_TextProps){
          .text = FL_Format("Loading (%.1f MiB) ...",
                            (f64)loader->file->nread / 1024.0 / 1024.0),
          .style = DefaultTextStyle(),
      }),
  });

  if (z_file_loader_is_done(loader)) {
    state->viewer = (ZProfileViewer *)loader->viewer;
    state->loader = 0;
    z_file_loader_free(loader);
  }

  return widget;
}

typedef struct UI_TimelineProps {
  i64 begin_time_ns;
  i64 end_time_ns;
} UI_TimelineProps;

static i64 timeline_calc_block_duration(i64 duration, f32 width,
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

static Str8 timeline_format_time(Arena *arena, i64 time, i64 duration) {
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
  char *period = buf.ptr + buf.len - 1;
  while (*period != '.') {
    period--;
  }
  if (*(period + 1) == '0') {
    memory_move(period, period + 2, buf.ptr + buf.len - period - 1);
    buf.len -= 2;
  }
  return buf;
}

#define UI_ProfileItemHeight 20

static void UI_Timeline_Layout(FL_Widget *widget,
                               FL_BoxConstraints constraints) {
  widget->size = FL_BoxConstraints_Constrain(
      constraints, (Vec2){F32_INFINITY, UI_ProfileItemHeight});
}

static void UI_Timeline_Paint(FL_Widget *widget, FL_PaintingContext *context,
                              Vec2 offset) {
  (void)context;

  UI_TimelineProps *props = FL_Widget_GetProps(widget, UI_TimelineProps);
  i64 begin = props->begin_time_ns;
  i64 end = props->end_time_ns;

  FL_Color color = {0, 0, 0, 1};

  Vec2 size = widget->size;
  f32 font_size = DefaultTextStyle().value.font_size.value;

  i64 duration = end - begin;
  i64 block_duration =
      timeline_calc_block_duration(duration, size.x, size.y * 1.5f);
  i64 large_block_duration = block_duration * 5;

  f32 line_thickness = 1.0f;
  FL_Canvas_FillRect(
      context->canvas,
      (FL_Rect){offset.x, offset.x + size.x, offset.y + size.y - line_thickness,
                offset.y + size.y},
      color);

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
    FL_Canvas_FillRect(
        context->canvas,
        (FL_Rect){x, x + line_thickness, bottom - height, bottom}, color);

    if (is_large_block) {
      Scratch scratch = scratch_begin(0, 0);
      // draw_text_str8(vec2(x + 4, bottom - 2 - font_size),
      //                timeline_format_time(scratch.arena, t, duration),
      //                font_size, 0, large_block_width - 4, color);
      scratch_end(scratch);
    }

    t += block_duration;
  }
}

static FL_WidgetClass UI_TimelineClass = {
    .name = "Timeline",
    .props_size = FL_SIZE_OF(UI_TimelineProps),
    .layout = UI_Timeline_Layout,
    .paint = UI_Timeline_Paint,
};

static FL_Widget *UI_Timeline(const UI_TimelineProps *props) {
  return FL_Widget_Create(&UI_TimelineClass, FL_Key_Zero(), props);
}

static void UI_ProfileItem_Layout(FL_Widget *widget,
                                  FL_BoxConstraints constraints) {
  widget->size = FL_BoxConstraints_Constrain(
      constraints, vec2(F32_INFINITY, UI_ProfileItemHeight));
}

typedef struct UI_ProfileCounterProps {
  ZProfileItemCounter *counter;
  i64 begin_time_ns;
  i64 end_time_ns;
} UI_ProfileCounterProps;

static usize UI_ProfileCounter_FindSamplesLowerBound(ZProfileSample *samples,
                                                     usize begin, usize end,
                                                     i64 time) {
  usize low = begin;
  usize high = end;

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

static void UI_ProfileCounter_PaintSample(FL_PaintingContext *context,
                                          Vec2 size, Vec2 offset, i64 bin_begin,
                                          i64 bin_duration, f32 bin_width,
                                          f32o *prev_x, f32o *prev_h, f32 d,
                                          ZProfileItemCounter *counter,
                                          ZProfileSeries *series,
                                          ZProfileSample *sample) {
  f32 bottom = offset.y + size.y;
  i64 sample_bin_begin = sample->time / bin_duration;
  f32 x = offset.x + (sample_bin_begin - bin_begin) * bin_width;
  f32 height = f32_max(1, (sample->value - counter->min_value) / d * size.y);
  if (prev_x->present) {
    f32 px = prev_x->value;
    f32 ph = prev_h->value;
    f32 top = bottom - ph;
    f32 left = px;
    f32 width = x - px;
    f32 right = px + width;
    // Vec2 min = vec2(px, top);
    // Vec2 max = vec2(px + width, bottom);
    FL_Canvas_FillRect(context->canvas, (FL_Rect){left, right, top, bottom},
                       COLORS[series->color_index]);
    if (height != ph) {
      FL_Canvas_FillRect(context->canvas,
                         (FL_Rect){right - 1, right, top, bottom - height},
                         (FL_Color){0, 0, 0, 0.5f});
    }
    FL_Canvas_FillRect(context->canvas, (FL_Rect){left, right, top, top + 1},
                       (FL_Color){0, 0, 0, 0.5f});
  } else {
    FL_Canvas_FillRect(context->canvas,
                       (FL_Rect){x - 1, x, bottom - height, bottom},
                       (FL_Color){0, 0, 0, 0.5f});
  }
  *prev_x = f32_some(x);
  *prev_h = f32_some(height);
}

static void UI_ProfileCounter_Paint(FL_Widget *widget,
                                    FL_PaintingContext *context, Vec2 offset) {
  (void)context;

  UI_ProfileCounterProps *props =
      FL_Widget_GetProps(widget, UI_ProfileCounterProps);

  ZProfileItemCounter *counter = props->counter;
  f64 d = (counter->max_value - counter->min_value);
  f32 point_per_ns =
      (f32)widget->size.x / (f32)(props->end_time_ns - props->begin_time_ns);

  f32 bin_width = 2.0f;
  f32 ns_per_point = 1.0f / point_per_ns;
  i64 bin_duration = f32_round(ns_per_point * bin_width);
  i64 bin_begin = props->begin_time_ns / bin_duration;
  i64 bin_end = props->end_time_ns / bin_duration + 1;
  Vec2 sample_offset =
      vec2(offset.x -
               (props->begin_time_ns - bin_begin * bin_duration) * point_per_ns,
           f32_round(offset.y));

  for (usize series_index = 0; series_index < counter->series_count;
       ++series_index) {
    ZProfileSeries *series = counter->series + series_index;

    f32o prev_x = f32_none();
    f32o prev_h = f32_none();

    usize prev_sample_index = 0;
    {
      usize first_sample_index = UI_ProfileCounter_FindSamplesLowerBound(
          series->samples, 0, series->sample_count, bin_begin * bin_duration);
      if (first_sample_index > 0) {
        usize sample_index = first_sample_index - 1;
        prev_sample_index = sample_index;
        ZProfileSample *sample = series->samples + sample_index;
        UI_ProfileCounter_PaintSample(
            context, widget->size, sample_offset, bin_begin, bin_duration,
            bin_width, &prev_x, &prev_h, d, counter, series, sample);
      }
    }

    for (i64 bin_index = bin_begin; bin_index < bin_end; ++bin_index) {
      i64 bin_begin_time_ns = bin_index * bin_duration;
      i64 bin_end_time_ns = bin_begin_time_ns + bin_duration;
      usize sample_index = UI_ProfileCounter_FindSamplesLowerBound(
          series->samples, prev_sample_index, series->sample_count,
          bin_begin_time_ns);
      if (sample_index < series->sample_count) {
        prev_sample_index = sample_index;
        ZProfileSample *sample = series->samples + sample_index;
        if (sample->time < bin_end_time_ns) {
          UI_ProfileCounter_PaintSample(
              context, widget->size, sample_offset, bin_begin, bin_duration,
              bin_width, &prev_x, &prev_h, d, counter, series, sample);
        }
      }
    }

    {
      usize last_sample_index = UI_ProfileCounter_FindSamplesLowerBound(
          series->samples, 0, series->sample_count, bin_end * bin_duration);
      if (last_sample_index < series->sample_count) {
        usize sample_index = last_sample_index;
        ZProfileSample *sample = series->samples + sample_index;
        UI_ProfileCounter_PaintSample(
            context, widget->size, sample_offset, bin_begin, bin_duration,
            bin_width, &prev_x, &prev_h, d, counter, series, sample);
      }
    }

    if (prev_x.present) {
      f32 bottom = offset.y + widget->size.y;
      f32 x = prev_x.value;
      f32 height = prev_h.value;
      FL_Canvas_FillRect(context->canvas,
                         (FL_Rect){x, x + 1, bottom - height, bottom},
                         (FL_Color){0, 0, 0, 0.5f});
    }
  }
}

static FL_WidgetClass UI_ProfileCounterClass = {
    .name = "ProfileCounter",
    .props_size = FL_SIZE_OF(UI_ProfileCounterProps),
    .layout = UI_ProfileItem_Layout,
    .paint = UI_ProfileCounter_Paint,
};

static FL_Widget *UI_ProfileCounter(const UI_ProfileCounterProps *props) {
  return FL_Widget_Create(&UI_ProfileCounterClass, FL_Key_Zero(), props);
}

typedef struct UI_ProfileTrackProps {
  ZProfileItemTrack *track;
  i64 begin_time_ns;
  i64 end_time_ns;
} UI_ProfileTrackProps;

// Find the first span with in range [begin, end) whose end_time_ns > time.
static usize UI_ProfileTrack_FindSpansUpperBound(ZProfileSpan *spans,
                                                 usize begin, usize end,
                                                 i64 time) {
  usize low = begin;
  usize high = end;

  while (low < high) {
    usize mid = low + (high - low) / 2;
    i64 mid_t = spans[mid].end_time_ns;
    if (mid_t <= time) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }

  return low;
}

static void UI_ProfileTrack_Paint(FL_Widget *widget,
                                  FL_PaintingContext *context, Vec2 offset) {
  (void)context;

  UI_ProfileTrackProps *props =
      FL_Widget_GetProps(widget, UI_ProfileTrackProps);

  f32 point_per_ns =
      (f32)widget->size.x / (f32)(props->end_time_ns - props->begin_time_ns);

  f32 bin_width = 4.0f;
  f32 ns_per_point = 1.0f / point_per_ns;
  i64 bin_duration = f32_round(ns_per_point * bin_width);
  i64 bin_begin = props->begin_time_ns / bin_duration - 1;
  i64 bin_end = props->end_time_ns / bin_duration + 1;
  f32 offset_x = offset.x - (props->begin_time_ns - bin_begin * bin_duration) *
                                point_per_ns;

  ZProfileItemTrack *track = props->track;
  usize last_span_index = track->span_count;
  for (i64 bin_index = bin_end; bin_index >= bin_begin; --bin_index) {
    i64 bin_begin_time_ns = bin_index * bin_duration;
    usize span_index = UI_ProfileTrack_FindSpansUpperBound(
        track->spans, 0, last_span_index, bin_begin_time_ns);
    if (span_index < last_span_index) {
      last_span_index = span_index;
      ZProfileSpan *span = track->spans + span_index;
      i64 span_bin_begin = span->begin_time_ns / bin_duration;
      i64 span_bin_end = span->end_time_ns / bin_duration +
                         (span->end_time_ns % bin_duration ? 1 : 0);

      f32 left = offset_x + (span_bin_begin - bin_begin) * bin_width;
      f32 right =
          left + f32_max(1, (span_bin_end - span_bin_begin)) * bin_width;
      left = f32_max(left, offset.x);
      right = f32_max(f32_min(right, offset.x + widget->size.x), left);

      if (right > left) {
        Vec2 min = vec2(left, offset.y);
        Vec2 max = vec2(right, offset.y + widget->size.y);
        f32 width = max.x - min.x;
        f32 height = max.y - min.y;
        FL_Canvas_FillRect(context->canvas,
                           (FL_Rect){min.x, max.x, min.y, max.y},
                           COLORS[span->color_index]);
        FL_Canvas_StrokeRect(context->canvas,
                             (FL_Rect){min.x, max.x, min.y, max.y},
                             (FL_Color){0, 0, 0, 0.5}, 1);

        FL_TextStyle text_style = DefaultTextStyle().value;
        f32 font_size = text_style.font_size.value;
        FL_Color text_color = text_style.color.value;
        // index-0 color is dark color, use white text color.
        if (span->color_index == 0) {
          text_color = (FL_Color){1, 1, 1, 1};
        }

        f32 text_padding_x = 8;
        if (width - 2 * text_padding_x > 0) {
          FL_TextMetrics metrics =
              FL_Canvas_MeasureText(context->canvas, span->name, font_size);

          f32 text_left = f32_max(left + text_padding_x,
                                  left + (width - metrics.width) / 2.0f);
          f32 text_top = offset.y +
                         (height - (metrics.font_bounding_box_ascent +
                                    metrics.font_bounding_box_descent)) /
                             2.0f +
                         metrics.font_bounding_box_ascent;
          bool should_clip = metrics.width + 2 * text_padding_x > width;
          if (should_clip) {
            FL_Canvas_Save(context->canvas);
            FL_Canvas_ClipRect(context->canvas,
                               (FL_Rect){min.x + text_padding_x,
                                         max.x - text_padding_x, min.y, max.y});
          }
          FL_Canvas_FillText(context->canvas, span->name, text_left, text_top,
                             font_size, text_color);
          if (should_clip) {
            FL_Canvas_Restore(context->canvas);
          }
        }
      }
    }
  }
}

static FL_Widget *UI_ProfileHeader(ZProfileItemHeader *header) {
  FL_Color line_color = {0.5, 0.5, 0.5, 1};
  f32 line_height = 2;
  return FL_Row(&(FL_RowProps){
      .children = FL_WidgetList_Make((FL_Widget *[]){
          FL_Container(&(FL_ContainerProps){
              .width = FL_f32_Some(16),
              .height = FL_f32_Some(line_height),
              .color = FL_Color_Some(line_color),
          }),
          FL_Padding(&(FL_PaddingProps){
              .padding = FL_EdgeInsets_Symmetric(8, 0),
              .child = FL_Text(&(FL_TextProps){
                  .text = header->name,
                  .style = DefaultTextStyle(),
              }),
          }),
          FL_Expanded(&(FL_ExpandedProps){
              .flex = 1,
              .child = FL_Container(&(FL_ContainerProps){
                  .height = FL_f32_Some(line_height),
                  .color = FL_Color_Some(line_color),
              }),
          }),
          0,
      }),
  });
}

static FL_WidgetClass UI_ProfileTrackClass = {
    .name = "ProfileTrack",
    .props_size = FL_SIZE_OF(UI_ProfileTrackProps),
    .layout = UI_ProfileItem_Layout,
    .paint = UI_ProfileTrack_Paint,
};

static FL_Widget *UI_ProfileTrack(const UI_ProfileTrackProps *props) {
  return FL_Widget_Create(&UI_ProfileTrackClass, FL_Key_Zero(), props);
}

static void UI_ProfileItem_OnScroll(void *ctx, FL_PointerEvent event) {
  ZProfileViewer *viewer = ctx;
  FL_Widget *widget = viewer->list_view;
  if (FL_PointerEventResolver_Register(widget)) {
    f32 pivot = event.local_position.x / widget->size.x;
    i64 duration = viewer->end_time_ns - viewer->begin_time_ns;
    i64 pivot_time = viewer->begin_time_ns + pivot * duration;

    Vec2 delta = event.delta;
    if (delta.y < 0) {
      duration = i64_max(duration * 0.8f, 1000);
    } else {
      i64 max_duration = (viewer->max_time_ns - viewer->min_time_ns) * 2.0;
      duration = i64_min(duration * 1.25f, max_duration);
    }

    viewer->begin_time_ns = pivot_time - pivot * duration;
    viewer->end_time_ns = viewer->begin_time_ns + duration;
  }
}

static FL_Widget *UI_ProfileItem(void *ctx, FL_i32 item_index) {
  ZProfileViewer *viewer = ctx;
  ZProfileItem *item = viewer->items + item_index;
  FL_Widget *child;
  switch (item->type) {
    case Z_PROFILE_ITEM_HEADER: {
      child = UI_ProfileHeader(&item->header);
    } break;

    case Z_PROFILE_ITEM_COUNTER: {
      child = UI_ProfileCounter(&(UI_ProfileCounterProps){
          .counter = &item->counter,
          .begin_time_ns = viewer->begin_time_ns,
          .end_time_ns = viewer->end_time_ns,
      });
    } break;

    case Z_PROFILE_ITEM_TRACK: {
      child = UI_ProfileTrack(&(UI_ProfileTrackProps){
          .track = &item->track,
          .begin_time_ns = viewer->begin_time_ns,
          .end_time_ns = viewer->end_time_ns,
      });
    } break;

    default: {
      UNREACHABLE;
    } break;
  }

  return FL_PointerListener(&(FL_PointerListenerProps){
      .context = viewer,
      .on_scroll = UI_ProfileItem_OnScroll,
      .child = child,
  });
}

static void UI_ProfileScreen_OnDragUpdate(void *ctx,
                                          FL_GestureDetails details) {
  ZProfileViewer *viewer = ctx;
  FL_Widget *widget = viewer->list_view;
  Vec2 delta = details.delta;
  i64 duration = viewer->end_time_ns - viewer->begin_time_ns;
  f64 ns_per_point = (f64)duration / (f64)widget->size.x;
  i64 offset = (i64)(ns_per_point * (f64)delta.x);
  viewer->begin_time_ns -= offset;
  viewer->end_time_ns = viewer->begin_time_ns + duration;
  viewer->scroll -= delta.y;
}

static FL_Widget *UI_ProfileScreen(ZState *state) {
  ZProfileViewer *viewer = state->viewer;
  if (!str8_is_empty(viewer->error)) {
    return FL_Center(&(FL_CenterProps){
        .child = FL_Text(&(FL_TextProps){
            .text = FL_Format("error: %.*s", (int)viewer->error.len,
                              viewer->error.ptr),
            .style = DefaultTextStyle(),
        }),
    });
  }

  viewer->list_view = FL_ListView(&(FL_ListViewProps){
      .item_extent = UI_ProfileItemHeight,
      .item_count = viewer->item_count,
      .item_builder = {.build = UI_ProfileItem, .ptr = viewer},
      .scroll = &viewer->scroll,
  });

  return FL_Column(&(FL_ColumnProps){
      .children = FL_WidgetList_Make((FL_Widget *[]){
          UI_Timeline(&(UI_TimelineProps){
              .begin_time_ns = viewer->begin_time_ns,
              .end_time_ns = viewer->end_time_ns,
          }),
          FL_Expanded(&(FL_ExpandedProps){
              .flex = 1,
              .child = FL_Scrollbar(&(FL_ScrollbarProps){
                  .child = FL_GestureDetector(&(FL_GestureDetectorProps){
                      .context = viewer,
                      .drag_update = UI_ProfileScreen_OnDragUpdate,
                      .child = viewer->list_view,
                  }),
              }),

          }),
          0,
      }),
  });
}

static FL_Widget *UI_MainScreen(ZState *state) {
  if (state->loader) {
    return UI_LoadingScreen(state);
  } else if (state->viewer) {
    return UI_ProfileScreen(state);
  } else {
    return UI_WelcomeScreen();
  }
}

static FL_Widget *UI_Build(ZState *state) {
  return FL_ColoredBox(&(FL_ColoredBoxProps){
      .color = {0.94, 0.94, 0.94, 1.0},
      .child = FL_Column(&(FL_ColumnProps){
          .children = FL_WidgetList_Make((FL_Widget *[]){
              UI_GlobalMenuBar(state),
              // Simulate a bottom border.
              // TODO: Impl DecorationBox.
              FL_Container(&(FL_ContainerProps){
                  .height = FL_f32_Some(1),
                  .color = FL_Color_Some((FL_Color){0.6, 0.6, 0.6, 1.0}),
              }),
              FL_Expanded(&(FL_ExpandedProps){
                  .flex = 1,
                  .child = UI_MainScreen(state),
              }),
              0,
          }),
      }),
  });
}

static ZState g_z_state;

void z_load_file(ZFile *file) {
  ZState *state = &g_z_state;

  if (state->loader) {
    z_file_loader_free(state->loader);
    state->loader = 0;
  }

  if (state->viewer) {
    z_profile_viewer_free(state->viewer);
    state->viewer = 0;
  }

  state->loader = z_file_loader_alloc(file);
  z_file_loader_start(state->loader);
}

void z_update(Vec2 viewport_size) {
  static u64 last_counter;
  static f32 last_frame_time;

  ZState *state = &g_z_state;

  f32 dt = 0.0f;
  u64 current_counter = platform_get_perf_counter();
  if (last_counter) {
    dt = (f32)((f64)(current_counter - last_counter) /
               (f64)platform_get_perf_freq());
  }
  last_counter = current_counter;

  state->dt = dt;
  state->frame_time = last_frame_time;

  FL_Run(&(FL_RunOptions){
      .widget = UI_Build(state),
      .viewport =
          {
              .left = 0,
              .right = viewport_size.x,
              .top = 0,
              .bottom = viewport_size.y,
          },
      .delta_time = dt,
  });

  last_frame_time = (f32)((f64)(platform_get_perf_counter() - last_counter) /
                          (f64)platform_get_perf_freq());
}

void z_quit(void) {
  ZState *state = &g_z_state;

  if (state->loader) {
    z_file_loader_free(state->loader);
    state->loader = 0;
  }
}
