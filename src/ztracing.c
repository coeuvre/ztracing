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

typedef enum ProfileItemType {
  ProfileItemType_Header,
  ProfileItemType_Counter,
  ProfileItemType_Track,
} ProfileItemType;

typedef struct ProfileItem_Header {
  ProfileItemType type;  // ProfileItemType_Header
  Str name;
} ProfileItem_Header;

typedef struct ProfileSample {
  i64 time;
  f64 value;
} ProfileSample;

typedef struct ProfileSeries {
  Str name;
  u32 color_index;
  usize sample_count;
  ProfileSample *samples;
} ProfileSeries;

typedef struct ProfileItem_Counter {
  ProfileItemType type;  // ProfileItemType_Counter
  Str name;
  usize series_count;
  ProfileSeries *series;
  f64 min_value;
  f64 max_value;
} ProfileItem_Counter;

typedef struct ProfileSpan {
  Str name;
  u32 color_index;
  i64 begin_time_ns;
  i64 end_time_ns;
  i64 self_duration_ns;
} ProfileSpan;

typedef struct ProfileItem_Track {
  ProfileItemType type;  // ProfileItemType_Track
  usize span_count;
  ProfileSpan *spans;
} ProfileItem_Track;

typedef union ProfileItem {
  ProfileItemType type;
  ProfileItem_Header header;
  ProfileItem_Counter counter;
  ProfileItem_Track track;
} ProfileItem;

typedef struct ProfileViewer {
  Arena *arena;
  Str name;
  Str error;
  i64 min_time_ns;
  i64 max_time_ns;
  i64 begin_time_ns;
  i64 end_time_ns;
  i32 item_count;
  ProfileItem *items;
  f32 scroll;
  FL_Widget *list_view;
} ProfileViewer;

static ProfileViewer *ProfileViewer_Create(Allocator allocator) {
  Arena *arena = Arena_Create(&(ArenaOptions){.allocator = allocator});
  ProfileViewer *viewer = Arena_PushStruct(arena, ProfileViewer);
  *viewer = (ProfileViewer){.arena = arena};
  return viewer;
}

static void ProfileViewer_Destroy(ProfileViewer *self) {
  Arena_Destroy(self->arena);
}

typedef struct FileLoader {
  Arena *arena;
  LoadingFile *file;
  Platform_Thread *thread;

  volatile ProfileViewer *viewer;
} FileLoader;

static FileLoader *FileLoader_Create(LoadingFile *file, Allocator allocator) {
  Arena *arena = Arena_Create(&(ArenaOptions){.allocator = allocator});
  FileLoader *state = Arena_PushStruct(arena, FileLoader);
  *state = (FileLoader){
      .arena = arena,
      .file = file,
  };
  return state;
}

static Str GetInput(void *c) {
  LoadingFile *self = c;
  Str buf = self->read(self);
  self->nread += buf.len;
  return buf;
}

static int FileLoader_CompareJsonTraceSpan(const void *a, const void *b) {
  JsonTraceSpan *sa = *(JsonTraceSpan *const *)a;
  JsonTraceSpan *sb = *(JsonTraceSpan *const *)b;
  int result = CompareI64(sa->begin_time_ns, sb->begin_time_ns);
  if (result == 0) {
    result = CompareI64(sa->end_time_ns, sb->end_time_ns);
  }
  return result;
}

typedef struct ProfileSpanNode ProfileSpanNode;
struct ProfileSpanNode {
  ProfileSpanNode *prev;
  ProfileSpanNode *next;
  JsonTraceSpan *span;
  usize self_duration_ns;
};

typedef struct ProfileTrackNode ProfileTrackNode;
struct ProfileTrackNode {
  ProfileTrackNode *prev;
  ProfileTrackNode *next;
  isize level;
  isize span_count;
  ProfileSpanNode *first_span;
  ProfileSpanNode *last_span;
};

static ProfileSpanNode *ProfileTrackNode_AddSpan(ProfileTrackNode *self,
                                                 Arena *arena,
                                                 JsonTraceSpan *span) {
  ProfileSpanNode *node = Arena_PushStruct(arena, ProfileSpanNode);
  *node = (ProfileSpanNode){
      .span = span,
  };
  DLL_APPEND(self->first_span, self->last_span, node, prev, next);
  self->span_count += 1;
  return node;
}

