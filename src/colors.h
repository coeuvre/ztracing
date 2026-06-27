#ifndef SRC_COLORS_H
#define SRC_COLORS_H

#include "src/imgui_types.h"

struct Theme {
  // Main Viewport Backgrounds
  ImU32 viewport_bg;
  ImU32 track_bg;

  // Time Ruler
  ImU32 ruler_bg;
  ImU32 ruler_border;
  ImU32 ruler_tick;
  ImU32 ruler_text;

  // Events
  ImU32 event_border;
  ImU32 event_border_selected;
  ImU32 event_border_focused;
  ImU32 event_focused_bg;

  // Tracks
  ImU32 track_text;
  ImU32 track_header_bg;
  ImU32 track_separator;

  // Timeline Selection
  ImU32 timeline_selection_bg;
  ImU32 timeline_selection_line;
  ImU32 timeline_selection_text;
  ImU32 timeline_selection_text_bg;

  // Box Selection
  ImU32 box_selection_bg;
  ImU32 box_selection_border;

  // Status
  ig_vec4_t status_loading;

  // Search Histogram
  ImU32 search_histogram_bg;
  ImU32 search_histogram_bar;
  ImU32 search_histogram_bar_hovered;
  ImU32 search_histogram_bar_selected;

  // Vertical Minimap
  ImU32 vertical_minimap_bg;
  ImU32 vertical_minimap_slider_bg;
  ImU32 vertical_minimap_slider_bg_hovered;
  ImU32 vertical_minimap_slider_bg_active;

  // UI Colors (mapped to ImGui)
  ImU32 ui_bg;
  ImU32 ui_fg;
  ImU32 ui_border;
  ImU32 ui_input_bg;
  ImU32 ui_button_bg;
  ImU32 ui_button_hovered;
  ImU32 ui_button_active;
  ImU32 ui_button_fg;
  ImU32 ui_header_hovered;
  ImU32 ui_header_active;
  ImU32 ui_selection_bg;
  ImU32 ui_text_disabled;

  // Event Palette
  uint32_t event_palette[8];
};

typedef struct Theme theme_t;

#ifdef __cplusplus
extern "C" {
#endif

const theme_t* theme_get_dark();
const theme_t* theme_get_light();

#ifdef __cplusplus
}
#endif

#endif  // SRC_COLORS_H
