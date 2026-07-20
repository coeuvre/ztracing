use crate::colors::{Theme, Vec4};
use base::allocation::CountingAllocator;
use std::ffi::{CString, c_char, c_void};
use std::marker::PhantomData;
use std::sync::atomic::{AtomicBool, Ordering};

static CONTEXT_ACTIVE: AtomicBool = AtomicBool::new(false);

// Flags & enums — keep values aligned with the FFI layer.
pub const TABLE_FLAGS_NONE: i32 = 0;
pub const TABLE_FLAGS_RESIZABLE: i32 = 1;
pub const TABLE_FLAGS_SORTABLE: i32 = 8;
pub const TABLE_FLAGS_ROW_BG: i32 = 64;
pub const TABLE_FLAGS_BORDERS: i32 = 1920;
pub const TABLE_FLAGS_SCROLL_Y: i32 = 33_554_432;
pub const TABLE_FLAGS_SORT_TRISTATE: i32 = 134_217_728;
pub const TABLE_FLAGS_NO_SAVED_SETTINGS: i32 = 16;
pub const TABLE_FLAGS_SIZING_FIXED_FIT: i32 = 8192;

pub const TABLE_COLUMN_FLAGS_NONE: i32 = 0;
pub const TABLE_COLUMN_FLAGS_WIDTH_FIXED: i32 = 16;
pub const TABLE_COLUMN_FLAGS_WIDTH_STRETCH: i32 = 8;

pub const WINDOW_FLAGS_NONE: i32 = 0;
pub const WINDOW_FLAGS_NO_TITLE_BAR: i32 = 1;
pub const WINDOW_FLAGS_NO_RESIZE: i32 = 2;
pub const WINDOW_FLAGS_NO_MOVE: i32 = 4;
pub const WINDOW_FLAGS_NO_SCROLLBAR: i32 = 8;
pub const WINDOW_FLAGS_NO_COLLAPSE: i32 = 32;
pub const WINDOW_FLAGS_NO_SCROLL_WITH_MOUSE: i32 = 16;
pub const WINDOW_FLAGS_NO_FOCUS_ON_APPEARING: i32 = 4096;

pub const INPUT_TEXT_FLAGS_NONE: i32 = 0;
pub const INPUT_TEXT_FLAGS_CALLBACK_RESIZE: i32 = 4_194_304;

pub const STYLE_VAR_WINDOW_PADDING: i32 = 2;
pub const STYLE_VAR_WINDOW_ROUNDING: i32 = 3;
pub const STYLE_VAR_WINDOW_BORDER_SIZE: i32 = 4;

pub const KEY_ENTER: i32 = 525;
pub const KEY_SLASH: i32 = 600;
pub const KEY_F: i32 = 551;

pub const COND_NONE: i32 = 0;
pub const COND_APPEARING: i32 = 1 << 3;

pub const MOUSE_CURSOR_RESIZE_EW: i32 = 4;

pub const SORT_DIRECTION_NONE: i32 = 0;
pub const SORT_DIRECTION_ASCENDING: i32 = 1;
pub const SORT_DIRECTION_DESCENDING: i32 = 2;

pub const DRAW_LIST_FLAGS_ANTI_ALIASED_LINES: i32 = 1;

pub const MOD_NONE: i32 = 0;
pub const MOD_CTRL: i32 = 1 << 12;
pub const MOD_SHIFT: i32 = 1 << 13;
pub const MOD_ALT: i32 = 1 << 14;
pub const MOD_SUPER: i32 = 1 << 15;

pub const HOVERED_FLAGS_NONE: i32 = 0;
pub const HOVERED_FLAGS_CHILD_WINDOWS: i32 = 1 << 0;

pub const DOCK_NODE_FLAGS_NONE: i32 = 0;
pub const DOCK_NODE_FLAGS_DOCKSPACE: i32 = 1 << 10;
pub const DOCK_NODE_FLAGS_NO_TAB_BAR: i32 = 1 << 12;
pub const DOCK_NODE_FLAGS_NO_DOCKING_OVER_ME: i32 = 1 << 20;

pub const DIR_RIGHT: i32 = 1;

pub const COL_POPUP_BG: i32 = 4;

pub const POPUP_FLAGS_NONE: i32 = 0;

pub const CONFIG_FLAGS_NAV_ENABLE_KEYBOARD: i32 = 1 << 0;
pub const CONFIG_FLAGS_DOCKING_ENABLE: i32 = 128;

pub const SELECTABLE_FLAGS_NONE: i32 = 0;
pub const SELECTABLE_FLAGS_DONT_CLOSE_POPUPS: i32 = 1 << 0;
pub const SELECTABLE_FLAGS_SPAN_ALL_COLUMNS: i32 = 1 << 1;
pub const SELECTABLE_FLAGS_ALLOW_DOUBLE_CLICKS: i32 = 1 << 2;
pub const SELECTABLE_FLAGS_DISABLED: i32 = 1 << 3;
pub const SELECTABLE_FLAGS_ALLOW_OVERLAP: i32 = 1 << 4;

pub const MAIN_VIEWPORT_WINDOW_FLAGS: i32 = WINDOW_FLAGS_NO_TITLE_BAR
    | WINDOW_FLAGS_NO_RESIZE
    | WINDOW_FLAGS_NO_MOVE
    | WINDOW_FLAGS_NO_SCROLLBAR
    | WINDOW_FLAGS_NO_COLLAPSE;

pub const SHORTCUTS_POPUP_FLAGS: i32 = WINDOW_FLAGS_NO_MOVE | WINDOW_FLAGS_NO_SCROLLBAR;

pub const MULTI_SELECT_TABLE_FLAGS: i32 = TABLE_FLAGS_RESIZABLE
    | TABLE_FLAGS_SORTABLE
    | TABLE_FLAGS_ROW_BG
    | TABLE_FLAGS_BORDERS
    | TABLE_FLAGS_SIZING_FIXED_FIT
    | TABLE_FLAGS_SCROLL_Y
    | TABLE_FLAGS_SORT_TRISTATE
    | TABLE_FLAGS_NO_SAVED_SETTINGS;

pub const CHEATSHEET_TABLE_FLAGS: i32 = TABLE_FLAGS_ROW_BG | TABLE_FLAGS_SIZING_FIXED_FIT;

pub const HOVER_PROPERTIES_TABLE_FLAGS: i32 =
    TABLE_FLAGS_NO_SAVED_SETTINGS | TABLE_FLAGS_SIZING_FIXED_FIT;