typedef struct ProfileThreadNode ProfileThreadNode;
struct ProfileThreadNode {
  ProfileThreadNode *prev;
  ProfileThreadNode *next;
  JsonTraceThread *thread;
  isize track_count;
  ProfileTrackNode *first_track;
  ProfileTrackNode *last_track;
};

static ProfileTrackNode *ProfileThreadNode_UpsertTrack(ProfileThreadNode *self,
                                                       Arena *arena,
                                                       isize level) {
  ProfileTrackNode *track = self->first_track;
  isize i = level;
  while (track && i > 0) {
    i--;
    track = track->next;
  }

  while (i > 0 || !track) {
    track = Arena_PushStruct(arena, ProfileTrackNode);
    *track = (ProfileTrackNode){
        .level = level,
    };
    DLL_APPEND(self->first_track, self->last_track, track, prev, next);
    self->track_count += 1;
    i--;
  }

  ASSERT(track->level == level);

  return track;
}

typedef struct ProfileTrackBuilder {
  usize thread_count;
  ProfileThreadNode *first_thread;
  ProfileThreadNode *last_thread;
} ProfileTrackBuilder;

static ProfileThreadNode *ProfileTrackBuilder_AddThread(
    ProfileTrackBuilder *self, JsonTraceThread *thread, Arena *arena) {
  ProfileThreadNode *thread_node = Arena_PushStruct(arena, ProfileThreadNode);
  *thread_node = (ProfileThreadNode){
      .thread = thread,
  };
  DLL_APPEND(self->first_thread, self->last_thread, thread_node, prev, next);
  self->thread_count += 1;
  return thread_node;
}

static usize FileLoader_MergeSpans(Arena *arena, ProfileThreadNode *thread,
                                   usize level, i64 parent_begin_time_ns,
                                   i64 parent_end_time_ns,
                                   JsonTraceSpan **spans, isize span_count,
                                   isize span_index, i64 *total_duration_ns) {
  i64 total = 0;
  for (; span_index < span_count;) {
    JsonTraceSpan *span = spans[span_index];
    i64 span_duration_ns = span->end_time_ns - span->begin_time_ns;
    if (span->begin_time_ns >= parent_begin_time_ns &&
        span->end_time_ns <= parent_end_time_ns) {
      ProfileTrackNode *track =
          ProfileThreadNode_UpsertTrack(thread, arena, level);
      ProfileSpanNode *span_node = ProfileTrackNode_AddSpan(track, arena, span);
      i64 children_duration_ns;
      span_index = FileLoader_MergeSpans(
          arena, thread, level + 1, span->begin_time_ns, span->end_time_ns,
          spans, span_count, span_index + 1, &children_duration_ns);
      span_node->self_duration_ns =
          MaxI64(span_duration_ns - children_duration_ns, 0);

      total += span_duration_ns;
    } else {
      break;
    }
  }
  *total_duration_ns = total;
  return span_index;
}

static void FileLoader_BuildTrackWithThread(JsonTraceThread *thread,
                                            ProfileTrackBuilder *builder,
                                            Arena *arena, Arena scratch) {
  ProfileThreadNode *thread_node =
      ProfileTrackBuilder_AddThread(builder, thread, arena);

  if (thread->span_count > 0) {
    JsonTraceSpan **spans =
        Arena_PushArray(&scratch, JsonTraceSpan *, thread->span_count);
    usize span_index = 0;
    for (JsonTraceSpan *span = thread->first_span; span; span = span->next) {
      spans[span_index++] = span;
    }
    qsort(spans, thread->span_count, sizeof(JsonTraceSpan *),
          FileLoader_CompareJsonTraceSpan);

    JsonTraceSpan *first_span = spans[0];
    i64 begin_time_ns = first_span->begin_time_ns;
    i64 end_time_ns = first_span->end_time_ns;
    for (span_index = 1; span_index < thread->span_count; ++span_index) {
      end_time_ns = MaxI64(end_time_ns, spans[span_index]->end_time_ns);
    }

    i64 total_duration_ns;
    FileLoader_MergeSpans(arena, thread_node, 0, begin_time_ns, end_time_ns,
                          spans, thread->span_count, 0, &total_duration_ns);
  }
}

static int FileLoader_CompareThread(const void *a, const void *b) {
  JsonTraceThread *ta = *(JsonTraceThread *const *)a;
  JsonTraceThread *tb = *(JsonTraceThread *const *)b;
  if (ta->sort_index.present) {
    if (tb->sort_index.present) {
      return CompareI64(ta->sort_index.value, tb->sort_index.value);
    } else {
      return -1;
    }
  } else if (tb->sort_index.present) {
    return 1;
  }

  return Str_CompareIgnoringCase(ta->name, tb->name);
}

