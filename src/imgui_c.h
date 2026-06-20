#ifndef ZTRACING_SRC_IMGUI_C_H_
#define ZTRACING_SRC_IMGUI_C_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types for C
typedef struct ig_draw_list ig_draw_list_t;
typedef struct ig_font ig_font_t;
typedef struct ig_table_sort_specs ig_table_sort_specs_t;
typedef struct ig_input_text_callback_data ig_input_text_callback_data_t;
typedef struct ig_list_clipper ig_list_clipper_t;
typedef struct ig_viewport ig_viewport_t;
typedef struct ig_dock_node ig_dock_node_t;
typedef struct ig_draw_data ig_draw_data_t;

// Basic structures
typedef struct ig_vec2 {
  float x, y;
} ig_vec2_t;

typedef struct ig_vec4 {
  float x, y, z, w;
} ig_vec4_t;

// Flags & Enums (exposing them as typedef int for version safety, initialized
// in .cc)
typedef int ig_window_flags_t;
typedef int ig_table_flags_t;
typedef int ig_table_column_flags_t;
typedef int ig_input_text_flags_t;
typedef int ig_style_var_t;
typedef int ig_key_t;
typedef int ig_mouse_cursor_t;
typedef int ig_sort_direction_t;
typedef int ig_draw_list_flags_t;

// Version-safe Constants (C23 / C++ constexpr)
constexpr ig_table_flags_t IG_TABLE_FLAGS_NONE = 0;
constexpr ig_table_flags_t IG_TABLE_FLAGS_RESIZABLE = 1;
constexpr ig_table_flags_t IG_TABLE_FLAGS_SORTABLE = 8;
constexpr ig_table_flags_t IG_TABLE_FLAGS_ROW_BG = 64;
constexpr ig_table_flags_t IG_TABLE_FLAGS_BORDERS = 1920;
constexpr ig_table_flags_t IG_TABLE_FLAGS_SCROLL_Y = 33554432;
constexpr ig_table_flags_t IG_TABLE_FLAGS_SORT_TRISTATE = 134217728;
constexpr ig_table_flags_t IG_TABLE_FLAGS_NO_SAVED_SETTINGS = 16;
constexpr ig_table_flags_t IG_TABLE_FLAGS_SIZING_FIXED_FIT = 8192;

constexpr ig_table_column_flags_t IG_TABLE_COLUMN_FLAGS_NONE = 0;
constexpr ig_table_column_flags_t IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED = 16;
constexpr ig_table_column_flags_t IG_TABLE_COLUMN_FLAGS_WIDTH_STRETCH = 8;

constexpr ig_window_flags_t IG_WINDOW_FLAGS_NONE = 0;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_TITLE_BAR = 1;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_RESIZE = 2;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_MOVE = 4;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_SCROLLBAR = 8;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_COLLAPSE = 32;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_SCROLL_WITH_MOUSE = 16;
constexpr ig_window_flags_t IG_WINDOW_FLAGS_NO_FOCUS_ON_APPEARING = 4096;

constexpr ig_input_text_flags_t IG_INPUT_TEXT_FLAGS_NONE = 0;
constexpr ig_input_text_flags_t IG_INPUT_TEXT_FLAGS_CALLBACK_RESIZE = 4194304;

constexpr ig_style_var_t IG_STYLE_VAR_WINDOW_PADDING = 2;

constexpr ig_key_t IG_KEY_ENTER = 525;
constexpr ig_key_t IG_KEY_SLASH = 600;
constexpr ig_key_t IG_KEY_F = 551;

typedef int ig_cond_t;
constexpr ig_cond_t IG_COND_NONE = 0;
constexpr ig_cond_t IG_COND_APPEARING = 1 << 3;

constexpr ig_mouse_cursor_t IG_MOUSE_CURSOR_RESIZE_EW = 4;

constexpr ig_sort_direction_t IG_SORT_DIRECTION_NONE = 0;
constexpr ig_sort_direction_t IG_SORT_DIRECTION_ASCENDING = 1;

constexpr ig_draw_list_flags_t IG_DRAW_LIST_FLAGS_ANTI_ALIASED_LINES = 1;

typedef int ig_mod_flags_t;
constexpr ig_mod_flags_t IG_MOD_NONE = 0;
constexpr ig_mod_flags_t IG_MOD_CTRL = 1 << 12;
constexpr ig_mod_flags_t IG_MOD_SHIFT = 1 << 13;
constexpr ig_mod_flags_t IG_MOD_ALT = 1 << 14;
constexpr ig_mod_flags_t IG_MOD_SUPER = 1 << 15;