pub const SELECTABLE_SPAN_OVERLAP: i32 =
    SELECTABLE_FLAGS_SPAN_ALL_COLUMNS | SELECTABLE_FLAGS_ALLOW_OVERLAP;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}
#[repr(C)]
pub struct DrawData {
    _private: [u8; 0],
}
#[repr(C)]
pub struct Viewport {
    _private: [u8; 0],
}
#[repr(C)]
pub struct TableSortSpecs {
    _private: [u8; 0],
}
#[repr(C)]
pub struct DockNode {
    _private: [u8; 0],
}
#[repr(C)]
pub struct DrawList {
    _private: [u8; 0],
}
#[repr(C)]
struct ListClipperHandle {
    _private: [u8; 0],
}

fn c_label(text: &str) -> CString {
    let Some(nul) = text.as_bytes().iter().position(|byte| *byte == 0) else {
        return CString::new(text).expect("text without NUL is a valid C string");
    };
    let hidden_id = text
        .get(nul..)
        .and_then(|suffix| suffix.find("##").map(|index| nul + index));
    let mut bytes = text.as_bytes()[..nul].to_vec();
    if let Some(hidden_id) = hidden_id {
        let suffix = &text.as_bytes()[hidden_id..];
        let end = suffix
            .iter()
            .position(|byte| *byte == 0)
            .unwrap_or(suffix.len());
        bytes.extend_from_slice(&suffix[..end]);
    }
    CString::new(bytes).expect("embedded NUL bytes were removed")
}

fn nul_terminated_prefix(text: &str) -> CString {
    let end = text
        .as_bytes()
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(text.len());
    CString::new(&text.as_bytes()[..end]).expect("embedded NUL bytes were removed")
}

fn text_pointer(text: &str) -> *const c_char {
    if text.is_empty() {
        c"".as_ptr()
    } else {
        text.as_ptr().cast()
    }
}

fn text_bounds(text: &str) -> (*const c_char, *const c_char) {
    let begin = text_pointer(text);
    // SAFETY: `begin` points to `text` when non-empty and to a static empty C
    // string otherwise, so advancing by the explicit byte length is in bounds.
    let end = unsafe { begin.add(text.len()) };
    (begin, end)
}

fn prepare_input_buffer(value: &mut String) -> Vec<u8> {
    let mut buffer = std::mem::take(value).into_bytes();
    let size = buffer
        .len()
        .saturating_add(1)
        .checked_next_power_of_two()
        .unwrap_or_else(|| buffer.len().saturating_add(1))
        .max(128);
    buffer.resize(size, 0);
    buffer
}

fn finish_input_buffer(mut buffer: Vec<u8>) -> String {
    let end = buffer
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(buffer.len());
    buffer.truncate(end);
    String::from_utf8(buffer)
        .unwrap_or_else(|error| String::from_utf8_lossy(error.as_bytes()).into_owned())
}

pub struct Context {
    font: Vec<u8>,
    _not_send: PhantomData<*mut ()>,
}

pub struct Frame<'a> {
    context: &'a mut Context,
    ended: bool,
}

impl Context {
    pub fn create() -> Self {
        assert!(
            CONTEXT_ACTIVE
                .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
                .is_ok(),
            "only one ImGui Context may be active"
        );
        // SAFETY: creation is performed once by the owning Context on the UI thread.
        let created = unsafe {
            ffi::ig_set_allocator_functions(
                Some(imgui_allocate),
                Some(imgui_free),
                std::ptr::null_mut(),
            );
            ffi::ig_create_context()
        };
        if !created {
            CONTEXT_ACTIVE.store(false, Ordering::Release);
            panic!("failed to create ImGui Context");
        }
        unsafe {
            ffi::ig_io_add_config_flags(
                CONFIG_FLAGS_NAV_ENABLE_KEYBOARD | CONFIG_FLAGS_DOCKING_ENABLE,
            )
        }
        Self {
            font: Vec::new(),
            _not_send: PhantomData,
        }
    }

    pub fn add_mouse_position(&mut self, x: f32, y: f32) {
        unsafe { ffi::ig_io_add_mouse_pos_event(x, y) }
    }
    pub fn add_mouse_button(&mut self, button: i32, down: bool) {
        unsafe { ffi::ig_io_add_mouse_button_event(button, down) }
    }
    pub fn add_mouse_wheel(&mut self, horizontal: f32, vertical: f32) {
        unsafe { ffi::ig_io_add_mouse_wheel_event(horizontal, vertical) }
    }
    pub fn add_key(&mut self, key: i32, down: bool) {
        unsafe { ffi::ig_io_add_key_event(key, down) }
    }
    pub fn add_text(&mut self, text: &str) {
        let text = nul_terminated_prefix(text);
        unsafe { ffi::ig_io_add_input_characters_utf8(text.as_ptr()) }
    }
    pub fn set_display_size(&mut self, width: f32, height: f32) {
        unsafe {
            ffi::ig_io_set_display_size(Vec2 {
                x: width,
                y: height,
            })
        }
    }
    pub fn set_delta_time(&mut self, seconds: f32) {
        unsafe { ffi::ig_io_set_delta_time(seconds) }
    }
    pub fn set_font(&mut self, data: &[u8], dpi_scale: f32) {
        self.font.clear();
        self.font.extend_from_slice(data);
        unsafe {
            ffi::ig_set_font_data(
                self.font.as_ptr().cast(),
                i32::try_from(self.font.len()).unwrap_or(i32::MAX),
                dpi_scale,
            )
        }
    }
    pub fn apply_theme(&mut self, theme: &Theme) {
        unsafe { ffi::ig_style_apply_theme(theme) }
    }
    pub fn frame(&mut self) -> Frame<'_> {
        unsafe { ffi::ig_new_frame() };
        Frame {
            context: self,
            ended: false,
        }
    }
}

unsafe extern "C" fn imgui_allocate(size: usize, _user_data: *mut c_void) -> *mut c_void {
    // SAFETY: ImGui pairs this callback with `imgui_free` below.
    unsafe { CountingAllocator::foreign_alloc(size).cast() }
}

unsafe extern "C" fn imgui_free(pointer: *mut c_void, _user_data: *mut c_void) {
    // SAFETY: ImGui passes only pointers returned by `imgui_allocate`.
    unsafe { CountingAllocator::foreign_free(pointer.cast()) }
}
impl Drop for Context {
    fn drop(&mut self) {
        unsafe { ffi::ig_destroy_context() };
        CONTEXT_ACTIVE.store(false, Ordering::Release);
    }
}

