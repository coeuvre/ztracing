#include "src/app.h"

#include <stdio.h>
#include <string.h>

#include "src/app_msg.h"
#include "src/colors.h"
#include "src/imgui_c.h"
#include "src/loading_screen.h"
#include "src/logging.h"
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
  if (theme == theme_get_dark()) {
    ig_style_colors_dark();
  } else {
    ig_style_colors_light();
  }
}

void app_stop_jobs(app_t* app) {
  if (app->trace_load_channel != nullptr) {
    // Send abort request to the loader task mailbox.
    // Asynchronous and 100% non-blocking!
    trace_load_send_abort(app->trace_load_channel);
    app->trace_load_channel = nullptr;
  }

  // Abort active search task (if any)
  if (app->trace_search_channel != nullptr) {
    trace_search_send_abort(app->trace_search_channel);
    app->trace_search_channel = nullptr;
  }
}

void app_init(app_t* app, allocator_t parent) {
  *app = (app_t){
      .counting_allocator = counting_allocator_init(parent),
      .power_save_mode = true,
      .first_frame = true,
  };

  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // Initialize the actor mailboxes
  app->ui_channel = channel_create(app_msg_t, 128, app_msg_deinit, allocator);
  app->trace_load_channel = nullptr;
  app->trace_search_channel = nullptr;

  trace_viewer_init(&app->trace_viewer);
}

void app_deinit(app_t* app) {
  // Signal background jobs to cancel and close channels
  app_stop_jobs(app);

  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  channel_destroy(app->ui_channel);

  // Deallocate structures
  trace_data_release(app->trace_data, allocator);
  array_list_deinit(&app->loading.filename, allocator);
  trace_viewer_deinit(&app->trace_viewer, allocator);
}

void app_poll_messages(app_t* app) {
  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // === Drain the Unified UI Actor Mailbox (Non-blocking loop) ===
  app_msg_t msg;
  while (channel_try_recv(app->ui_channel, &msg)) {
    app->loading.request_update = true;
    switch (msg.type) {
      case MSG_TRACE_LOAD_PROGRESS:
        app->loading.event_count = msg.as.load_progress.event_count;
        app->loading.total_bytes = msg.as.load_progress.total_bytes;
        break;

      case MSG_TRACE_LOAD_COMPLETE: {
        app_msg_load_result_t result = msg.as.load_result;

        // Adopt the completed parsed trace data and organized tracks
        trace_data_release(app->trace_data, allocator);
        app->trace_data = result.trace_data;
        app->trace_viewer.tracks = result.tracks;
        app->trace_viewer.viewport.min_ts = result.min_ts;
        app->trace_viewer.viewport.max_ts = result.max_ts;

        // Clear pointers in the message envelope so app_msg_deinit doesn't
        // free/release them!
        msg.as.load_result.trace_data = nullptr;
        msg.as.load_result.tracks = (array_list_t){};

        // The loader channel has been sent back and will be destroyed by
        // app_msg_deinit. We set the app pointer to null to prevent dangling
        // references or double-free.
        if (app->trace_load_channel == result.task_channel) {
          app->trace_load_channel = nullptr;
        }

        app->loading.active = false;
        trace_viewer_reset_view(&app->trace_viewer);
        trace_viewer_precompute_minimap_heatmap(&app->trace_viewer,
                                                app->trace_data, allocator);
        break;
      }

      case MSG_TRACE_LOAD_ABORTED: {
        app_msg_load_aborted_t aborted = msg.as.load_aborted;
        if (app->trace_load_channel == aborted.task_channel) {
          app->trace_load_channel = nullptr;
        }
        app->loading.active = false;
        break;
      }

      case MSG_TRACE_SEARCH_COMPLETE: {
        app_msg_search_result_t result = msg.as.search_result;
        bool is_active = (app->trace_search_channel == result.task_channel);

        if (is_active) {
          // Adopt results and histogram synchronously on the UI thread
          trace_viewer_adopt_search_results(&app->trace_viewer, app->trace_data,
                                            result.results, result.histogram,
                                            allocator);
          // Clear results in envelope so app_msg_deinit doesn't free them
          msg.as.search_result.results = (array_list_t){};

          app->trace_search_channel = nullptr;
          app->trace_viewer.search.is_searching = false;
        }
        break;
      }

      case MSG_TRACE_SEARCH_ABORTED: {
        app_msg_search_aborted_t aborted = msg.as.search_aborted;

        if (app->trace_search_channel == aborted.task_channel) {
          app->trace_search_channel = nullptr;
        }
        break;
      }

      default:
        break;
    }

    // Centralized message resource cleanup!
    app_msg_deinit(&msg);
  }
}

