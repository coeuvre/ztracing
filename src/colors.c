#include "src/colors.h"
#include "src/imgui_types.h"

typedef struct {
  bool is_dark;
  ImU32 bg;
  ImU32 sidebar_bg;
  ImU32 border;
  ImU32 fg;
  ImU32 fg_muted;
  ImU32 fg_disabled;
  ImU32 accent;
  ImU32 input_bg;
  uint32_t event_palette[8];
} theme_palette_t;

static uint8_t clamp_u8(float v) {
  if (v < 0.0f) return 0;
  if (v > 255.0f) return 255;
  return (uint8_t)v;
}

static ImU32 color_with_alpha(ImU32 col, uint8_t alpha) {
  return (col & 0x00FFFFFF) | ((ImU32)alpha << 24);
}

static ImU32 color_adjust(ImU32 col, float factor) {
  uint8_t r = (uint8_t)((col >> 0) & 0xFF);
  uint8_t g = (uint8_t)((col >> 8) & 0xFF);
  uint8_t b = (uint8_t)((col >> 16) & 0xFF);
  uint8_t a = (uint8_t)((col >> 24) & 0xFF);
  
  return IG_COL32(clamp_u8(r * factor), clamp_u8(g * factor), clamp_u8(b * factor), a);
}

static ig_vec4_t u32_to_float4(ImU32 in) {
  float s = 1.0f / 255.0f;
  return (ig_vec4_t){
      (float)((in >> 0) & 0xFF) * s,
      (float)((in >> 8) & 0xFF) * s,
      (float)((in >> 16) & 0xFF) * s,
      (float)((in >> 24) & 0xFF) * s,
  };
}

static theme_t theme_derive(const theme_palette_t* pal) {
  theme_t t = {0};

  t.viewport_bg = pal->bg;
  t.track_bg = pal->bg;

  // Time Ruler
  t.ruler_bg = pal->sidebar_bg;
  t.ruler_border = pal->border;
  t.ruler_tick = pal->fg_disabled;
  t.ruler_text = pal->fg_muted;

  // Events
  t.event_border = pal->is_dark ? IG_COL32(255, 255, 255, 20) : IG_COL32(0, 0, 0, 15);
  t.event_border_selected = pal->fg;
  t.event_border_focused = pal->accent;
  t.event_focused_bg = color_with_alpha(pal->accent, 51); // ~20% alpha

  // Tracks
  t.track_text = pal->fg;
  t.track_header_bg = pal->sidebar_bg;
  t.track_separator = pal->border;

  // Timeline Selection
  t.timeline_selection_bg = color_with_alpha(pal->accent, 26); // ~10% alpha
  t.timeline_selection_line = pal->accent;
  t.timeline_selection_text = IG_COL32(255, 255, 255, 255);
  t.timeline_selection_text_bg = pal->is_dark ? color_adjust(pal->accent, 0.8f) : pal->accent;

  // Box Selection
  t.box_selection_bg = color_with_alpha(pal->accent, 38); // ~15% alpha
  t.box_selection_border = pal->accent;

  // Status
  t.status_loading = u32_to_float4(pal->accent);

  // Search Histogram
  t.search_histogram_bg = pal->sidebar_bg;
  t.search_histogram_bar = pal->fg_disabled;
  t.search_histogram_bar_hovered = pal->fg;
  t.search_histogram_bar_selected = pal->accent;

  // Vertical Minimap
  t.vertical_minimap_bg = color_with_alpha(pal->sidebar_bg, 204); // ~80% alpha
  t.vertical_minimap_slider_bg = pal->is_dark ? IG_COL32(168, 169, 170, 64) : IG_COL32(100, 100, 100, 32);
  t.vertical_minimap_slider_bg_hovered = pal->is_dark ? IG_COL32(168, 169, 170, 96) : IG_COL32(100, 100, 100, 64);
  t.vertical_minimap_slider_bg_active = pal->is_dark ? IG_COL32(168, 169, 170, 128) : IG_COL32(100, 100, 100, 96);

  // UI Colors (mapped to ImGui)
  t.ui_bg = pal->sidebar_bg;
  t.ui_fg = pal->fg;
  t.ui_border = pal->border;
  t.ui_input_bg = pal->input_bg;
  
  t.ui_button_bg = pal->is_dark ? color_adjust(pal->accent, 0.8f) : pal->accent;
  if (pal->is_dark) {
    t.ui_button_hovered = color_adjust(t.ui_button_bg, 1.05f);
    t.ui_button_active = color_adjust(t.ui_button_bg, 0.80f);
  } else {
    t.ui_button_hovered = color_adjust(t.ui_button_bg, 0.95f);
    t.ui_button_active = color_adjust(t.ui_button_bg, 0.80f);
  }
  t.ui_button_fg = IG_COL32(255, 255, 255, 255); // Always white text for high contrast
  
  t.ui_header_hovered = pal->is_dark ? color_adjust(pal->sidebar_bg, 1.2f) : color_adjust(pal->sidebar_bg, 0.96f);
  t.ui_header_active = color_with_alpha(pal->accent, pal->is_dark ? 51 : 26);
  t.ui_selection_bg = color_with_alpha(pal->accent, pal->is_dark ? 38 : 26);
  t.ui_text_disabled = pal->fg_disabled;

  for (int i = 0; i < 8; ++i) {
    t.event_palette[i] = pal->event_palette[i];
  }

  return t;
}