impl Frame<'_> {
    pub fn render(mut self) -> *mut DrawData {
        unsafe { ffi::ig_render() };
        self.ended = true;
        unsafe { ffi::ig_get_draw_data() }
    }

    pub fn text(&self, text: &str) {
        unsafe { ffi::ig_text_unformatted_range(text_pointer(text), text.len()) }
    }

    pub fn text_wrapped(&self, text: &str) {
        unsafe { ffi::ig_text_wrapped_range(text_pointer(text), text.len()) }
    }
    pub fn begin_child<'a>(
        &'a self,
        name: &str,
        size: Vec2,
        border: bool,
        flags: i32,
    ) -> Option<Child<'a>> {
        let name = c_label(name);
        let visible = unsafe { ffi::ig_begin_child(name.as_ptr(), size, border, flags) };
        Some(Child {
            frame: self,
            visible,
        })
    }
    pub fn mouse_position(&self) -> Vec2 {
        unsafe { ffi::ig_get_io_mouse_pos() }
    }
    pub fn mouse_clicked_position(&self) -> Vec2 {
        unsafe { ffi::ig_get_io_mouse_clicked_pos(0) }
    }
    pub fn mouse_delta(&self) -> Vec2 {
        unsafe { ffi::ig_get_io_mouse_delta() }
    }
    pub fn mouse_drag_delta(&self) -> Vec2 {
        unsafe { ffi::ig_get_mouse_drag_delta(0, -1.0) }
    }
    pub fn mouse_wheel(&self) -> f32 {
        unsafe { ffi::ig_get_io_mouse_wheel() }
    }
    pub fn mouse_down(&self) -> bool {
        unsafe { ffi::ig_is_mouse_down(0) }
    }
    pub fn mouse_clicked(&self) -> bool {
        unsafe { ffi::ig_is_mouse_clicked(0, false) }
    }
    pub fn mouse_double_clicked(&self) -> bool {
        unsafe { ffi::ig_is_mouse_double_clicked(0) }
    }
    pub fn mouse_released(&self) -> bool {
        unsafe { ffi::ig_is_mouse_released(0) }
    }
    pub fn mouse_dragging(&self) -> bool {
        unsafe { ffi::ig_is_mouse_dragging(0, -1.0) }
    }
    pub fn drag_threshold(&self) -> f32 {
        unsafe { ffi::ig_get_io_mouse_drag_threshold() }
    }
    pub fn ctrl_down(&self) -> bool {
        unsafe { ffi::ig_get_io_key_ctrl() }
    }
    pub fn shift_down(&self) -> bool {
        unsafe { ffi::ig_get_io_key_shift() }
    }
    pub fn key_down(&self, key: i32) -> bool {
        unsafe { ffi::ig_is_key_down(key) }
    }
    pub fn mouse_hovering_rect(&self, minimum: Vec2, maximum: Vec2, clip: bool) -> bool {
        unsafe { ffi::ig_is_mouse_hovering_rect(minimum, maximum, clip) }
    }
    pub fn set_mouse_cursor(&self, cursor: i32) {
        unsafe { ffi::ig_set_mouse_cursor(cursor) }
    }
    pub fn invisible_button(&self, label: &str, size: Vec2) {
        let label = c_label(label);
        let _ = unsafe { ffi::ig_invisible_button(label.as_ptr(), size) };
    }
    pub fn item_active(&self) -> bool {
        unsafe { ffi::ig_is_item_active() }
    }
    pub fn item_hovered(&self) -> bool {
        unsafe { ffi::ig_is_item_hovered() }
    }
    pub fn item_focused(&self) -> bool {
        unsafe { ffi::ig_is_item_focused() }
    }
    pub fn item_activated(&self) -> bool {
        unsafe { ffi::ig_is_item_activated() }
    }
    pub fn item_deactivated(&self) -> bool {
        unsafe { ffi::ig_is_item_deactivated() }
    }
    pub fn window_hovered(&self) -> bool {
        unsafe { ffi::ig_is_window_hovered(HOVERED_FLAGS_CHILD_WINDOWS) }
    }
    pub fn set_keyboard_focus_here(&self) {
        unsafe { ffi::ig_set_keyboard_focus_here(0) }
    }
    pub fn key_pressed(&self, key: i32) -> bool {
        unsafe { ffi::ig_is_key_pressed(key, false) }
    }
    pub fn want_text_input(&self) -> bool {
        unsafe { ffi::ig_get_io_want_text_input() }
    }
    pub fn set_clipboard(&self, text: &str) {
        let text = nul_terminated_prefix(text);
        unsafe { ffi::ig_set_clipboard_text(text.as_ptr()) }
    }
    pub fn scroll_y(&self) -> f32 {
        unsafe { ffi::ig_get_scroll_y() }
    }
    pub fn frame_height(&self) -> f32 {
        unsafe { ffi::ig_get_frame_height() }
    }
    pub fn font_size(&self) -> f32 {
        unsafe { ffi::ig_get_font_size() }
    }
    pub fn set_cursor_position(&self, pos: Vec2) {
        unsafe { ffi::ig_set_cursor_pos(pos) }
    }
    pub fn dummy(&self, size: Vec2) {
        unsafe { ffi::ig_dummy(size) }
    }
    pub fn begin_window<'a>(
        &'a self,
        name: &str,
        open: Option<&mut bool>,
        flags: i32,
    ) -> Option<Window<'a>> {
        let name = c_label(name);
        let visible = unsafe {
            ffi::ig_begin(
                name.as_ptr(),
                open.map_or(std::ptr::null_mut(), |v| v),
                flags,
            )
        };
        Some(Window {
            frame: self,
            visible,
        })
    }
    pub fn calc_text_size(&self, text: &str) -> Vec2 {
        unsafe { ffi::ig_calc_text_size_range(text_pointer(text), text.len()) }
    }

    pub fn color(&self, color: u32) -> Vec4 {
        // SAFETY: color is a packed ImGui color value.
        unsafe { ffi::ig_color_convert_u32_to_float4(color) }
    }
    pub fn text_colored(&self, color: Vec4, text: &str) {
        unsafe { ffi::ig_text_colored_range(color, text_pointer(text), text.len()) }
    }

    pub fn text_disabled(&self, text: &str) {
        unsafe { ffi::ig_text_disabled_range(text_pointer(text), text.len()) }
    }
    pub fn begin_main_menu_bar(&self) -> Option<MainMenuBar<'_>> {
        // SAFETY: called during an active frame on the UI thread.
        unsafe { ffi::ig_begin_main_menu_bar() }.then(|| MainMenuBar { frame: self })
    }
    pub fn begin_menu(&self, label: &str) -> Option<Menu<'_>> {
        let label = c_label(label);
        // SAFETY: label is NUL-terminated and called inside a menu bar/menu.
        unsafe { ffi::ig_begin_menu(label.as_ptr(), true) }.then(|| Menu { frame: self })
    }
    pub fn menu_item(&self, label: &str, shortcut: Option<&str>, selected: bool) -> bool {
        let label = c_label(label);
        let shortcut = shortcut.map(nul_terminated_prefix);
        // SAFETY: pointers remain valid for the duration of this call.
        unsafe {
            ffi::ig_menu_item(
                label.as_ptr(),
                shortcut.as_ref().map_or(std::ptr::null(), |s| s.as_ptr()),
                selected,
                true,
            )
        }
    }
    pub fn menu_item_toggle(&self, label: &str, value: &mut bool) -> bool {
        let label = c_label(label);
        // SAFETY: label and mutable bool are valid for this synchronous call.
        unsafe { ffi::ig_menu_item_ptr(label.as_ptr(), std::ptr::null(), value, true) }
    }
    pub fn separator(&self) {
        unsafe { ffi::ig_separator() }
    }
    pub fn spacing(&self) {
        unsafe { ffi::ig_spacing() }
    }
    pub fn same_line(&self, offset: f32, spacing: f32) {
        unsafe { ffi::ig_same_line(offset, spacing) }
    }
    pub fn set_scroll_y(&self, scroll_y: f32) {
        unsafe { ffi::ig_set_scroll_y(scroll_y) }
    }
    pub fn window_position(&self) -> Vec2 {
        unsafe { ffi::ig_get_window_pos() }
    }
    pub fn window_size(&self) -> Vec2 {
        unsafe { ffi::ig_get_window_size() }
    }
    pub fn open_popup(&self, id: &str) {
        let id = c_label(id);
        unsafe { ffi::ig_open_popup(id.as_ptr(), 0) }
    }
    pub fn show_metrics_window(&self, open: &mut bool) {
        // SAFETY: open is valid for this synchronous ImGui call.
        unsafe { ffi::ig_show_metrics_window(open) }
    }
    pub fn show_about_window(&self, open: &mut bool) {
        // SAFETY: open is valid for this synchronous ImGui call.
        unsafe { ffi::ig_show_about_window(open) }
    }
    pub fn begin_popup_modal<'a>(
        &'a self,
        name: &str,
        open: Option<&mut bool>,
        flags: i32,
    ) -> Option<PopupModal<'a>> {
        let name = c_label(name);
        let visible = unsafe {
            ffi::ig_begin_popup_modal(
                name.as_ptr(),
                open.map_or(std::ptr::null_mut(), |v| v),
                flags,
            )
        };
        if visible {
            Some(PopupModal { frame: self })
        } else {
            None
        }
    }
    pub fn close_current_popup(&self) {
        unsafe { ffi::ig_close_current_popup() }
    }
    pub fn begin_group(&self) -> Group<'_> {
        unsafe { ffi::ig_begin_group() };
        Group {
            _frame: PhantomData,
        }
    }
    pub fn text_unformatted(&self, text: &str) {
        unsafe { ffi::ig_text_unformatted_range(text_pointer(text), text.len()) }
    }
    pub fn push_window_padding(&self, value: f32) -> StyleVars<'_> {
        unsafe { ffi::ig_push_style_var(2, Vec2 { x: value, y: value }) };
        StyleVars {
            count: 1,
            _frame: PhantomData,
        }
    }

    pub fn push_style_color_u32(&self, index: i32, color: u32) -> StyleColors<'_> {
        unsafe { ffi::ig_push_style_color_u32(index, color) };
        StyleColors {
            count: 1,
            _frame: PhantomData,
        }
    }
    pub fn text_line_height(&self) -> f32 {
        unsafe { ffi::ig_get_text_line_height() }
    }
    pub fn cursor_pos_x(&self) -> f32 {
        unsafe { ffi::ig_get_cursor_pos_x() }
    }
    pub fn set_cursor_pos_x(&self, x: f32) {
        unsafe { ffi::ig_set_cursor_pos_x(x) }
    }
    pub fn push_window_style(&self) -> StyleVars<'_> {
        unsafe {
            ffi::ig_push_style_var_float(3, 0.0);
            ffi::ig_push_style_var_float(4, 0.0);
            ffi::ig_push_style_var(2, Vec2 { x: 0.0, y: 0.0 })
        }
        StyleVars {
            count: 3,
            _frame: PhantomData,
        }
    }
    pub fn main_viewport(&self) -> ViewportRef<'_> {
        let pointer = unsafe { ffi::ig_get_main_viewport() };
        assert!(!pointer.is_null(), "ImGui main viewport is unavailable");
        ViewportRef {
            pointer,
            _frame: PhantomData,
        }
    }
    pub fn set_next_window_position(&self, pos: Vec2, condition: i32, pivot: Vec2) {
        unsafe { ffi::ig_set_next_window_pos(pos, condition, pivot) }
    }
    pub fn set_next_window_size(&self, size: Vec2, condition: i32) {
        unsafe { ffi::ig_set_next_window_size(size, condition) }
    }
    pub fn setup_default_dock(&self, first_frame: bool) -> u32 {
        let viewport = self.main_viewport();
        let id = unsafe { ffi::ig_dock_space_over_viewport(0, viewport.pointer, 0) };
        if first_frame {
            unsafe {
                ffi::ig_dock_builder_remove_node(id);
                ffi::ig_dock_builder_add_node(id, 1 << 10);
                ffi::ig_dock_builder_set_node_size(id, viewport.size());
                let mut main = id;
                let mut right = 0;
                ffi::ig_dock_builder_split_node(main, 1, 0.30, &mut right, &mut main);
                ffi::ig_dock_builder_dock_window(c"Details".as_ptr(), right);
                let node = ffi::ig_dock_builder_get_node(main);
                if !node.is_null() {
                    ffi::ig_dock_node_add_local_flags(node, (1 << 12) | (1 << 20))
                }
                ffi::ig_dock_builder_dock_window(c"Main Viewport".as_ptr(), main);
                ffi::ig_dock_builder_finish(id)
            }
        }
        id
    }
    pub fn content_available(&self) -> Vec2 {
        unsafe { ffi::ig_get_content_region_avail() }
    }
    pub fn cursor_screen_position(&self) -> Vec2 {
        unsafe { ffi::ig_get_cursor_screen_pos() }
    }
    pub fn set_cursor_screen_position(&self, pos: Vec2) {
        unsafe { ffi::ig_set_cursor_screen_pos(pos) }
    }
    pub fn checkbox(&self, label: &str, value: &mut bool) -> bool {
        let label = c_label(label);
        unsafe { ffi::ig_checkbox(label.as_ptr(), value) }
    }

    pub fn small_button(&self, label: &str) -> bool {
        let label = c_label(label);
        unsafe { ffi::ig_small_button(label.as_ptr()) }
    }

    pub fn input_text(&self, label: &str, value: &mut String) -> bool {
        let label = c_label(label);
        let mut buffer = prepare_input_buffer(value);
        let changed = unsafe {
            ffi::ig_input_text(
                label.as_ptr(),
                buffer.as_mut_ptr().cast(),
                buffer.len(),
                INPUT_TEXT_FLAGS_CALLBACK_RESIZE,
                Some(resize_input_text),
                std::ptr::from_mut(&mut buffer).cast(),
            )
        };
        *value = finish_input_buffer(buffer);
        changed
    }

    pub fn button(&self, label: &str, size: Vec2) -> bool {
        let label = c_label(label);
        unsafe { ffi::ig_button(label.as_ptr(), size) }
    }
    pub fn selectable(&self, label: &str, selected: bool) -> bool {
        self.selectable_flags(label, selected, 0)
    }
    pub fn selectable_flags(&self, label: &str, selected: bool, flags: i32) -> bool {
        let label = c_label(label);
        unsafe { ffi::ig_selectable(label.as_ptr(), selected, flags, Vec2::default()) }
    }
    pub fn begin_table<'a>(
        &'a self,
        id: &str,
        columns: i32,
        flags: i32,
        size: Vec2,
    ) -> Option<Table<'a>> {
        let id = c_label(id);
        unsafe { ffi::ig_begin_table(id.as_ptr(), columns, flags, size, 0.0) }
            .then(|| Table { frame: self })
    }
    pub fn table_setup_column(&self, label: &str, flags: i32, width: f32) {
        let label = c_label(label);
        unsafe { ffi::ig_table_setup_column(label.as_ptr(), flags, width, 0) }
    }
    pub fn table_setup_scroll_freeze(&self, columns: i32, rows: i32) {
        unsafe { ffi::ig_table_setup_scroll_freeze(columns, rows) }
    }
    pub fn table_headers_row(&self) {
        unsafe { ffi::ig_table_headers_row() }
    }
    pub fn table_next_row(&self) {
        unsafe { ffi::ig_table_next_row() }
    }
    pub fn table_next_column(&self) -> bool {
        unsafe { ffi::ig_table_next_column() }
    }
    pub fn table_sort_update(&self) -> Option<TableSort> {
        let specs = unsafe { ffi::ig_table_get_sort_specs() };
        if specs.is_null() || !unsafe { ffi::ig_table_sort_specs_get_dirty(specs) } {
            return None;
        }
        let update = if unsafe { ffi::ig_table_sort_specs_get_count(specs) } == 0 {
            TableSort::Unsorted
        } else {
            let column = unsafe { ffi::ig_table_sort_specs_get_column_index(specs, 0) };
            match unsafe { ffi::ig_table_sort_specs_get_sort_direction(specs, 0) } {
                SORT_DIRECTION_ASCENDING => TableSort::Column {
                    column,
                    ascending: true,
                },
                SORT_DIRECTION_DESCENDING => TableSort::Column {
                    column,
                    ascending: false,
                },
                _ => TableSort::Unsorted,
            }
        };
        unsafe { ffi::ig_table_sort_specs_clear_dirty(specs) };
        Some(update)
    }
    pub fn progress_bar(&self, fraction: f32, size: Vec2, overlay: Option<&str>) {
        let overlay = overlay.map(nul_terminated_prefix);
        unsafe {
            ffi::ig_progress_bar(
                fraction,
                size,
                overlay.as_ref().map_or(std::ptr::null(), |s| s.as_ptr()),
            )
        }
    }
    pub fn foreground_draw_list(&self) -> DrawListRef<'_> {
        DrawListRef {
            pointer: unsafe { ffi::ig_get_foreground_draw_list() },
            _frame: PhantomData,
        }
    }
    pub fn draw_list(&self) -> DrawListRef<'_> {
        DrawListRef {
            pointer: unsafe { ffi::ig_get_window_draw_list() },
            _frame: PhantomData,
        }
    }
    pub fn tooltip(&self, body: impl FnOnce(&Self)) {
        unsafe { ffi::ig_begin_tooltip() };
        let _tooltip = Tooltip {
            _frame: PhantomData,
        };
        body(self);
    }

    pub fn list_clipper(&self, items_count: i32, items_height: f32) -> ListClipper<'_> {
        let pointer = unsafe { ffi::ig_list_clipper_create() };
        assert!(!pointer.is_null(), "failed to create ImGui list clipper");
        unsafe { ffi::ig_list_clipper_begin(pointer, items_count, items_height) };
        ListClipper {
            pointer,
            _frame: PhantomData,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TableSort {
    Unsorted,
    Column { column: i32, ascending: bool },
}

unsafe extern "C" fn resize_input_text(data: *mut c_void) -> i32 {
    let requested = unsafe { ffi::ig_input_text_callback_data_get_buf_size(data) };
    let buffer =
        unsafe { &mut *ffi::ig_input_text_callback_data_get_user_data(data).cast::<Vec<u8>>() };
    buffer.resize(requested.max(1) as usize, 0);
    unsafe { ffi::ig_input_text_callback_data_set_buf(data, buffer.as_mut_ptr().cast()) };
    0
}

impl Drop for Frame<'_> {
    fn drop(&mut self) {
        if !self.ended {
            unsafe { ffi::ig_render() }
        }
        let _ = &mut self.context;
    }
}

