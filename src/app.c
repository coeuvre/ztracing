#include "src/app.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "src/colors.h"
#include "src/imgui_c.h"
#include "src/loading_screen.h"
#include "src/logging.h"
#include "src/platform.h"
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

static void trace_loading_job(void* user_data) {
  trace_loading_state_t* loading = (trace_loading_state_t*)user_data;
  int my_session_id = loading->session_id;
  allocator_t allocator = loading->allocator;

  LOG_DEBUG("trace_loading_job started (session_id: %d)", my_session_id);

  size_t total_discarded_bytes = 0;
  trace_event_matcher_t matcher = {};

  while (true) {
    trace_chunk_t chunk = {};
    bool popped = false;

    pthread_mutex_lock(&loading->chunk_queue.mutex);
    while (loading->chunk_queue.head == nullptr &&
           !atomic_load(&loading->jobs_should_abort) &&
           loading->session_id == my_session_id) {
      pthread_cond_wait(&loading->chunk_queue.cv, &loading->chunk_queue.mutex);
    }

    if (atomic_load(&loading->jobs_should_abort) ||
        loading->session_id != my_session_id) {
      pthread_mutex_unlock(&loading->chunk_queue.mutex);
      break;
    }

    if (loading->chunk_queue.head != nullptr) {
      trace_chunk_node_t* node = loading->chunk_queue.head;
      chunk = node->chunk;
      loading->chunk_queue.head = node->next;
      if (loading->chunk_queue.head == nullptr) {
        loading->chunk_queue.tail = nullptr;
      }
      allocator_free(allocator, node, sizeof(trace_chunk_node_t));
      atomic_fetch_sub_explicit(&loading->chunk_queue.queue_size_bytes,
                                chunk.size, memory_order_relaxed);
      popped = true;
    }
    pthread_mutex_unlock(&loading->chunk_queue.mutex);

    if (popped && chunk.data) {
      total_discarded_bytes += trace_parser_feed(
          &loading->parser, chunk.data, chunk.size, chunk.is_eof, allocator);
      allocator_free(allocator, chunk.data, chunk.size);

      atomic_store_explicit(&loading->input_consumed_bytes,
                            chunk.input_consumed_bytes, memory_order_relaxed);
    }

    trace_event_t event;
    while (trace_parser_next(&loading->parser, &event, allocator)) {
      trace_data_add_event(loading->trace_data, loading->theme, &event,
                           &matcher, allocator);
      atomic_fetch_add_explicit(&loading->event_count, 1, memory_order_relaxed);
      atomic_store_explicit(&loading->total_bytes,
                            total_discarded_bytes + loading->parser.pos,
                            memory_order_relaxed);
    }

    atomic_store_explicit(&loading->request_update, true, memory_order_relaxed);

    if (popped && chunk.is_eof) break;
  }

  if (!atomic_load(&loading->jobs_should_abort) &&
      loading->session_id == my_session_id) {
    double parse_end_time = platform_get_now();
    double duration_ms = parse_end_time - loading->start_time;
    double duration_s = duration_ms / 1000.0;
    double speed_mb_s =
        duration_s > 0.0
            ? ((double)atomic_load(&loading->total_bytes) / (1024.0 * 1024.0)) /
                  duration_s
            : 0.0;
    LOG_INFO("parsed %zu events (total) in %.3f ms (%.2f mb/s)",
             atomic_load(&loading->event_count), duration_ms, speed_mb_s);

    double organize_start_time = platform_get_now();
    int64_t min_ts, max_ts;
    track_organize(loading->trace_data, loading->theme,
                   &loading->trace_viewer->tracks, &min_ts, &max_ts, allocator);
    double organize_end_time = platform_get_now();
    LOG_INFO("organize track in %.3f ms",
             organize_end_time - organize_start_time);
    loading->trace_viewer->viewport.min_ts = min_ts;
    loading->trace_viewer->viewport.max_ts = max_ts;
    trace_viewer_reset_view(loading->trace_viewer);
    trace_viewer_precompute_minimap_heatmap(loading->trace_viewer,
                                            loading->trace_data, allocator);
    atomic_store_explicit(&loading->request_update, true, memory_order_relaxed);
    LOG_DEBUG("trace_loading_job finished (session_id: %d)", my_session_id);
  } else {
    LOG_DEBUG("trace_loading_job aborted/superseded (session_id: %d)",
              my_session_id);
  }

  trace_event_matcher_deinit(&matcher, allocator);
  trace_parser_deinit(&loading->parser, allocator);

  pthread_mutex_lock(&loading->quit_mutex);
  atomic_store(&loading->active, false);
  pthread_cond_broadcast(&loading->quit_cv);
  pthread_mutex_unlock(&loading->quit_mutex);
}