typedef int ig_hovered_flags_t;
constexpr ig_hovered_flags_t IG_HOVERED_FLAGS_NONE = 0;
constexpr ig_hovered_flags_t IG_HOVERED_FLAGS_CHILD_WINDOWS = 1 << 0;

typedef int ig_dock_node_flags_t;
typedef int ig_dir_t;
typedef int ig_col_t;
typedef int ig_popup_flags_t;

constexpr ig_dock_node_flags_t IG_DOCK_NODE_FLAGS_NONE = 0;
constexpr ig_dock_node_flags_t IG_DOCK_NODE_FLAGS_DOCKSPACE = 1 << 10;
constexpr ig_dock_node_flags_t IG_DOCK_NODE_FLAGS_NO_TAB_BAR = 1 << 12;
constexpr ig_dock_node_flags_t IG_DOCK_NODE_FLAGS_NO_DOCKING_OVER_ME = 1 << 20;

constexpr ig_dir_t IG_DIR_RIGHT = 1;

constexpr ig_col_t IG_COL_POPUP_BG = 4;

constexpr ig_popup_flags_t IG_POPUP_FLAGS_NONE = 0;

constexpr ig_style_var_t IG_STYLE_VAR_WINDOW_ROUNDING = 3;
constexpr ig_style_var_t IG_STYLE_VAR_WINDOW_BORDER_SIZE = 4;

typedef int ig_config_flags_t;
constexpr ig_config_flags_t IG_CONFIG_FLAGS_NAV_ENABLE_KEYBOARD = 1 << 0;
constexpr ig_config_flags_t IG_CONFIG_FLAGS_DOCKING_ENABLE = 128;

// Context, IO, Style & Fonts
void ig_create_context(void);
void ig_destroy_context(void);
void ig_io_set_display_size(ig_vec2_t size);
void ig_io_set_delta_time(float dt);
void ig_set_allocator_functions(void* (*alloc_func)(size_t sz, void* user_data),
                                void (*free_func)(void* ptr, void* user_data),
                                void* user_data);
void ig_io_add_config_flags(int flags);
ig_vec2_t ig_get_io_display_size(void);
ig_vec2_t ig_get_io_display_framebuffer_scale(void);
void ig_set_font_data(const void* font_data, int font_size, float dpi_scale);
void ig_new_frame(void);
void ig_render(void);
ig_draw_data_t* ig_get_draw_data(void);

ig_draw_list_t* ig_get_window_draw_list(void);
ig_vec2_t ig_get_cursor_screen_pos(void);
ig_vec2_t ig_get_content_region_avail(void);
void ig_set_cursor_screen_pos(ig_vec2_t pos);
void ig_set_cursor_pos_x(float x);
float ig_get_cursor_pos_x(void);
void ig_set_cursor_pos(ig_vec2_t pos);

float ig_get_frame_height(void);
float ig_get_font_size(void);
float ig_get_text_line_height(void);
ig_font_t* ig_get_font(void);
ig_vec2_t ig_font_calc_text_size_a(const ig_font_t* font, float size,
                                   float max_width, float wrap_width,
                                   const char* text_begin,
                                   const char* text_end);

// IO getters
float ig_get_io_mouse_wheel(void);
float ig_get_io_mouse_wheel_h(void);
ig_vec2_t ig_get_io_mouse_clicked_pos(int button);
ig_vec2_t ig_get_io_mouse_delta(void);
float ig_get_io_mouse_drag_threshold(void);
ig_vec2_t ig_get_io_mouse_pos(void);
bool ig_get_io_key_shift(void);
bool ig_get_io_key_ctrl(void);
bool ig_get_io_want_text_input(void);

// Style getters
ig_vec2_t ig_get_style_window_padding(void);

// Windows, Child Windows & Tooltips
bool ig_begin(const char* name, bool* p_open, ig_window_flags_t flags);
void ig_end(void);
bool ig_begin_child(const char* str_id, ig_vec2_t size, bool border,
                    ig_window_flags_t flags);
void ig_end_child(void);
void ig_begin_tooltip(void);
void ig_end_tooltip(void);
bool ig_is_window_hovered(ig_hovered_flags_t flags);
ig_vec2_t ig_get_window_pos(void);
ig_vec2_t ig_get_window_size(void);
void ig_set_next_window_pos(ig_vec2_t pos, ig_cond_t cond, ig_vec2_t pivot);
void ig_set_next_window_size(ig_vec2_t size, ig_cond_t cond);

void ig_show_metrics_window(bool* p_open);
void ig_show_about_window(bool* p_open);

// Popups
void ig_open_popup(const char* str_id, ig_popup_flags_t flags);
bool ig_begin_popup_modal(const char* name, bool* p_open,
                          ig_window_flags_t flags);
