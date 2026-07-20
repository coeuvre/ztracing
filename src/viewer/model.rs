use crate::format;
use crate::imgui::{
    HOVER_PROPERTIES_TABLE_FLAGS, MOD_SUPER, MOUSE_CURSOR_RESIZE_EW,
    TABLE_COLUMN_FLAGS_WIDTH_FIXED, WINDOW_FLAGS_NO_MOVE, WINDOW_FLAGS_NO_SCROLL_WITH_MOUSE,
    WINDOW_FLAGS_NO_SCROLLBAR,
};
use crate::trace::data::TraceData;
use crate::trace::heatmap::BUCKET_COUNT;
use crate::trace::histogram::Histogram;
use crate::trace::track::{Track, TrackType};
use crate::viewer::renderer::{CounterBlocks, RenderBlock, State as TrackRenderer};
use std::sync::Arc;

const MAX_ZOOM_FACTOR: f64 = 1.2;
const MIN_ZOOM_DURATION: f64 = 1_000.0;
const VERTICAL_MINIMAP_WIDTH: f32 = 64.0;
const VERTICAL_MINIMAP_LANE_HEIGHT: f32 = 1.0;

fn counter_visual_height(value: f64, maximum: f64, height: f32) -> f32 {
    let minimum = maximum / f64::from(height);
    (value.max(minimum) / maximum * f64::from(height)) as f32
}

