#include "src/app.h"

#include <stdio.h>
#include <string.h>

#include "core/counting_allocator.h"
#include "core/logging.h"
#include "src/colors.h"
#include "src/imgui_c.h"
#include "src/loading_screen.h"
#include "src/platform.h"
#include "src/trace_load_task.h"
#include "src/trace_search_task.h"
#include "src/welcome_screen.h"

// Helper functions for cheatsheet rendering
static void cheatsheet_add_row(const theme_t* theme, const char* action,
                               const char* shortcut) {
  ig_table_next_row();
  ig_table_next_column();
  ig_text_unformatted(action, nullptr);
  ig_table_next_column();
  ig_text_colored(ig_color_convert_u32_to_float4(theme->ruler_text), "%s",
                  shortcut);
}

static void cheatsheet_add_section(const theme_t* theme, const char* title,
                                   void (*content_func)(const theme_t* theme)) {
  ig_begin_group();
  ig_spacing();
  ig_text_colored(ig_color_convert_u32_to_float4(theme->track_text), "%s",
                  title);
  ig_separator();
  ig_spacing();

  if (ig_begin_table(title, 2,
                     IG_TABLE_FLAGS_ROW_BG | IG_TABLE_FLAGS_SIZING_FIXED_FIT,
                     (ig_vec2_t){0.0f, 0.0f}, 0.0f)) {
    ig_table_setup_column("Action", IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED, 150.0f,
                          0);
    ig_table_setup_column("Key", IG_TABLE_COLUMN_FLAGS_WIDTH_STRETCH, 0.0f, 0);
    content_func(theme);
    ig_end_table();
  }
  ig_end_group();
}

static void draw_general_shortcuts(const theme_t* theme) {
  bool is_mac = platform_is_mac();
  cheatsheet_add_row(theme, "Toggle Shortcuts", "?");
  cheatsheet_add_row(theme, "Search Events", is_mac ? "Cmd + F" : "Ctrl + F");
  cheatsheet_add_row(theme, "Toggle Details", "Menu > View");
  cheatsheet_add_row(theme, "Metrics / Debug", "Menu > Tools");
}

static void draw_navigation_shortcuts(const theme_t* theme) {
  cheatsheet_add_row(theme, "Zoom In/Out", "Ctrl + Scroll");
  cheatsheet_add_row(theme, "Zoom to Event", "Double Click");
  cheatsheet_add_row(theme, "Pan Horizontally", "Shift + Scroll");
  cheatsheet_add_row(theme, "Pan (Any Direction)", "Left Drag");
  cheatsheet_add_row(theme, "Reset View", "Menu > View");
}

static void draw_selection_shortcuts(const theme_t* theme) {
  cheatsheet_add_row(theme, "Timeline Selection", "Drag on Ruler");
  cheatsheet_add_row(theme, "Rectangle Select", "Shift + Drag");
  cheatsheet_add_row(theme, "Select Event", "Left Click");
  cheatsheet_add_row(theme, "Clear Selection", "Click Background");
  cheatsheet_add_row(theme, "Clear Focused", "Click Background");
}

static void app_apply_theme(app_t* app, const theme_t* theme) {
  if (app->theme == theme) return;
  app->theme = theme;
  ig_style_apply_theme(theme);
}

void app_stop_jobs(app_t* app) {
  if (app->trace_load_task != nullptr) {
    trace_load_task_abort(app->trace_load_task);
    trace_load_task_release(app->trace_load_task);
    app->trace_load_task = nullptr;
    app->loading.active = false;
  }

  // Cancel active search task (if any)
  if (app->active_search_task != nullptr) {
    task_queue_cancel_submission(app->task_queue, app->active_search_task);
    app->active_search_task = nullptr;
  }
}