static void FileLoader_BuildTrackWithProcess(JsonTraceProcess *process,
                                             ProfileTrackBuilder *builder,
                                             Arena *arena, Arena scratch) {
  JsonTraceThread **threads =
      Arena_PushArray(&scratch, JsonTraceThread *, process->thread_count);
  usize thread_index = 0;
  HashTrieIter thread_iter = HashTrie_Iter(&process->threads, &scratch);
  JsonTraceThread *thread;
  while ((thread = HashTrie_Next(&thread_iter, JsonTraceThread))) {
    threads[thread_index++] = thread;
  }
  qsort(threads, process->thread_count, sizeof(*threads),
        FileLoader_CompareThread);

  for (thread_index = 0; thread_index < process->thread_count; ++thread_index) {
    FileLoader_BuildTrackWithThread(threads[thread_index], builder, arena,
                                    scratch);
  }
}

static void FileLoader_BuildTrack(JsonTraceProfile *profile,
                                  ProfileTrackBuilder *builder, Arena *arena,
                                  Arena scratch) {
  HashTrieIter process_iter = HashTrie_Iter(&profile->processes, &scratch);
  JsonTraceProcess *process;
  while ((process = HashTrie_Next(&process_iter, JsonTraceProcess))) {
    FileLoader_BuildTrackWithProcess(process, builder, arena, scratch);
  }
}

static int FileLoader_CompareSample(const void *a, const void *b) {
  const ProfileSample *sa = a;
  const ProfileSample *sb = b;
  return CompareI64(sa->time, sb->time);
}

static void FileLoader_CollectSeries(ProfileItem_Counter *c,
                                     JsonTraceCounter *counter, Arena *arena,
                                     Arena scratch) {
  HashTrieIter series_iter = HashTrie_Iter(&counter->series, &scratch);
  usize series_index = 0;
  JsonTraceSeries *series;
  while ((series = HashTrie_Next(&series_iter, JsonTraceSeries))) {
    ProfileSeries *s = &c->series[series_index++];

    s->name = Str_Dup(arena, series->name);
    s->color_index = Str_Hash(s->name) % COUNT_OF(COLORS);
    s->sample_count = series->sample_count;
    s->samples = Arena_PushArray(arena, ProfileSample, s->sample_count);

    usize sample_index = 0;
    for (JsonTraceSample *sample = series->first; sample;
         sample = sample->next) {
      s->samples[sample_index++] = (ProfileSample){
          .time = sample->time,
          .value = sample->value,
      };
    }

    qsort(s->samples, s->sample_count, sizeof(*s->samples),
          FileLoader_CompareSample);
  }
}

static int FileLoader_CompareCounter(const void *a, const void *b) {
  JsonTraceCounter *const *ca = a;
  JsonTraceCounter *const *cb = b;
  return Str_CompareIgnoringCase((*ca)->name, (*cb)->name);
}

static void FileLoader_CollectCounters(ProfileItem *items, usize *item_index,
                                       JsonTraceProcess *process, Arena *arena,
                                       Arena scratch) {
  JsonTraceCounter **sorted_counters =
      Arena_PushArray(&scratch, JsonTraceCounter *, process->counter_count);
  {
    usize counter_index = 0;
    HashTrieIter counter_iter = HashTrie_Iter(&process->counters, &scratch);
    JsonTraceCounter *counter;
    while ((counter = HashTrie_Next(&counter_iter, JsonTraceCounter))) {
      sorted_counters[counter_index++] = counter;
    }
  }
  qsort(sorted_counters, process->counter_count, sizeof(*sorted_counters),
        FileLoader_CompareCounter);

  for (usize counter_index = 0; counter_index < process->counter_count;
       ++counter_index) {
    JsonTraceCounter *counter = sorted_counters[counter_index];

    items[(*item_index)++] = (ProfileItem){
        .header =
            {
                .type = ProfileItemType_Header,
                .name = Str_Dup(arena, counter->name),
            },
    };

    ProfileItem_Counter c = {
        .type = ProfileItemType_Counter,
    };
    c.name = Str_Dup(arena, counter->name);
    c.series_count = counter->series_count;
    c.series = Arena_PushArray(arena, ProfileSeries, c.series_count);
    c.min_value = counter->min_value;
    c.max_value = counter->max_value;

    FileLoader_CollectSeries(&c, counter, arena, scratch);

    items[(*item_index)++] = (ProfileItem){
        .counter = c,
    };
  }
}

