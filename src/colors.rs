#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec4 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
    pub w: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Theme {
    pub viewport_bg: u32,
    pub track_bg: u32,
    pub ruler_bg: u32,
    pub ruler_border: u32,
    pub ruler_tick: u32,
    pub ruler_text: u32,
    pub event_border: u32,
    pub event_border_selected: u32,
    pub event_border_focused: u32,
    pub event_focused_bg: u32,
    pub track_text: u32,
    pub track_header_bg: u32,
    pub track_separator: u32,
    pub timeline_selection_bg: u32,
    pub timeline_selection_line: u32,
    pub timeline_selection_text: u32,
    pub timeline_selection_text_bg: u32,
    pub box_selection_bg: u32,
    pub box_selection_border: u32,
    pub status_loading: Vec4,
    pub search_histogram_bg: u32,
    pub search_histogram_bar: u32,
    pub search_histogram_bar_hovered: u32,
    pub search_histogram_bar_selected: u32,
    pub vertical_minimap_bg: u32,
    pub vertical_minimap_slider_bg: u32,
    pub vertical_minimap_slider_bg_hovered: u32,
    pub vertical_minimap_slider_bg_active: u32,
    pub ui_bg: u32,
    pub ui_fg: u32,
    pub ui_border: u32,
    pub ui_input_bg: u32,
    pub ui_button_bg: u32,
    pub ui_button_hovered: u32,
    pub ui_button_active: u32,
    pub ui_button_fg: u32,
    pub ui_header_hovered: u32,
    pub ui_header_active: u32,
    pub ui_selection_bg: u32,
    pub ui_text_disabled: u32,
    pub event_palette: [u32; 8],
}

const fn col(r: u8, g: u8, b: u8, a: u8) -> u32 {
    (r as u32) | ((g as u32) << 8) | ((b as u32) << 16) | ((a as u32) << 24)
}
fn alpha(color: u32, value: u8) -> u32 {
    color & 0x00ff_ffff | (u32::from(value) << 24)
}

fn adjust(color: u32, factor: f32) -> u32 {
    let channel =
        |shift: u32| (((color >> shift) & 0xff_u32) as f32 * factor).clamp(0.0, 255.0) as u8;
    col(
        channel(0),
        channel(8),
        channel(16),
        ((color >> 24) & 0xff) as u8,
    )
}

fn float4(color: u32) -> Vec4 {
    let s = 1.0 / 255.0;
    Vec4 {
        x: (color & 0xff) as f32 * s,
        y: ((color >> 8) & 0xff) as f32 * s,
        z: ((color >> 16) & 0xff) as f32 * s,
        w: ((color >> 24) & 0xff) as f32 * s,
    }
}

fn derive(
    dark: bool,
    bg: u32,
    sidebar: u32,
    border: u32,
    fg: u32,
    muted: u32,
    disabled: u32,
    accent: u32,
    input: u32,
    palette: [u32; 8],
) -> Theme {
    let button = if dark { adjust(accent, 0.8) } else { accent };
    Theme {
        viewport_bg: bg,
        track_bg: bg,
        ruler_bg: sidebar,
        ruler_border: border,
        ruler_tick: disabled,
        ruler_text: muted,
        event_border: if dark {
            col(255, 255, 255, 20)
        } else {
            col(0, 0, 0, 15)
        },
        event_border_selected: if dark {
            adjust(fg, 1.35)
        } else {
            adjust(fg, 0.5)
        },
        event_border_focused: if dark {
            adjust(accent, 1.5)
        } else {
            adjust(accent, 0.8)
        },
        event_focused_bg: alpha(accent, 51),
        track_text: fg,
        track_header_bg: sidebar,
        track_separator: border,
        timeline_selection_bg: alpha(accent, 26),
        timeline_selection_line: accent,
        timeline_selection_text: col(255, 255, 255, 255),
        timeline_selection_text_bg: if dark { adjust(accent, 0.8) } else { accent },
        box_selection_bg: alpha(accent, 38),
        box_selection_border: accent,
        status_loading: float4(accent),
        search_histogram_bg: sidebar,
        search_histogram_bar: disabled,
        search_histogram_bar_hovered: fg,
        search_histogram_bar_selected: accent,
        vertical_minimap_bg: alpha(sidebar, 204),
        vertical_minimap_slider_bg: if dark {
            col(168, 169, 170, 64)
        } else {
            col(100, 100, 100, 32)
        },
        vertical_minimap_slider_bg_hovered: if dark {
            col(168, 169, 170, 96)
        } else {
            col(100, 100, 100, 64)
        },
        vertical_minimap_slider_bg_active: if dark {
            col(168, 169, 170, 128)
        } else {
            col(100, 100, 100, 96)
        },
        ui_bg: sidebar,
        ui_fg: fg,
        ui_border: border,
        ui_input_bg: input,
        ui_button_bg: button,
        ui_button_hovered: adjust(button, if dark { 1.05 } else { 0.95 }),
        ui_button_active: adjust(button, 0.8),
        ui_button_fg: col(255, 255, 255, 255),
        ui_header_hovered: adjust(sidebar, if dark { 2.2 } else { 0.88 }),
        ui_header_active: alpha(accent, if dark { 51 } else { 26 }),
        ui_selection_bg: alpha(accent, if dark { 38 } else { 26 }),
        ui_text_disabled: disabled,
        event_palette: palette,
    }
}