void app_init(app_t* app, allocator_t* parent) {
  *app = (app_t){
      .power_save_mode = true,
      .first_frame = true,
  };
  counting_allocator_init(&app->counting_allocator, parent);

  allocator_t* allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // Initialize the global background task queue
  app->task_queue = task_queue_create(1024, platform_submit_job, allocator);
  app->trace_load_task = nullptr;
  app->active_search_task = nullptr;

  trace_viewer_init(&app->trace_viewer);

  // Load saved theme mode
  char theme_str[16] = {0};
  app->theme_mode = THEME_MODE_AUTO; // Default
  if (platform_get_setting("theme_mode", theme_str, sizeof(theme_str))) {
    if (strcmp(theme_str, "dark") == 0) {
      app->theme_mode = THEME_MODE_DARK;
    } else if (strcmp(theme_str, "light") == 0) {
      app->theme_mode = THEME_MODE_LIGHT;
    }
  }
}

void app_deinit(app_t* app) {
  // Signal background jobs to cancel and close channels
  app_stop_jobs(app);

  allocator_t* allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  task_queue_destroy(app->task_queue);

  // Deallocate structures
  trace_data_release(app->trace_data, allocator);
  darray_deinit(&app->loading.filename, allocator);
  trace_viewer_deinit(&app->trace_viewer, allocator);
}

void app_poll_completions(app_t* app) {
  allocator_t* allocator =
      counting_allocator_get_allocator(&app->counting_allocator);
  task_completion_t cqe;
  bool reaped_any = false;

  // Drain all completed tasks from the global Completion Queue (CQ)
  while (task_queue_peek_completion(app->task_queue, &cqe)) {
    reaped_any = true;

    // 1. Identify the task type DIRECTLY by its function pointer address!
    // Since the task engine now preserves the task pointer even on
    // cancellation, we can rely on cqe.task being trace_load_task_run 100% of
    // the time!
    if (cqe.task == trace_load_task_run) {
      trace_load_task_chunk_t* payload =
          (trace_load_task_chunk_t*)cqe.user_data;
      trace_load_task_t* task = payload->task;

      // A. SUCCESS PATH
      if (cqe.status == TASK_STATUS_OK) {
        // Update UI progress metrics smooth as butter!
        app->loading.event_count = payload->parsed_event_count;
        app->loading.total_bytes = payload->processed_bytes;
        app->loading.request_update = true;

        // On EOF, adopt the final parsed results
        if (payload->is_eof) {
          LOG_DEBUG(
              "app_poll_completions: loader task completed! Adopting "
              "results.");

          trace_data_release(app->trace_data, allocator);
          app->trace_data = payload->completed_td;
          app->trace_viewer.tracks = payload->completed_tracks;
          app->trace_viewer.viewport.min_ts = payload->completed_min_ts;
          app->trace_viewer.viewport.max_ts = payload->completed_max_ts;

          app->trace_load_task = nullptr;
          app->loading.active = false;

          trace_load_task_release(task);  // Release UI thread reference

          trace_viewer_reset_view(&app->trace_viewer);
          trace_viewer_precompute_minimap_heatmap(&app->trace_viewer,
                                                  app->trace_data, allocator);

          // Print performance stats on the UI thread once adopted
          if (payload->stats.ready) {
            LOG_INFO(
                "parsed %zu events, %.2f MB in %.3f ms (%.2f mb/s) "
                "[starvation: "
                "%.3f ms (%.2f%%)]",
                app->trace_data->events.len, payload->stats.size_mb,
                payload->stats.ingestion_duration_ms, payload->stats.speed_mb_s,
                payload->stats.starvation_ms, payload->stats.starvation_pct);

            LOG_INFO("organized %zu tracks in %.3f ms",
                     app->trace_viewer.tracks.len,
                     payload->stats.organize_duration_ms);
          }
        }
      }
      // B. CANCELLATION / FAILURE PATH (Zero-Leak Guard)
      else {
        LOG_DEBUG("app_poll_completions: loader task cancelled or failed!");

        if (payload->is_eof) {
          app->trace_load_task = nullptr;
          app->loading.active = false;
          trace_load_task_release(task);  // Release UI thread reference
        }
      }

      // Note: payload->data and payload itself are allocated from the
      // task-local arena and will be automatically reclaimed when
      // task_queue_remove_completion is called.
      trace_load_task_release(task);  // Decrements active_tasks for the CQE
    } else if (cqe.task == trace_search_task_run) {
      trace_search_task_t* task = (trace_search_task_t*)cqe.user_data;
      bool is_active = (app->active_search_task == task);

      if (is_active) {
        if (cqe.status == TASK_STATUS_OK) {
          // Adopt results and histogram synchronously on the UI thread
          trace_viewer_adopt_search_results(&app->trace_viewer, app->trace_data,
                                            task->results, task->histogram,
                                            allocator);
          // Clear results in task context so trace_search_task_destroy doesn't
          // free them
          task->results = (darray_int64_t){};
        }
        app->active_search_task = nullptr;
        app->trace_viewer.search.is_searching = false;
      }

      // Always destroy the task context and release trace_data reference
      trace_data_release((trace_data_t*)task->td, allocator);
      trace_search_task_destroy(task);
    }
    // Always remove the completion from the queue to free the slot!
    task_queue_remove_completion(app->task_queue);
  }

  if (reaped_any) {
    app->loading.request_update = true;
  }
}