static int FileLoader_CompareProfileSpan(const void *a, const void *b) {
  const ProfileSpan *sa = a;
  const ProfileSpan *sb = b;
  int result = CompareI64(sa->end_time_ns, sb->end_time_ns);
  if (result == 0) {
    result = CompareI64(sa->begin_time_ns, sb->begin_time_ns);
  }
  return result;
}

static void FileLoader_CollectTracks(Arena *arena, ProfileItem *items,
                                     usize *item_index,
                                     ProfileTrackBuilder *track_builder) {
  for (ProfileThreadNode *thread = track_builder->first_thread; thread;
       thread = thread->next) {
    Str name;
    if (Str_IsEmpty(thread->thread->name)) {
      name = Str_Format(arena, "Thread %lld", thread->thread->tid);
    } else {
      name = Str_Dup(arena, thread->thread->name);
    }
    items[(*item_index)++] = (ProfileItem){
        .header =
            {
                .type = ProfileItemType_Header,
                .name = name,
            },
    };

    for (ProfileTrackNode *track = thread->first_track; track;
         track = track->next) {
      ProfileSpan *spans =
          Arena_PushArray(arena, ProfileSpan, track->span_count);

      ProfileSpanNode *span_node = track->first_span;
      for (isize span_index = 0; span_index < track->span_count; ++span_index) {
        ProfileSpan *span = spans + span_index;
        span->name = Str_Dup(arena, span_node->span->name);
        span->color_index = Str_Hash(span->name) % COUNT_OF(COLORS);
        span->begin_time_ns = span_node->span->begin_time_ns;
        span->end_time_ns = span_node->span->end_time_ns;
        span->self_duration_ns = span_node->self_duration_ns;
        span_node = span_node->next;
      }
      qsort(spans, track->span_count, sizeof(ProfileSpan),
            FileLoader_CompareProfileSpan);

      items[(*item_index)++] = (ProfileItem){
          .track =
              {
                  .type = ProfileItemType_Track,
                  .span_count = track->span_count,
                  .spans = spans,
              },
      };
    }
  }
}

static void FileLoader_CollectItems(ProfileItem *items,
                                    JsonTraceProfile *profile,
                                    ProfileTrackBuilder *track_builder,
                                    Arena *arena, Arena scratch) {
  usize item_index = 0;
  HashTrieIter process_iter = HashTrie_Iter(&profile->processes, &scratch);
  JsonTraceProcess *process;
  while ((process = HashTrie_Next(&process_iter, JsonTraceProcess))) {
    FileLoader_CollectCounters(items, &item_index, process, arena, scratch);
  }

  FileLoader_CollectTracks(arena, items, &item_index, track_builder);
}

static i32 FileLoader_CountItems(JsonTraceProfile *profile,
                                 ProfileTrackBuilder *track_builder,
                                 Arena scratch) {
  i32 item_count = 0;
  HashTrieIter process_iter = HashTrie_Iter(&profile->processes, &scratch);
  JsonTraceProcess *process;
  while ((process = HashTrie_Next(&process_iter, JsonTraceProcess))) {
    item_count += 2 * process->counter_count;
  }

  item_count += track_builder->thread_count;
  for (ProfileThreadNode *thread = track_builder->first_thread; thread;
       thread = thread->next) {
    item_count += thread->track_count;
  }

  return item_count;
}

static void FileLoader_ConvertProfile(ProfileViewer *viewer,
                                      JsonTraceProfile *profile,
                                      Arena scratch) {
  ProfileTrackBuilder track_builder = {0};
  FileLoader_BuildTrack(profile, &track_builder, &scratch, *viewer->arena);

  viewer->item_count = FileLoader_CountItems(profile, &track_builder, scratch);
  viewer->items =
      Arena_PushArray(viewer->arena, ProfileItem, viewer->item_count);
  FileLoader_CollectItems(viewer->items, profile, &track_builder, viewer->arena,
                          scratch);

  viewer->min_time_ns = profile->min_time_ns;
  viewer->max_time_ns = profile->max_time_ns;

  i64 duration = (profile->max_time_ns - profile->min_time_ns);
  i64 offset = (i64)RoundF64((f64)duration * 0.1);
  viewer->begin_time_ns = profile->min_time_ns - offset;
  viewer->end_time_ns = profile->max_time_ns + offset;
}