void ig_end_popup(void);
void ig_close_current_popup(void);

// Menu Bar
bool ig_begin_main_menu_bar(void);
void ig_end_main_menu_bar(void);
bool ig_begin_menu(const char* label, bool enabled);
void ig_end_menu(void);
bool ig_menu_item(const char* label, const char* shortcut, bool selected,
                  bool enabled);
bool ig_menu_item_ptr(const char* label, const char* shortcut, bool* p_selected,
                      bool enabled);

// Docking & Viewport
uint32_t ig_dock_space_over_viewport(uint32_t unused_id,
                                     const ig_viewport_t* viewport,
                                     ig_dock_node_flags_t flags);
const ig_viewport_t* ig_get_main_viewport(void);
ig_vec2_t ig_viewport_get_size(const ig_viewport_t* viewport);
ig_vec2_t ig_viewport_get_center(const ig_viewport_t* viewport);
void ig_dock_builder_remove_node(uint32_t node_id);
void ig_dock_builder_add_node(uint32_t node_id, ig_dock_node_flags_t flags);
void ig_dock_builder_set_node_size(uint32_t node_id, ig_vec2_t size);
uint32_t ig_dock_builder_split_node(uint32_t node_id, ig_dir_t split_dir,
                                    float size_ratio_for_child_at_op_dir,
                                    uint32_t* out_id_at_dir,
                                    uint32_t* out_id_at_op_dir);
void ig_dock_builder_dock_window(const char* window_name, uint32_t node_id);
void ig_dock_builder_finish(uint32_t node_id);
ig_dock_node_t* ig_dock_builder_get_node(uint32_t node_id);
void ig_dock_node_add_local_flags(ig_dock_node_t* node,
                                  ig_dock_node_flags_t flags);

// Groups
void ig_begin_group(void);
void ig_end_group(void);

// Style push/pop
void ig_push_style_color(ig_col_t idx, ig_vec4_t col);
void ig_pop_style_color(int count);
void ig_push_style_var(ig_style_var_t idx, ig_vec2_t val);
void ig_push_style_var_float(ig_style_var_t idx, float val);
void ig_pop_style_var(int count);

void ig_style_colors_dark(void);
void ig_style_colors_light(void);

// Layout & Spacing
void ig_same_line(float offset_from_start_x, float spacing);
void ig_spacing(void);
void ig_dummy(ig_vec2_t size);
void ig_separator(void);

// Tables
bool ig_begin_table(const char* str_id, int column, ig_table_flags_t flags,
                    ig_vec2_t outer_size, float inner_width);
void ig_end_table(void);
void ig_table_setup_column(const char* label, ig_table_column_flags_t flags,
                           float init_width_or_weight, uint32_t user_id);
void ig_table_setup_scroll_freeze(int cols, int rows);
void ig_table_headers_row(void);
void ig_table_next_row(void);
bool ig_table_next_column(void);

// Table Sort Specs
ig_table_sort_specs_t* ig_table_get_sort_specs(void);
bool ig_table_sort_specs_get_dirty(const ig_table_sort_specs_t* specs);
void ig_table_sort_specs_clear_dirty(ig_table_sort_specs_t* specs);
int ig_table_sort_specs_get_count(const ig_table_sort_specs_t* specs);
int ig_table_sort_specs_get_column_index(const ig_table_sort_specs_t* specs,
                                         int index);
ig_sort_direction_t ig_table_sort_specs_get_sort_direction(
    const ig_table_sort_specs_t* specs, int index);

// List Clipper
ig_list_clipper_t* ig_list_clipper_create(void);
void ig_list_clipper_destroy(ig_list_clipper_t* clipper);
void ig_list_clipper_begin(ig_list_clipper_t* clipper, int items_count,
                           float items_height);
bool ig_list_clipper_step(ig_list_clipper_t* clipper);
int ig_list_clipper_get_display_start(const ig_list_clipper_t* clipper);
int ig_list_clipper_get_display_end(const ig_list_clipper_t* clipper);

// Widgets
bool ig_button(const char* label, ig_vec2_t size);
bool ig_small_button(const char* label);
bool ig_invisible_button(const char* str_id, ig_vec2_t size);
bool ig_checkbox(const char* label, bool* v);
typedef int ig_selectable_flags_t;
constexpr ig_selectable_flags_t IG_SELECTABLE_FLAGS_NONE = 0;
constexpr ig_selectable_flags_t IG_SELECTABLE_FLAGS_DONT_CLOSE_POPUPS = 1 << 0;
constexpr ig_selectable_flags_t IG_SELECTABLE_FLAGS_SPAN_ALL_COLUMNS = 1 << 1;
constexpr ig_selectable_flags_t IG_SELECTABLE_FLAGS_ALLOW_DOUBLE_CLICKS = 1
                                                                          << 2;
