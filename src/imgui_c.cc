#include "src/imgui_c.h"

#include <stdarg.h>

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

static_assert(IG_TABLE_FLAGS_NONE == ImGuiTableFlags_None,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_RESIZABLE == ImGuiTableFlags_Resizable,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_SORTABLE == ImGuiTableFlags_Sortable,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_ROW_BG == ImGuiTableFlags_RowBg,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_BORDERS == ImGuiTableFlags_Borders,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_SCROLL_Y == ImGuiTableFlags_ScrollY,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_SORT_TRISTATE == ImGuiTableFlags_SortTristate,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_NO_SAVED_SETTINGS ==
                  ImGuiTableFlags_NoSavedSettings,
              "ImGuiTableFlags mismatch");
static_assert(IG_TABLE_FLAGS_SIZING_FIXED_FIT == ImGuiTableFlags_SizingFixedFit,
              "ImGuiTableFlags mismatch");

static_assert(IG_TABLE_COLUMN_FLAGS_NONE == ImGuiTableColumnFlags_None,
              "ImGuiTableColumnFlags mismatch");
static_assert(IG_TABLE_COLUMN_FLAGS_WIDTH_FIXED ==
                  ImGuiTableColumnFlags_WidthFixed,
              "ImGuiTableColumnFlags mismatch");
static_assert(IG_TABLE_COLUMN_FLAGS_WIDTH_STRETCH ==
                  ImGuiTableColumnFlags_WidthStretch,
              "ImGuiTableColumnFlags mismatch");

static_assert(IG_WINDOW_FLAGS_NONE == ImGuiWindowFlags_None,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_TITLE_BAR == ImGuiWindowFlags_NoTitleBar,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_RESIZE == ImGuiWindowFlags_NoResize,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_MOVE == ImGuiWindowFlags_NoMove,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_SCROLLBAR == ImGuiWindowFlags_NoScrollbar,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_COLLAPSE == ImGuiWindowFlags_NoCollapse,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_SCROLL_WITH_MOUSE ==
                  ImGuiWindowFlags_NoScrollWithMouse,
              "ImGuiWindowFlags mismatch");
static_assert(IG_WINDOW_FLAGS_NO_FOCUS_ON_APPEARING ==
                  ImGuiWindowFlags_NoFocusOnAppearing,
              "ImGuiWindowFlags mismatch");

static_assert(IG_INPUT_TEXT_FLAGS_NONE == ImGuiInputTextFlags_None,
              "ImGuiInputTextFlags mismatch");
static_assert(IG_INPUT_TEXT_FLAGS_CALLBACK_RESIZE ==
                  ImGuiInputTextFlags_CallbackResize,
              "ImGuiInputTextFlags mismatch");

static_assert(IG_STYLE_VAR_WINDOW_PADDING == ImGuiStyleVar_WindowPadding,
              "ImGuiStyleVar mismatch");

static_assert(IG_KEY_ENTER == ImGuiKey_Enter, "ImGuiKey mismatch");
static_assert(IG_KEY_SLASH == ImGuiKey_Slash, "ImGuiKey mismatch");
static_assert(IG_KEY_F == ImGuiKey_F, "ImGuiKey mismatch");
static_assert(IG_COND_APPEARING == ImGuiCond_Appearing, "ImGuiCond mismatch");

static_assert(IG_MOUSE_CURSOR_RESIZE_EW == ImGuiMouseCursor_ResizeEW,
              "ImGuiMouseCursor mismatch");

static_assert(IG_SORT_DIRECTION_NONE == ImGuiSortDirection_None,
              "ImGuiSortDirection mismatch");
static_assert(IG_SORT_DIRECTION_ASCENDING == ImGuiSortDirection_Ascending,
              "ImGuiSortDirection mismatch");

static_assert(IG_DRAW_LIST_FLAGS_ANTI_ALIASED_LINES ==
                  ImDrawListFlags_AntiAliasedLines,
              "ImDrawListFlags mismatch");

