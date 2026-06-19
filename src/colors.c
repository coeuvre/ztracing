#include "src/colors.h"

const theme_t* theme_get_dark() {
  static const theme_t dark_theme = {
      // Main Viewport Backgrounds
      .viewport_bg = IG_COL32(0, 0, 0, 255),
      .track_bg = IG_COL32(25, 25, 25, 255),

      // Time Ruler
      .ruler_bg = IG_COL32(40, 40, 40, 255),
      .ruler_border = IG_COL32(100, 100, 100, 255),
      .ruler_tick = IG_COL32(150, 150, 150, 255),
      .ruler_text = IG_COL32(200, 200, 200, 255),

      // Events
      .event_border = IG_COL32(255, 255, 255, 80),
      .event_border_selected = IG_COL32(255, 255, 255, 255),
      .event_border_focused = IG_COL32(0, 255, 255, 255),  // Electric Cyan
      .event_focused_bg = IG_COL32(0, 255, 255, 40),
      .event_text = IG_COL32(255, 255, 255, 255),
      .event_text_selected = IG_COL32(255, 255, 255, 255),
      .event_text_focused = IG_COL32(255, 255, 255, 255),

      // Tracks
      .track_text = IG_COL32(255, 255, 255, 255),
      .track_header_bg = IG_COL32(35, 35, 35, 255),
      .track_separator = IG_COL32(60, 60, 60, 255),

      // Timeline Selection
      .timeline_selection_bg = IG_COL32(0, 0, 0, 100),
      .timeline_selection_line = IG_COL32(255, 255, 0, 255),
      .timeline_selection_text = IG_COL32(255, 255, 255, 255),
      .timeline_selection_text_bg = IG_COL32(30, 30, 30, 230),

      // Box Selection
      .box_selection_bg = IG_COL32(0, 120, 215, 50),
      .box_selection_border = IG_COL32(0, 120, 215, 255),

      // Status
      .status_loading = {1.0f, 1.0f, 0.0f, 1.0f},

      // Search Histogram
      .search_histogram_bg = IG_COL32(30, 30, 30, 255),
      .search_histogram_bar = IG_COL32(150, 150, 150, 255),  // Mid Grey
      .search_histogram_bar_hovered =
          IG_COL32(255, 255, 255, 255),  // Solid White
      .search_histogram_bar_selected =
          IG_COL32(0, 255, 255, 255),  // Electric Cyan

      // Vertical Minimap
      .vertical_minimap_bg = IG_COL32(18, 18, 18, 210),
      .vertical_minimap_slider_bg = IG_COL32(255, 255, 255, 40),
      .vertical_minimap_slider_bg_hovered = IG_COL32(255, 255, 255, 60),
      .vertical_minimap_slider_bg_active = IG_COL32(255, 255, 255, 80),

      // Event Palette (0xAABBGGRR) - Refined Vibrant (30% darker than previous)
      .event_palette = {
          0xFF966D3C,  // Refined Blue
          0xFF7B8D37,  // Refined Green
          0xFF546590,  // Refined Salmon
          0xFF865E8A,  // Refined Purple
          0xFFB29A6D,  // Refined Light Blue
          0xFF3232AB,  // Refined Red
          0xFF779A9A,  // Refined Yellow
          0xFF5A5A5A,  // Refined Grey
      }};
  return &dark_theme;
}