static int FileLoader_Thread(void *self_) {
  FileLoader *self = self_;
  LoadingFile *file = self->file;

  ProfileViewer *viewer = ProfileViewer_Create(Arena_GetAllocator(self->arena));
  viewer->name = Str_Dup(viewer->arena, file->name);

  Arena *scratch = Arena_Create(&(ArenaOptions){
      .allocator = Arena_GetAllocator(viewer->arena),
  });
  JsonParser parser = json_parser(GetInput, file);
  u64 before = Platform_GetPerformanceCounter();
  JsonTraceProfile *profile = JsonTraceProfile_Parse(scratch, &parser);
  u64 after = Platform_GetPerformanceCounter();

  if (!file->interrupted) {
    f64 mb = (f64)file->nread / 1024.0 / 1024.0;
    f64 secs = (f64)(after - before) / (f64)Platform_GetPerformanceFrequency();
    INFO("Loaded %.1f MiB over %.1f seconds, %.1f MiB / s", mb, secs,
         mb / secs);

    if (Str_IsEmpty(profile->error)) {
      FileLoader_ConvertProfile(viewer, profile, *scratch);
    } else {
      viewer->error = Str_Dup(viewer->arena, profile->error);
    }

    self->viewer = viewer;
  }
  Arena_Destroy(scratch);

  return 0;
}

static void FileLoader_Start(FileLoader *self) {
  self->thread = Platform_Thread_Start(FileLoader_Thread, "FileLoader", self);
}

static bool FileLoader_IsDone(FileLoader *self) { return self->viewer; }

static void FileLoader_Destroy(FileLoader *self) {
  self->file->interrupted = true;
  Platform_Thread_Wait(self->thread);

  self->file->close(self->file);
  Arena_Destroy(self->arena);
}

typedef struct App {
  FL_Arena *arena;

  isize allocated_bytes;
  u64 last_counter;
  f32 dt;
  f32 frame_time;

  FileLoader *loader;
  ProfileViewer *viewer;
} App;

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

// static bool do_button(Str text, ButtonType type) {
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

static FL_Widget *UI_GlobalMenuBar(App *app) {
  Str name = {0};
  if (app->loader) {
    name = app->loader->file->name;
  } else if (app->viewer) {
    name = app->viewer->name;
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
                  .text =
                      FL_Format("%.1fMB %.1fms",
                                ((f64)app->allocated_bytes / 1024.0 / 1024.0),
                                (f64)app->frame_time * 1000.0),
                  .style = DefaultTextStyle(),
              }),
              0,
          }),
      }),
  });
}

static FL_Widget *UI_WelcomeScreen(void) {
  Str logo[] = {
      // clang-format off
      STR_C(" ________  _________  _______          _        ______  _____  ____  _____   ______"),
      STR_C("|  __   _||  _   _  ||_   __ \\        / \\     .' ___  ||_   _||_   \\|_   _|.' ___  |"),
      STR_C("|_/  / /  |_/ | | \\_|  | |__) |      / _ \\   / .'   \\_|  | |    |   \\ | | / .'   \\_|"),
      STR_C("   .'.' _     | |      |  __ /      / ___ \\  | |         | |    | |\\ \\| | | |   ____"),
      STR_C(" _/ /__/ |   _| |_    _| |  \\ \\_  _/ /   \\ \\_\\ `.___.'\\ _| |_  _| |_\\   |_\\ `.___]  |"),
      STR_C("|________|  |_____|  |____| |___||____| |____|`.____ .'|_____||_____|\\____|`._____.'"),
      // clang-format on
  };

  FL_WidgetList children = {0};
  for (isize i = 0; i < COUNT_OF(logo); ++i) {
    FL_WidgetList_Append(&children, FL_Text(&(FL_TextProps){
                                        .text = logo[i],
                                        .style = DefaultTextStyle(),
                                    }));
  }

  FL_Widget *logo_widget = FL_Column(&(FL_ColumnProps){
      .cross_axis_alignment = FL_CrossAxisAlignment_Start,
      .children = children,
  });

  return FL_Column(&(FL_ColumnProps){
      .main_axis_alignment = FL_MainAxisAlignment_Center,
      .children = FL_WidgetList_Make((FL_Widget *[]){
          logo_widget,
          FL_Padding(&(FL_PaddingProps){
              .padding = FL_EdgeInsets_Symmetric(0, 10),
          }),
          FL_Text(&(FL_TextProps){
              .text = STR_C("Drag & Drop a json trace profile to start."),
              .style = DefaultTextStyle(),
          }),
          0,
      }),
  });
}

