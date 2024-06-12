#include <zlib.h>

struct Profile;

enum DocumentState {
  Document_Loading,
  Document_View,
};

struct LoadState {
  Arena *document_arena;
  OsLoadingFile *file;
  volatile isize loaded;
  Buffer error;
  Profile *profile;
};

struct ViewState {
  Buffer error;
  Profile *profile;
  i64 begin_time;
  i64 end_time;
  bool show_lane_border;
};

struct Document {
  Arena arena;
  Buffer path;

  DocumentState state;
  union {
    struct {
      Task *task;
      LoadState state;
    } loading;

    ViewState view;
  };
};

static voidpf ZLibAlloc(voidpf opaque, uInt items, uInt size) {
  Arena *arena = (Arena *)opaque;
  return PushSize(arena, items * size);
}

static void ZLibFree(voidpf opaque, voidpf address) {}

enum LoadProgress {
  LoadProgress_Init,
  LoadProgress_Regular,
  LoadProgress_Gz,
  LoadProgress_Done,
};

struct Load {
  Arena *arena;
  OsLoadingFile *file;

  LoadProgress progress;

  z_stream zstream;
  Buffer zstream_buf;
};

static Load InitLoad(Arena *arena, OsLoadingFile *file) {
  Load load = {};
  load.arena = arena;
  load.file = file;
  load.zstream.zalloc = ZLibAlloc;
  load.zstream.zfree = ZLibFree;
  load.zstream.opaque = arena;
  return load;
}

static u32 LoadIntoBuffer(Load *load, Buffer buf) {
  u32 nread = 0;
  bool done = false;
  while (!done) {
    switch (load->progress) {
      case LoadProgress_Init: {
        nread = OsReadFile(load->file, buf.data, buf.size);
        if (nread >= 2 && (buf.data[0] == 0x1F && buf.data[1] == 0x8B)) {
          int zret = inflateInit2(&load->zstream, MAX_WBITS | 32);
          // TODO: Error handling.
          ASSERT(zret == Z_OK);
          load->zstream_buf =
              PushBufferNoZero(load->arena, MAX(4096, buf.size));
          CopyMemory(load->zstream_buf.data, buf.data, nread);
          load->zstream.avail_in = nread;
          load->zstream.next_in = load->zstream_buf.data;
          load->progress = LoadProgress_Gz;
        } else {
          load->progress = LoadProgress_Regular;
          done = true;
        }
      } break;

      case LoadProgress_Regular: {
        nread = OsReadFile(load->file, buf.data, buf.size);
        if (nread == 0) {
          load->progress = LoadProgress_Done;
        }
        done = true;
      } break;

      case LoadProgress_Gz: {
        if (load->zstream.avail_in == 0) {
          load->zstream.avail_in = OsReadFile(
              load->file, load->zstream_buf.data, load->zstream_buf.size);
          load->zstream.next_in = load->zstream_buf.data;
        }

        if (load->zstream.avail_in != 0) {
          load->zstream.avail_out = buf.size;
          load->zstream.next_out = buf.data;

          int zret = inflate(&load->zstream, Z_NO_FLUSH);
          switch (zret) {
            case Z_OK: {
            } break;

            case Z_STREAM_END: {
              load->progress = LoadProgress_Done;
            } break;

            default: {
              // TODO: Error handling.
              ABORT("inflate returned %d", zret);
            } break;
          }

          nread = buf.size - load->zstream.avail_out;
        } else {
          load->progress = LoadProgress_Done;
        }

        done = true;
      } break;

      case LoadProgress_Done: {
        done = true;
      } break;

      default:
        UNREACHABLE;
    }
  }
  return nread;
}

struct GetJsonInputData {
  Task *task;
  Load load;
  Buffer buf;
};

static Buffer GetJsonInput(void *data_) {
  Buffer result = {};
  GetJsonInputData *data = (GetJsonInputData *)data_;
  if (!IsTaskCancelled(data->task)) {
    result.size = LoadIntoBuffer(&data->load, data->buf);
    result.data = data->buf.data;

    LoadState *state = (LoadState *)data->task->data;
    isize loaded = state->loaded;
    loaded += result.size;
    state->loaded = loaded;
  }
  return result;
}