const theme_t* theme_get_light() {
  static const theme_t light_theme = {
      // Main Viewport Backgrounds
      .viewport_bg =
          IG_COL32(249, 250, 251, 255),  // Modern Slate Off-White (#F9FAFB)
      .track_bg =
          IG_COL32(255, 255, 255, 255),  // Pristine Crisp White (#FFFFFF)

      // Time Ruler
      .ruler_bg = IG_COL32(243, 244, 246, 255),  // Clean Slate Gray (#F3F4F6)
      .ruler_border = IG_COL32(229, 231, 235, 255),  // Subtle Border (#E5E7EB)
      .ruler_tick = IG_COL32(209, 213, 219, 255),    // Soft Tick (#D1D5DB)
      .ruler_text = IG_COL32(75, 85, 99, 255),  // Medium Muted Slate (#4B5563)

      // Events
      .event_border =
          IG_COL32(0, 0, 0, 25),  // Ultra-Subtle Event Border (10% Black)
      .event_border_selected =
          IG_COL32(15, 23, 42, 255),  // Slate-900 High-Contrast (#0F172A)
      .event_border_focused =
          IG_COL32(37, 99, 235, 255),  // Glow blue (#2563EB)
      .event_focused_bg =
          IG_COL32(37, 99, 235, 20),  // Very soft blue overlay (8% opacity)
      .event_text =
          IG_COL32(30, 41, 59, 235),  // Highly readable Slate-800 (#1E293B)
      .event_text_selected =
          IG_COL32(15, 23, 42, 255),  // Stay sharp slate-900 (#0F172A)
      .event_text_focused =
          IG_COL32(30, 41, 59, 235),  // Same readable slate-800 (#1E293B)

      // Tracks
      .track_text = IG_COL32(
          31, 41, 55, 255),  // Crisp Slate-800 for track titles (#1F2937)
      .track_header_bg =
          IG_COL32(243, 244, 246, 255),  // Premium off-white (#F3F4F6)
      .track_separator =
          IG_COL32(229, 231, 235, 255),  // Clean divider (#E5E7EB)

      // Timeline Selection
      .timeline_selection_bg =
          IG_COL32(59, 130, 246, 25),  // Very soft blue highlight (10% opacity)
      .timeline_selection_line =
          IG_COL32(59, 130, 246, 255),  // Blue-500 line (#3B82F6)
      .timeline_selection_text =
          IG_COL32(30, 41, 59, 255),  // Dark Slate text (#1E293B)
      .timeline_selection_text_bg =
          IG_COL32(255, 255, 255, 240),  // High-contrast white pill bg

      // Box Selection
      .box_selection_bg =
          IG_COL32(59, 130, 246, 40),  // Selection overlay blue (15% opacity)
      .box_selection_border =
          IG_COL32(59, 130, 246, 255),  // Blue-500 border (#3B82F6)

      // Status
      .status_loading = {0.23f, 0.51f, 0.96f, 1.0f},  // Modern loading blue

      // Search Histogram
      .search_histogram_bg =
          IG_COL32(249, 250, 251, 255),  // Matches viewport_bg (#F9FAFB)
      .search_histogram_bar =
          IG_COL32(209, 213, 219, 255),  // Slate-300 bar (#D1D5DB)
      .search_histogram_bar_hovered =
          IG_COL32(156, 163, 175, 255),  // Slate-400 hover (#9CA3AF)
      .search_histogram_bar_selected =
          IG_COL32(59, 130, 246, 255),  // Blue-500 selection (#3B82F6)

      // Vertical Minimap
      .vertical_minimap_bg = IG_COL32(
          243, 244, 246, 200),  // Sleek, transparent off-white (#F3F4F6 at 78%)
      .vertical_minimap_slider_bg =
          IG_COL32(156, 163, 175, 50),  // Soft slider pill
      .vertical_minimap_slider_bg_hovered = IG_COL32(156, 163, 175, 100),
      .vertical_minimap_slider_bg_active = IG_COL32(156, 163, 175, 150),

      // Event Palette
      .event_palette = {
          IG_COL32(160, 207, 253, 255),  // Sweet-Spot Medium Blue (#A0CFFD)
          IG_COL32(145, 235, 190, 255),  // Sweet-Spot Medium Emerald (#91EBBE)
          IG_COL32(253, 218, 105,
                   255),  // Sweet-Spot Medium Amber/Gold (#FDDA69)
          IG_COL32(253, 180, 190, 255),  // Sweet-Spot Medium Rose (#FDB4BE)
          IG_COL32(205, 195, 254, 255),  // Sweet-Spot Medium Violet (#CDC3FE)
          IG_COL32(120, 240, 220, 255),  // Sweet-Spot Medium Teal (#78F0DC)
          IG_COL32(253, 198, 140,
                   255),  // Sweet-Spot Medium Peach-Orange (#FDC68C)
          IG_COL32(215, 222, 232, 255),  // Sweet-Spot Elegant Slate (#D7DEE8)
      }};
  return &light_theme;
}