void app_update(app_t* app) {
  allocator_t* allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // === 0. Search Coordination (Task Queue Spawning) ===
  if (app->trace_viewer.search_query_dirty) {
    app->trace_viewer.search_query_dirty = false;

    // Cancel the previous running search task (if any)
    if (app->active_search_task != nullptr) {
      task_queue_cancel_submission(app->task_queue, app->active_search_task);
      app->active_search_task = nullptr;
    }

    const char* query = (const char*)app->trace_viewer.search_query.ptr;
    if (query && query[0] != '\0' && app->trace_data != nullptr) {
      // 1. Lease a submission slot from the shared task queue
      task_submission_t* sub = task_queue_get_submission(app->task_queue);
      if (sub != nullptr) {
        // Retain a reference to trace_data for the background task!
        trace_data_retain(app->trace_data);

        // 2. Prepare the search task context using the submission slot
        app->active_search_task = trace_search_task_create(
            query, app->trace_data, !app->trace_viewer.exclude_thread_events,
            !app->trace_viewer.exclude_counter_events, sub, allocator);

        // 3. Submit and flush the queue!
        task_queue_submit(app->task_queue);

        app->trace_viewer.search.is_searching = true;
      } else {
        LOG_DEBUG("app_update: Task Queue is full! Dropping search query.");
      }
    } else {
      // For empty queries, clear the search state cleanly
      trace_viewer_clear_search(&app->trace_viewer, allocator);
    }
  }

  // === 1. Main Menu Bar ===
  if (ig_begin_main_menu_bar()) {
    if (ig_begin_menu("View", true)) {
      if (ig_menu_item("Reset View", nullptr, false, true)) {
        trace_viewer_reset_view(&app->trace_viewer);
      }
      ig_separator();

      ig_menu_item_ptr("Power-save Mode", nullptr, &app->power_save_mode, true);
      ig_menu_item_ptr("Details Panel", nullptr,
                       &app->trace_viewer.show_details_panel, true);

      ig_separator();
      if (ig_begin_menu("Theme", true)) {
        if (ig_menu_item("Auto", nullptr, app->theme_mode == THEME_MODE_AUTO,
                         true)) {
          app->theme_mode = THEME_MODE_AUTO;
          app_on_theme_changed(app, platform_is_dark_mode());
          platform_set_setting("theme_mode", "auto");
        }
        if (ig_menu_item("Dark", nullptr, app->theme_mode == THEME_MODE_DARK,
                         true)) {
          app->theme_mode = THEME_MODE_DARK;
          app_apply_theme(app, theme_get_dark());
          platform_set_setting("theme_mode", "dark");
        }
        if (ig_menu_item("Light", nullptr, app->theme_mode == THEME_MODE_LIGHT,
                         true)) {
          app->theme_mode = THEME_MODE_LIGHT;
          app_apply_theme(app, theme_get_light());
          platform_set_setting("theme_mode", "light");
        }
        ig_end_menu();
      }
      ig_end_menu();
    }
    if (ig_begin_menu("Tools", true)) {
      const char* search_shortcut = platform_is_mac() ? "Cmd+F" : "Ctrl+F";
      if (ig_menu_item("Search Events", search_shortcut, false, true)) {
        app->trace_viewer.show_details_panel = true;
        app->trace_viewer.focus_search_input = true;
      }
      ig_menu_item_ptr("Metrics/Debugger", nullptr, &app->show_metrics_window,
                       true);
      ig_end_menu();
    }
    if (ig_begin_menu("Help", true)) {
      if (ig_menu_item("Shortcuts", "?", false, true)) {
        app->show_shortcuts_window = true;
      }
      ig_menu_item_ptr("About Dear ImGui", nullptr, &app->show_about_window,
                       true);
      ig_end_menu();
    }

    // Right-aligned memory usage
    size_t allocated =
        counting_allocator_get_allocated_bytes(&app->counting_allocator);
    char mem_buf[64];
    snprintf(mem_buf, sizeof(mem_buf), "%.1f MB",
             (double)allocated / (1024.0 * 1024.0));
    float text_width = ig_calc_text_size(mem_buf).x;
    ig_same_line(ig_get_window_size().x - text_width - 8.0f * 2.0f, 0.0f);
    ig_text_disabled("%s", mem_buf);

    ig_end_main_menu_bar();
  }

  // === 2. Setup DockSpace ===
  ig_dock_node_flags_t dockspace_flags = IG_DOCK_NODE_FLAGS_NONE;
  uint32_t dockspace_id =
      ig_dock_space_over_viewport(0, ig_get_main_viewport(), dockspace_flags);

  if (app->first_frame) {
    app->first_frame = false;
    ig_dock_builder_remove_node(dockspace_id);
    ig_dock_builder_add_node(dockspace_id,
                             dockspace_flags | IG_DOCK_NODE_FLAGS_DOCKSPACE);
    ig_dock_builder_set_node_size(dockspace_id,
                                  ig_viewport_get_size(ig_get_main_viewport()));

    uint32_t dock_id_main = dockspace_id;
    uint32_t dock_id_right;
    ig_dock_builder_split_node(dock_id_main, IG_DIR_RIGHT, 0.30f,
                               &dock_id_right, &dock_id_main);
    ig_dock_builder_dock_window("Details", dock_id_right);

    ig_dock_node_t* main_node = ig_dock_builder_get_node(dock_id_main);
    if (main_node) {
      ig_dock_node_add_local_flags(main_node,
                                   IG_DOCK_NODE_FLAGS_NO_TAB_BAR |
                                       IG_DOCK_NODE_FLAGS_NO_DOCKING_OVER_ME);
    }

    ig_dock_builder_dock_window("Main Viewport", dock_id_main);
    ig_dock_builder_finish(dockspace_id);
  }

  if (app->show_metrics_window)
    ig_show_metrics_window(&app->show_metrics_window);
  if (app->show_about_window) ig_show_about_window(&app->show_about_window);

  if (ig_is_key_pressed(IG_KEY_SLASH, false) && ig_get_io_key_shift() &&
      !ig_get_io_want_text_input()) {
    app->show_shortcuts_window = !app->show_shortcuts_window;
  }

  if (ig_is_key_pressed(IG_KEY_F, false) && ig_get_io_key_ctrl() &&
      !ig_get_io_want_text_input()) {
    app->trace_viewer.show_details_panel = true;
    app->trace_viewer.focus_search_input = true;
  }

  ig_vec2_t center = ig_viewport_get_center(ig_get_main_viewport());
  ig_vec2_t viewport_size = ig_viewport_get_size(ig_get_main_viewport());
  ig_set_next_window_pos(center, 8,
                         (ig_vec2_t){0.5f, 0.5f});  // ImGuiCond_Appearing = 8
  ig_set_next_window_size(
      (ig_vec2_t){viewport_size.x * 0.7f, viewport_size.y * 0.7f},
      8);  // ImGuiCond_Appearing = 8

  if (app->show_shortcuts_window) {
    ig_open_popup("Shortcuts", IG_POPUP_FLAGS_NONE);
  }

  ig_push_style_color(IG_COL_POPUP_BG,
                      ig_color_convert_u32_to_float4(app->theme->viewport_bg));
  if (ig_begin_popup_modal("Shortcuts", &app->show_shortcuts_window,
                           IG_WINDOW_FLAGS_NO_MOVE | 8)) {
    if (ig_is_mouse_clicked(0, false)) {
      ig_vec2_t mouse_pos = ig_get_io_mouse_pos();
      ig_vec2_t window_pos = ig_get_window_pos();
      ig_vec2_t window_size = ig_get_window_size();
      bool inside = mouse_pos.x >= window_pos.x &&
                    mouse_pos.x <= window_pos.x + window_size.x &&
                    mouse_pos.y >= window_pos.y &&
                    mouse_pos.y <= window_pos.y + window_size.y;
      if (!inside) {
        app->show_shortcuts_window = false;
        app->trace_viewer.ignore_next_release = true;
        ig_close_current_popup();
      }
    }

    if (ig_begin_child("CheatsheetContent", (ig_vec2_t){0, 0}, false,
                       IG_WINDOW_FLAGS_NO_SCROLLBAR)) {
      float width = ig_get_content_region_avail().x;
      float gap = 40.0f;
      float col_w = (width - gap) * 0.5f;

      // Left Column
      ig_begin_child("LeftCol", (ig_vec2_t){col_w, 0}, false,
                     IG_WINDOW_FLAGS_NO_SCROLLBAR);
      cheatsheet_add_section(app->theme, "GENERAL", draw_general_shortcuts);
      cheatsheet_add_section(app->theme, "NAVIGATION",
                             draw_navigation_shortcuts);
      ig_end_child();

      ig_same_line(0.0f, gap);

      // Right Column
      ig_begin_child("RightCol", (ig_vec2_t){col_w, 0}, false,
                     IG_WINDOW_FLAGS_NO_SCROLLBAR);
      cheatsheet_add_section(app->theme, "SELECTION", draw_selection_shortcuts);
      ig_end_child();

      ig_end_child();
    }
    ig_end_popup();
  }
  ig_pop_style_color(1);

  // === 3. Scene Rendering ===
  ig_window_flags_t viewport_flags =
      IG_WINDOW_FLAGS_NO_TITLE_BAR | IG_WINDOW_FLAGS_NO_COLLAPSE |
      IG_WINDOW_FLAGS_NO_RESIZE | IG_WINDOW_FLAGS_NO_MOVE |
      IG_WINDOW_FLAGS_NO_SCROLLBAR;

  ig_push_style_var_float(IG_STYLE_VAR_WINDOW_ROUNDING, 0.0f);
  ig_push_style_var_float(IG_STYLE_VAR_WINDOW_BORDER_SIZE, 0.0f);
  ig_push_style_var(IG_STYLE_VAR_WINDOW_PADDING, (ig_vec2_t){0.0f, 0.0f});

  if (ig_begin("Main Viewport", nullptr, viewport_flags)) {
    if (app->loading.active) {
      const char* filename = app->loading.filename.len > 0
                                 ? (const char*)app->loading.filename.ptr
                                 : "";
      loading_screen_draw(filename, app->loading.event_count,
                          app->loading.total_bytes,
                          app->loading.input_consumed_bytes,
                          app->loading.input_total_bytes, app->theme);
    } else if (app->trace_data != nullptr && app->trace_data->events.len > 0 &&
               !app->loading.active) {
      trace_viewer_draw(&app->trace_viewer, app->trace_data, allocator,
                        app->theme);
    } else {
      welcome_screen_draw(app->theme);
    }
  }
  ig_end();
  ig_pop_style_var(3);
}

