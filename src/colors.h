#ifndef ZTRACING_SRC_COLORS_H_
#define ZTRACING_SRC_COLORS_H_

#include <stdint.h>

#include "third_party/imgui/imgui.h"

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
  ImU32 event_text;
  ImU32 event_text_selected;

  // Tracks
  ImU32 track_text;
  ImU32 track_header_bg;
  ImU32 track_separator;

  // Timeline Selection
  ImU32 timeline_selection_bg;
  ImU32 timeline_selection_line;
  ImU32 timeline_selection_text;

  // Box Selection
  ImU32 box_selection_bg;
  ImU32 box_selection_border;

  // Status
  ImVec4 status_loading;

  // Event Palette
  uint32_t event_palette[8];
};

const Theme* theme_get_dark();
const Theme* theme_get_light();

#endif  // ZTRACING_SRC_COLORS_H_