static_assert(IG_DOCK_NODE_FLAGS_NONE == ImGuiDockNodeFlags_None, "ImGuiDockNodeFlags mismatch");
static_assert(IG_DOCK_NODE_FLAGS_DOCKSPACE == ImGuiDockNodeFlags_DockSpace, "ImGuiDockNodeFlags mismatch");
static_assert(IG_DOCK_NODE_FLAGS_NO_TAB_BAR == ImGuiDockNodeFlags_NoTabBar, "ImGuiDockNodeFlags mismatch");
static_assert(IG_DOCK_NODE_FLAGS_NO_DOCKING_OVER_ME == ImGuiDockNodeFlags_NoDockingOverMe, "ImGuiDockNodeFlags mismatch");

static_assert(IG_DIR_RIGHT == ImGuiDir_Right, "ImGuiDir mismatch");

static_assert(IG_COL_POPUP_BG == ImGuiCol_PopupBg, "ImGuiCol mismatch");

static_assert(IG_POPUP_FLAGS_NONE == ImGuiPopupFlags_None, "ImGuiPopupFlags mismatch");

static_assert(IG_STYLE_VAR_WINDOW_ROUNDING == ImGuiStyleVar_WindowRounding, "ImGuiStyleVar mismatch");
static_assert(IG_STYLE_VAR_WINDOW_BORDER_SIZE == ImGuiStyleVar_WindowBorderSize, "ImGuiStyleVar mismatch");

static_assert(IG_CONFIG_FLAGS_NAV_ENABLE_KEYBOARD == ImGuiConfigFlags_NavEnableKeyboard, "ImGuiConfigFlags mismatch");
static_assert(IG_CONFIG_FLAGS_DOCKING_ENABLE == ImGuiConfigFlags_DockingEnable, "ImGuiConfigFlags mismatch");

extern "C" {

// Context, IO, Style & Fonts
void ig_create_context(void) {
  ImGui::CreateContext();
}

void ig_set_allocator_functions(void* (*alloc_func)(size_t sz, void* user_data),
                                void (*free_func)(void* ptr, void* user_data),
                                void* user_data) {
  ImGui::SetAllocatorFunctions(alloc_func, free_func, user_data);
}

void ig_io_add_config_flags(int flags) {
  ImGui::GetIO().ConfigFlags |= flags;
}

ig_vec2_t ig_get_io_display_size(void) {
  ImVec2 size = ImGui::GetIO().DisplaySize;
  return {size.x, size.y};
}

ig_vec2_t ig_get_io_display_framebuffer_scale(void) {
  ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
  return {scale.x, scale.y};
}

void ig_set_font_data(const void* font_data, int font_size, float dpi_scale) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  ImFontConfig font_cfg;
  font_cfg.FontDataOwnedByAtlas = false;
  io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(font_data), font_size,
                                 16.0f * dpi_scale, &font_cfg);
  io.Fonts->Build();
  io.FontGlobalScale = 1.0f / dpi_scale;
}

void ig_new_frame(void) {
  ImGui::NewFrame();
}

void ig_render(void) {
  ImGui::Render();
}

ig_draw_data_t* ig_get_draw_data(void) {
  return reinterpret_cast<ig_draw_data_t*>(ImGui::GetDrawData());
}

ig_draw_list_t* ig_get_window_draw_list(void) {
  return reinterpret_cast<ig_draw_list_t*>(ImGui::GetWindowDrawList());
}

ig_vec2_t ig_get_cursor_screen_pos(void) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  return {pos.x, pos.y};
}

ig_vec2_t ig_get_content_region_avail(void) {
  ImVec2 size = ImGui::GetContentRegionAvail();
  return {size.x, size.y};
}

void ig_set_cursor_screen_pos(ig_vec2_t pos) {
  ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));
}

void ig_set_cursor_pos_x(float x) { ImGui::SetCursorPosX(x); }

float ig_get_cursor_pos_x(void) { return ImGui::GetCursorPosX(); }

void ig_set_cursor_pos(ig_vec2_t pos) {
  ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
}

float ig_get_frame_height(void) { return ImGui::GetFrameHeight(); }

float ig_get_font_size(void) { return ImGui::GetFontSize(); }

float ig_get_text_line_height(void) { return ImGui::GetTextLineHeight(); }

ig_font_t* ig_get_font(void) {
  return reinterpret_cast<ig_font_t*>(ImGui::GetFont());
}