struct SeriesSample {
  i64 time;
  f64 value;
};

struct Series {
  Buffer name;
  SeriesSample *samples;
  isize sample_size;
};

struct CounterHeader {
  Buffer name;
};

struct Counter {
  Series *series;
  isize series_size;
  f64 min;
  f64 max;
};

enum LaneType {
  LaneType_CounterHeader,
  LaneType_Counter,
};

struct Lane {
  LaneType type;
  union {
    CounterHeader counter_header;
    Counter counter;
  };
};

struct Process {
  i64 pid;
  Lane *lanes;
  isize lane_size;
};

struct Profile {
  i64 min_time;
  i64 max_time;
  Process *processes;
  isize process_size;
};

static int CompareSeriesSample(const void *a_, const void *b_) {
  SeriesSample *a = (SeriesSample *)a_;
  SeriesSample *b = (SeriesSample *)b_;
  return a->time - b->time;
}

static void BuildSeries(Arena *arena, SeriesResult *series_result,
                        Series *series, Counter *counter) {
  series->name = PushBuffer(arena, series_result->name);
  series->sample_size = series_result->sample_size;
  series->samples = PushArray(arena, SeriesSample, series->sample_size);
  SampleResult *sample_result = series_result->first;
  for (isize sample_index = 0; sample_index < series->sample_size;
       ++sample_index) {
    SeriesSample *sample = series->samples + sample_index;
    ASSERT(sample_result);
    sample->time = sample_result->time;
    sample->value = sample_result->value;
    sample_result = sample_result->next;
    counter->min = MIN(counter->min, sample->value);
    counter->max = MAX(counter->max, sample->value);
  }
  qsort(series->samples, series->sample_size, sizeof(series->samples[0]),
        CompareSeriesSample);
}

static void BuildCounter(Arena *arena, Arena scratch,
                         CounterResult *counter_result, Counter *counter) {
  counter->series_size = counter_result->series_size;
  counter->series = PushArray(arena, Series, counter->series_size);
  counter->max = -DBL_MAX;
  counter->min = DBL_MAX;

  HashMapIter series_result_iter =
      InitHashMapIter(&scratch, &counter_result->series);
  for (isize series_index = 0; series_index < counter->series_size;
       ++series_index) {
    SeriesResult *series_result = (SeriesResult *)IterNext(&series_result_iter);
    ASSERT(series_result);

    Series *series = counter->series + series_index;
    BuildSeries(arena, series_result, series, counter);
  }
}

static Lane *GetLane(Process *process, isize lane_index) {
  ASSERT(lane_index < process->lane_size);
  return process->lanes + lane_index;
}

static void BuildCounters(Arena *arena, Arena scratch,
                          ProcessResult *process_result, Process *process,
                          isize *lane_index) {
  // TODO: Sort counters
  HashMapIter counter_result_iter =
      InitHashMapIter(&scratch, &process_result->counters);
  for (isize counter_index = 0; counter_index < process_result->counter_size;
       ++counter_index) {
    CounterResult *counter_result =
        (CounterResult *)IterNext(&counter_result_iter);
    ASSERT(counter_result);

    Lane *counter_header_lane = GetLane(process, (*lane_index)++);
    counter_header_lane->type = LaneType_CounterHeader;
    counter_header_lane->counter_header.name =
        PushBuffer(arena, counter_result->name);

    Lane *counter_lane = GetLane(process, (*lane_index)++);
    counter_lane->type = LaneType_Counter;
    Counter *counter = &counter_lane->counter;
    BuildCounter(arena, scratch, counter_result, counter);
  }
}