pub fn dark() -> Theme {
    derive(
        true,
        col(31, 31, 31, 255),
        col(25, 26, 27, 255),
        col(42, 43, 44, 255),
        col(191, 191, 191, 255),
        col(140, 140, 140, 255),
        col(85, 85, 85, 255),
        col(57, 148, 188, 255),
        col(25, 26, 27, 255),
        [
            0xFF966D3C, 0xFF7B8D37, 0xFF546590, 0xFF865E8A, 0xFFB29A6D, 0xFF3232AB, 0xFF779A9A,
            0xFF8F8F8F,
        ],
    )
}

pub fn light() -> Theme {
    derive(
        false,
        col(255, 255, 255, 255),
        col(250, 250, 253, 255),
        col(240, 241, 242, 255),
        col(32, 32, 32, 255),
        col(72, 72, 72, 255),
        col(112, 112, 112, 255),
        col(0, 105, 204, 255),
        col(220, 220, 220, 255),
        [
            col(127, 180, 229, 255),
            col(132, 194, 171, 255),
            col(219, 202, 127, 255),
            col(209, 138, 138, 255),
            col(215, 127, 237, 255),
            col(146, 191, 204, 255),
            col(239, 194, 139, 255),
            col(175, 175, 175, 255),
        ],
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, offset_of, size_of};

    #[test]
    fn theme_matches_cpp_abi() {
        assert_eq!(align_of::<Vec4>(), 4);
        assert_eq!(size_of::<Vec4>(), 16);
        assert_eq!(align_of::<Theme>(), 4);
        assert_eq!(size_of::<Theme>(), 204);
        assert_eq!(offset_of!(Theme, status_loading), 76);
        assert_eq!(offset_of!(Theme, search_histogram_bg), 92);
        assert_eq!(offset_of!(Theme, vertical_minimap_bg), 108);
        assert_eq!(offset_of!(Theme, ui_bg), 124);
        assert_eq!(offset_of!(Theme, event_palette), 172);
    }

    #[test]
    fn dark_theme_header_hover_has_visible_contrast() {
        let theme = dark();
        let bg_r = (theme.ui_bg & 0xff) as i32;
        let hover_r = (theme.ui_header_hovered & 0xff) as i32;
        assert!((hover_r - bg_r).abs() >= 25);
    }

    #[test]
    fn light_theme_header_hover_has_visible_contrast() {
        let theme = light();
        let bg_r = (theme.ui_bg & 0xff) as i32;
        let hover_r = (theme.ui_header_hovered & 0xff) as i32;
        assert!((hover_r - bg_r).abs() >= 25);
    }

    #[test]
    fn event_border_selected_has_high_contrast() {
        let dark_theme = dark();
        let light_theme = light();
        let dark_bg_r = (dark_theme.viewport_bg & 0xff) as i32;
        let dark_selected_r = (dark_theme.event_border_selected & 0xff) as i32;
        assert!((dark_selected_r - dark_bg_r).abs() >= 150);

        let light_bg_r = (light_theme.viewport_bg & 0xff) as i32;
        let light_selected_r = (light_theme.event_border_selected & 0xff) as i32;
        assert!((light_selected_r - light_bg_r).abs() >= 150);
    }

    #[test]
    fn event_border_focused_has_high_contrast() {
        let dark_theme = dark();
        let light_theme = light();
        let dark_bg_g = ((dark_theme.viewport_bg >> 8) & 0xff) as i32;
        let dark_focused_g = ((dark_theme.event_border_focused >> 8) & 0xff) as i32;
        assert!((dark_focused_g - dark_bg_g).abs() >= 150);

        let light_bg_g = ((light_theme.viewport_bg >> 8) & 0xff) as i32;
        let light_focused_g = ((light_theme.event_border_focused >> 8) & 0xff) as i32;
        assert!((light_focused_g - light_bg_g).abs() >= 50);
    }
}