const theme_t* theme_get_dark() {
  static theme_t dark_theme;
  static bool initialized = false;
  if (!initialized) {
    static const theme_palette_t dark_palette = {
        .is_dark = true,
        .bg = IG_COL32(31, 31, 31, 255),                  // #1F1F1F
        .sidebar_bg = IG_COL32(25, 26, 27, 255),           // #191A1B
        .border = IG_COL32(42, 43, 44, 255),               // #2A2B2C
        .fg = IG_COL32(191, 191, 191, 255),               // #BFBFBF
        .fg_muted = IG_COL32(140, 140, 140, 255),          // #8C8C8C
        .fg_disabled = IG_COL32(85, 85, 85, 255),          // #555555
        .accent = IG_COL32(57, 148, 188, 255),             // #3994BC
        .input_bg = IG_COL32(25, 26, 27, 255),             // #191A1B
        .event_palette = {
            0xFF966D3C,  // Refined Blue
            0xFF7B8D37,  // Refined Green
            0xFF546590,  // Refined Salmon
            0xFF865E8A,  // Refined Purple
            0xFFB29A6D,  // Refined Light Blue
            0xFF3232AB,  // Refined Red
            0xFF779A9A,  // Refined Yellow
            0xFF8F8F8F,  // Refined Gray
        }
    };
    dark_theme = theme_derive(&dark_palette);
    initialized = true;
  }
  return &dark_theme;
}

const theme_t* theme_get_light() {
  static theme_t light_theme;
  static bool initialized = false;
  if (!initialized) {
    static const theme_palette_t light_palette = {
        .is_dark = false,
        .bg = IG_COL32(255, 255, 255, 255),                // #FFFFFF
        .sidebar_bg = IG_COL32(250, 250, 253, 255),        // #FAFAFD
        .border = IG_COL32(240, 241, 242, 255),            // #F0F1F2
        .fg = IG_COL32(32, 32, 32, 255),                   // #202020
        .fg_muted = IG_COL32(72, 72, 72, 255),             // #484848
        .fg_disabled = IG_COL32(112, 112, 112, 255),       // #707070
        .accent = IG_COL32(0, 105, 204, 255),              // #0069CC
        .input_bg = IG_COL32(220, 220, 220, 255),          // #DCDCDC
        .event_palette = {
            IG_COL32(127, 180, 229, 255),  // Soft VS Code Blue (#7FB4E5)
            IG_COL32(132, 194, 171, 255),  // Soft VS Code Green (#84C2AB)
            IG_COL32(219, 202, 127, 255),  // Soft VS Code Gold (#DBCA7F)
            IG_COL32(209, 138, 138, 255),  // Soft VS Code Red (#D18A8A)
            IG_COL32(215, 127, 237, 255),  // Soft VS Code Purple (#D77FED)
            IG_COL32(146, 191, 204, 255),  // Soft VS Code Teal (#92BFCC)
            IG_COL32(239, 194, 139, 255),  // Soft VS Code Orange (#EFC28B)
            IG_COL32(175, 175, 175, 255),  // Soft VS Code Gray (#AFAFAF)
        }
    };
    light_theme = theme_derive(&light_palette);
    initialized = true;
  }
  return &light_theme;
}
