#include <zlib.h>

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
  WaitTask(document->loading.task);

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

static void DrawTimeline(Arena scratch, i64 begin_time, i64 end_time) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  ImGuiStyle *style = &ImGui::GetStyle();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  Vec2 size = {ImGui::GetWindowWidth(),
               window->CalcFontSize() + style->FramePadding.y * 4.0f};
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
    f32 point_per_time = size.x / duration;
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

static void UpdateProfile(ViewState *view, Arena scratch) {
  DrawTimeline(scratch, view->begin_time, view->end_time);
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