ig_vec2_t ig_font_calc_text_size_a(const ig_font_t* font, float size,
                                   float max_width, float wrap_width,
                                   const char* text_begin,
                                   const char* text_end) {
  ImVec2 val =
      const_cast<ImFont*>(reinterpret_cast<const ImFont*>(font))
          ->CalcTextSizeA(size, max_width, wrap_width, text_begin, text_end);
  return {val.x, val.y};
}

// IO getters
float ig_get_io_mouse_wheel(void) { return ImGui::GetIO().MouseWheel; }

float ig_get_io_mouse_wheel_h(void) { return ImGui::GetIO().MouseWheelH; }

ig_vec2_t ig_get_io_mouse_clicked_pos(int button) {
  ImVec2 pos = ImGui::GetIO().MouseClickedPos[button];
  return {pos.x, pos.y};
}

ig_vec2_t ig_get_io_mouse_delta(void) {
  ImVec2 delta = ImGui::GetIO().MouseDelta;
  return {delta.x, delta.y};
}

float ig_get_io_mouse_drag_threshold(void) {
  return ImGui::GetIO().MouseDragThreshold;
}

ig_vec2_t ig_get_io_mouse_pos(void) {
  ImVec2 pos = ImGui::GetIO().MousePos;
  return {pos.x, pos.y};
}

bool ig_get_io_key_shift(void) {
  return ImGui::GetIO().KeyShift;
}

bool ig_get_io_key_ctrl(void) {
  return ImGui::GetIO().KeyCtrl;
}

bool ig_get_io_want_text_input(void) {
  return ImGui::GetIO().WantTextInput;
}

// Style getters
ig_vec2_t ig_get_style_window_padding(void) {
  ImVec2 padding = ImGui::GetStyle().WindowPadding;
  return {padding.x, padding.y};
}

// Windows, Child Windows & Tooltips
bool ig_begin(const char* name, bool* p_open, ig_window_flags_t flags) {
  return ImGui::Begin(name, p_open, flags);
}

void ig_end(void) { ImGui::End(); }

bool ig_begin_child(const char* str_id, ig_vec2_t size, bool border,
                    ig_window_flags_t flags) {
  return ImGui::BeginChild(str_id, ImVec2(size.x, size.y), border, flags);
}

void ig_end_child(void) { ImGui::EndChild(); }

void ig_begin_tooltip(void) { ImGui::BeginTooltip(); }

void ig_end_tooltip(void) { ImGui::EndTooltip(); }

bool ig_is_window_hovered(ig_hovered_flags_t flags) {
  return ImGui::IsWindowHovered(flags);
}

ig_vec2_t ig_get_window_pos(void) {
  ImVec2 pos = ImGui::GetWindowPos();
  return {pos.x, pos.y};
}

ig_vec2_t ig_get_window_size(void) {
  ImVec2 size = ImGui::GetWindowSize();
  return {size.x, size.y};
}

void ig_set_next_window_pos(ig_vec2_t pos, ig_cond_t cond, ig_vec2_t pivot) {
  ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), cond, ImVec2(pivot.x, pivot.y));
}

void ig_set_next_window_size(ig_vec2_t size, ig_cond_t cond) {
  ImGui::SetNextWindowSize(ImVec2(size.x, size.y), cond);
}

// Style push/pop
void ig_push_style_var(ig_style_var_t idx, ig_vec2_t val) {
  ImGui::PushStyleVar(idx, ImVec2(val.x, val.y));
}

void ig_pop_style_var(int count) { ImGui::PopStyleVar(count); }

// Layout & Spacing
void ig_same_line(float offset_from_start_x, float spacing) {
  ImGui::SameLine(offset_from_start_x, spacing);
}

void ig_spacing(void) { ImGui::Spacing(); }

void ig_dummy(ig_vec2_t size) { ImGui::Dummy(ImVec2(size.x, size.y)); }

void ig_separator(void) { ImGui::Separator(); }

// Tables
bool ig_begin_table(const char* str_id, int column, ig_table_flags_t flags,
                    ig_vec2_t outer_size, float inner_width) {
  return ImGui::BeginTable(str_id, column, flags,
                           ImVec2(outer_size.x, outer_size.y), inner_width);
}

void ig_end_table(void) { ImGui::EndTable(); }

void ig_table_setup_column(const char* label, ig_table_column_flags_t flags,
                           float init_width_or_weight, uint32_t user_id) {
  ImGui::TableSetupColumn(label, flags, init_width_or_weight, user_id);
}