pub struct StyleVars<'a> {
    count: i32,
    _frame: PhantomData<&'a Frame<'a>>,
}

impl Drop for StyleVars<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_pop_style_var(self.count) }
    }
}

pub struct StyleColors<'a> {
    count: i32,
    _frame: PhantomData<&'a Frame<'a>>,
}

impl Drop for StyleColors<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_pop_style_color(self.count) }
    }
}

pub struct Group<'a> {
    _frame: PhantomData<&'a Frame<'a>>,
}

impl Drop for Group<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_group() }
    }
}

struct Tooltip<'a> {
    _frame: PhantomData<&'a Frame<'a>>,
}

impl Drop for Tooltip<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_tooltip() }
    }
}

pub struct ViewportRef<'a> {
    pointer: *const Viewport,
    _frame: PhantomData<&'a Frame<'a>>,
}

impl ViewportRef<'_> {
    pub fn size(&self) -> Vec2 {
        unsafe { ffi::ig_viewport_get_size(self.pointer) }
    }

    pub fn center(&self) -> Vec2 {
        unsafe { ffi::ig_viewport_get_center(self.pointer) }
    }
}

pub struct MainMenuBar<'a> {
    frame: &'a Frame<'a>,
}
pub struct Table<'a> {
    frame: &'a Frame<'a>,
}
impl<'a> Table<'a> {
    pub fn frame(&self) -> &'a Frame<'a> {
        self.frame
    }
}
impl Drop for Table<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_table() }
    }
}