constexpr ig_selectable_flags_t IG_SELECTABLE_FLAGS_DISABLED = 1 << 3;
constexpr ig_selectable_flags_t IG_SELECTABLE_FLAGS_ALLOW_OVERLAP = 1 << 4;

bool ig_selectable(const char* label, bool selected,
                   ig_selectable_flags_t flags, ig_vec2_t size);
void ig_progress_bar(float fraction, ig_vec2_t size_arg, const char* overlay);

// InputText with Callback support
typedef int (*ig_input_text_callback_t)(ig_input_text_callback_data_t* data);
bool ig_input_text(const char* label, char* buf, size_t buf_size,
                   ig_input_text_flags_t flags,
                   ig_input_text_callback_t callback, void* user_data);

void* ig_input_text_callback_data_get_user_data(
    ig_input_text_callback_data_t* data);
int ig_input_text_callback_data_get_event_flag(
    const ig_input_text_callback_data_t* data);
int ig_input_text_callback_data_get_buf_size(
    const ig_input_text_callback_data_t* data);
void ig_input_text_callback_data_set_buf(ig_input_text_callback_data_t* data,
                                         char* buf);

// Item state queries
bool ig_is_item_hovered(void);
bool ig_is_item_active(void);
bool ig_is_item_focused(void);
bool ig_is_item_activated(void);
bool ig_is_item_deactivated(void);

// Keyboard & Mouse queries
bool ig_is_key_down(ig_key_t key);
bool ig_is_key_pressed(ig_key_t key, bool repeat);
bool ig_is_mouse_down(int button);
bool ig_is_mouse_clicked(int button, bool repeat);
bool ig_is_mouse_double_clicked(int button);
bool ig_is_mouse_released(int button);
bool ig_is_mouse_dragging(int button, float lock_threshold);
ig_vec2_t ig_get_mouse_drag_delta(int button, float lock_threshold);
bool ig_is_mouse_hovering_rect(ig_vec2_t r_min, ig_vec2_t r_max, bool clip);

// Scroll & Clipboard
float ig_get_scroll_y(void);
void ig_set_scroll_y(float scroll_y);
void ig_set_clipboard_text(const char* text);
void ig_set_keyboard_focus_here(int offset);
void ig_set_mouse_cursor(ig_mouse_cursor_t cursor);

// Draw List Commands
void ig_draw_list_add_rect_filled(ig_draw_list_t* draw_list, ig_vec2_t p_min,
                                  ig_vec2_t p_max, uint32_t col);
void ig_draw_list_add_rect(ig_draw_list_t* draw_list, ig_vec2_t p_min,
                           ig_vec2_t p_max, uint32_t col, float rounding,
                           int flags, float thickness);
void ig_draw_list_add_line(ig_draw_list_t* draw_list, ig_vec2_t p1,
                           ig_vec2_t p2, uint32_t col, float thickness);
void ig_draw_list_add_text_simple(ig_draw_list_t* draw_list, ig_vec2_t pos,
                                  uint32_t col, const char* text_begin,
                                  const char* text_end);
void ig_draw_list_add_text(ig_draw_list_t* draw_list, const ig_font_t* font,
                           float font_size, ig_vec2_t pos, uint32_t col,
                           const char* text_begin, const char* text_end,
                           float wrap_width,
                           const ig_vec4_t* cpu_fine_clip_rect);

ig_draw_list_flags_t ig_draw_list_get_flags(const ig_draw_list_t* draw_list);
void ig_draw_list_set_flags(ig_draw_list_t* draw_list,
                            ig_draw_list_flags_t flags);

// Standard Text
ig_vec2_t ig_calc_text_size(const char* text);
void ig_text(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ig_text_colored(ig_vec4_t col, const char* fmt, ...)
    __attribute__((format(printf, 2, 3)));
void ig_text_disabled(const char* fmt, ...)
    __attribute__((format(printf, 1, 2)));
void ig_text_unformatted(const char* text, const char* text_end);
void ig_text_wrapped(const char* fmt, ...)
    __attribute__((format(printf, 1, 2)));

// Color helpers
uint32_t ig_color_convert_float4_to_u32(ig_vec4_t in);
ig_vec4_t ig_color_convert_u32_to_float4(uint32_t in);

#define IG_COL32(R, G, B, A)                                              \
  (((uint32_t)(A) << 24) | ((uint32_t)(B) << 16) | ((uint32_t)(G) << 8) | \
   ((uint32_t)(R) << 0))

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_IMGUI_C_H_