void ig_table_setup_scroll_freeze(int cols, int rows) {
  ImGui::TableSetupScrollFreeze(cols, rows);
}

void ig_table_headers_row(void) { ImGui::TableHeadersRow(); }

void ig_table_next_row(void) { ImGui::TableNextRow(); }

bool ig_table_next_column(void) { return ImGui::TableNextColumn(); }

// Table Sort Specs
ig_table_sort_specs_t* ig_table_get_sort_specs(void) {
  return reinterpret_cast<ig_table_sort_specs_t*>(ImGui::TableGetSortSpecs());
}

bool ig_table_sort_specs_get_dirty(const ig_table_sort_specs_t* specs) {
  return reinterpret_cast<const ImGuiTableSortSpecs*>(specs)->SpecsDirty;
}

void ig_table_sort_specs_clear_dirty(ig_table_sort_specs_t* specs) {
  reinterpret_cast<ImGuiTableSortSpecs*>(specs)->SpecsDirty = false;
}

int ig_table_sort_specs_get_count(const ig_table_sort_specs_t* specs) {
  return reinterpret_cast<const ImGuiTableSortSpecs*>(specs)->SpecsCount;
}

int ig_table_sort_specs_get_column_index(const ig_table_sort_specs_t* specs,
                                         int index) {
  return reinterpret_cast<const ImGuiTableSortSpecs*>(specs)
      ->Specs[index]
      .ColumnIndex;
}

ig_sort_direction_t ig_table_sort_specs_get_sort_direction(
    const ig_table_sort_specs_t* specs, int index) {
  return static_cast<ig_sort_direction_t>(
      reinterpret_cast<const ImGuiTableSortSpecs*>(specs)
          ->Specs[index]
          .SortDirection);
}

// List Clipper
ig_list_clipper_t* ig_list_clipper_create(void) {
  return reinterpret_cast<ig_list_clipper_t*>(new ImGuiListClipper());
}

void ig_list_clipper_destroy(ig_list_clipper_t* clipper) {
  delete reinterpret_cast<ImGuiListClipper*>(clipper);
}

void ig_list_clipper_begin(ig_list_clipper_t* clipper, int items_count,
                           float items_height) {
  reinterpret_cast<ImGuiListClipper*>(clipper)->Begin(items_count,
                                                      items_height);
}

bool ig_list_clipper_step(ig_list_clipper_t* clipper) {
  return reinterpret_cast<ImGuiListClipper*>(clipper)->Step();
}

int ig_list_clipper_get_display_start(const ig_list_clipper_t* clipper) {
  return reinterpret_cast<const ImGuiListClipper*>(clipper)->DisplayStart;
}

int ig_list_clipper_get_display_end(const ig_list_clipper_t* clipper) {
  return reinterpret_cast<const ImGuiListClipper*>(clipper)->DisplayEnd;
}

// Widgets
bool ig_button(const char* label, ig_vec2_t size) {
  return ImGui::Button(label, ImVec2(size.x, size.y));
}

bool ig_small_button(const char* label) { return ImGui::SmallButton(label); }

bool ig_invisible_button(const char* str_id, ig_vec2_t size) {
  return ImGui::InvisibleButton(str_id, ImVec2(size.x, size.y));
}

bool ig_checkbox(const char* label, bool* v) {
  return ImGui::Checkbox(label, v);
}

bool ig_selectable(const char* label, bool selected,
                   ig_selectable_flags_t flags, ig_vec2_t size) {
  return ImGui::Selectable(label, selected, flags, ImVec2(size.x, size.y));
}

void ig_progress_bar(float fraction, ig_vec2_t size_arg, const char* overlay) {
  ImGui::ProgressBar(fraction, ImVec2(size_arg.x, size_arg.y), overlay);
}

// InputText Trampoline Callback Wrapper
struct InputTextWrapperUserData {
  ig_input_text_callback_t c_callback;
  void* c_user_data;
};

static int ImGuiInputTextCallbackWrapper(ImGuiInputTextCallbackData* data) {
  InputTextWrapperUserData* wrapper_ud =
      reinterpret_cast<InputTextWrapperUserData*>(data->UserData);

  // Back up original UserData
  void* original_user_data = data->UserData;

  // Expose the real C user_data to the C callback
  data->UserData = wrapper_ud->c_user_data;

  // Invoke the C callback
  int result = wrapper_ud->c_callback(
      reinterpret_cast<ig_input_text_callback_data_t*>(data));

  // Restore original UserData
  data->UserData = original_user_data;

  return result;
}