pub struct ListClipper<'a> {
    pointer: *mut ListClipperHandle,
    _frame: PhantomData<&'a Frame<'a>>,
}

impl ListClipper<'_> {
    pub fn step(&self) -> bool {
        unsafe { ffi::ig_list_clipper_step(self.pointer) }
    }
    pub fn display_start(&self) -> i32 {
        unsafe { ffi::ig_list_clipper_get_display_start(self.pointer) }
    }
    pub fn display_end(&self) -> i32 {
        unsafe { ffi::ig_list_clipper_get_display_end(self.pointer) }
    }
}
impl Drop for ListClipper<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_list_clipper_destroy(self.pointer) }
    }
}
impl Drop for MainMenuBar<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_main_menu_bar() }
    }
}
impl MainMenuBar<'_> {
    pub fn frame(&self) -> &Frame<'_> {
        self.frame
    }
}
pub struct Menu<'a> {
    frame: &'a Frame<'a>,
}
impl Drop for Menu<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_menu() }
    }
}
impl Menu<'_> {
    pub fn frame(&self) -> &Frame<'_> {
        self.frame
    }
}
pub struct Child<'a> {
    frame: &'a Frame<'a>,
    pub visible: bool,
}
impl Drop for Child<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_child() }
    }
}
impl Child<'_> {
    pub fn frame(&self) -> &Frame<'_> {
        self.frame
    }
}
pub struct Window<'a> {
    frame: &'a Frame<'a>,
    pub visible: bool,
}
impl Drop for Window<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end() }
    }
}
impl Window<'_> {
    pub fn frame(&self) -> &Frame<'_> {
        self.frame
    }
}
pub struct DrawListRef<'a> {
    pointer: *mut DrawList,
    _frame: PhantomData<&'a Frame<'a>>,
}
impl DrawListRef<'_> {
    pub fn disable_anti_aliased_lines(&self) -> DrawListFlags<'_> {
        let previous = unsafe { ffi::ig_draw_list_get_flags(self.pointer) };
        unsafe {
            ffi::ig_draw_list_set_flags(
                self.pointer,
                previous & !DRAW_LIST_FLAGS_ANTI_ALIASED_LINES,
            )
        };
        DrawListFlags {
            pointer: self.pointer,
            previous,
            _draw_list: PhantomData,
        }
    }
    pub fn rect_filled(&self, min: Vec2, max: Vec2, color: u32) {
        // SAFETY: the draw list belongs to the active frame and the values are plain C data.
        unsafe { ffi::ig_draw_list_add_rect_filled(self.pointer, min, max, color) }
    }
    pub fn rect(&self, min: Vec2, max: Vec2, color: u32, thickness: f32) {
        self.rect_rounded(min, max, color, 0.0, thickness)
    }
    pub fn rect_rounded(&self, min: Vec2, max: Vec2, color: u32, rounding: f32, thickness: f32) {
        unsafe { ffi::ig_draw_list_add_rect(self.pointer, min, max, color, rounding, 0, thickness) }
    }
    pub fn text(&self, pos: Vec2, color: u32, text: &str) {
        let (text_begin, text_end) = text_bounds(text);
        unsafe { ffi::ig_draw_list_add_text_simple(self.pointer, pos, color, text_begin, text_end) }
    }

    pub fn text_sized(&self, pos: Vec2, color: u32, font_size: f32, text: &str) {
        let (text_begin, text_end) = text_bounds(text);
        unsafe {
            ffi::ig_draw_list_add_text(
                self.pointer,
                ffi::ig_get_font(),
                font_size,
                pos,
                color,
                text_begin,
                text_end,
                0.0,
                std::ptr::null(),
            )
        }
    }
    pub fn text_clipped(&self, pos: Vec2, color: u32, text: &str, clip: Vec4) {
        let (text_begin, text_end) = text_bounds(text);
        // SAFETY: the byte string and its computed end remain valid for this synchronous call.
        unsafe {
            ffi::ig_draw_list_add_text(
                self.pointer,
                ffi::ig_get_font(),
                ffi::ig_get_font_size(),
                pos,
                color,
                text_begin,
                text_end,
                0.0,
                &clip,
            )
        }
    }
    pub fn line(&self, a: Vec2, b: Vec2, color: u32, thickness: f32) {
        unsafe { ffi::ig_draw_list_add_line(self.pointer, a, b, color, thickness) }
    }
}