static Profile *BuildProfile(Arena *arena, Arena scratch,
                             ProfileResult *profile_result) {
  Profile *profile = PushStruct(arena, Profile);
  profile->min_time = profile_result->min_time;
  profile->max_time = profile_result->max_time;
  profile->process_size = profile_result->process_size;
  profile->processes = PushArray(arena, Process, profile->process_size);

  HashMapIter process_result_iter =
      InitHashMapIter(&scratch, &profile_result->processes);
  for (isize process_index = 0; process_index < profile->process_size;
       ++process_index) {
    ProcessResult *process_result =
        (ProcessResult *)IterNext(&process_result_iter);
    ASSERT(profile_result);

    Process *process = profile->processes + process_index;
    process->pid = process_result->pid;

    // 1 counter header, 1 counter
    process->lane_size = process_result->counter_size * 2;
    process->lanes = PushArray(arena, Lane, process->lane_size);
    isize lane_size = 0;
    BuildCounters(arena, scratch, process_result, process, &lane_size);
  }

  return profile;
}

static void DoLoadDocument(Task *task) {
  LoadState *state = (LoadState *)task->data;
  Buffer path = OsGetFilePath(state->file);
  INFO("Loading file %.*s ...", (int)path.size, path.data);

  Arena parse_arena = InitArena();
  ProfileResult *profile_result = 0;

  u64 start_counter = OsGetPerformanceCounter();
  {
    Arena load_arena = task->arena;
    Arena token_arena = InitArena();
    Arena value_arena = InitArena();

    GetJsonInputData get_json_input_data = {};
    get_json_input_data.task = task;
    get_json_input_data.buf = PushBufferNoZero(&load_arena, 4096);
    get_json_input_data.load = InitLoad(&load_arena, state->file);

    JsonParser parser = InitJsonParser(GetJsonInput, &get_json_input_data);
    profile_result =
        ParseJsonTrace(&parse_arena, value_arena, token_arena, &parser);

    OsCloseFile(state->file);
    ClearArena(&token_arena);
    ClearArena(&value_arena);
  }
  u64 end_counter = OsGetPerformanceCounter();
  f32 seconds =
      (f64)(end_counter - start_counter) / (f64)OsGetPerformanceFrequency();

  if (!IsTaskCancelled(task)) {
    INFO("Loaded %.1f MB in %.2f s (%.1f MB/s).",
         state->loaded / 1024.0f / 1024.0f, seconds,
         state->loaded / seconds / 1024.0f / 1024.0f);
    if (profile_result->error.size) {
      state->error = PushBuffer(state->document_arena, profile_result->error);
      ERROR("%.*s", (int)state->error.size, state->error.data);
    }
    state->profile =
        BuildProfile(state->document_arena, task->arena, profile_result);
  }

  ClearArena(&parse_arena);
}

static void WaitLoading(Document *document) {
  ASSERT(document->state == Document_Loading);
  if (WaitTask(document->loading.task)) {
    LoadState *state = &document->loading.state;
    document->state = Document_View;
    document->view.error = state->error;
    if (document->view.error.data) {
      ImGui::OpenPopup("Error");
    }
    document->view.profile = state->profile;
    document->view.begin_time = document->view.profile->min_time;
    document->view.end_time = document->view.profile->max_time;
    if (document->view.end_time <= document->view.begin_time) {
      document->view.begin_time = 0;
      document->view.end_time = 1'000'000'000;
    }
    i64 duration = document->view.end_time - document->view.begin_time;
    document->view.begin_time -= duration * 0.1f;
    document->view.end_time += duration * 0.1f;
  }
}

static Document *LoadDocument(OsLoadingFile *file) {
  Document *document = BootstrapPushStruct(Document, arena);
  document->path = PushBuffer(&document->arena, OsGetFilePath(file));
  document->state = Document_Loading;
  document->loading.task = CreateTask(DoLoadDocument, &document->loading.state);
  document->loading.state.document_arena = &document->arena;
  document->loading.state.file = file;
  return document;
}

static void UnloadDocument(Document *document) {
  if (document->state == Document_Loading) {
    CancelTask(document->loading.task);
    WaitLoading(document);
  }
  ClearArena(&document->arena);
}