bool ig_input_text(const char* label, char* buf, size_t buf_size,
                   ig_input_text_flags_t flags,
                   ig_input_text_callback_t callback, void* user_data) {
  if (callback != nullptr) {
    InputTextWrapperUserData wrapper_ud = {callback, user_data};
    return ImGui::InputText(label, buf, buf_size, flags,
                            ImGuiInputTextCallbackWrapper, &wrapper_ud);
  } else {
    return ImGui::InputText(label, buf, buf_size, flags, nullptr, user_data);
  }
}

void* ig_input_text_callback_data_get_user_data(
    ig_input_text_callback_data_t* data) {
  return reinterpret_cast<ImGuiInputTextCallbackData*>(data)->UserData;
}

int ig_input_text_callback_data_get_event_flag(
    const ig_input_text_callback_data_t* data) {
  return reinterpret_cast<const ImGuiInputTextCallbackData*>(data)->EventFlag;
}

int ig_input_text_callback_data_get_buf_size(
    const ig_input_text_callback_data_t* data) {
  return reinterpret_cast<const ImGuiInputTextCallbackData*>(data)->BufSize;
}

void ig_input_text_callback_data_set_buf(ig_input_text_callback_data_t* data,
                                         char* buf) {
  reinterpret_cast<ImGuiInputTextCallbackData*>(data)->Buf = buf;
}

// Item state queries
bool ig_is_item_hovered(void) { return ImGui::IsItemHovered(); }

bool ig_is_item_active(void) { return ImGui::IsItemActive(); }

bool ig_is_item_focused(void) { return ImGui::IsItemFocused(); }

bool ig_is_item_activated(void) { return ImGui::IsItemActivated(); }

bool ig_is_item_deactivated(void) { return ImGui::IsItemDeactivated(); }

// Keyboard & Mouse queries
bool ig_is_key_down(ig_key_t key) {
  return ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
}

bool ig_is_key_pressed(ig_key_t key, bool repeat) {
  return ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), repeat);
}

bool ig_is_mouse_down(int button) { return ImGui::IsMouseDown(button); }

bool ig_is_mouse_clicked(int button, bool repeat) {
  return ImGui::IsMouseClicked(button, repeat);
}

bool ig_is_mouse_double_clicked(int button) {
  return ImGui::IsMouseDoubleClicked(button);
}

bool ig_is_mouse_released(int button) { return ImGui::IsMouseReleased(button); }

bool ig_is_mouse_dragging(int button, float lock_threshold) {
  return ImGui::IsMouseDragging(button, lock_threshold);
}

ig_vec2_t ig_get_mouse_drag_delta(int button, float lock_threshold) {
  ImVec2 delta = ImGui::GetMouseDragDelta(button, lock_threshold);
  return {delta.x, delta.y};
}

bool ig_is_mouse_hovering_rect(ig_vec2_t r_min, ig_vec2_t r_max, bool clip) {
  return ImGui::IsMouseHoveringRect(ImVec2(r_min.x, r_min.y),
                                    ImVec2(r_max.x, r_max.y), clip);
}

// Scroll & Clipboard
float ig_get_scroll_y(void) { return ImGui::GetScrollY(); }

void ig_set_scroll_y(float scroll_y) { ImGui::SetScrollY(scroll_y); }

void ig_set_clipboard_text(const char* text) { ImGui::SetClipboardText(text); }

void ig_set_keyboard_focus_here(int offset) {
  ImGui::SetKeyboardFocusHere(offset);
}

void ig_set_mouse_cursor(ig_mouse_cursor_t cursor) {
  ImGui::SetMouseCursor(static_cast<ImGuiMouseCursor>(cursor));
}

// Draw List Commands
void ig_draw_list_add_rect_filled(ig_draw_list_t* draw_list, ig_vec2_t p_min,
                                  ig_vec2_t p_max, uint32_t col) {
  reinterpret_cast<ImDrawList*>(draw_list)->AddRectFilled(
      ImVec2(p_min.x, p_min.y), ImVec2(p_max.x, p_max.y), col);
}