static void app_apply_theme(app_t* app, const theme_t* theme) {
  if (app->theme == theme) return;
  app->theme = theme;
  if (theme == theme_get_dark()) {
    ig_style_colors_dark();
  } else {
    ig_style_colors_light();
  }

  if (atomic_load(&app->loading.active)) return;

  // Re-compute all event colors when theme changes
  size_t events_count = app->trace_data.events.len;
  for (size_t i = 0; i < events_count; i++) {
    trace_data_update_event_color(&app->trace_data, (uint32_t)i, app->theme);
  }

  // Re-compute all counter track colors when theme changes
  track_update_colors(&app->trace_viewer.tracks, &app->trace_data, app->theme);
}

static void app_stop_jobs(app_t* app) {
  atomic_store(&app->loading.jobs_should_abort, true);
  pthread_mutex_lock(&app->loading.chunk_queue.mutex);
  pthread_cond_broadcast(&app->loading.chunk_queue.cv);
  pthread_mutex_unlock(&app->loading.chunk_queue.mutex);

  if (atomic_load(&app->loading.active)) {
    pthread_mutex_lock(&app->loading.quit_mutex);
    while (atomic_load(&app->loading.active)) {
      pthread_cond_wait(&app->loading.quit_cv, &app->loading.quit_mutex);
    }
    pthread_mutex_unlock(&app->loading.quit_mutex);
  }
}

void app_init(app_t* app, allocator_t parent) {
  *app = (app_t){
      .counting_allocator = counting_allocator_init(parent),
      .power_save_mode = true,
      .first_frame = true,
  };

  pthread_mutex_init(&app->loading.chunk_queue.mutex, nullptr);
  pthread_cond_init(&app->loading.chunk_queue.cv, nullptr);
  pthread_mutex_init(&app->loading.quit_mutex, nullptr);
  pthread_cond_init(&app->loading.quit_cv, nullptr);
}

void app_deinit(app_t* app) {
  app_stop_jobs(app);

  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);
  trace_data_deinit(&app->trace_data, allocator);
  array_list_deinit(&app->loading.filename, allocator);
  trace_viewer_deinit(&app->trace_viewer, allocator);

  pthread_mutex_destroy(&app->loading.chunk_queue.mutex);
  pthread_cond_destroy(&app->loading.chunk_queue.cv);
  pthread_mutex_destroy(&app->loading.quit_mutex);
  pthread_cond_destroy(&app->loading.quit_cv);

  pthread_mutex_lock(&app->loading.chunk_queue.mutex);
  trace_chunk_node_t* curr = app->loading.chunk_queue.head;
  while (curr) {
    trace_chunk_node_t* next = curr->next;
    if (curr->chunk.data) {
      allocator_free(allocator, curr->chunk.data, curr->chunk.size);
    }
    allocator_free(allocator, curr, sizeof(trace_chunk_node_t));
    curr = next;
  }
  pthread_mutex_unlock(&app->loading.chunk_queue.mutex);
}