static i64 CalcBlockDuration(i64 duration, f32 width, f32 target_block_width) {
  isize num_blocks = floorf(width / target_block_width);
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

static const char *TIME_UNIT[] = {"ns", "us", "ms", "s"};

static char *PushFormatTimeZ(Arena *arena, i64 time_) {
  if (time_ == 0) {
    return PushFormatZ(arena, "%s", "0");
  }

  isize time_unit_index = 0;
  f64 time = time_;
  while (fabs(time) >= 1000.0 && time_unit_index < ARRAY_SIZE(TIME_UNIT)) {
    time /= 1000.0;
    time_unit_index++;
  }

  Buffer buf = PushFormat(arena, "%.1lf%s", time, TIME_UNIT[time_unit_index]);
  char *period = (char *)(buf.data + buf.size - 1);
  while (*period != '.') {
    period--;
  }
  if (*(period + 1) == '0') {
    // buf has extra 0 in the end, guaranteed by PushFormat.
    MoveMemory(period, period + 2, buf.data + buf.size - (u8 *)period - 1);
  }
  return (char *)buf.data;
}

static void UpdateTimeline(Arena scratch, f32 width, f32 point_per_time,
                           i64 begin_time, i64 end_time) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  ImGuiStyle *style = &ImGui::GetStyle();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  Vec2 size = {width, window->CalcFontSize() + style->FramePadding.y * 4.0f};
  Vec2 min = ImGui::GetCursorScreenPos();
  Vec2 max = min + size;

  f32 text_baseline_y = style->FramePadding.y * 2.0f;
  f32 text_top = min.y + text_baseline_y;

  ImGuiID id = ImGui::GetID("timeline");
  ImGui::ItemSize(size, text_baseline_y);
  ImRect bb = {min, max};
  if (ImGui::ItemAdd(bb, id)) {
    draw_list->AddLine({min.x, max.y}, max, IM_COL32_BLACK);

    i64 duration = end_time - begin_time;
    i64 block_duration = CalcBlockDuration(duration, size.x, size.y * 1.5f);
    i64 large_block_duration = block_duration * 5;

    i64 time = begin_time / block_duration * block_duration;
    while (time <= end_time) {
      f32 x = min.x + (time - begin_time) * point_per_time;
      bool is_large_block = time % large_block_duration == 0;
      f32 height = is_large_block ? style->FramePadding.y * 4.0f
                                  : style->FramePadding.y * 2.0f;

      draw_list->AddLine({x, max.y}, {x, max.y - height}, IM_COL32_BLACK);

      if (is_large_block) {
        draw_list->AddText({x + style->FramePadding.x * 2.0f, text_top},
                           IM_COL32_BLACK, PushFormatTimeZ(&scratch, time));
      }

      time += block_duration;
    }

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);
    if (hovered) {
      f32 mouse_x = ImGui::GetMousePos().x;
      i64 time = begin_time + (mouse_x - min.x) / point_per_time;
      if (ImGui::BeginTooltip()) {
        ImGui::Text("%s", PushFormatTimeZ(&scratch, time));
        ImGui::EndTooltip();
      }
    }
  }
}

static void UpdateCounterHeader(CounterHeader *counter_header, Vec2 lane_min,
                                Arena scratch) {
  char *text = PushFormatZ(&scratch, "%.*s", (int)counter_header->name.size,
                           counter_header->name.data);
  ImGui::SetCursorScreenPos(lane_min);
  ImGui::SeparatorText(text);
  if (ImGui::BeginItemTooltip()) {
    ImGui::Text("%s", text);
    ImGui::EndTooltip();
  }
}