void ig_draw_list_add_rect(ig_draw_list_t* draw_list, ig_vec2_t p_min,
                           ig_vec2_t p_max, uint32_t col, float rounding,
                           int flags, float thickness) {
  reinterpret_cast<ImDrawList*>(draw_list)->AddRect(
      ImVec2(p_min.x, p_min.y), ImVec2(p_max.x, p_max.y), col, rounding, flags,
      thickness);
}

void ig_draw_list_add_line(ig_draw_list_t* draw_list, ig_vec2_t p1,
                           ig_vec2_t p2, uint32_t col, float thickness) {
  reinterpret_cast<ImDrawList*>(draw_list)->AddLine(
      ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col, thickness);
}

void ig_draw_list_add_text_simple(ig_draw_list_t* draw_list, ig_vec2_t pos,
                                  uint32_t col, const char* text_begin,
                                  const char* text_end) {
  reinterpret_cast<ImDrawList*>(draw_list)->AddText(ImVec2(pos.x, pos.y), col,
                                                    text_begin, text_end);
}

void ig_draw_list_add_text(ig_draw_list_t* draw_list, const ig_font_t* font,
                           float font_size, ig_vec2_t pos, uint32_t col,
                           const char* text_begin, const char* text_end,
                           float wrap_width,
                           const ig_vec4_t* cpu_fine_clip_rect) {
  ImVec4 fine_clip;
  const ImVec4* clip_ptr = nullptr;
  if (cpu_fine_clip_rect != nullptr) {
    fine_clip = ImVec4(cpu_fine_clip_rect->x, cpu_fine_clip_rect->y,
                       cpu_fine_clip_rect->z, cpu_fine_clip_rect->w);
    clip_ptr = &fine_clip;
  }
  reinterpret_cast<ImDrawList*>(draw_list)->AddText(
      const_cast<ImFont*>(reinterpret_cast<const ImFont*>(font)), font_size,
      ImVec2(pos.x, pos.y), col, text_begin, text_end, wrap_width, clip_ptr);
}

ig_draw_list_flags_t ig_draw_list_get_flags(const ig_draw_list_t* draw_list) {
  return reinterpret_cast<const ImDrawList*>(draw_list)->Flags;
}

void ig_draw_list_set_flags(ig_draw_list_t* draw_list,
                            ig_draw_list_flags_t flags) {
  reinterpret_cast<ImDrawList*>(draw_list)->Flags = flags;
}

// Standard Text
ig_vec2_t ig_calc_text_size(const char* text) {
  ImVec2 size = ImGui::CalcTextSize(text);
  return {size.x, size.y};
}

void ig_text(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextV(fmt, args);
  va_end(args);
}

void ig_text_colored(ig_vec4_t col, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextColoredV(ImVec4(col.x, col.y, col.z, col.w), fmt, args);
  va_end(args);
}

void ig_text_disabled(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextDisabledV(fmt, args);
  va_end(args);
}

void ig_text_unformatted(const char* text, const char* text_end) {
  ImGui::TextUnformatted(text, text_end);
}

void ig_text_wrapped(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::TextWrappedV(fmt, args);
  va_end(args);
}

// Color helpers
uint32_t ig_color_convert_float4_to_u32(ig_vec4_t in) {
  return ImGui::ColorConvertFloat4ToU32(ImVec4(in.x, in.y, in.z, in.w));
}

ig_vec4_t ig_color_convert_u32_to_float4(uint32_t in) {
  ImVec4 val = ImGui::ColorConvertU32ToFloat4(in);
  return {val.x, val.y, val.z, val.w};
}

void ig_show_metrics_window(bool* p_open) {
  ImGui::ShowMetricsWindow(p_open);
}

void ig_show_about_window(bool* p_open) {
  ImGui::ShowAboutWindow(p_open);
}

// Popups
void ig_open_popup(const char* str_id, ig_popup_flags_t flags) {
  ImGui::OpenPopup(str_id, flags);
}

bool ig_begin_popup_modal(const char* name, bool* p_open, ig_window_flags_t flags) {
  return ImGui::BeginPopupModal(name, p_open, flags);
}

void ig_end_popup(void) {
  ImGui::EndPopup();
}