void app_update(app_t* app) {
  // 0. Main Menu Bar
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
          app_on_theme_changed(app);
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

  // 1. Setup DockSpace
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
  if (ig_begin_popup_modal(
          "Shortcuts", &app->show_shortcuts_window,
          IG_WINDOW_FLAGS_NO_MOVE | 8)) {  // IG_WINDOW_FLAGS_NO_SCROLLBAR is
                                           // not needed if we do AutoResize
    // Close when clicking outside
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

  // 2. Scene Rendering
  ig_window_flags_t viewport_flags =
      IG_WINDOW_FLAGS_NO_TITLE_BAR | IG_WINDOW_FLAGS_NO_COLLAPSE |
      IG_WINDOW_FLAGS_NO_RESIZE | IG_WINDOW_FLAGS_NO_MOVE |
      IG_WINDOW_FLAGS_NO_SCROLLBAR;

  ig_push_style_var_float(IG_STYLE_VAR_WINDOW_ROUNDING, 0.0f);
  ig_push_style_var_float(IG_STYLE_VAR_WINDOW_BORDER_SIZE, 0.0f);
  ig_push_style_var(IG_STYLE_VAR_WINDOW_PADDING, (ig_vec2_t){0.0f, 0.0f});

  if (ig_begin("Main Viewport", nullptr, viewport_flags)) {
    if (atomic_load(&app->loading.active)) {
      const char* filename = app->loading.filename.len > 0
                                 ? (const char*)app->loading.filename.ptr
                                 : "";
      loading_screen_draw(
          filename, (size_t)atomic_load(&app->loading.event_count),
          (size_t)atomic_load(&app->loading.total_bytes),
          (size_t)atomic_load(&app->loading.input_consumed_bytes),
          (size_t)atomic_load(&app->loading.input_total_bytes), app->theme);
    } else if (app->trace_data.events.len > 0 &&
               !atomic_load(&app->loading.active)) {
      allocator_t allocator =
          counting_allocator_get_allocator(&app->counting_allocator);
      trace_viewer_draw(&app->trace_viewer, &app->trace_data, allocator,
                        app->theme);
    } else {
      welcome_screen_draw(app->theme);
    }
  }
  ig_end();
  ig_pop_style_var(3);
}

void app_on_theme_changed(app_t* app) {
  if (app->theme_mode == THEME_MODE_AUTO) {
    app_apply_theme(
        app, platform_is_dark_mode() ? theme_get_dark() : theme_get_light());
  }
}

void app_begin_session(app_t* app, int session_id, const char* filename,
                       size_t input_total_bytes) {
  app_stop_jobs(app);

  app->loading.trace_data = &app->trace_data;
  app->loading.trace_viewer = &app->trace_viewer;
  app->loading.allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  app->trace_viewer.search.td = &app->trace_data;
  app->trace_viewer.search.allocator = app->loading.allocator;

  app->loading.parser = (trace_parser_t){};
  allocator_t allocator = app->loading.allocator;
  trace_data_clear(&app->trace_data, allocator);
  atomic_store_explicit(&app->loading.event_count, 0, memory_order_relaxed);
  atomic_store_explicit(&app->loading.total_bytes, 0, memory_order_relaxed);
  atomic_store_explicit(&app->loading.input_total_bytes, input_total_bytes,
                        memory_order_relaxed);
  atomic_store_explicit(&app->loading.input_consumed_bytes, 0,
                        memory_order_relaxed);
  app->loading.start_time = platform_get_now();
  atomic_store_explicit(&app->loading.active, true, memory_order_relaxed);
  app->loading.session_id = session_id;
  app->loading.theme = app->theme;
  app->trace_viewer.focused_event_idx = -1;
  app->trace_viewer.request_scroll_to_focused_event = false;
  array_list_clear(&app->trace_viewer.selected_event_indices);

  atomic_store_explicit(&app->loading.jobs_should_abort, false,
                        memory_order_relaxed);
  {
    pthread_mutex_lock(&app->loading.chunk_queue.mutex);
    trace_chunk_node_t* curr = app->loading.chunk_queue.head;
    while (curr) {
      trace_chunk_node_t* next = curr->next;
      if (curr->chunk.data) {
        allocator_free(allocator, curr->chunk.data, curr->chunk.size);
      }
      allocator_free(allocator, curr, sizeof(trace_chunk_node_t));
      curr = next;
    }
    app->loading.chunk_queue.head = nullptr;
    app->loading.chunk_queue.tail = nullptr;
    atomic_store_explicit(&app->loading.chunk_queue.queue_size_bytes, 0,
                          memory_order_relaxed);
    app->loading.chunk_queue.closed = false;
    pthread_mutex_unlock(&app->loading.chunk_queue.mutex);
  }

  array_list_clear(&app->loading.filename);
  if (filename) {
    size_t len = strlen(filename) + 1;
    char* dest = (char*)array_list_append_(&app->loading.filename, len,
                                           sizeof(char), allocator);
    memcpy(dest, filename, len);
  }

  pthread_mutex_lock(&app->loading.chunk_queue.mutex);
  pthread_cond_broadcast(&app->loading.chunk_queue.cv);
  pthread_mutex_unlock(&app->loading.chunk_queue.mutex);

  platform_submit_job((platform_job_fn_t)trace_loading_job, &app->loading);
}

size_t app_handle_file_chunk(app_t* app, int session_id, char* data,
                             size_t size, size_t input_consumed_bytes,
                             bool is_eof) {
  if (session_id != app->loading.session_id) {
    if (data && size > 0) {
      allocator_t allocator =
          counting_allocator_get_allocator(&app->counting_allocator);
      allocator_free(allocator, data, size);
    }
    return atomic_load_explicit(&app->loading.chunk_queue.queue_size_bytes,
                                memory_order_relaxed);
  }

  trace_chunk_t chunk = {
      .data = data,
      .size = size,
      .input_consumed_bytes = input_consumed_bytes,
      .is_eof = is_eof,
  };

  allocator_t allocator =
      counting_allocator_get_allocator(&app->counting_allocator);

  pthread_mutex_lock(&app->loading.chunk_queue.mutex);
  trace_chunk_node_t* node = (trace_chunk_node_t*)allocator_alloc(
      allocator, sizeof(trace_chunk_node_t));
  if (node) {
    node->chunk = chunk;
    node->next = nullptr;
    if (app->loading.chunk_queue.tail) {
      app->loading.chunk_queue.tail->next = node;
    } else {
      app->loading.chunk_queue.head = node;
    }
    app->loading.chunk_queue.tail = node;

    atomic_fetch_add_explicit(&app->loading.chunk_queue.queue_size_bytes, size,
                              memory_order_relaxed);
    if (is_eof) app->loading.chunk_queue.closed = true;
    pthread_cond_broadcast(&app->loading.chunk_queue.cv);
  }
  pthread_mutex_unlock(&app->loading.chunk_queue.mutex);

  return atomic_load_explicit(&app->loading.chunk_queue.queue_size_bytes,
                              memory_order_relaxed);
}

size_t app_get_queue_size(app_t* app) {
  return atomic_load_explicit(&app->loading.chunk_queue.queue_size_bytes,
                              memory_order_relaxed);
}