void app_on_theme_changed(app_t* app, bool is_dark) {
  if (app->theme_mode == THEME_MODE_AUTO) {
    app_apply_theme(app, is_dark ? theme_get_dark() : theme_get_light());
  } else if (app->theme_mode == THEME_MODE_DARK) {
    app_apply_theme(app, theme_get_dark());
  } else if (app->theme_mode == THEME_MODE_LIGHT) {
    app_apply_theme(app, theme_get_light());
  }
}

void app_begin_session(app_t* app, int session_id, const char* filename,
                       size_t input_total_bytes) {
  // Stop any active running session (non-blocking abort)
  app_stop_jobs(app);

  allocator_t* allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // Reset the trace viewer state
  trace_viewer_deinit(&app->trace_viewer, allocator);
  app->trace_viewer = (trace_viewer_t){};

  // Clear old trace data
  trace_data_release(app->trace_data, allocator);
  app->trace_data = nullptr;

  // Initialize progress counters
  app->loading.event_count = 0;
  app->loading.total_bytes = 0;
  app->loading.input_total_bytes = input_total_bytes;
  app->loading.input_consumed_bytes = 0;
  app->loading.start_time = platform_get_now();
  app->loading.active = true;
  app->loading.session_id = session_id;
  app->loading.request_update = false;

  // Cache the trace filename
  darray_clear(&app->loading.filename);
  if (filename) {
    size_t len = strlen(filename) + 1;
    darray_resize(&app->loading.filename, len, allocator);
    memcpy(app->loading.filename.ptr, filename, len);
  }

  // Reset the loading task: since the old task (if active) was aborted
  // and set to null in app_stop_jobs, its ownership is managed by its own
  // active task reference counter. We simply generate a new unique stream ID
  // and start a fresh loading task on our background task queue.
  task_stream_t stream_id = (task_stream_t)session_id;
  app->loading.stream_id = stream_id;
  app->trace_load_task =
      trace_load_task_create(app->task_queue, stream_id, allocator);
}