void ig_close_current_popup(void) {
  ImGui::CloseCurrentPopup();
}

// Menu Bar
bool ig_begin_main_menu_bar(void) {
  return ImGui::BeginMainMenuBar();
}

void ig_end_main_menu_bar(void) {
  ImGui::EndMainMenuBar();
}

bool ig_begin_menu(const char* label, bool enabled) {
  return ImGui::BeginMenu(label, enabled);
}

void ig_end_menu(void) {
  ImGui::EndMenu();
}

bool ig_menu_item(const char* label, const char* shortcut, bool selected, bool enabled) {
  return ImGui::MenuItem(label, shortcut, selected, enabled);
}

bool ig_menu_item_ptr(const char* label, const char* shortcut, bool* p_selected, bool enabled) {
  return ImGui::MenuItem(label, shortcut, p_selected, enabled);
}

// Docking & Viewport
uint32_t ig_dock_space_over_viewport(uint32_t unused_id, const ig_viewport_t* viewport, ig_dock_node_flags_t flags) {
  return ImGui::DockSpaceOverViewport(unused_id, reinterpret_cast<const ImGuiViewport*>(viewport), flags);
}

const ig_viewport_t* ig_get_main_viewport(void) {
  return reinterpret_cast<const ig_viewport_t*>(ImGui::GetMainViewport());
}

ig_vec2_t ig_viewport_get_size(const ig_viewport_t* viewport) {
  if (!viewport) return {0.0f, 0.0f};
  ImVec2 size = reinterpret_cast<const ImGuiViewport*>(viewport)->Size;
  return {size.x, size.y};
}

ig_vec2_t ig_viewport_get_center(const ig_viewport_t* viewport) {
  if (!viewport) return {0.0f, 0.0f};
  ImVec2 center = reinterpret_cast<const ImGuiViewport*>(viewport)->GetCenter();
  return {center.x, center.y};
}

void ig_dock_builder_remove_node(uint32_t node_id) {
  ImGui::DockBuilderRemoveNode(node_id);
}

void ig_dock_builder_add_node(uint32_t node_id, ig_dock_node_flags_t flags) {
  ImGui::DockBuilderAddNode(node_id, flags);
}

void ig_dock_builder_set_node_size(uint32_t node_id, ig_vec2_t size) {
  ImGui::DockBuilderSetNodeSize(node_id, ImVec2(size.x, size.y));
}

uint32_t ig_dock_builder_split_node(uint32_t node_id, ig_dir_t split_dir, float size_ratio_for_child_at_op_dir, uint32_t* out_id_at_dir, uint32_t* out_id_at_op_dir) {
  return ImGui::DockBuilderSplitNode(node_id, static_cast<ImGuiDir>(split_dir), size_ratio_for_child_at_op_dir, out_id_at_dir, out_id_at_op_dir);
}

void ig_dock_builder_dock_window(const char* window_name, uint32_t node_id) {
  ImGui::DockBuilderDockWindow(window_name, node_id);
}

void ig_dock_builder_finish(uint32_t node_id) {
  ImGui::DockBuilderFinish(node_id);
}

ig_dock_node_t* ig_dock_builder_get_node(uint32_t node_id) {
  return reinterpret_cast<ig_dock_node_t*>(ImGui::DockBuilderGetNode(node_id));
}

void ig_dock_node_add_local_flags(ig_dock_node_t* node, ig_dock_node_flags_t flags) {
  if (node) {
    reinterpret_cast<ImGuiDockNode*>(node)->LocalFlags |= flags;
  }
}

// Groups
void ig_begin_group(void) {
  ImGui::BeginGroup();
}

void ig_end_group(void) {
  ImGui::EndGroup();
}

// Style push/pop
void ig_push_style_color(ig_col_t idx, ig_vec4_t col) {
  ImGui::PushStyleColor(idx, ImVec4(col.x, col.y, col.z, col.w));
}

void ig_pop_style_color(int count) {
  ImGui::PopStyleColor(count);
}

void ig_push_style_var_float(ig_style_var_t idx, float val) {
  ImGui::PushStyleVar(idx, val);
}

void ig_style_colors_dark(void) {
  ImGui::StyleColorsDark();
}

void ig_style_colors_light(void) {
  ImGui::StyleColorsLight();
}

}  // extern "C"