static void UpdateCounter(Counter *counter, Vec2 lane_min, Vec2 lane_size,
                          f32 point_per_time, i64 begin_time, i64 end_time,
                          Arena scratch) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  f64 total = counter->max - counter->min;
  for (isize series_index = 0; series_index < counter->series_size;
       ++series_index) {
    Series *series = counter->series + series_index;
    f32 x = lane_min.x;
    // TODO: Get first point that is <= begin_time, if none, get first point
    for (isize sample_index = 0; sample_index < series->sample_size;
         ++sample_index) {
      SeriesSample *sample = series->samples + sample_index;

      f32 left = lane_min.x + point_per_time * (sample->time - begin_time);
      f32 bottom = lane_min.y + lane_size.y;
      f32 height = sample->value / total * lane_size.y;
      f32 top = bottom - height;

      // TODO: Properly draw bar chart
      draw_list->AddRect({left, top}, {left + 2, top + 2},
                         IM_COL32(255, 0, 0, 255));
    }
  }
}

static void UpdateProcess(Process *process, bool show_lane_border, f32 width,
                          f32 point_per_time, i64 begin_time, i64 end_time,
                          Arena scratch) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  ImGuiStyle *style = &ImGui::GetStyle();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  Vec2 lane_min = ImGui::GetCursorScreenPos();
  Vec2 lane_size = {width,
                    window->CalcFontSize() + style->FramePadding.y * 4.0f};

  for (isize lane_index = 0; lane_index < process->lane_size; ++lane_index) {
    Vec2 lane_max = lane_min + lane_size;

    if (show_lane_border) {
      draw_list->AddRect(lane_min, lane_max, IM_COL32(255, 0, 0, 255));
    }

    Lane *lane = GetLane(process, lane_index);
    switch (lane->type) {
      case LaneType_CounterHeader: {
        UpdateCounterHeader(&lane->counter_header, lane_min, scratch);
      } break;

      case LaneType_Counter: {
        UpdateCounter(&lane->counter, lane_min, lane_size, point_per_time,
                      begin_time, end_time, scratch);
      } break;

      default: {
        UNREACHABLE;
      } break;
    }

    lane_min.y += lane_size.y;
  }
}

static void UpdateProfile(ViewState *view, Arena scratch) {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      Vec2{ImGui::GetStyle().ItemSpacing.x, 0});

  f32 width = ImGui::GetWindowWidth();
  f32 point_per_time = width / (view->end_time - view->begin_time);

  UpdateTimeline(scratch, width, point_per_time, view->begin_time,
                 view->end_time);

  ImGui::BeginChild("Main");
  Profile *profile = view->profile;
  for (isize process_index = 0; process_index < profile->process_size;
       ++process_index) {
    Process *process = profile->processes + process_index;

    ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    char *title = PushFormatZ(&scratch, "Process %d", (int)process->pid);
    if (ImGui::CollapsingHeader(title, 0)) {
      UpdateProcess(process, view->show_lane_border, width, point_per_time,
                    view->begin_time, view->end_time, scratch);
    }
  }
  ImGui::EndChild();

  ImGui::PopStyleVar(1);
}

static void UpdateDocument(Document *document, Arena scratch) {
  switch (document->state) {
    case Document_Loading: {
      {
        ImGuiStyle *style = &ImGui::GetStyle();

        char *text =
            PushFormatZ(&scratch, "Loading %.1f MB ...",
                        document->loading.state.loaded / 1024.0f / 1024.0f);
        Vec2 window_size = ImGui::GetWindowSize();
        Vec2 text_size = ImGui::CalcTextSize(text);
        Vec2 total_size = text_size + Vec2{style->FramePadding.x * 4.0f,
                                           style->FramePadding.y * 4.0f};
        ImGui::SetCursorPos((window_size - total_size) / 2.0f);
        ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), total_size, text);
      }

      if (IsTaskDone(document->loading.task)) {
        WaitLoading(document);
      }
    } break;

    case Document_View: {
      ViewState *view = &document->view;
      if (view->error.data) {
        Buffer error = view->error;
        if (ImGui::BeginPopupModal("Error", 0,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("%.*s", (int)error.size, error.data);
          ImGui::Separator();
          if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }
      }
      UpdateProfile(view, scratch);
    } break;

    default: {
      UNREACHABLE;
    } break;
  }
}