pub struct DrawListFlags<'a> {
    pointer: *mut DrawList,
    previous: i32,
    _draw_list: PhantomData<&'a DrawListRef<'a>>,
}

impl Drop for DrawListFlags<'_> {
    fn drop(&mut self) {
        unsafe { ffi::ig_draw_list_set_flags(self.pointer, self.previous) }
    }
}

pub struct PopupModal<'a> {
    pub frame: &'a Frame<'a>,
}
impl<'a> Drop for PopupModal<'a> {
    fn drop(&mut self) {
        unsafe { ffi::ig_end_popup() }
    }
}

#[allow(dead_code)]
mod ffi {
    use super::*;
    unsafe extern "C" {
        pub fn ig_create_context() -> bool;
        pub fn ig_destroy_context();
        pub fn ig_set_allocator_functions(
            alloc: Option<unsafe extern "C" fn(usize, *mut c_void) -> *mut c_void>,
            free: Option<unsafe extern "C" fn(*mut c_void, *mut c_void)>,
            user_data: *mut c_void,
        );
        pub fn ig_io_add_config_flags(flags: i32);
        pub fn ig_io_add_mouse_pos_event(x: f32, y: f32);
        pub fn ig_io_add_mouse_button_event(button: i32, down: bool);
        pub fn ig_io_add_mouse_wheel_event(horizontal: f32, vertical: f32);
        pub fn ig_io_add_key_event(key: i32, down: bool);
        pub fn ig_io_add_input_characters_utf8(text: *const c_char);
        pub fn ig_io_set_display_size(size: Vec2);
        pub fn ig_io_set_delta_time(dt: f32);
        pub fn ig_set_font_data(data: *const c_void, size: i32, dpi: f32);
        pub fn ig_new_frame();
        pub fn ig_render();
        pub fn ig_get_draw_data() -> *mut DrawData;
        pub fn ig_style_apply_theme(theme: *const Theme);
        pub fn ig_text_unformatted_range(text: *const c_char, length: usize);
        pub fn ig_text_wrapped_range(text: *const c_char, length: usize);
        pub fn ig_text_colored_range(color: Vec4, text: *const c_char, length: usize);
        pub fn ig_text_disabled_range(text: *const c_char, length: usize);
        pub fn ig_calc_text_size_range(text: *const c_char, length: usize) -> Vec2;
        pub fn ig_begin_main_menu_bar() -> bool;
        pub fn ig_end_main_menu_bar();
        pub fn ig_begin_menu(label: *const c_char, enabled: bool) -> bool;
        pub fn ig_end_menu();
        pub fn ig_menu_item(
            label: *const c_char,
            shortcut: *const c_char,
            selected: bool,
            enabled: bool,
        ) -> bool;
        pub fn ig_menu_item_ptr(
            label: *const c_char,
            shortcut: *const c_char,
            selected: *mut bool,
            enabled: bool,
        ) -> bool;
        pub fn ig_separator();
        pub fn ig_spacing();
        pub fn ig_same_line(offset: f32, spacing: f32);
        pub fn ig_get_window_size() -> Vec2;
        pub fn ig_push_style_var_float(index: i32, value: f32);
        pub fn ig_push_style_var(index: i32, value: Vec2);
        pub fn ig_pop_style_var(count: i32);
        pub fn ig_push_style_color_u32(index: i32, color: u32);
        pub fn ig_pop_style_color(count: i32);
        pub fn ig_get_text_line_height() -> f32;
        pub fn ig_get_cursor_pos_x() -> f32;
        pub fn ig_set_cursor_pos_x(x: f32);
        pub fn ig_get_main_viewport() -> *const Viewport;
        pub fn ig_viewport_get_size(viewport: *const Viewport) -> Vec2;
        pub fn ig_viewport_get_center(viewport: *const Viewport) -> Vec2;
        pub fn ig_set_next_window_pos(pos: Vec2, condition: i32, pivot: Vec2);
        pub fn ig_set_next_window_size(size: Vec2, condition: i32);
        pub fn ig_dock_space_over_viewport(id: u32, viewport: *const Viewport, flags: i32) -> u32;
        pub fn ig_dock_builder_remove_node(id: u32);
        pub fn ig_dock_builder_add_node(id: u32, flags: i32);
        pub fn ig_dock_builder_set_node_size(id: u32, size: Vec2);
        pub fn ig_dock_builder_split_node(
            id: u32,
            direction: i32,
            ratio: f32,
            at_direction: *mut u32,
            opposite: *mut u32,
        ) -> u32;
        pub fn ig_dock_builder_dock_window(name: *const c_char, id: u32);
        pub fn ig_dock_builder_finish(id: u32);
        pub fn ig_dock_builder_get_node(id: u32) -> *mut DockNode;
        pub fn ig_dock_node_add_local_flags(node: *mut DockNode, flags: i32);
        pub fn ig_begin(name: *const c_char, open: *mut bool, flags: i32) -> bool;
        pub fn ig_begin_child(name: *const c_char, size: Vec2, border: bool, flags: i32) -> bool;
        pub fn ig_end_child();
        pub fn ig_get_frame_height() -> f32;
        pub fn ig_get_io_mouse_pos() -> Vec2;
        pub fn ig_get_io_mouse_clicked_pos(button: i32) -> Vec2;
        pub fn ig_get_io_mouse_delta() -> Vec2;
        pub fn ig_get_mouse_drag_delta(button: i32, threshold: f32) -> Vec2;
        pub fn ig_get_io_mouse_wheel() -> f32;
        pub fn ig_is_mouse_down(button: i32) -> bool;
        pub fn ig_is_mouse_clicked(button: i32, repeat: bool) -> bool;
        pub fn ig_is_mouse_double_clicked(button: i32) -> bool;
        pub fn ig_is_mouse_released(button: i32) -> bool;
        pub fn ig_is_mouse_dragging(button: i32, threshold: f32) -> bool;
        pub fn ig_is_mouse_hovering_rect(minimum: Vec2, maximum: Vec2, clip: bool) -> bool;
        pub fn ig_set_mouse_cursor(cursor: i32);
        pub fn ig_get_io_mouse_drag_threshold() -> f32;
        pub fn ig_get_io_key_ctrl() -> bool;
        pub fn ig_get_io_key_shift() -> bool;
        pub fn ig_is_key_down(key: i32) -> bool;
        pub fn ig_invisible_button(label: *const c_char, size: Vec2) -> bool;
        pub fn ig_is_item_active() -> bool;
        pub fn ig_is_item_hovered() -> bool;
        pub fn ig_is_item_focused() -> bool;
        pub fn ig_is_item_activated() -> bool;
        pub fn ig_is_item_deactivated() -> bool;
        pub fn ig_is_window_hovered(flags: i32) -> bool;
        pub fn ig_set_keyboard_focus_here(offset: i32);
        pub fn ig_is_key_pressed(key: i32, repeat: bool) -> bool;
        pub fn ig_get_io_want_text_input() -> bool;
        pub fn ig_set_clipboard_text(text: *const c_char);
        pub fn ig_get_scroll_y() -> f32;
        pub fn ig_get_font_size() -> f32;
        pub fn ig_set_cursor_pos(pos: Vec2);
        pub fn ig_dummy(size: Vec2);
        pub fn ig_end();
        pub fn ig_get_content_region_avail() -> Vec2;
        pub fn ig_get_cursor_screen_pos() -> Vec2;
        pub fn ig_set_cursor_screen_pos(pos: Vec2);
        pub fn ig_button(label: *const c_char, size: Vec2) -> bool;
        pub fn ig_selectable(label: *const c_char, selected: bool, flags: i32, size: Vec2) -> bool;
        pub fn ig_begin_table(
            id: *const c_char,
            columns: i32,
            flags: i32,
            size: Vec2,
            inner_width: f32,
        ) -> bool;
        pub fn ig_end_table();
        pub fn ig_table_setup_column(label: *const c_char, flags: i32, width: f32, user_id: u32);
        pub fn ig_table_setup_scroll_freeze(columns: i32, rows: i32);
        pub fn ig_table_headers_row();
        pub fn ig_table_next_row();
        pub fn ig_table_next_column() -> bool;
        pub fn ig_table_get_sort_specs() -> *mut TableSortSpecs;
        pub fn ig_table_sort_specs_get_dirty(specs: *const TableSortSpecs) -> bool;
        pub fn ig_table_sort_specs_clear_dirty(specs: *mut TableSortSpecs);
        pub fn ig_table_sort_specs_get_count(specs: *const TableSortSpecs) -> i32;
        pub fn ig_table_sort_specs_get_column_index(
            specs: *const TableSortSpecs,
            index: i32,
        ) -> i32;
        pub fn ig_table_sort_specs_get_sort_direction(
            specs: *const TableSortSpecs,
            index: i32,
        ) -> i32;
        pub fn ig_list_clipper_create() -> *mut ListClipperHandle;
        pub fn ig_list_clipper_destroy(clipper: *mut ListClipperHandle);
        pub fn ig_list_clipper_begin(
            clipper: *mut ListClipperHandle,
            items_count: i32,
            items_height: f32,
        );
        pub fn ig_list_clipper_step(clipper: *mut ListClipperHandle) -> bool;
        pub fn ig_list_clipper_get_display_start(clipper: *const ListClipperHandle) -> i32;
        pub fn ig_list_clipper_get_display_end(clipper: *const ListClipperHandle) -> i32;
        pub fn ig_small_button(label: *const c_char) -> bool;
        pub fn ig_checkbox(label: *const c_char, value: *mut bool) -> bool;
        pub fn ig_input_text(
            label: *const c_char,
            buffer: *mut c_char,
            size: usize,
            flags: i32,
            callback: Option<unsafe extern "C" fn(*mut c_void) -> i32>,
            user_data: *mut c_void,
        ) -> bool;
        pub fn ig_input_text_callback_data_get_user_data(data: *mut c_void) -> *mut c_void;
        pub fn ig_input_text_callback_data_get_buf_size(data: *const c_void) -> i32;
        pub fn ig_input_text_callback_data_set_buf(data: *mut c_void, buffer: *mut c_char);
        pub fn ig_progress_bar(fraction: f32, size: Vec2, overlay: *const c_char);
        pub fn ig_get_window_draw_list() -> *mut DrawList;
        pub fn ig_begin_tooltip();
        pub fn ig_end_tooltip();
        pub fn ig_get_foreground_draw_list() -> *mut DrawList;
        pub fn ig_draw_list_get_flags(list: *const DrawList) -> i32;
        pub fn ig_draw_list_set_flags(list: *mut DrawList, flags: i32);
        pub fn ig_draw_list_add_rect_filled(list: *mut DrawList, min: Vec2, max: Vec2, color: u32);
        pub fn ig_draw_list_add_rect(
            list: *mut DrawList,
            min: Vec2,
            max: Vec2,
            color: u32,
            rounding: f32,
            flags: i32,
            thickness: f32,
        );
        pub fn ig_get_font() -> *mut c_void;
        pub fn ig_draw_list_add_text(
            list: *mut DrawList,
            font: *mut c_void,
            font_size: f32,
            pos: Vec2,
            color: u32,
            begin: *const c_char,
            end: *const c_char,
            wrap_width: f32,
            clip: *const Vec4,
        );
        pub fn ig_draw_list_add_text_simple(
            list: *mut DrawList,
            pos: Vec2,
            color: u32,
            begin: *const c_char,
            end: *const c_char,
        );
        pub fn ig_draw_list_add_line(
            list: *mut DrawList,
            a: Vec2,
            b: Vec2,
            color: u32,
            thickness: f32,
        );
        pub fn ig_color_convert_u32_to_float4(color: u32) -> Vec4;
        pub fn ig_get_window_pos() -> Vec2;
        pub fn ig_open_popup(str_id: *const c_char, flags: i32);
        pub fn ig_begin_popup_modal(name: *const c_char, open: *mut bool, flags: i32) -> bool;
        pub fn ig_end_popup();
        pub fn ig_close_current_popup();
        pub fn ig_show_metrics_window(open: *mut bool);
        pub fn ig_show_about_window(open: *mut bool);
        pub fn ig_set_scroll_y(scroll_y: f32);
        pub fn ig_begin_group();
        pub fn ig_end_group();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::{align_of, size_of};

    #[test]
    fn vector_types_match_the_c_abi() {
        assert_eq!(align_of::<Vec2>(), 4);
        assert_eq!(size_of::<Vec2>(), 8);
        assert_eq!(align_of::<Vec4>(), 4);
        assert_eq!(size_of::<Vec4>(), 16);
    }

    #[test]
    fn labels_truncate_nul_but_preserve_hidden_ids() {
        assert_eq!(c_label("visible\0ignored").to_bytes(), b"visible");
        assert_eq!(
            c_label("visible\0ignored##stable").to_bytes(),
            b"visible##stable"
        );
        assert_eq!(c_label("visible##stable").to_bytes(), b"visible##stable");
        assert_eq!(
            nul_terminated_prefix("visible\0ignored").to_bytes(),
            b"visible"
        );
    }

    #[test]
    fn empty_text_has_valid_equal_range_pointers() {
        let (begin, end) = text_bounds("");
        assert!(!begin.is_null());
        assert_eq!(begin, end);
    }

    #[test]
    fn input_buffers_are_terminated_and_restore_strings() {
        let mut value = "search text".to_owned();
        let mut buffer = prepare_input_buffer(&mut value);
        assert!(value.is_empty());
        assert_eq!(&buffer[..11], b"search text");
        assert_eq!(buffer[11], 0);
        assert!(buffer.len() >= 128);

        buffer[..7].copy_from_slice(b"changed");
        buffer[7] = 0;
        assert_eq!(finish_input_buffer(buffer), "changed");
    }

    #[test]
    fn invalid_input_bytes_are_repaired_before_forming_a_string() {
        assert_eq!(finish_input_buffer(vec![b'a', 0xff, b'b', 0]), "a\u{fffd}b");
    }
}