static FL_Widget *UI_LoadingScreen(App *app) {
  FileLoader *loader = app->loader;

  FL_Widget *widget = FL_Center(&(FL_CenterProps){
      .child = FL_Text(&(FL_TextProps){
          .text = FL_Format("Loading (%.1f MiB) ...",
                            (f64)loader->file->nread / 1024.0 / 1024.0),
          .style = DefaultTextStyle(),
      }),
  });

  if (FileLoader_IsDone(loader)) {
    app->viewer = (ProfileViewer *)loader->viewer;
    app->loader = 0;
    FileLoader_Destroy(loader);
  }

  return widget;
}

typedef struct UI_TimelineProps {
  i64 begin_time_ns;
  i64 end_time_ns;
} UI_TimelineProps;

static i64 timeline_calc_block_duration(i64 duration, f32 width,
                                        f32 target_block_width) {
  f32 num_blocks = Floor(width / target_block_width);
  i64 block_duration = (i64)Round((f32)duration / (f32)num_blocks);
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

static Str UI_Timeline_FormatTime(FL_Arena *arena, i64 time, i64 duration) {
  static const char *TIME_UNITS[] = {"ns", "us", "ms", "s"};

  if (time == 0) {
    return STR_C("0");
  }

  f64 t = (f64)time;
  usize time_unit_index = 0;

  if (duration > 0) {
    i64 tmp = duration / 1000;
    while (tmp > 0 && (time_unit_index + 1) < COUNT_OF(TIME_UNITS)) {
      tmp /= 1000;
      t /= 1000.0;
      time_unit_index++;
    }
  } else {
    while (AbsF64(t) >= 1000.0 &&
           (time_unit_index + 1) < COUNT_OF(TIME_UNITS)) {
      t /= 1000.0;
      time_unit_index++;
    }
  }

  Str buf = Str_Format(arena, "%.1lf%s", t, TIME_UNITS[time_unit_index]);
  char *period = buf.ptr + buf.len - 1;
  while (*period != '.') {
    period--;
  }
  if (*(period + 1) == '0') {
    MoveMemory(period, period + 2, buf.ptr + buf.len - period - 1);
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
  // f32 large_block_width = large_block_duration * point_per_ns;

  i64 t = begin / block_duration * block_duration;
  // Truncate away from 0
  if (t < 0) {
    t -= block_duration;
  }
  f32 bottom = offset.y + size.y;
  while (t <= end) {
    f32 x = offset.x + (f32)(t - begin) * point_per_ns;
    bool is_large_block = t % large_block_duration == 0;
    f32 height = is_large_block ? size.y * 0.4f : size.y * 0.2f;
    FL_Canvas_FillRect(
        context->canvas,
        (FL_Rect){x, x + line_thickness, bottom - height, bottom}, color);

    if (is_large_block) {
      FL_Arena scratch = *FL_Widget_GetArena(widget);
      FL_Canvas_FillText(context->canvas,
                         UI_Timeline_FormatTime(&scratch, t, duration), x + 4,
                         bottom - 2 - font_size / 2.0f, font_size, color);
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
  ProfileItem_Counter *counter;
  i64 begin_time_ns;
  i64 end_time_ns;
} UI_ProfileCounterProps;

static usize UI_ProfileCounter_FindSamplesLowerBound(ProfileSample *samples,
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
                                          ProfileItem_Counter *counter,
                                          ProfileSeries *series,
                                          ProfileSample *sample) {
  f32 bottom = offset.y + size.y;
  i64 sample_bin_begin = sample->time / bin_duration;
  f32 x = offset.x + (sample_bin_begin - bin_begin) * bin_width;
  f32 height = Max(1, (sample->value - counter->min_value) / d * size.y);
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
  *prev_x = f32_Some(x);
  *prev_h = f32_Some(height);
}

static void UI_ProfileCounter_Paint(FL_Widget *widget,
                                    FL_PaintingContext *context, Vec2 offset) {
  (void)context;

  UI_ProfileCounterProps *props =
      FL_Widget_GetProps(widget, UI_ProfileCounterProps);

  ProfileItem_Counter *counter = props->counter;
  f64 d = (counter->max_value - counter->min_value);
  f32 point_per_ns =
      (f32)widget->size.x / (f32)(props->end_time_ns - props->begin_time_ns);

  f32 bin_width = 2.0f;
  f32 ns_per_point = 1.0f / point_per_ns;
  i64 bin_duration = Round(ns_per_point * bin_width);
  i64 bin_begin = props->begin_time_ns / bin_duration;
  i64 bin_end = props->end_time_ns / bin_duration + 1;
  Vec2 sample_offset =
      vec2(offset.x -
               (props->begin_time_ns - bin_begin * bin_duration) * point_per_ns,
           Round(offset.y));

  for (usize series_index = 0; series_index < counter->series_count;
       ++series_index) {
    ProfileSeries *series = counter->series + series_index;

    f32o prev_x = f32_None();
    f32o prev_h = f32_None();

    usize prev_sample_index = 0;
    {
      usize first_sample_index = UI_ProfileCounter_FindSamplesLowerBound(
          series->samples, 0, series->sample_count, bin_begin * bin_duration);
      if (first_sample_index > 0) {
        usize sample_index = first_sample_index - 1;
        prev_sample_index = sample_index;
        ProfileSample *sample = series->samples + sample_index;
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
        ProfileSample *sample = series->samples + sample_index;
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
        ProfileSample *sample = series->samples + sample_index;
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
  ProfileItem_Track *track;
  i64 begin_time_ns;
  i64 end_time_ns;
} UI_ProfileTrackProps;

// Find the first span with in range [begin, end) whose end_time_ns > time.
static usize UI_ProfileTrack_FindSpansUpperBound(ProfileSpan *spans,
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
  UI_ProfileTrackProps *props =
      FL_Widget_GetProps(widget, UI_ProfileTrackProps);

  f32 point_per_ns =
      (f32)widget->size.x / (f32)(props->end_time_ns - props->begin_time_ns);

  f32 bin_width = 4.0f;
  f32 ns_per_point = 1.0f / point_per_ns;
  i64 bin_duration = (i64)Round(ns_per_point * bin_width);
  i64 bin_begin = props->begin_time_ns / bin_duration - 1;
  i64 bin_end = props->end_time_ns / bin_duration + 1;
  f32 offset_x =
      offset.x -
      (f32)(props->begin_time_ns - bin_begin * bin_duration) * point_per_ns;

  ProfileItem_Track *track = props->track;
  usize last_span_index = track->span_count;
  for (i64 bin_index = bin_end; bin_index >= bin_begin; --bin_index) {
    i64 bin_begin_time_ns = bin_index * bin_duration;
    usize span_index = UI_ProfileTrack_FindSpansUpperBound(
        track->spans, 0, last_span_index, bin_begin_time_ns);
    if (span_index < last_span_index) {
      last_span_index = span_index;
      ProfileSpan *span = track->spans + span_index;
      i64 span_bin_begin = span->begin_time_ns / bin_duration;
      i64 span_bin_end = span->end_time_ns / bin_duration +
                         (span->end_time_ns % bin_duration ? 1 : 0);

      f32 left = offset_x + (f32)(span_bin_begin - bin_begin) * bin_width;
      f32 right =
          left + Max(1, (f32)(span_bin_end - span_bin_begin)) * bin_width;
      left = Max(left, offset.x);
      right = Max(Min(right, offset.x + widget->size.x), left);

      if (right > left) {
        Vec2 min = {left, offset.y};
        Vec2 max = {right, offset.y + widget->size.y};
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

          f32 text_left =
              Max(left + text_padding_x, left + (width - metrics.width) / 2.0f);
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

static FL_Widget *UI_ProfileHeader(ProfileItem_Header *header) {
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
  ProfileViewer *viewer = ctx;
  FL_Widget *widget = viewer->list_view;
  if (FL_PointerEventResolver_Register(widget)) {
    f32 pivot = event.local_position.x / widget->size.x;
    i64 duration = viewer->end_time_ns - viewer->begin_time_ns;
    i64 pivot_time =
        viewer->begin_time_ns + (i64)RoundF64((f64)pivot * (f64)duration);

    Vec2 delta = event.delta;
    if (delta.y < 0) {
      duration = MaxI64((i64)RoundF64((f64)duration * 0.8), 1000);
    } else {
      i64 max_duration =
          (i64)RoundF64((f64)(viewer->max_time_ns - viewer->min_time_ns) * 2.0);
      duration = MinI64((i64)RoundF64((f64)duration * 1.25), max_duration);
    }

    viewer->begin_time_ns =
        pivot_time - (i64)RoundF64((f64)pivot * (f64)duration);
    viewer->end_time_ns = viewer->begin_time_ns + duration;
  }
}

static FL_Widget *UI_ProfileItem(void *ctx, FL_i32 item_index) {
  ProfileViewer *viewer = ctx;
  ProfileItem *item = viewer->items + item_index;
  FL_Widget *child;
  switch (item->type) {
    case ProfileItemType_Header: {
      child = UI_ProfileHeader(&item->header);
    } break;

    case ProfileItemType_Counter: {
      child = UI_ProfileCounter(&(UI_ProfileCounterProps){
          .counter = &item->counter,
          .begin_time_ns = viewer->begin_time_ns,
          .end_time_ns = viewer->end_time_ns,
      });
    } break;

    case ProfileItemType_Track: {
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
  ProfileViewer *viewer = ctx;
  FL_Widget *widget = viewer->list_view;
  Vec2 delta = details.delta;
  i64 duration = viewer->end_time_ns - viewer->begin_time_ns;
  f64 ns_per_point = (f64)duration / (f64)widget->size.x;
  i64 offset = (i64)RoundF64(ns_per_point * (f64)delta.x);
  viewer->begin_time_ns -= offset;
  viewer->end_time_ns = viewer->begin_time_ns + duration;
  viewer->scroll -= delta.y;
}

static FL_Widget *UI_ProfileScreen(App *app) {
  ProfileViewer *viewer = app->viewer;
  if (!Str_IsEmpty(viewer->error)) {
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

static FL_Widget *UI_MainScreen(App *app) {
  if (app->loader) {
    return UI_LoadingScreen(app);
  } else if (app->viewer) {
    return UI_ProfileScreen(app);
  } else {
    return UI_WelcomeScreen();
  }
}

static FL_Widget *UI_Build(App *app) {
  return FL_ColoredBox(&(FL_ColoredBoxProps){
      .color = {0.94, 0.94, 0.94, 1.0},
      .child = FL_Column(&(FL_ColumnProps){
          .children = FL_WidgetList_Make((FL_Widget *[]){
              UI_GlobalMenuBar(app),
              // Simulate a bottom border.
              // TODO: Impl DecorationBox.
              FL_Container(&(FL_ContainerProps){
                  .height = FL_f32_Some(1),
                  .color = FL_Color_Some((FL_Color){0.6, 0.6, 0.6, 1.0}),
              }),
              FL_Expanded(&(FL_ExpandedProps){
                  .flex = 1,
                  .child = UI_MainScreen(app),
              }),
              0,
          }),
      }),
  });
}

App *App_Create(FL_Allocator allocator) {
  FL_Arena *arena = FL_Arena_Create(&(FL_ArenaOptions){
      .allocator = allocator,
  });
  App *app = FL_Arena_PushStruct(arena, App);
  *app = (App){
      .arena = arena,
  };
  return app;
}

void App_LoadFile(App *app, LoadingFile *file) {
  if (app->loader) {
    FileLoader_Destroy(app->loader);
    app->loader = 0;
  }

  if (app->viewer) {
    ProfileViewer_Destroy(app->viewer);
    app->viewer = 0;
  }

  app->loader = FileLoader_Create(file, Arena_GetAllocator(app->arena));
  FileLoader_Start(app->loader);
}

void App_Update(App *app, Vec2 viewport_size, isize allocated_bytes) {
  f32 dt = 0.0f;
  u64 current_counter = Platform_GetPerformanceCounter();
  if (app->last_counter) {
    dt = (f32)((f64)(current_counter - app->last_counter) /
               (f64)Platform_GetPerformanceFrequency());
  }
  app->last_counter = current_counter;

  app->allocated_bytes = allocated_bytes;
  app->dt = dt;

  FL_Run(&(FL_RunOptions){
      .widget = UI_Build(app),
      .viewport =
          {
              .left = 0,
              .right = viewport_size.x,
              .top = 0,
              .bottom = viewport_size.y,
          },
      .delta_time = dt,
  });

  app->frame_time =
      (f32)((f64)(Platform_GetPerformanceCounter() - app->last_counter) /
            (f64)Platform_GetPerformanceFrequency());
}

void App_Destroy(App *app) {
  if (app->loader) {
    FileLoader_Destroy(app->loader);
    app->loader = 0;
  }

  FL_Arena_Destroy(app->arena);
}