void app_update(app_t* app) {
  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // === 0. Search Coordination (Actor Model Spawning) ===
  if (app->trace_viewer.search_query_dirty) {
    app->trace_viewer.search_query_dirty = false;

    // Abort the previous running search task (if any)
    if (app->trace_search_channel != nullptr) {
      trace_search_send_abort(app->trace_search_channel);
    }

    // Create a NEW channel for the new search task.
    // The old channel remains alive and will be cleanly destroyed
    // by the UI thread when it receives the aborted message.
    app->trace_search_channel =
        channel_create(trace_search_msg_t, 8, nullptr, allocator);

    const char* query = (const char*)app->trace_viewer.search_query.ptr;
    if (query && query[0] != '\0' && app->trace_data != nullptr) {
      // Retain a reference to trace_data for the background task!
      trace_data_retain(app->trace_data);

      // Spawn a new background search task!
      trace_search_start(query, app->trace_data,
                         !app->trace_viewer.exclude_thread_events,
                         !app->trace_viewer.exclude_counter_events,
                         app->ui_channel, app->trace_search_channel, allocator);
    } else {
      // For empty queries, clear the search results synchronously
      array_list_clear(&app->trace_viewer.selected_event_indices);
      app->trace_viewer.histogram = (duration_histogram_t){};  // ZII
      app->trace_viewer.selected_events_dirty = true;
      app->trace_viewer.search.is_searching = false;
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
        }
        if (ig_menu_item("Dark", nullptr, app->theme_mode == THEME_MODE_DARK,
                         true)) {
          app->theme_mode = THEME_MODE_DARK;
          app_apply_theme(app, theme_get_dark());
        }
        if (ig_menu_item("Light", nullptr, app->theme_mode == THEME_MODE_LIGHT,
                         true)) {
          app->theme_mode = THEME_MODE_LIGHT;
          app_apply_theme(app, theme_get_light());
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
  }
}

void app_begin_session(app_t* app, int session_id, const char* filename,
                       size_t input_total_bytes) {
  // Stop any active running session (non-blocking abort)
  app_stop_jobs(app);

  allocator_t allocator =
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
  array_list_clear(&app->loading.filename);
  if (filename) {
    size_t len = strlen(filename) + 1;
    char* dest = (char*)array_list_append_(&app->loading.filename, len,
                                           sizeof(char), allocator);
    memcpy(dest, filename, len);
  }

  // Reset the loader channel: since the old channel (if active) was aborted
  // and set to null in app_stop_jobs, its ownership has been transferred to the
  // exiting worker thread. It will be safely destroyed inside app_msg_deinit
  // when the main thread processes the final abort message. We simply create a
  // fresh channel for the new loader task.
  app->trace_load_channel = channel_create(trace_load_msg_t, 1024, trace_load_msg_deinit, allocator);

  // Spawn the background loader worker task!
  trace_load_start(app->ui_channel, app->trace_load_channel, allocator);
}

size_t app_handle_file_chunk(app_t* app, int session_id, char* data,
                             size_t size, size_t input_consumed_bytes,
                             bool is_eof) {
  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  // Stale session safety check: immediately clean up chunk and discard
  if (session_id != app->loading.session_id) {
    if (data && size > 0) {
      allocator_free(allocator, data, size);
    }
    return app->trace_load_channel != nullptr
               ? channel_get_size(app->trace_load_channel)
               : 0;
  }

  // Track consumed bytes for progress bar display
  app->loading.input_consumed_bytes = input_consumed_bytes;

  // Push raw chunk through the safe helper.
  // If the loader queue is full or closed, trace_load_send_chunk AUTOMATICALLY
  // frees 'data'!
  if (app->trace_load_channel != nullptr) {
    trace_load_send_chunk(app->trace_load_channel, data, size,
                          input_consumed_bytes, is_eof, allocator);
  } else {
    if (data && size > 0) {
      allocator_free(allocator, data, size);
    }
  }

  // Return current queue size (in item count) to trigger backpressure
  return app->trace_load_channel != nullptr
             ? channel_get_size(app->trace_load_channel)
             : 0;
}

size_t app_get_queue_size(app_t* app) {
  return app->trace_load_channel != nullptr
             ? channel_get_size(app->trace_load_channel)
             : 0;
}
