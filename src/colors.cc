#include "src/colors.h"

const Theme* theme_get_dark() {
  static const Theme dark_theme = {
      // Trace Viewport Backgrounds
      IM_COL32(0, 0, 0, 255),     // viewport_bg
      IM_COL32(25, 25, 25, 255),  // track_bg

      // Time Ruler
      IM_COL32(40, 40, 40, 255),     // ruler_bg
      IM_COL32(100, 100, 100, 255),  // ruler_border
      IM_COL32(150, 150, 150, 255),  // ruler_tick
      IM_COL32(200, 200, 200, 255),  // ruler_text

      // Events
      IM_COL32(255, 255, 255, 50),   // event_border
      IM_COL32(255, 255, 255, 255),  // event_border_selected
      IM_COL32(255, 255, 255, 255),  // event_text
      IM_COL32(0, 0, 0, 255),        // event_text_selected
      IM_COL32(180, 160, 0,
               255),  // event_selected (Muted gold instead of bright yellow)

      // Tracks
      IM_COL32(255, 255, 255, 255),  // track_text
      IM_COL32(35, 35, 35, 255),     // track_header_bg
      IM_COL32(60, 60, 60, 255),     // track_separator

      // Status
      ImVec4(1, 1, 0, 1),  // status_loading

      // Event Palette (0xAABBGGRR) - Darker and more muted
      {
          0xFF3D324A,  // Muted Purple
          0xFF4A3D32,  // Muted Blue/Grey
          0xFF324A3D,  // Muted Green
          0xFF4A4A32,  // Muted Olive
          0xFF32324A,  // Muted Red/Brown
          0xFF404040,  // Dark Grey
          0xFF2D3E4A,  // Muted Teal
          0xFF4A2D3E,  // Muted Maroon
      }};
  return &dark_theme;
}

const Theme* theme_get_light() {
  static const Theme light_theme = {
      // Trace Viewport Backgrounds
      IM_COL32(245, 245, 245, 255),  // viewport_bg
      IM_COL32(230, 230, 230, 255),  // track_bg

      // Time Ruler
      IM_COL32(210, 210, 210, 255),  // ruler_bg
      IM_COL32(180, 180, 180, 255),  // ruler_border
      IM_COL32(150, 150, 150, 255),  // ruler_tick
      IM_COL32(60, 60, 60, 255),     // ruler_text

      // Events
      IM_COL32(0, 0, 0, 60),         // event_border
      IM_COL32(0, 0, 0, 255),        // event_border_selected
      IM_COL32(0, 0, 0, 220),        // event_text (slightly soft black)
      IM_COL32(255, 255, 255, 255),  // event_text_selected
      IM_COL32(0, 120, 215, 255),    // event_selected (Modern Blue)

      // Tracks
      IM_COL32(50, 50, 50, 255),     // track_text
      IM_COL32(220, 220, 220, 255),  // track_header_bg
      IM_COL32(200, 200, 200, 255),  // track_separator

      // Status
      ImVec4(0.8f, 0.4f, 0.0f, 1.0f),  // status_loading (Dark Orange)

      // Event Palette (0xAABBGGRR) - From "MRS. L'S CLASSROOM" (brightened for
      // legibility)
      {
          0xFF8092CD,  // #CD9280 (Dusty Rose)
          0xFF5D94D7,  // #D7945D (Orange)
          0xFF5DB8E4,  // #E4B85D (Gold)
          0xFF9CB69B,  // #9BB69C (Brightened Green)
          0xFFB4A987,  // #87A9B4 (Brightened Teal/Blue)
          0xFFC3C9B1,  // #B1C9C3 (Light Teal)
          0xFFB9D4E0,  // #E0D4B9 (Beige)
          0xFF8092CD,  // Reuse Rose
      }};
  return &light_theme;
}