fn update_counter_visual_offsets(
    offsets: &mut Vec<f32>,
    blocks: CounterBlocks<'_>,
    series_count: usize,
    maximum: f64,
    height: f32,
) {
    offsets.clear();
    offsets.resize(blocks.len() * (series_count + 1), 0.0);
    for (block_index, _) in blocks.iter().enumerate() {
        let base = block_index * (series_count + 1);
        let mut accumulated = 0.0;
        for (series, value) in blocks.peaks(block_index).iter().enumerate() {
            accumulated += counter_visual_height(*value, maximum, height);
            offsets[base + series + 1] = accumulated;
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
struct SelectionProximity {
    near_start: bool,
    near_end: bool,
}

fn selection_proximity(
    active: bool,
    start: f64,
    end: f64,
    mouse: f64,
    threshold: f64,
) -> SelectionProximity {
    if !active {
        return SelectionProximity::default();
    }
    let start_distance = (mouse - start).abs();
    let end_distance = (mouse - end).abs();
    let mut result = SelectionProximity {
        near_start: start_distance < threshold,
        near_end: end_distance < threshold,
    };
    if result.near_start && result.near_end {
        if start_distance < end_distance {
            result.near_end = false;
        } else {
            result.near_start = false;
        }
    }
    result
}

#[derive(Clone, Copy, Debug, Default)]
struct Viewport {
    minimum: i64,
    maximum: i64,
    start: f64,
    end: f64,
}
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum DragMode {
    #[default]
    None,
    RulerNew,
    RulerStart,
    RulerEnd,
    TracksStart,
    TracksEnd,
    BoxSelect,
}
#[derive(Clone, Debug, Default)]
struct Input {
    canvas_x: f32,
    canvas_y: f32,
    canvas_width: f32,
    canvas_height: f32,
    ruler_height: f32,
    lane_height: f32,
    tracks_scroll_y: f32,
    viewport_x: f32,
    viewport_y: f32,
    viewport_width: f32,
    viewport_height: f32,
    mouse_x: f32,
    mouse_y: f32,
    mouse_wheel: f32,
    click_x: f32,
    mouse_delta_x: f32,
    drag_delta_x: f32,
    drag_delta_y: f32,
    drag_threshold: f32,
    mouse_down: bool,
    mouse_clicked: bool,
    mouse_double_clicked: bool,
    mouse_released: bool,
    ctrl_down: bool,
    shift_down: bool,
    ruler_active: bool,
    ruler_activated: bool,
    ruler_deactivated: bool,
    tracks_hovered: bool,
}
#[derive(Clone, Debug, Default)]
struct TrackInfo {
    y: f32,
    relative_y: f32,
    height: f32,
    visible: bool,
    name: String,
}
#[derive(Clone, Debug)]
struct HoverMatch {
    track_index: usize,
    block_index: usize,
    y1: f32,
    y2: f32,
    block: RenderBlock,
}
#[derive(Clone, Debug)]
struct RulerTick {
    x: f32,
    label: String,
}
#[derive(Clone, Debug, Default)]
struct SelectionLayout {
    active: bool,
    x1: f32,
    x2: f32,
    duration_label: String,
}

#[derive(Clone, Copy, Debug, Default)]
struct MinimapLayout {
    active: bool,
    x: f32,
    y: f32,
    height: f32,
    minimap_scroll_y: f32,
    slider_y1: f32,
    slider_y2: f32,
    hovered: bool,
}

impl MinimapLayout {
    fn project_y(&self, relative_y: f32, scale: f32) -> f32 {
        self.y + relative_y * scale - self.minimap_scroll_y
    }
}

#[derive(Default)]
pub struct Viewer {
    viewport: Viewport,
    selection_active: bool,
    selection_start: f64,
    selection_end: f64,
    drag_mode: DragMode,
    box_start: (f32, f32),
    box_end: (f32, f32),
    tracks: Vec<Track>,
    track_info: Vec<TrackInfo>,
    ruler_ticks: Vec<RulerTick>,
    selection_layout: SelectionLayout,
    total_tracks_height: f32,
    hover_matches: Vec<HoverMatch>,
    focused_event: Option<usize>,
    selected_events: Arc<Vec<i64>>,
    target_focused_event: Option<usize>,
    target_scroll_y: Option<f32>,
    show_details: bool,
    ignore_next_release: bool,
    last_inner_width: f32,
    last_inner_height: f32,
    last_tracks_x: f32,
    last_tracks_y: f32,
    last_lane_height: f32,
    last_tracks_scroll_y: f32,
    filtered_events: Vec<i64>,
    histogram: Histogram,
    renderer: TrackRenderer,
    counter_visual_offsets: Vec<f32>,
    renderer_selection: Arc<Vec<i64>>,
    track_has_selected: Vec<bool>,
    minimap_heatmaps: Vec<[Option<usize>; BUCKET_COUNT]>,
    request_scroll_to_focused_event: bool,
    minimap_dragging: bool,
    minimap_drag_offset_y: f32,
    minimap_layout: MinimapLayout,
    snap_line: Option<(f32, f32, f32)>,
    box_selection_changed: bool,
}

impl Viewer {
    pub fn draw(
        &mut self,
        data: &TraceData,
        frame: &crate::imgui::Frame<'_>,
        theme: &crate::colors::Theme,
    ) {
        if self.minimap_heatmaps.len() != self.tracks.len() {
            self.minimap_heatmaps = crate::trace::heatmap::compute(
                &self.tracks,
                data,
                self.viewport.minimum,
                self.viewport.maximum,
            );
        }
        let position = frame.cursor_screen_position();
        let available = frame.content_available();
        let ruler_height = frame.frame_height();
        let tracks_width = (available.x - VERTICAL_MINIMAP_WIDTH).max(1.0);
        let primary_modifier =
            crate::platform::primary_modifier_down(frame.ctrl_down(), frame.key_down(MOD_SUPER));
        let draw = frame.draw_list();
        draw.rect_filled(
            position,
            crate::imgui::Vec2 {
                x: position.x + available.x,
                y: position.y + available.y,
            },
            theme.viewport_bg,
        );
        frame.set_cursor_screen_position(position);
        frame.invisible_button(
            "##Ruler",
            crate::imgui::Vec2 {
                x: tracks_width,
                y: ruler_height,
            },
        );
        let ruler_active = frame.item_active();
        let ruler_activated = frame.item_activated();
        let ruler_deactivated = frame.item_deactivated();
        frame.set_cursor_screen_position(crate::imgui::Vec2 {
            x: position.x,
            y: position.y + ruler_height,
        });
        let mut child_flags = WINDOW_FLAGS_NO_MOVE | WINDOW_FLAGS_NO_SCROLLBAR;
        if primary_modifier {
            child_flags |= WINDOW_FLAGS_NO_SCROLL_WITH_MOUSE;
        }
        if let Some(child) = frame.begin_child(
            "TrackList",
            crate::imgui::Vec2 {
                x: tracks_width,
                y: available.y - ruler_height,
            },
            false,
            child_flags,
        ) {
            if child.visible {
                let child_frame = child.frame();
                let tracks_position = child_frame.cursor_screen_position();
                let mouse = frame.mouse_position();
                let click = frame.mouse_clicked_position();
                let delta = frame.mouse_delta();
                let drag = frame.mouse_drag_delta();
                let input = Input {
                    canvas_x: tracks_position.x,
                    canvas_y: position.y,
                    canvas_width: tracks_width,
                    canvas_height: available.y,
                    ruler_height,
                    lane_height: ruler_height,
                    tracks_scroll_y: child_frame.scroll_y(),
                    viewport_x: position.x,
                    viewport_y: position.y,
                    viewport_width: available.x,
                    viewport_height: available.y,
                    mouse_x: mouse.x,
                    mouse_y: mouse.y,
                    click_x: click.x,
                    mouse_wheel: frame.mouse_wheel(),
                    mouse_delta_x: delta.x,
                    drag_delta_x: drag.x,
                    drag_delta_y: drag.y,
                    drag_threshold: frame.drag_threshold(),
                    mouse_down: frame.mouse_down(),
                    mouse_clicked: frame.mouse_clicked(),
                    mouse_double_clicked: frame.mouse_double_clicked(),
                    mouse_released: frame.mouse_released(),
                    ctrl_down: primary_modifier,
                    shift_down: frame.shift_down(),
                    ruler_active,
                    ruler_activated,
                    ruler_deactivated,
                    tracks_hovered: child_frame.window_hovered(),
                };
                self.step(data, &input);
                let duration = (self.viewport.end - self.viewport.start).max(1.0);
                let mouse_timestamp = self.px_to_ts(tracks_width, tracks_position.x, mouse.x);
                let proximity = selection_proximity(
                    self.selection_active,
                    self.selection_start,
                    self.selection_end,
                    mouse_timestamp,
                    5.0 / f64::from(tracks_width) * duration,
                );
                if self.drag_mode != DragMode::BoxSelect
                    && ((self.drag_mode != DragMode::None && self.drag_mode != DragMode::RulerNew)
                        || proximity.near_start
                        || proximity.near_end)
                {
                    frame.set_mouse_cursor(MOUSE_CURSOR_RESIZE_EW);
                }
                if let Some(target_scroll) = self.target_scroll_y.take() {
                    child_frame.set_scroll_y(target_scroll.max(0.0));
                }
                if child_frame.window_hovered()
                    && frame.mouse_dragging()
                    && self.drag_mode == DragMode::None
                {
                    child_frame.set_scroll_y((child_frame.scroll_y() - delta.y).max(0.0));
                }
                child_frame.dummy(crate::imgui::Vec2 {
                    x: 0.0,
                    y: self.total_tracks_height,
                });
                child_frame.set_cursor_position(crate::imgui::Vec2::default());
                let list = child_frame.draw_list();
                for (index, track) in self.tracks.iter().enumerate() {
                    let info = &self.track_info[index];
                    if !info.visible {
                        continue;
                    }
                    let top = crate::imgui::Vec2 {
                        x: tracks_position.x,
                        y: info.y,
                    };
                    list.rect_filled(
                        top,
                        crate::imgui::Vec2 {
                            x: top.x + tracks_width,
                            y: top.y + info.height,
                        },
                        theme.track_bg,
                    );
                    list.rect_filled(
                        top,
                        crate::imgui::Vec2 {
                            x: top.x + tracks_width,
                            y: top.y + ruler_height,
                        },
                        theme.track_header_bg,
                    );
                    list.line(
                        crate::imgui::Vec2 {
                            x: top.x,
                            y: top.y + ruler_height - 1.0,
                        },
                        crate::imgui::Vec2 {
                            x: top.x + tracks_width,
                            y: top.y + ruler_height - 1.0,
                        },
                        theme.track_separator,
                        1.0,
                    );
                    let header_text_position = crate::imgui::Vec2 {
                        x: top.x + 5.0,
                        y: top.y + (ruler_height - frame.font_size()) * 0.5,
                    };
                    list.text(header_text_position, theme.track_text, &info.name);
                    let header_text_size = frame.calc_text_size(&info.name);
                    let mouse_timestamp = self.px_to_ts(tracks_width, tracks_position.x, mouse.x);
                    let mouse_inside_selection = !self.selection_active
                        || (mouse_timestamp >= self.selection_start.min(self.selection_end)
                            && mouse_timestamp <= self.selection_start.max(self.selection_end));
                    if mouse_inside_selection
                        && frame.mouse_hovering_rect(
                            header_text_position,
                            crate::imgui::Vec2 {
                                x: header_text_position.x + header_text_size.x,
                                y: header_text_position.y + header_text_size.y,
                            },
                            true,
                        )
                    {
                        let _tooltip_padding = child_frame.push_window_padding(10.0);
                        child_frame.tooltip(|tooltip| {
                            tooltip.text(&format!("PID: {}", track.process_id));
                            if track.kind == TrackType::Thread {
                                tooltip.text(&format!("TID: {}", track.thread_id));
                            }
                        });
                    }
                    if track.kind == TrackType::Thread {
                        for block in self.renderer.thread_blocks(
                            track,
                            data,
                            self.viewport.start,
                            self.viewport.end,
                            tracks_width,
                            tracks_position.x,
                            self.focused_event,
                        ) {
                            let y1 = top.y + (block.depth + 1) as f32 * ruler_height;
                            let y2 = y1 + ruler_height - 1.0;
                            let color = theme.event_palette
                                [block.palette_index as usize % theme.event_palette.len()];
                            Self::draw_thread_event(
                                &list,
                                frame,
                                data,
                                theme,
                                &block,
                                color,
                                y1,
                                y2,
                                tracks_position.x,
                                tracks_width,
                                ruler_height,
                            );
                        }
                    } else if track.kind == TrackType::Counter {
                        let content_y = top.y + ruler_height;
                        let content_h = (info.height - ruler_height).max(1.0);
                        let baseline = top.y + info.height;
                        let max_total = track.counter_max_total.max(1.0);
                        let blocks = self.renderer.counter_blocks(
                            track,
                            data,
                            self.viewport.start,
                            self.viewport.end,
                            tracks_width,
                            tracks_position.x,
                            self.focused_event,
                        );
                        let series_count = track.counter_series.len();
                        update_counter_visual_offsets(
                            &mut self.counter_visual_offsets,
                            blocks,
                            series_count,
                            max_total,
                            content_h,
                        );
                        let hovered_block = self
                            .hover_matches
                            .last()
                            .filter(|hover| hover.track_index == index)
                            .map(|hover| hover.block_index);
                        for (block_index, block) in blocks.iter().enumerate() {
                            let base = block_index * (series_count + 1);
                            for series in 0..series_count {
                                let y2 = baseline - self.counter_visual_offsets[base + series];
                                let y1 = baseline - self.counter_visual_offsets[base + series + 1];
                                let color = theme.event_palette[track.counter_palette_indices
                                    [series]
                                    as usize
                                    % theme.event_palette.len()];
                                list.rect_filled(
                                    crate::imgui::Vec2 { x: block.x1, y: y1 },
                                    crate::imgui::Vec2 { x: block.x2, y: y2 },
                                    color,
                                );
                            }
                            if hovered_block == Some(block_index) {
                                let background = theme.viewport_bg;
                                let red = (background & 255) as f32 / 255.0;
                                let green = ((background >> 8) & 255) as f32 / 255.0;
                                let blue = ((background >> 16) & 255) as f32 / 255.0;
                                let dark = red * 0.299 + green * 0.587 + blue * 0.114 < 0.5;
                                list.rect_filled(
                                    crate::imgui::Vec2 {
                                        x: block.x1,
                                        y: content_y,
                                    },
                                    crate::imgui::Vec2 {
                                        x: block.x2,
                                        y: content_y + content_h,
                                    },
                                    if dark { 0x1eff_ffff } else { 0x0f00_0000 },
                                );
                            }
                            if block.focused {
                                list.rect_filled(
                                    crate::imgui::Vec2 {
                                        x: block.x1,
                                        y: content_y,
                                    },
                                    crate::imgui::Vec2 {
                                        x: block.x2,
                                        y: content_y + content_h,
                                    },
                                    theme.event_focused_bg,
                                );
                            }
                        }
                        let _line_style = list.disable_anti_aliased_lines();
                        for series in 0..track.counter_series.len() {
                            let mut previous = None;
                            for (block_index, block) in blocks.iter().enumerate() {
                                let y = baseline
                                    - self.counter_visual_offsets
                                        [block_index * (series_count + 1) + series + 1];
                                let line_col = if block.focused {
                                    theme.event_border_focused
                                } else if block.selected {
                                    theme.event_border_selected
                                } else {
                                    theme.event_border
                                };
                                let thickness = if block.focused { 3.0 } else { 1.0 };
                                list.line(
                                    crate::imgui::Vec2 { x: block.x1, y },
                                    crate::imgui::Vec2 { x: block.x2, y },
                                    line_col,
                                    thickness,
                                );
                                if let Some(previous_y) = previous {
                                    if y != previous_y {
                                        list.line(
                                            crate::imgui::Vec2 {
                                                x: block.x1,
                                                y: previous_y,
                                            },
                                            crate::imgui::Vec2 { x: block.x1, y },
                                            theme.event_border,
                                            1.0,
                                        )
                                    }
                                }
                                previous = Some(y)
                            }
                        }
                    }
                }
                if let Some(hover) = self.hover_matches.last() {
                    let track = &self.tracks[hover.track_index];
                    if track.kind == TrackType::Thread && !hover.block.selected {
                        let mut color = theme.event_palette
                            [hover.block.palette_index as usize % theme.event_palette.len()];
                        let bg = theme.viewport_bg;
                        let r = (bg & 255) as f32 / 255.0;
                        let g = ((bg >> 8) & 255) as f32 / 255.0;
                        let b = ((bg >> 16) & 255) as f32 / 255.0;
                        let is_dark = r * 0.299 + g * 0.587 + b * 0.114 < 0.5;
                        let mut cr = (color & 255) as f32 / 255.0;
                        let mut cg = ((color >> 8) & 255) as f32 / 255.0;
                        let mut cb = ((color >> 16) & 255) as f32 / 255.0;
                        if is_dark {
                            cr = (cr + 0.15).min(1.0);
                            cg = (cg + 0.15).min(1.0);
                            cb = (cb + 0.15).min(1.0);
                        } else {
                            cr = (cr - 0.15).max(0.0);
                            cg = (cg - 0.15).max(0.0);
                            cb = (cb - 0.15).max(0.0);
                        }
                        color = ((cb * 255.0) as u32) << 16
                            | ((cg * 255.0) as u32) << 8
                            | (cr * 255.0) as u32
                            | 0xff000000;
                        Self::draw_thread_event(
                            &list,
                            frame,
                            data,
                            theme,
                            &hover.block,
                            color,
                            hover.y1,
                            hover.y2,
                            tracks_position.x,
                            tracks_width,
                            ruler_height,
                        );
                    } else if track.kind == TrackType::Counter {
                        // Counter hover overlay is drawn under step lines above.
                    }
                    let _tooltip_padding = child_frame.push_window_padding(10.0);
                    child_frame.tooltip(|tooltip| {
                        if hover.block.count == 1 {
                            Self::draw_hover_event_properties(
                                tooltip,
                                data,
                                self,
                                theme,
                                track,
                                hover.block.event_index,
                            );
                        } else if hover.block.count > 1 {
                            tooltip.text(&format!("{} merged events", hover.block.count));
                            let ts1 =
                                self.px_to_ts(tracks_width, tracks_position.x, hover.block.x1);
                            let ts2 =
                                self.px_to_ts(tracks_width, tracks_position.x, hover.block.x2);
                            tooltip.text(&format!(
                                "Duration: {}",
                                crate::format::duration(ts2 - ts1, 0.0)
                            ));
                        }
                    });
                }
                if self.drag_mode == DragMode::BoxSelect {
                    let (x1, x2) = (
                        self.box_start.0.min(self.box_end.0),
                        self.box_start.0.max(self.box_end.0),
                    );
                    let (y1, y2) = (
                        self.box_start.1.min(self.box_end.1),
                        self.box_start.1.max(self.box_end.1),
                    );
                    list.rect_filled(
                        crate::imgui::Vec2 { x: x1, y: y1 },
                        crate::imgui::Vec2 { x: x2, y: y2 },
                        theme.box_selection_bg,
                    );
                    list.rect(
                        crate::imgui::Vec2 { x: x1, y: y1 },
                        crate::imgui::Vec2 { x: x2, y: y2 },
                        theme.box_selection_border,
                        1.0,
                    );
                }
                if let Some((x, y1, y2)) = self.snap_line {
                    list.line(
                        crate::imgui::Vec2 { x, y: y1 },
                        crate::imgui::Vec2 { x, y: y2 },
                        0xff00_00ff,
                        3.0,
                    );
                }
                let window_pos = child_frame.window_position();
                let window_size = child_frame.window_size();
                Self::draw_selection_overlay(
                    &list,
                    child_frame,
                    theme,
                    &self.selection_layout,
                    tracks_position.x,
                    window_pos.y,
                    tracks_width,
                    window_size.y,
                    true,
                );
            }
        }
        let ruler = frame.draw_list();
        ruler.rect_filled(
            position,
            crate::imgui::Vec2 {
                x: position.x + available.x,
                y: position.y + ruler_height,
            },
            theme.ruler_bg,
        );
        ruler.line(
            crate::imgui::Vec2 {
                x: position.x,
                y: position.y + ruler_height - 1.0,
            },
            crate::imgui::Vec2 {
                x: position.x + available.x,
                y: position.y + ruler_height - 1.0,
            },
            theme.ruler_border,
            1.0,
        );
        for tick in &self.ruler_ticks {
            ruler.line(
                crate::imgui::Vec2 {
                    x: tick.x,
                    y: position.y + ruler_height * 0.6,
                },
                crate::imgui::Vec2 {
                    x: tick.x,
                    y: position.y + ruler_height - 1.0,
                },
                theme.ruler_tick,
                1.0,
            );
            ruler.text(
                crate::imgui::Vec2 {
                    x: tick.x + 3.0,
                    y: position.y + 2.0,
                },
                theme.ruler_text,
                &tick.label,
            )
        }
        let mini_x = self.minimap_layout.x;
        let minimap_height = self.minimap_layout.height;
        let minimap_y = self.minimap_layout.y;
        ruler.rect_filled(
            crate::imgui::Vec2 {
                x: mini_x,
                y: minimap_y,
            },
            crate::imgui::Vec2 {
                x: position.x + available.x,
                y: minimap_y + minimap_height,
            },
            theme.vertical_minimap_bg,
        );
        ruler.line(
            crate::imgui::Vec2 {
                x: mini_x,
                y: minimap_y,
            },
            crate::imgui::Vec2 {
                x: mini_x,
                y: minimap_y + minimap_height,
            },
            theme.track_separator,
            1.0,
        );
        let scale = VERTICAL_MINIMAP_LANE_HEIGHT / ruler_height.max(1.0);
        let cell_width = (VERTICAL_MINIMAP_WIDTH - 2.0) / BUCKET_COUNT as f32;
        for (index, heatmap) in self.minimap_heatmaps.iter().enumerate() {
            let info = &self.track_info[index];
            let full_y1 = self.minimap_layout.project_y(info.relative_y, scale);
            let y1 = full_y1 + VERTICAL_MINIMAP_LANE_HEIGHT;
            let y2 = self
                .minimap_layout
                .project_y(info.relative_y + info.height, scale);
            if y2 <= minimap_y || full_y1 >= minimap_y + minimap_height {
                continue;
            }
            if y1 >= y2 {
                continue;
            }
            for (bucket, event) in heatmap.iter().enumerate() {
                if let Some(event) = event {
                    let color = theme.event_palette[data.events[*event].palette_index as usize % 8];
                    ruler.rect_filled(
                        crate::imgui::Vec2 {
                            x: mini_x + 1.0 + bucket as f32 * cell_width,
                            y: y1,
                        },
                        crate::imgui::Vec2 {
                            x: mini_x + 1.0 + (bucket + 1) as f32 * cell_width,
                            y: y2,
                        },
                        color,
                    )
                }
            }
        }
        for index in 0..self.tracks.len() {
            let has_selected = self.track_has_selected.get(index).copied().unwrap_or(false);
            if !has_selected {
                continue;
            }
            let info = &self.track_info[index];
            let draw_y = self
                .minimap_layout
                .project_y(info.relative_y + (info.height + ruler_height) * 0.5, scale);
            if draw_y >= minimap_y && draw_y <= minimap_y + minimap_height {
                ruler.line(
                    crate::imgui::Vec2 {
                        x: mini_x + 1.0,
                        y: draw_y,
                    },
                    crate::imgui::Vec2 {
                        x: position.x + available.x,
                        y: draw_y,
                    },
                    theme.timeline_selection_line,
                    1.5,
                );
            }
        }
        if let Some((slider_x1, slider_y1, slider_x2, slider_y2)) = self.minimap_slider_bounds() {
            if slider_y1.is_finite() && slider_y2.is_finite() {
                let slider_col = if self.minimap_dragging {
                    theme.vertical_minimap_slider_bg_active
                } else if self.minimap_layout.hovered {
                    theme.vertical_minimap_slider_bg_hovered
                } else {
                    theme.vertical_minimap_slider_bg
                };
                ruler.rect_filled(
                    crate::imgui::Vec2 {
                        x: slider_x1,
                        y: slider_y1,
                    },
                    crate::imgui::Vec2 {
                        x: slider_x2,
                        y: slider_y2,
                    },
                    slider_col,
                );
            }
        }
        Self::draw_selection_overlay(
            &ruler,
            frame,
            theme,
            &self.selection_layout,
            position.x,
            position.y,
            available.x,
            ruler_height,
            false,
        );
    }

    fn draw_selection_overlay(
        draw: &crate::imgui::DrawListRef<'_>,
        frame: &crate::imgui::Frame<'_>,
        theme: &crate::colors::Theme,
        layout: &SelectionLayout,
        pos_x: f32,
        pos_y: f32,
        size_x: f32,
        size_y: f32,
        draw_duration_text: bool,
    ) {
        if !layout.active {
            return;
        }
        let right = pos_x + size_x;
        let bottom = pos_y + size_y;
        let dim_left_x2 = layout.x1.clamp(pos_x, right);
        let dim_right_x1 = layout.x2.clamp(pos_x, right);
        if pos_x < dim_left_x2 {
            draw.rect_filled(
                crate::imgui::Vec2 { x: pos_x, y: pos_y },
                crate::imgui::Vec2 {
                    x: dim_left_x2,
                    y: bottom,
                },
                theme.timeline_selection_bg,
            );
        }
        if dim_right_x1 < right {
            draw.rect_filled(
                crate::imgui::Vec2 {
                    x: dim_right_x1,
                    y: pos_y,
                },
                crate::imgui::Vec2 {
                    x: right,
                    y: bottom,
                },
                theme.timeline_selection_bg,
            );
        }
        let draw_x1 = layout.x1;
        let draw_x2 = layout.x2 - 1.0;
        if layout.x1 >= pos_x && layout.x1 <= right {
            draw.line(
                crate::imgui::Vec2 {
                    x: draw_x1,
                    y: pos_y,
                },
                crate::imgui::Vec2 {
                    x: draw_x1,
                    y: bottom,
                },
                theme.timeline_selection_line,
                1.0,
            );
        }
        if layout.x2 >= pos_x && layout.x2 <= right {
            draw.line(
                crate::imgui::Vec2 {
                    x: draw_x2,
                    y: pos_y,
                },
                crate::imgui::Vec2 {
                    x: draw_x2,
                    y: bottom,
                },
                theme.timeline_selection_line,
                1.0,
            );
        }
        if !draw_duration_text {
            return;
        }
        let text_size = frame.calc_text_size(&layout.duration_label);
        let mut text_x = (layout.x1 + layout.x2) * 0.5 - text_size.x * 0.5;
        text_x = text_x.max(pos_x + 5.0).min(right - text_size.x - 5.0);
        let text_y = pos_y + size_y / 3.0 - text_size.y * 0.5;
        let line_y = text_y + text_size.y * 0.5;
        let bg_min = crate::imgui::Vec2 {
            x: text_x - 4.0,
            y: text_y - 2.0,
        };
        let bg_max = crate::imgui::Vec2 {
            x: text_x + text_size.x + 4.0,
            y: text_y + text_size.y + 2.0,
        };
        draw.rect_filled(bg_min, bg_max, theme.timeline_selection_text_bg);
        draw.rect_rounded(bg_min, bg_max, theme.timeline_selection_line, 4.0, 1.0);
        draw.text(
            crate::imgui::Vec2 {
                x: text_x,
                y: text_y,
            },
            theme.timeline_selection_text,
            &layout.duration_label,
        );
        let left_end = text_x - 5.0;
        let right_start = text_x + text_size.x + 5.0;
        if left_end > draw_x1 {
            draw.line(
                crate::imgui::Vec2 {
                    x: draw_x1,
                    y: line_y,
                },
                crate::imgui::Vec2 {
                    x: left_end,
                    y: line_y,
                },
                theme.timeline_selection_line,
                1.0,
            );
            for offset in [-5.0, 5.0] {
                draw.line(
                    crate::imgui::Vec2 {
                        x: draw_x1,
                        y: line_y,
                    },
                    crate::imgui::Vec2 {
                        x: draw_x1 + 5.0,
                        y: line_y + offset,
                    },
                    theme.timeline_selection_line,
                    1.0,
                );
            }
        }
        if right_start < draw_x2 {
            draw.line(
                crate::imgui::Vec2 {
                    x: right_start,
                    y: line_y,
                },
                crate::imgui::Vec2 {
                    x: draw_x2,
                    y: line_y,
                },
                theme.timeline_selection_line,
                1.0,
            );
            for offset in [-5.0, 5.0] {
                draw.line(
                    crate::imgui::Vec2 {
                        x: draw_x2,
                        y: line_y,
                    },
                    crate::imgui::Vec2 {
                        x: draw_x2 - 5.0,
                        y: line_y + offset,
                    },
                    theme.timeline_selection_line,
                    1.0,
                );
            }
        }
    }

    fn draw_thread_event(
        list: &crate::imgui::DrawListRef<'_>,
        frame: &crate::imgui::Frame<'_>,
        data: &TraceData,
        theme: &crate::colors::Theme,
        block: &RenderBlock,
        color: u32,
        y1: f32,
        y2: f32,
        tracks_x: f32,
        tracks_width: f32,
        lane_height: f32,
    ) {
        let event_width = block.x2 - block.x1;
        list.rect_filled(
            crate::imgui::Vec2 { x: block.x1, y: y1 },
            crate::imgui::Vec2 { x: block.x2, y: y2 },
            color,
        );
        let draw_border = event_width > 3.01;
        if block.focused || block.selected || draw_border {
            let (border_col, thickness) = if block.focused {
                (theme.event_border_focused, 3.0)
            } else if block.selected {
                (theme.event_border_selected, 1.0)
            } else {
                (theme.event_border, 1.0)
            };
            list.rect(
                crate::imgui::Vec2 { x: block.x1, y: y1 },
                crate::imgui::Vec2 { x: block.x2, y: y2 },
                border_col,
                thickness,
            );
        }
        if event_width > 22.0 {
            let name = data.string_lossy(block.name);
            let r = (color & 255) as f32 / 255.0;
            let g = ((color >> 8) & 255) as f32 / 255.0;
            let b = ((color >> 16) & 255) as f32 / 255.0;
            let text = if block.focused || r * 0.299 + g * 0.587 + b * 0.114 <= 0.5 {
                0xffffffff
            } else {
                0xff202020
            };
            let text_width = frame.calc_text_size(&name).x;
            let visible_x1 = block.x1.max(tracks_x);
            let visible_x2 = block.x2.min(tracks_x + tracks_width);
            if visible_x2 > visible_x1 {
                list.text_clipped(
                    crate::imgui::Vec2 {
                        x: (block.x1 + (event_width - text_width) * 0.5).max(block.x1 + 6.0),
                        y: y1 + (lane_height - frame.font_size()) * 0.5,
                    },
                    text,
                    &name,
                    crate::colors::Vec4 {
                        x: visible_x1 + 6.0,
                        y: y1,
                        z: visible_x2 - 6.0,
                        w: y2,
                    },
                );
            }
        }
    }

    pub fn reset_view(&mut self) {
        let start = self.viewport.minimum as f64;
        let end = self.viewport.maximum as f64;
        let duration = ((end - start) * MAX_ZOOM_FACTOR).max(MIN_ZOOM_DURATION);
        let center = (start + end) * 0.5;
        self.viewport.start = center - duration * 0.5;
        self.viewport.end = center + duration * 0.5;
    }

    pub fn adopt_trace(&mut self, tracks: Vec<Track>, minimum: i64, maximum: i64) {
        self.tracks = tracks;
        self.viewport = Viewport {
            minimum,
            maximum,
            start: minimum as f64,
            end: maximum as f64,
        };
        self.reset_view();
        self.selection_active = false;
        self.focused_event = None;
        self.selected_events = Arc::new(vec![]);
        self.filtered_events.clear();
        self.histogram = Histogram::default();
        self.renderer_selection = Arc::new(vec![]);
        self.renderer.update_selection(0, &[]);
        self.track_has_selected.clear();
        self.minimap_heatmaps.clear();
        self.minimap_layout = MinimapLayout::default();
        self.minimap_dragging = false;
        self.box_selection_changed = false;
    }

    pub fn adopt_search_results(
        &mut self,
        selected_events: Arc<Vec<i64>>,
        filtered_events: Vec<i64>,
        histogram: Histogram,
    ) {
        self.selected_events = selected_events;
        self.filtered_events = filtered_events;
        self.histogram = histogram;
    }

    pub fn set_filtered_events(&mut self, filtered_events: Vec<i64>) {
        self.filtered_events = filtered_events;
    }

    pub fn clear_search_results(&mut self) {
        Arc::make_mut(&mut self.selected_events).clear();
        self.filtered_events.clear();
        self.histogram = Histogram::default();
    }

    pub fn selected_events(&self) -> &[i64] {
        &self.selected_events
    }

    pub fn selected_events_snapshot(&self) -> Arc<Vec<i64>> {
        Arc::clone(&self.selected_events)
    }

    pub fn filtered_events(&self) -> &[i64] {
        &self.filtered_events
    }

    pub fn histogram(&self) -> &Histogram {
        &self.histogram
    }

    pub fn focused_event(&self) -> Option<usize> {
        self.focused_event
    }

    pub fn show_details(&self) -> bool {
        self.show_details
    }

    pub fn set_show_details(&mut self, show: bool) {
        self.show_details = show;
    }

    pub fn set_ignore_next_release(&mut self) {
        self.ignore_next_release = true;
    }

    pub fn request_focus_event(&mut self, event_index: Option<usize>) {
        self.target_focused_event = event_index;
    }

    pub fn minimum_timestamp(&self) -> i64 {
        self.viewport.minimum
    }

    pub fn self_duration(&self, event_index: usize) -> Option<i64> {
        self.tracks.iter().find_map(|track| {
            track
                .event_indices
                .iter()
                .position(|candidate| *candidate == event_index)
                .map(|position| track.self_durations[position])
        })
    }

    pub fn viewport_range(&self) -> (f64, f64) {
        (self.viewport.start, self.viewport.end)
    }

    pub fn tracks_scroll_y(&self) -> f32 {
        self.last_tracks_scroll_y
    }

    pub fn minimap_scroll_y(&self) -> f32 {
        self.minimap_layout.minimap_scroll_y
    }

    pub fn minimap_slider_bounds(&self) -> Option<(f32, f32, f32, f32)> {
        (self.minimap_layout.active && self.minimap_layout.height > 2.0).then(|| {
            (
                self.minimap_layout.x + 2.0,
                self.minimap_layout.slider_y1 + 1.0,
                self.minimap_layout.x + VERTICAL_MINIMAP_WIDTH - 2.0,
                self.minimap_layout.slider_y2 - 1.0,
            )
        })
    }

    pub fn take_box_selection_changed(&mut self) -> bool {
        std::mem::take(&mut self.box_selection_changed)
    }

    fn update_box_selection(
        &mut self,
        data: &TraceData,
        input: &Input,
        origin_x: f32,
        origin_y: f32,
        width: f32,
        height: f32,
    ) {
        let (x1, x2) = (
            self.box_start.0.min(self.box_end.0),
            self.box_start.0.max(self.box_end.0),
        );
        let (y1, y2) = (
            self.box_start.1.min(self.box_end.1),
            self.box_start.1.max(self.box_end.1),
        );
        let ts1 = self.px_to_ts(width, origin_x, x1);
        let ts2 = self.px_to_ts(width, origin_x, x2);
        let selected_events = Arc::make_mut(&mut self.selected_events);
        selected_events.clear();
        for (track_index, track) in self.tracks.iter().enumerate() {
            let Some(info) = self.track_info.get(track_index) else {
                continue;
            };
            if !info.visible {
                continue;
            }
            if info.y + info.height < y1 || info.y > y2 {
                continue;
            }
            if track.kind == TrackType::Thread {
                for (position, &index) in track.event_indices.iter().enumerate() {
                    let event = &data.events[index];
                    let event_end = event.timestamp.saturating_add(event.duration) as f64;
                    if event.timestamp as f64 > ts2 || event_end < ts1 {
                        continue;
                    }
                    let depth = track.depths.get(position).copied().unwrap_or(0);
                    let event_y1 = info.y + (depth + 1) as f32 * input.lane_height;
                    let event_y2 = event_y1 + input.lane_height;
                    if event_y2 >= y1 && event_y1 <= y2 {
                        selected_events.push(index as i64);
                    }
                }
            } else {
                let chart_y1 = info.y + input.lane_height;
                let chart_y2 = info.y + info.height;
                if chart_y2 >= y1 && chart_y1 <= y2 {
                    for &index in &track.event_indices {
                        let event = &data.events[index];
                        if event.timestamp as f64 >= ts1 && event.timestamp as f64 <= ts2 {
                            selected_events.push(index as i64);
                        }
                    }
                }
            }
        }
        selected_events.sort_unstable();
        selected_events.dedup();
        self.histogram = crate::trace::histogram::compute(&self.selected_events, data);
        self.filtered_events = self.selected_events.as_ref().clone();
        if !self.selected_events.is_empty() {
            self.show_details = true;
        }
        self.box_selection_changed = true;
        let _ = origin_y;
        let _ = height;
    }

    fn draw_hover_event_properties(
        frame: &crate::imgui::Frame<'_>,
        data: &TraceData,
        viewer: &Viewer,
        theme: &crate::colors::Theme,
        track: &Track,
        index: usize,
    ) {
        let Some(event) = data.events.get(index) else {
            return;
        };
        frame.spacing();
        let Some(_table) = frame.begin_table(
            "##focused_event_table_unified",
            3,
            HOVER_PROPERTIES_TABLE_FLAGS,
            crate::imgui::Vec2::default(),
        ) else {
            return;
        };
        frame.table_setup_column("Label", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
        frame.table_setup_column("Value", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
        frame.table_setup_column("Action", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
        let add_row = |label: &str, value: &str, swatch: Option<u32>| {
            frame.table_next_row();
            frame.table_next_column();
            if let Some(color) = swatch {
                let pos = frame.cursor_screen_position();
                let line = frame.text_line_height();
                let size = line * 0.7;
                let offset = (line - size) * 0.5;
                frame.draw_list().rect_filled(
                    crate::imgui::Vec2 {
                        x: pos.x,
                        y: pos.y + offset,
                    },
                    crate::imgui::Vec2 {
                        x: pos.x + size,
                        y: pos.y + offset + size,
                    },
                    color,
                );
                frame.set_cursor_pos_x(frame.cursor_pos_x() + size + 4.0);
            }
            frame.text_disabled(label);
            frame.table_next_column();
            frame.text_unformatted(value);
            frame.table_next_column();
        };
        let name = if track.kind == TrackType::Counter {
            data.string_lossy(track.name).into_owned()
        } else {
            let phase = data.string(event.phase);
            if phase == b"C" {
                "(Counter Event)".to_owned()
            } else {
                data.string_lossy(event.name).into_owned()
            }
        };
        add_row("Name", &name, None);
        let category = data.string_lossy(event.category).into_owned();
        if !category.is_empty() {
            add_row("Category", &category, None);
        }
        let start = event.timestamp - viewer.viewport.minimum;
        let start_text = crate::format::duration(start as f64, 0.0);
        add_row("Start", &start_text, None);
        if event.duration > 0 {
            let duration_text = crate::format::duration(event.duration as f64, 0.0);
            add_row("Duration", &duration_text, None);
            if track.kind == TrackType::Thread {
                let self_duration = track
                    .event_indices
                    .iter()
                    .position(|candidate| *candidate == index)
                    .map(|position| track.self_durations[position])
                    .unwrap_or(event.duration);
                let self_text = crate::format::duration(self_duration as f64, 0.0);
                add_row("Self Time", &self_text, None);
            }
        }
        if track.kind != TrackType::Counter {
            let pid_tid = format!("{} / {}", event.process_id, event.thread_id);
            add_row("PID / TID", &pid_tid, None);
        }
        if !data.string(event.id).is_empty() {
            let id = data.string_lossy(event.id).into_owned();
            add_row("ID", &id, None);
        }
        let mut skip_keys = Vec::new();
        if track.kind == TrackType::Counter {
            let track_name = data.string(track.name);
            let single_redundant = track.counter_series.len() == 1 && {
                let series = data.string(track.counter_series[0]);
                series == track_name || series.is_empty() || series.eq_ignore_ascii_case(b"value")
            };
            if single_redundant {
                let key = track.counter_series[0];
                let mut value = "0.00".to_owned();
                for arg in data.event_args(event) {
                    if arg.key == key {
                        value = if data.string(arg.value).is_empty() {
                            format!("{:.2}", arg.number)
                        } else {
                            data.string_lossy(arg.value).into_owned()
                        };
                        break;
                    }
                }
                add_row("Value", &value, None);
            } else {
                let mut total = 0.0_f64;
                let mut has_total_series = false;
                for series_index in (0..track.counter_series.len()).rev() {
                    let key = track.counter_series[series_index];
                    let series_name = data.string_lossy(key).into_owned();
                    if series_name.eq_ignore_ascii_case("total") {
                        has_total_series = true;
                    }
                    let mut value = "0.00".to_owned();
                    let mut numeric = 0.0_f64;
                    for arg in data.event_args(event) {
                        if arg.key == key {
                            numeric = arg.number;
                            value = if data.string(arg.value).is_empty() {
                                format!("{:.2}", arg.number)
                            } else {
                                data.string_lossy(arg.value).into_owned()
                            };
                            break;
                        }
                    }
                    total += numeric;
                    let palette = track
                        .counter_palette_indices
                        .get(series_index)
                        .copied()
                        .unwrap_or(0) as usize
                        % theme.event_palette.len();
                    add_row(&series_name, &value, Some(theme.event_palette[palette]));
                }
                if track.counter_series.len() > 1 && !has_total_series {
                    let total_text = format!("{total:.2}");
                    add_row("Total", &total_text, None);
                }
            }
            skip_keys.extend(track.counter_series.iter().copied());
        }
        for arg in data.event_args(event) {
            if skip_keys.contains(&arg.key) {
                continue;
            }
            let key = data.string_lossy(arg.key).into_owned();
            let value = if data.string(arg.value).is_empty() {
                format!("{:.2}", arg.number)
            } else {
                data.string_lossy(arg.value).into_owned()
            };
            add_row(&key, &value, None);
        }
    }

    fn px_to_ts(&self, width: f32, origin: f32, x: f32) -> f64 {
        self.viewport.start
            + f64::from((x - origin) / width) * (self.viewport.end - self.viewport.start)
    }
    fn zoom_to_event(&mut self, index: usize, data: &TraceData) {
        let Some(event) = data.events.get(index) else {
            return;
        };
        self.focused_event = Some(index);
        self.show_details = true;
        let event_start = event.timestamp as f64;
        let event_end = event.timestamp.saturating_add(event.duration) as f64;
        if event.duration > 0 {
            let event_dur = event_end - event_start;
            let mut padding = event_dur * 0.05;
            let mut target_dur = event_dur + padding * 2.0;
            if target_dur < MIN_ZOOM_DURATION {
                target_dur = MIN_ZOOM_DURATION;
                padding = (target_dur - event_dur) * 0.5;
            }
            self.viewport.start = event_start - padding;
            self.viewport.end = event_start - padding + target_dur;
            self.selection_active = true;
            self.selection_start = event_start;
            self.selection_end = event_end;
        } else {
            let current_dur = (self.viewport.end - self.viewport.start).max(1.0);
            self.viewport.start = event_start - current_dur * 0.5;
            self.viewport.end = self.viewport.start + current_dur;
            self.selection_active = false;
        }
        self.drag_mode = DragMode::None;
        self.request_scroll_to_focused_event = true;
    }

    fn update_minimap(&mut self, input: &Input) {
        let minimap_x = input.viewport_x + input.viewport_width - VERTICAL_MINIMAP_WIDTH;
        let minimap_y = input.viewport_y + input.ruler_height;
        let visible_height = (input.viewport_height - input.ruler_height).max(0.0);
        let total_height = self.total_tracks_height;
        let scale = VERTICAL_MINIMAP_LANE_HEIGHT / input.lane_height.max(1.0);
        let content_height = total_height * scale;
        let draw_height = content_height.min(visible_height);
        let scroll_range = (total_height - visible_height).max(0.0);
        let minimap_scroll = if content_height > visible_height && scroll_range > 0.0 {
            input.tracks_scroll_y / scroll_range * (content_height - visible_height)
        } else {
            0.0
        };
        let hovered = total_height > 0.0
            && input.mouse_x >= minimap_x
            && input.mouse_x <= minimap_x + VERTICAL_MINIMAP_WIDTH
            && input.mouse_y >= minimap_y
            && input.mouse_y <= minimap_y + draw_height;
        let inside_slider = self.minimap_layout.active
            && input.mouse_y >= self.minimap_layout.slider_y1
            && input.mouse_y <= self.minimap_layout.slider_y2;
        let mut jump_clicked = false;
        if hovered && input.mouse_clicked {
            self.minimap_dragging = true;
            if inside_slider {
                self.minimap_drag_offset_y = input.mouse_y - self.minimap_layout.slider_y1;
            } else {
                jump_clicked = true;
                if scroll_range > 0.0 {
                    let content_y = (input.mouse_y - minimap_y + minimap_scroll) / scale;
                    if let Some(info) = self.track_info.iter().find(|info| {
                        content_y >= info.relative_y && content_y < info.relative_y + info.height
                    }) {
                        let target = info.relative_y - (visible_height - info.height) * 0.5;
                        self.target_scroll_y = Some(target.clamp(0.0, scroll_range));
                    }
                }
                self.minimap_drag_offset_y = -1.0;
            }
        }
        if !input.mouse_down {
            self.minimap_dragging = false;
        }
        let slider_height = if total_height > 0.0 {
            (visible_height * scale).clamp(10.0_f32.min(draw_height), draw_height)
        } else {
            0.0
        };
        if self.minimap_dragging && !jump_clicked && scroll_range > 0.0 {
            let current_slider_y1 = minimap_y + input.tracks_scroll_y * scale - minimap_scroll;
            if self.minimap_drag_offset_y == -1.0 {
                self.minimap_drag_offset_y = input.mouse_y - current_slider_y1;
            }
            let mouse_y = (input.mouse_y - minimap_y).clamp(0.0, draw_height);
            let target_y = mouse_y - self.minimap_drag_offset_y;
            let maximum_slider_y = draw_height - slider_height;
            let percentage = if maximum_slider_y > 0.0 {
                (target_y / maximum_slider_y).clamp(0.0, 1.0)
            } else {
                0.0
            };
            self.target_scroll_y = Some(percentage * scroll_range);
        }
        let slider_y1 = if total_height > 0.0 {
            (minimap_y + input.tracks_scroll_y * scale - minimap_scroll)
                .clamp(minimap_y, minimap_y + draw_height - slider_height)
        } else {
            minimap_y
        };
        self.minimap_layout = MinimapLayout {
            active: total_height > 0.0,
            x: minimap_x,
            y: minimap_y,
            height: draw_height,
            minimap_scroll_y: minimap_scroll,
            slider_y1,
            slider_y2: slider_y1 + slider_height,
            hovered,
        };
    }

    fn step(&mut self, data: &TraceData, input: &Input) {
        if let Some(index) = self.target_focused_event.take() {
            self.zoom_to_event(index, data)
        }
        let mut duration = (self.viewport.end - self.viewport.start).max(1.0);
        let origin_x = input.canvas_x;
        let origin_y = input.canvas_y + input.ruler_height;
        let width = input.canvas_width.max(1.0);
        let height = input.canvas_height - input.ruler_height;
        self.last_tracks_x = origin_x;
        self.last_tracks_y = origin_y;
        self.last_inner_width = width;
        self.last_inner_height = height;
        self.last_lane_height = input.lane_height;
        self.last_tracks_scroll_y = input.tracks_scroll_y;
        let mouse_ts = self.px_to_ts(width, origin_x, input.mouse_x);
        let threshold = 5.0 / width as f64 * duration;
        let proximity = selection_proximity(
            self.selection_active,
            self.selection_start,
            self.selection_end,
            mouse_ts,
            threshold,
        );
        let near_start = proximity.near_start;
        let near_end = proximity.near_end;
        let in_selection = !self.selection_active
            || (mouse_ts >= self.selection_start.min(self.selection_end)
                && mouse_ts <= self.selection_start.max(self.selection_end));
        let ignored = self.ignore_next_release;
        if input.ruler_active {
            if !ignored && input.ruler_activated {
                self.drag_mode = if near_start {
                    DragMode::RulerStart
                } else if near_end {
                    DragMode::RulerEnd
                } else {
                    DragMode::RulerNew
                }
            }
            if self.drag_mode == DragMode::RulerNew
                && input.drag_delta_x.abs() >= input.drag_threshold
            {
                self.selection_active = true;
                self.selection_start = self.px_to_ts(width, origin_x, input.click_x)
            }
        } else {
            if !ignored
                && input.ruler_deactivated
                && input.drag_delta_x.abs() < input.drag_threshold
                && self.drag_mode == DragMode::RulerNew
            {
                self.selection_active = false
            }
            if matches!(
                self.drag_mode,
                DragMode::RulerNew | DragMode::RulerStart | DragMode::RulerEnd
            ) {
                self.drag_mode = DragMode::None
            }
        }
        if input.tracks_hovered && !input.ruler_active {
            if !ignored && self.drag_mode == DragMode::None {
                if input.shift_down && input.mouse_clicked {
                    self.drag_mode = DragMode::BoxSelect;
                    self.box_start = (input.mouse_x, input.mouse_y);
                    self.box_end = self.box_start
                } else if self.selection_active && input.mouse_clicked && (near_start || near_end) {
                    self.drag_mode = if near_start {
                        DragMode::TracksStart
                    } else {
                        DragMode::TracksEnd
                    }
                }
            }
            if input.mouse_wheel != 0.0 && input.ctrl_down {
                let ratio = f64::from((input.mouse_x - origin_x) / width);
                let mut new_duration = duration
                    * if input.mouse_wheel > 0.0 {
                        0.8
                    } else {
                        MAX_ZOOM_FACTOR
                    };
                let trace_duration = (self.viewport.maximum - self.viewport.minimum) as f64;
                let mut maximum = trace_duration * MAX_ZOOM_FACTOR;
                let mut minimum = MIN_ZOOM_DURATION;
                if self.selection_active {
                    let selected = (self.selection_end - self.selection_start).abs();
                    if selected > 0.0 {
                        minimum = minimum.max(selected);
                        maximum = maximum.min(selected * 10.0).max(minimum)
                    }
                }
                maximum = maximum.max(minimum);
                new_duration = new_duration.clamp(minimum, maximum);
                self.viewport.start = mouse_ts - ratio * new_duration;
                if self.selection_active {
                    let low = self.selection_start.min(self.selection_end);
                    let high = self.selection_start.max(self.selection_end);
                    if self.viewport.start > low {
                        self.viewport.start = low
                    }
                    if self.viewport.start + new_duration < high {
                        self.viewport.start = high - new_duration
                    }
                }
                self.viewport.end = self.viewport.start + new_duration;
                duration = new_duration
            }
            if input.mouse_down && !input.mouse_clicked && self.drag_mode == DragMode::None {
                let delta = f64::from(input.mouse_delta_x / width) * duration;
                self.viewport.start -= delta;
                if self.selection_active {
                    let low = self.selection_start.min(self.selection_end);
                    let high = self.selection_start.max(self.selection_end);
                    if self.viewport.start > low {
                        self.viewport.start = low
                    }
                    if self.viewport.start + duration < high {
                        self.viewport.start = high - duration
                    }
                }
                self.viewport.end = self.viewport.start + duration
            }
        }
        if matches!(self.drag_mode, DragMode::TracksStart | DragMode::TracksEnd)
            && !input.mouse_down
        {
            self.drag_mode = DragMode::None;
        }
        let was_drag = input.drag_delta_x.abs() >= input.drag_threshold
            || input.drag_delta_y.abs() >= input.drag_threshold;
        let should_snap =
            self.drag_mode != DragMode::None && self.drag_mode != DragMode::BoxSelect && was_drag;
        let mut best = (mouse_ts, 5.0_f32, None::<(f32, f32, f32)>);
        self.hover_matches.clear();
        self.track_info.clear();
        self.total_tracks_height = 0.0;
        if !Arc::ptr_eq(&self.renderer_selection, &self.selected_events) {
            self.renderer
                .update_selection(data.events.len(), &self.selected_events);
            self.renderer_selection = Arc::clone(&self.selected_events);
            self.track_has_selected =
                self.tracks
                    .iter()
                    .map(|track| {
                        track.event_indices.iter().any(|index| {
                            self.selected_events.binary_search(&(*index as i64)).is_ok()
                        })
                    })
                    .collect();
        }
        let mouse_in_tracks_content = input.mouse_x >= origin_x
            && input.mouse_x < origin_x + width
            && input.mouse_y >= origin_y
            && input.mouse_y < origin_y + height;
        for (track_index, track) in self.tracks.iter().enumerate() {
            let track_height = if track.kind == TrackType::Counter {
                3.0 * input.lane_height
            } else {
                (track.max_depth + 2) as f32 * input.lane_height
            };
            let y_relative = self.total_tracks_height;
            let y = origin_y + y_relative - input.tracks_scroll_y;
            let visible = y + track_height >= origin_y && y <= input.canvas_y + input.canvas_height;
            let name = if data.string(track.name).is_empty() {
                if track.kind == TrackType::Thread {
                    format!("Thread {}", track.thread_id)
                } else {
                    "Counter".to_owned()
                }
            } else if data.string(track.id).is_empty() {
                data.string_lossy(track.name).into_owned()
            } else {
                format!(
                    "{} ({})",
                    data.string_lossy(track.name),
                    data.string_lossy(track.id)
                )
            };
            self.track_info.push(TrackInfo {
                y,
                relative_y: y_relative,
                height: track_height,
                visible,
                name,
            });
            if self.request_scroll_to_focused_event
                && self
                    .focused_event
                    .is_some_and(|focused| track.event_indices.contains(&focused))
            {
                self.target_scroll_y = Some(y_relative - (height - track_height) * 0.5);
                self.request_scroll_to_focused_event = false
            }
            self.total_tracks_height += track_height;
            if !visible {
                continue;
            }
            if track.kind == TrackType::Thread {
                let blocks = self.renderer.thread_blocks(
                    track,
                    data,
                    self.viewport.start,
                    self.viewport.end,
                    width,
                    origin_x,
                    self.focused_event,
                );
                for (block_index, block) in blocks.into_iter().enumerate() {
                    let y1 = y + (block.depth + 1) as f32 * input.lane_height;
                    let y2 = y1 + input.lane_height - 1.0;
                    if should_snap {
                        for x in [block.x1, block.x2] {
                            let distance = (input.mouse_x - x).abs();
                            if distance < best.1 {
                                best = (
                                    self.viewport.start
                                        + f64::from((x - origin_x) / width) * duration,
                                    distance,
                                    Some((x, y1, y2)),
                                )
                            }
                        }
                    }
                    let hit = input.tracks_hovered
                        && mouse_in_tracks_content
                        && (in_selection || input.mouse_double_clicked)
                        && self.drag_mode == DragMode::None
                        && !near_start
                        && !near_end
                        && input.mouse_y >= y1
                        && input.mouse_y < y2
                        && input.mouse_x >= block.x1
                        && input.mouse_x < block.x2;
                    if hit {
                        self.hover_matches.push(HoverMatch {
                            track_index,
                            block_index,
                            y1,
                            y2,
                            block: block.clone(),
                        })
                    }
                }
            } else if track.kind == TrackType::Counter {
                let blocks = self.renderer.counter_blocks(
                    track,
                    data,
                    self.viewport.start,
                    self.viewport.end,
                    width,
                    origin_x,
                    self.focused_event,
                );
                let y1 = y + input.lane_height;
                let y2 = y + track_height;
                for (block_index, block) in blocks.iter().enumerate() {
                    let hit = input.tracks_hovered
                        && mouse_in_tracks_content
                        && (in_selection || input.mouse_double_clicked)
                        && self.drag_mode == DragMode::None
                        && !near_start
                        && !near_end
                        && input.mouse_y >= y1
                        && input.mouse_y < y2
                        && input.mouse_x >= block.x1
                        && input.mouse_x < block.x2;
                    if hit {
                        if let Some(event_index) = block.event_index() {
                            self.hover_matches.push(HoverMatch {
                                track_index,
                                block_index,
                                y1,
                                y2,
                                block: RenderBlock {
                                    x1: block.x1,
                                    x2: block.x2,
                                    palette_index: 0,
                                    name: crate::string_interner::StringId(0),
                                    depth: 0,
                                    count: 1,
                                    selected: block.selected,
                                    focused: block.focused,
                                    event_index,
                                },
                            });
                        }
                    }
                }
            }
        }
        let snapped = best.0;
        self.snap_line = best.2;
        if !ignored {
            match self.drag_mode {
                DragMode::RulerNew if input.ruler_active && was_drag => {
                    self.selection_end = snapped
                }
                DragMode::RulerStart if input.ruler_active => self.selection_start = snapped,
                DragMode::RulerEnd if input.ruler_active => self.selection_end = snapped,
                DragMode::TracksStart if input.tracks_hovered => {
                    self.selection_start = if input.mouse_clicked {
                        mouse_ts
                    } else {
                        snapped
                    }
                }
                DragMode::TracksEnd if input.tracks_hovered => {
                    self.selection_end = if input.mouse_clicked {
                        mouse_ts
                    } else {
                        snapped
                    }
                }
                _ => {}
            }
        }
        if input.mouse_double_clicked {
            if let Some(hit) = self.hover_matches.last()
                && self.tracks[hit.track_index].kind == TrackType::Thread
            {
                self.ignore_next_release = true;
                self.zoom_to_event(hit.block.event_index, data)
            }
        } else if !ignored && input.mouse_released && !was_drag {
            if let Some(index) = self.hover_matches.last().map(|m| m.block.event_index) {
                self.focused_event = Some(index);
                self.show_details = true
            } else if input.tracks_hovered && mouse_in_tracks_content {
                if self.selection_active && !in_selection {
                    self.selection_active = false;
                    self.focused_event = None
                } else if in_selection {
                    self.focused_event = None
                } else {
                    self.focused_event = None
                }
            }
        }
        if self.drag_mode == DragMode::BoxSelect {
            self.box_end = (input.mouse_x, input.mouse_y);
            if !input.mouse_down {
                self.update_box_selection(data, input, origin_x, origin_y, width, height);
                self.renderer
                    .update_selection(data.events.len(), &self.selected_events);
                self.drag_mode = DragMode::None
            }
        }
        if input.mouse_released && ignored {
            self.ignore_next_release = false
        }
        self.selection_layout.active = self.selection_active;
        if self.selection_active {
            let low = self.selection_start.min(self.selection_end);
            let high = self.selection_start.max(self.selection_end);
            self.selection_layout.x1 =
                origin_x + ((low - self.viewport.start) / duration) as f32 * width;
            self.selection_layout.x2 =
                origin_x + ((high - self.viewport.start) / duration) as f32 * width;
            self.selection_layout.duration_label = format::duration(high - low, high - low)
        }
        self.ruler_ticks.clear();
        let interval = format::tick_interval(duration, width as f64, 100.0);
        let display_start = self.viewport.start - self.viewport.minimum as f64;
        let display_end = self.viewport.end - self.viewport.minimum as f64;
        let mut relative = (display_start / interval).ceil() * interval;
        while relative <= display_end {
            let timestamp = relative + self.viewport.minimum as f64;
            let x = origin_x + ((timestamp - self.viewport.start) / duration) as f32 * width;
            if x >= origin_x && x <= origin_x + width {
                self.ruler_ticks.push(RulerTick {
                    x,
                    label: format::duration(relative, interval),
                })
            }
            relative += interval
        }
        self.update_minimap(input);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::data::PersistedEvent;

    #[test]
    fn counter_series_have_the_legacy_one_pixel_minimum_height() {
        assert_eq!(counter_visual_height(0.0, 100.0, 40.0), 1.0);
        assert_eq!(counter_visual_height(-10.0, 100.0, 40.0), 1.0);
        assert_eq!(counter_visual_height(25.0, 100.0, 40.0), 10.0);
    }

    fn input() -> Input {
        Input {
            canvas_width: 1_000.0,
            canvas_height: 500.0,
            ruler_height: 20.0,
            lane_height: 20.0,
            viewport_width: 1_000.0,
            viewport_height: 500.0,
            drag_threshold: 5.0,
            ..Input::default()
        }
    }

    fn thread(data: &TraceData, indices: Vec<usize>) -> Track {
        let mut track = Track::thread(0, 1);
        track.event_indices = indices;
        track.sort_events(data);
        track.update_max_duration(data);
        track.calculate_depths(data);
        track
    }

    #[test]
    fn zoom_in_around_mouse() {
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 0,
            maximum: 1_000_000,
            start: 0.0,
            end: 1_000_000.0,
        };
        viewer.step(
            &TraceData::new(),
            &Input {
                canvas_width: 1000.0,
                mouse_x: 500.0,
                mouse_wheel: 1.0,
                ctrl_down: true,
                tracks_hovered: true,
                ..Input::default()
            },
        );
        assert!(viewer.viewport.end - viewer.viewport.start < 1_000_000.0);
        assert!(((viewer.viewport.start + viewer.viewport.end) * 0.5 - 500_000.0).abs() < 1.0)
    }

    #[test]
    fn zooming_an_empty_trace_keeps_the_minimum_viewport_duration() {
        let mut viewer = Viewer::default();
        viewer.adopt_trace(Vec::new(), 0, 0);

        viewer.step(
            &TraceData::new(),
            &Input {
                canvas_width: 1000.0,
                mouse_x: 500.0,
                mouse_wheel: 1.0,
                ctrl_down: true,
                tracks_hovered: true,
                ..Input::default()
            },
        );

        assert_eq!(viewer.viewport.end - viewer.viewport.start, 1_000.0);
    }
    #[test]
    fn panning() {
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 0,
            maximum: 1_000_000,
            start: 100_000.0,
            end: 200_000.0,
        };
        viewer.step(
            &TraceData::new(),
            &Input {
                canvas_width: 1000.0,
                mouse_delta_x: 10.0,
                mouse_down: true,
                tracks_hovered: true,
                ..Input::default()
            },
        );
        assert!((viewer.viewport.start - 99_000.0).abs() < 1.0)
    }

    #[test]
    fn track_boundary_drag_ends_on_release() {
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.selection_active = true;
        viewer.selection_start = 100.0;
        viewer.selection_end = 200.0;
        viewer.step(
            &TraceData::new(),
            &Input {
                mouse_x: 101.0,
                mouse_clicked: true,
                mouse_down: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert_eq!(viewer.drag_mode, DragMode::TracksStart);
        viewer.step(
            &TraceData::new(),
            &Input {
                mouse_x: 50.0,
                mouse_released: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert_eq!(viewer.drag_mode, DragMode::None);
    }

    #[test]
    fn selection_proximity_matches_legacy_boundary_and_tie_breaking() {
        assert_eq!(
            selection_proximity(true, 100.0, 200.0, 105.0, 5.0),
            SelectionProximity::default()
        );
        assert_eq!(
            selection_proximity(true, 100.0, 106.0, 103.0, 5.0),
            SelectionProximity {
                near_start: false,
                near_end: true,
            }
        );

        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.selection_active = true;
        viewer.selection_start = 100.0;
        viewer.selection_end = 106.0;
        viewer.step(
            &TraceData::new(),
            &Input {
                mouse_x: 103.0,
                mouse_clicked: true,
                mouse_down: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert_eq!(viewer.drag_mode, DragMode::TracksEnd);
    }

    #[test]
    fn release_outside_track_content_does_not_clear_state() {
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.selection_active = true;
        viewer.selection_start = 100.0;
        viewer.selection_end = 200.0;
        viewer.focused_event = Some(0);
        viewer.step(
            &TraceData::new(),
            &Input {
                mouse_x: 1_001.0,
                mouse_y: 50.0,
                mouse_released: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert!(viewer.selection_active);
        assert_eq!(viewer.focused_event, Some(0));
    }

    #[test]
    fn reset_view_preserves_interaction_state() {
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 0,
            maximum: 10_000,
            start: 2_000.0,
            end: 3_000.0,
        };
        viewer.selection_active = true;
        viewer.selection_start = 100.0;
        viewer.selection_end = 200.0;
        viewer.focused_event = Some(3);
        viewer.selected_events = Arc::new(vec![1, 2]);
        viewer.reset_view();
        assert!(viewer.selection_active);
        assert_eq!(viewer.focused_event, Some(3));
        assert_eq!(viewer.selected_events.as_slice(), [1, 2]);
        assert_eq!(viewer.viewport.start, -1_000.0);
        assert_eq!(viewer.viewport.end, 11_000.0);
    }

    #[test]
    fn zero_width_selection_does_not_constrain_zoom() {
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 0,
            maximum: 1_000_000,
            start: 0.0,
            end: 1_000.0,
        };
        viewer.selection_active = true;
        viewer.selection_start = 500.0;
        viewer.selection_end = 500.0;
        viewer.step(
            &TraceData::new(),
            &Input {
                mouse_x: 500.0,
                mouse_wheel: -1.0,
                ctrl_down: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert_eq!(viewer.viewport.end - viewer.viewport.start, 1_200.0);
    }

    #[test]
    fn box_selection_checks_each_thread_lane() {
        let mut data = TraceData::new();
        data.events = vec![
            PersistedEvent {
                timestamp: 100,
                duration: 200,
                ..PersistedEvent::default()
            },
            PersistedEvent {
                timestamp: 110,
                duration: 50,
                ..PersistedEvent::default()
            },
        ];
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0, 1]));
        viewer.drag_mode = DragMode::BoxSelect;
        viewer.box_start = (100.0, 65.0);
        viewer.step(
            &data,
            &Input {
                mouse_x: 200.0,
                mouse_y: 75.0,
                mouse_released: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert_eq!(viewer.selected_events.as_slice(), [1]);
        assert!(viewer.take_box_selection_changed());
    }

    #[test]
    fn box_selection_finishes_when_mouse_is_no_longer_down() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 100,
            duration: 100,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0]));
        viewer.drag_mode = DragMode::BoxSelect;
        viewer.box_start = (100.0, 45.0);

        viewer.step(
            &data,
            &Input {
                mouse_x: 200.0,
                mouse_y: 55.0,
                mouse_down: false,
                mouse_released: false,
                tracks_hovered: true,
                ..input()
            },
        );

        assert_eq!(viewer.drag_mode, DragMode::None);
        assert_eq!(viewer.selected_events.as_slice(), [0]);
    }

    #[test]
    fn box_selection_ignores_culled_tracks() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 100,
            duration: 100,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0]));
        viewer.drag_mode = DragMode::BoxSelect;
        viewer.box_start = (50.0, -70.0);
        viewer.step(
            &data,
            &Input {
                tracks_scroll_y: 100.0,
                mouse_x: 200.0,
                mouse_y: -40.0,
                mouse_released: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert!(viewer.selected_events.is_empty());
    }

    #[test]
    fn minimap_click_inside_slider_drags_without_jumping() {
        let data = TraceData::new();
        let mut viewer = Viewer::default();
        viewer.tracks = (0..10).map(|tid| Track::thread(0, tid)).collect();
        let base = Input {
            canvas_width: 736.0,
            viewport_width: 800.0,
            viewport_height: 200.0,
            canvas_height: 200.0,
            ruler_height: 20.0,
            lane_height: 20.0,
            ..Input::default()
        };
        viewer.step(&data, &base);
        assert_eq!(viewer.minimap_layout.slider_y1, 20.0);
        viewer.step(
            &data,
            &Input {
                mouse_x: 768.0,
                mouse_y: 25.0,
                mouse_clicked: true,
                mouse_down: true,
                ..base.clone()
            },
        );
        assert_eq!(viewer.target_scroll_y, Some(0.0));
        viewer.step(
            &data,
            &Input {
                mouse_x: 768.0,
                mouse_y: 30.0,
                mouse_down: true,
                ..base
            },
        );
        assert_eq!(viewer.target_scroll_y, Some(110.0));
    }

    #[test]
    fn minimap_click_outside_slider_jumps_to_track() {
        let data = TraceData::new();
        let mut viewer = Viewer::default();
        viewer.tracks = (0..10).map(|tid| Track::thread(0, tid)).collect();
        let mut base = Input {
            canvas_width: 736.0,
            viewport_width: 800.0,
            viewport_height: 200.0,
            canvas_height: 200.0,
            ruler_height: 20.0,
            lane_height: 20.0,
            ..Input::default()
        };
        viewer.step(&data, &base);
        base.mouse_x = 768.0;
        base.mouse_y = 35.0;
        base.mouse_clicked = true;
        base.mouse_down = true;
        viewer.step(&data, &base);
        assert_eq!(viewer.target_scroll_y, Some(210.0));
    }

    #[test]
    fn snapping_updates_selection_and_exposes_guide() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 102,
            duration: 10,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0]));
        viewer.step(
            &data,
            &Input {
                mouse_x: 100.0,
                click_x: 100.0,
                ruler_active: true,
                ruler_activated: true,
                ..input()
            },
        );
        viewer.step(
            &data,
            &Input {
                mouse_x: 104.0,
                click_x: 100.0,
                drag_delta_x: 10.0,
                ruler_active: true,
                ..input()
            },
        );
        assert!((viewer.selection_start - 100.0).abs() < 0.001);
        assert!((viewer.selection_end - 102.0).abs() < 0.001);
        assert!(viewer.snap_line.is_some());
    }

    #[test]
    fn thread_events_are_hit_tested_and_clicked_to_focus() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 500,
            duration: 100,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0]));
        let click = Input {
            mouse_x: 550.0,
            mouse_y: 50.0,
            mouse_released: true,
            tracks_hovered: true,
            ..input()
        };

        viewer.step(&data, &click);

        assert_eq!(viewer.hover_matches.len(), 1);
        assert_eq!(viewer.hover_matches[0].track_index, 0);
        assert_eq!(viewer.hover_matches[0].block.event_index, 0);
        assert_eq!(viewer.focused_event, Some(0));
        assert!(viewer.show_details);
    }

    #[test]
    fn double_click_can_focus_an_event_outside_the_selection() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 500,
            duration: 100,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.selection_active = true;
        viewer.selection_start = 0.0;
        viewer.selection_end = 100.0;
        viewer.tracks.push(thread(&data, vec![0]));

        viewer.step(
            &data,
            &Input {
                mouse_x: 550.0,
                mouse_y: 50.0,
                mouse_double_clicked: true,
                tracks_hovered: true,
                ..input()
            },
        );

        assert_eq!(viewer.focused_event, Some(0));
        assert_eq!(viewer.selection_start, 500.0);
        assert_eq!(viewer.selection_end, 600.0);
        assert!(viewer.ignore_next_release);
    }

    #[test]
    fn zoom_and_pan_keep_the_active_selection_visible() {
        let data = TraceData::new();
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 0,
            maximum: 10_000,
            start: 0.0,
            end: 10_000.0,
        };
        viewer.selection_active = true;
        viewer.selection_start = 4_000.0;
        viewer.selection_end = 6_000.0;
        viewer.step(
            &data,
            &Input {
                mouse_x: 5_000.0,
                mouse_wheel: 1.0,
                ctrl_down: true,
                tracks_hovered: true,
                ..input()
            },
        );
        assert!(viewer.viewport.start <= 4_000.0);
        assert!(viewer.viewport.end >= 6_000.0);

        viewer.step(
            &data,
            &Input {
                mouse_down: true,
                mouse_delta_x: 10_000.0,
                tracks_hovered: true,
                ..input()
            },
        );
        assert!(viewer.viewport.start <= 4_000.0);
        assert!(viewer.viewport.end >= 6_000.0);
    }

    #[test]
    fn snapping_requires_a_drag_and_ignores_culled_tracks() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 100,
            duration: 10,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0]));

        viewer.step(
            &data,
            &Input {
                mouse_x: 102.0,
                click_x: 100.0,
                drag_delta_x: 2.0,
                ruler_active: true,
                ruler_activated: true,
                ..input()
            },
        );
        assert!(viewer.snap_line.is_none());

        viewer.step(
            &data,
            &Input {
                mouse_x: 102.0,
                click_x: 100.0,
                drag_delta_x: 10.0,
                tracks_scroll_y: 1_000.0,
                ruler_active: true,
                ..input()
            },
        );
        assert!(viewer.snap_line.is_none());
    }

    #[test]
    fn box_selection_handles_long_events_and_ignores_counter_headers() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 0,
            duration: 1_000,
            ..PersistedEvent::default()
        });
        data.events.push(PersistedEvent {
            timestamp: 100,
            ..PersistedEvent::default()
        });
        let mut counter = Track::counter(0, Default::default(), Default::default());
        counter.event_indices.push(1);
        counter.update_max_duration(&data);
        let mut viewer = Viewer::default();
        viewer.viewport.end = 1_000.0;
        viewer.tracks.push(thread(&data, vec![0]));
        viewer.tracks.push(counter);
        viewer.step(&data, &input());

        viewer.box_start = (500.0, 45.0);
        viewer.box_end = (600.0, 55.0);
        viewer.update_box_selection(&data, &input(), 0.0, 20.0, 1_000.0, 480.0);
        assert_eq!(viewer.selected_events.as_slice(), [0]);
        assert_eq!(viewer.filtered_events, [0]);
        assert_eq!(viewer.histogram.total_count, 1);

        viewer.box_start = (50.0, 65.0);
        viewer.box_end = (150.0, 75.0);
        viewer.update_box_selection(&data, &input(), 0.0, 20.0, 1_000.0, 480.0);
        assert!(viewer.selected_events.is_empty());

        viewer.box_start = (50.0, 85.0);
        viewer.box_end = (150.0, 95.0);
        viewer.update_box_selection(&data, &input(), 0.0, 20.0, 1_000.0, 480.0);
        assert_eq!(viewer.selected_events.as_slice(), [1]);
        assert!(viewer.take_box_selection_changed());
        assert!(!viewer.take_box_selection_changed());
    }

    #[test]
    fn track_layout_names_selection_overlay_and_ruler_ticks_are_computed() {
        let mut data = TraceData::new();
        let name = data.intern(b"Worker");
        let id = data.intern(b"7");
        let mut named = Track::thread(0, 1);
        named.name = name;
        named.id = id;
        let unnamed = Track::thread(0, 42);
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 1_000,
            maximum: 3_000,
            start: 1_000.0,
            end: 3_000.0,
        };
        viewer.selection_active = true;
        viewer.selection_start = 1_500.0;
        viewer.selection_end = 2_000.0;
        viewer.tracks = vec![named, unnamed];

        viewer.step(&data, &input());

        assert_eq!(viewer.track_info[0].name, "Worker (7)");
        assert_eq!(viewer.track_info[1].name, "Thread 42");
        assert_eq!(viewer.track_info[0].relative_y, 0.0);
        assert_eq!(viewer.track_info[1].relative_y, 40.0);
        assert!(viewer.selection_layout.active);
        assert_eq!(viewer.selection_layout.x1, 250.0);
        assert_eq!(viewer.selection_layout.x2, 500.0);
        assert!(!viewer.ruler_ticks.is_empty());
        assert!(
            viewer
                .ruler_ticks
                .iter()
                .all(|tick| (0.0..=1_000.0).contains(&tick.x))
        );
    }

    #[test]
    fn track_layout_culls_only_tracks_outside_the_viewport() {
        let data = TraceData::new();
        let mut viewer = Viewer::default();
        viewer.tracks = (0..3).map(|tid| Track::thread(0, tid)).collect();
        let mut layout = Input {
            canvas_y: 100.0,
            canvas_width: 1_000.0,
            canvas_height: 100.0,
            ruler_height: 20.0,
            lane_height: 20.0,
            viewport_width: 1_000.0,
            viewport_height: 100.0,
            ..Input::default()
        };

        viewer.step(&data, &layout);
        assert_eq!(viewer.total_tracks_height, 120.0);
        assert_eq!(viewer.track_info[0].y, 120.0);
        assert_eq!(viewer.track_info[1].y, 160.0);
        assert!(viewer.track_info.iter().all(|info| info.visible));

        layout.tracks_scroll_y = 50.0;
        viewer.step(&data, &layout);
        assert!(!viewer.track_info[0].visible);
        assert!(viewer.track_info[1].visible);
        assert!(viewer.track_info[2].visible);
    }

    #[test]
    fn focus_request_centers_track_and_zero_duration_event() {
        let mut data = TraceData::new();
        data.events.push(PersistedEvent {
            timestamp: 5_000,
            ..PersistedEvent::default()
        });
        let mut viewer = Viewer::default();
        viewer.viewport = Viewport {
            minimum: 0,
            maximum: 10_000,
            start: 0.0,
            end: 10_000.0,
        };
        viewer.tracks = vec![Track::thread(0, 0), thread(&data, vec![0])];
        viewer.target_focused_event = Some(0);

        viewer.step(&data, &input());

        assert_eq!(viewer.focused_event, Some(0));
        assert!(!viewer.selection_active);
        assert_eq!(viewer.viewport.start, 0.0);
        assert_eq!(viewer.viewport.end, 10_000.0);
        assert_eq!(viewer.target_scroll_y, Some(-180.0));
    }

    #[test]
    fn minimap_slider_has_fixed_minimum_size_and_clamps_to_boundaries() {
        let data = TraceData::new();
        let mut viewer = Viewer::default();
        viewer.tracks = (0..100).map(|tid| Track::thread(0, tid)).collect();
        let mut base = Input {
            canvas_width: 736.0,
            viewport_width: 800.0,
            viewport_height: 200.0,
            canvas_height: 200.0,
            ruler_height: 20.0,
            lane_height: 20.0,
            ..Input::default()
        };
        viewer.step(&data, &base);
        assert_eq!(
            viewer.minimap_layout.slider_y2 - viewer.minimap_layout.slider_y1,
            10.0
        );
        assert_eq!(viewer.minimap_layout.slider_y1, 20.0);
        let scale = VERTICAL_MINIMAP_LANE_HEIGHT / base.lane_height;
        let initial_track_y = viewer
            .minimap_layout
            .project_y(viewer.track_info[50].relative_y, scale);

        base.tracks_scroll_y =
            viewer.total_tracks_height - (base.viewport_height - base.ruler_height);
        viewer.step(&data, &base);
        assert_eq!(
            viewer.minimap_layout.slider_y2,
            viewer.minimap_layout.y + viewer.minimap_layout.height
        );
        let scrolled_track_y = viewer
            .minimap_layout
            .project_y(viewer.track_info[50].relative_y, scale);
        assert_eq!(
            initial_track_y - scrolled_track_y,
            viewer.minimap_layout.minimap_scroll_y
        );
        assert!(scrolled_track_y < initial_track_y);
    }
}