size_t app_handle_file_chunk(app_t* app, int session_id, char* data,
                             size_t size, size_t input_consumed_bytes,
                             bool is_eof) {
  allocator_t* allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  size_t result = 0;

  // 1. Process only if the session is still active/valid
  if (session_id == app->loading.session_id) {
    // Track consumed bytes for progress bar display
    app->loading.input_consumed_bytes = input_consumed_bytes;

    if (app->trace_load_task != nullptr) {
      // Obtain a vacant submission slot from the shared task queue
      task_submission_t* sub = task_queue_get_submission(app->task_queue);
      if (sub != nullptr) {
        // Prepare the chunk submission (internally copies the transient buffer
        // to the arena!)
        trace_load_task_prep_chunk(app->trace_load_task, sub, data, size,
                                   input_consumed_bytes, is_eof);
        // Submit and flush the queue!
        task_queue_submit(app->task_queue);
      }

      // Fetch buffered bytes for backpressure flow control
      result = trace_load_task_get_buffered_bytes(app->trace_load_task);
    }
  }

  // 2. Single-Exit Cleanup: Always free the incoming buffer (caller-allocated)
  if (data && size > 0) {
    allocator_free(allocator, data, size);
  }

  return result;
}

size_t app_get_buffered_bytes(app_t* app) {
  return app->trace_load_task != nullptr
             ? trace_load_task_get_buffered_bytes(app->trace_load_task)
             : 0;
}
