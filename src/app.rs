use crate::colors::{self, Theme};
use crate::imgui::{
    CHEATSHEET_TABLE_FLAGS, COL_POPUP_BG, COND_APPEARING, Frame, KEY_ENTER, KEY_F, KEY_SLASH,
    MAIN_VIEWPORT_WINDOW_FLAGS, MULTI_SELECT_TABLE_FLAGS, SELECTABLE_SPAN_OVERLAP,
    SHORTCUTS_POPUP_FLAGS, TABLE_COLUMN_FLAGS_WIDTH_FIXED, TABLE_COLUMN_FLAGS_WIDTH_STRETCH,
    TABLE_FLAGS_NO_SAVED_SETTINGS, TableSort, Vec2, WINDOW_FLAGS_NO_FOCUS_ON_APPEARING,
    WINDOW_FLAGS_NO_MOVE, WINDOW_FLAGS_NO_RESIZE, WINDOW_FLAGS_NO_SCROLLBAR,
};
use crate::platform;
use crate::trace::search::{
    DurationFilter, Options as SearchOptions, Sort as SearchSort, SortColumn,
};
use crate::trace::session::{LoadSession, LoadedTrace};
use crate::viewer::Viewer;
use base::allocation::CountingAllocator;
use base::task::{Completion, TaskHandle, TaskQueue, ThreadPoolExecutor};
use base::{debug, info};
use std::sync::Arc;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ThemeMode {
    #[default]
    Auto,
    Dark,
    Light,
}

impl ThemeMode {
    fn from_setting(value: &str) -> Option<Self> {
        match value {
            "auto" => Some(Self::Auto),
            "dark" => Some(Self::Dark),
            "light" => Some(Self::Light),
            _ => None,
        }
    }

    fn setting(self) -> &'static str {
        match self {
            Self::Auto => "auto",
            Self::Dark => "dark",
            Self::Light => "light",
        }
    }

    fn theme(self, system_dark: bool) -> Theme {
        match self {
            Self::Auto => {
                if system_dark {
                    colors::dark()
                } else {
                    colors::light()
                }
            }
            Self::Dark => colors::dark(),
            Self::Light => colors::light(),
        }
    }
}

#[derive(Default)]
pub struct LoadingState {
    pub event_count: usize,
    pub total_bytes: usize,
    pub input_total_bytes: usize,
    pub input_consumed_bytes: usize,
    pub start_time: f64,
    pub active: bool,
    pub session_id: i32,
    pub filename: String,
    pub request_update: bool,
}
#[derive(Debug)]
pub struct SearchState {
    pub query: String,
    pub include_threads: bool,
    pub include_counters: bool,
    pub focus_input: bool,
    pub selected_histogram_bucket: Option<usize>,
    sort: Option<SearchSort>,
    /// Work requested by the current frame and submitted at the start of the
    /// next frame, after ImGui has released its borrows.
    pending: Option<PendingSearch>,
    show_progress: bool,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum PendingSearch {
    Full,
    Display,
}

impl Default for SearchState {
    fn default() -> Self {
        Self {
            query: String::new(),
            include_threads: true,
            include_counters: true,
            focus_input: false,
            selected_histogram_bucket: None,
            sort: None,
            pending: None,
            show_progress: false,
        }
    }
}

impl SearchState {
    fn request_display_update(&mut self) {
        self.show_progress = false;
        if self.pending != Some(PendingSearch::Full) {
            self.pending = Some(PendingSearch::Display);
        }
    }
}

pub struct App {
    pub theme_mode: ThemeMode,
    pub theme: Theme,
    pub power_save_mode: bool,
    pub first_frame: bool,
    pub show_metrics: bool,
    pub show_about: bool,
    pub show_shortcuts: bool,
    pub loading: LoadingState,
    pub trace_data: Option<Arc<crate::trace::TraceData>>,
    pub viewer: Viewer,
    pub search: SearchState,
    load_session: Option<LoadSession>,
    task_queue: TaskQueue<AppTaskResult>,
    active_search: Option<TaskHandle>,
    active_search_kind: Option<PendingSearch>,
    search_generation: u64,
    theme_changed: bool,
    error: Option<String>,
}

enum AppTaskResult {
    Load(Result<LoadedTrace, String>),
    Search {
        generation: u64,
        output: Option<SearchOutput>,
    },
}

enum SearchOutput {
    Full(crate::trace::search::Results),
    Display(Vec<i64>),
}

impl Default for App {
    fn default() -> Self {
        Self::with_task_queue(TaskQueue::new(Arc::new(ThreadPoolExecutor::new(2)), 1024))
    }
}

impl App {
    fn with_task_queue(task_queue: TaskQueue<AppTaskResult>) -> Self {
        let theme_mode = platform::get_setting("theme_mode")
            .as_deref()
            .and_then(ThemeMode::from_setting)
            .unwrap_or_default();
        let theme = theme_mode.theme(platform::is_dark_mode());
        Self {
            theme_mode,
            theme,
            power_save_mode: true,
            first_frame: true,
            show_metrics: false,
            show_about: false,
            show_shortcuts: false,
            loading: LoadingState::default(),
            trace_data: None,
            viewer: Viewer::default(),
            search: SearchState::default(),
            load_session: None,
            task_queue,
            active_search: None,
            active_search_kind: None,
            search_generation: 0,
            theme_changed: true,
            error: None,
        }
    }

    pub fn new() -> Self {
        platform::initialize_main_thread();
        Self::default()
    }
    pub fn on_theme_changed(&mut self, dark: bool) {
        self.update_theme(self.theme_mode.theme(dark));
    }

    pub fn set_theme_mode(&mut self, mode: ThemeMode) {
        self.theme_mode = mode;
        platform::set_setting("theme_mode", mode.setting());
        self.update_theme(mode.theme(platform::is_dark_mode()));
        self.loading.request_update = true;
    }

    pub(crate) fn take_theme_changed(&mut self) -> bool {
        std::mem::take(&mut self.theme_changed)
    }

    fn update_theme(&mut self, theme: Theme) {
        if self.theme != theme {
            self.theme = theme;
            self.theme_changed = true;
        }
    }

    pub fn begin_session(
        &mut self,
        session_id: i32,
        filename: impl Into<String>,
        input_total_bytes: usize,
    ) -> bool {
        self.stop_jobs();
        self.trace_data = None;
        self.viewer = Viewer::default();
        self.search = SearchState::default();
        self.error = None;
        self.loading = LoadingState {
            input_total_bytes,
            start_time: platform::now_ms(),
            active: true,
            session_id,
            filename: filename.into(),
            request_update: true,
            ..LoadingState::default()
        };
        match LoadSession::new(&self.task_queue, AppTaskResult::Load) {
            Ok(session) => {
                self.load_session = Some(session);
                true
            }
            Err(error) => {
                debug!("app_begin_session: failed to submit loader: {error:?}");
                self.loading.active = false;
                self.load_session = None;
                self.error = Some(format!("Unable to start trace loader: {error:?}"));
                false
            }
        }
    }

    pub fn handle_file_chunk(
        &mut self,
        session_id: i32,
        data: Vec<u8>,
        input_consumed_bytes: usize,
        eof: bool,
    ) -> usize {
        if !self.loading.active || session_id != self.loading.session_id {
            return self.buffered_bytes();
        }
        self.loading.input_consumed_bytes = input_consumed_bytes;
        self.loading.request_update = true;
        if let Some(session) = &self.load_session {
            let _ = session.push(data, eof);
        }
        self.buffered_bytes()
    }
    pub fn buffered_bytes(&self) -> usize {
        self.load_session
            .as_ref()
            .map_or(0, |session| session.progress().buffered_bytes())
    }

    pub fn report_error(&mut self, error: impl Into<String>) {
        self.error = Some(error.into());
        self.loading.request_update = true;
    }

    pub fn fail_session(&mut self, session_id: i32, error: impl Into<String>) {
        if session_id != self.loading.session_id {
            return;
        }
        self.stop_jobs();
        self.report_error(error);
    }

    pub fn error(&self) -> Option<&str> {
        self.error.as_deref()
    }

    pub fn stop_jobs(&mut self) {
        if let Some(session) = self.load_session.take() {
            session.cancel()
        }
        if let Some(search) = self.active_search.take() {
            search.cancel();
        }
        self.active_search_kind = None;
        self.search.show_progress = false;
        self.search_generation = self.search_generation.wrapping_add(1);
        self.loading.active = false;
    }
    pub fn poll_completions(&mut self) {
        while let Some(completion) = self.task_queue.try_completion() {
            self.handle_completion(completion);
        }
        let progress = self.load_session.as_ref().map(|session| {
            (
                session.progress().event_count(),
                session.progress().parsed_bytes(),
            )
        });
        if let Some((events, bytes)) = progress {
            if events != self.loading.event_count || bytes != self.loading.total_bytes {
                self.loading.event_count = events;
                self.loading.total_bytes = bytes;
                self.loading.request_update = true
            }
        }
    }

    fn handle_completion(&mut self, completion: Completion<AppTaskResult>) {
        let load_finished =
            self.load_session.as_ref().and_then(LoadSession::task_id) == Some(completion.id);
        let search_finished =
            self.active_search.as_ref().map(TaskHandle::id) == Some(completion.id);
        if load_finished {
            self.load_session.take();
            self.loading.active = false;
            self.loading.request_update = true;
        }
        if search_finished {
            self.active_search = None;
            self.active_search_kind = None;
        }
        let Some(output) = completion.output else {
            return;
        };
        match output {
            AppTaskResult::Load(_) if !load_finished => {
                debug!("app_poll_completions: ignoring stale loader result");
            }
            AppTaskResult::Load(Ok(loaded)) => {
                debug!("app_poll_completions: loader task completed! Adopting results.");
                info!(
                    "parsed {} events, {:.2} MB in {:.3} ms ({:.2} mb/s) \
                     [starvation: {:.3} ms ({:.2}%)]",
                    loaded.data.events.len(),
                    loaded.decompressed_size as f64 / (1024.0 * 1024.0),
                    loaded.ingestion_ms,
                    loaded.throughput_mb_per_second,
                    loaded.starvation_ms,
                    loaded.starvation_percent,
                );
                info!(
                    "organized {} tracks in {:.3} ms",
                    loaded.tracks.len(),
                    loaded.organization_ms,
                );
                self.viewer.adopt_trace(
                    loaded.tracks,
                    loaded.minimum_timestamp,
                    loaded.maximum_timestamp,
                );
                self.trace_data = Some(Arc::new(loaded.data));
            }
            AppTaskResult::Load(Err(error)) => {
                debug!("app_poll_completions: loader task cancelled or failed: {error}");
                self.report_error(error);
            }
            AppTaskResult::Search { generation, output } => {
                if !search_finished || generation != self.search_generation {
                    debug!("app_poll_completions: ignoring stale search result");
                    return;
                }
                match output {
                    Some(SearchOutput::Full(results)) => {
                        self.viewer.adopt_search_results(
                            results.event_indices,
                            results.display_event_indices,
                            results.histogram,
                        );
                        self.viewer.set_show_details(true);
                        self.loading.request_update = true;
                    }
                    Some(SearchOutput::Display(indices)) => {
                        self.viewer.set_filtered_events(indices);
                        self.loading.request_update = true;
                    }
                    None => {}
                }
            }
        }
    }
    pub fn apply_search(&mut self) {
        let Some(data) = self.trace_data.as_ref().map(Arc::clone) else {
            return;
        };
        if let Some(search) = self.active_search.take() {
            search.cancel();
        }
        self.active_search_kind = None;
        self.search.show_progress = false;
        self.search_generation = self.search_generation.wrapping_add(1);
        if self.search.query.is_empty() {
            self.viewer.clear_search_results();
            self.search.selected_histogram_bucket = None;
            return;
        }
        let generation = self.search_generation;
        let query = self.search.query.clone();
        let options = SearchOptions {
            include_threads: self.search.include_threads,
            include_counters: self.search.include_counters,
            sort: self.search.sort,
            duration_filter: self.search_duration_filter(),
        };
        self.active_search = match self.task_queue.try_submit(move |cancel| {
            let results = crate::trace::search::compute(&data, query.as_bytes(), options, || {
                cancel.is_cancelled()
            });
            Ok(AppTaskResult::Search {
                generation,
                output: results.map(SearchOutput::Full),
            })
        }) {
            Ok(handle) => {
                self.active_search_kind = Some(PendingSearch::Full);
                self.search.show_progress = true;
                Some(handle)
            }
            Err(error) => {
                debug!("app_update: failed to submit search: {error:?}");
                self.report_error(format!("Unable to start search: {error:?}"));
                None
            }
        };
    }

    fn apply_search_display(&mut self) {
        let Some(data) = self.trace_data.as_ref().map(Arc::clone) else {
            return;
        };
        if let Some(search) = self.active_search.take() {
            search.cancel();
        }
        self.active_search_kind = None;
        self.search.show_progress = false;
        self.search_generation = self.search_generation.wrapping_add(1);
        let generation = self.search_generation;
        let indices = self.viewer.selected_events_snapshot();
        let sort = self.search.sort;
        let duration_filter = self.search_duration_filter();
        self.active_search = match self.task_queue.try_submit(move |cancel| {
            let output = crate::trace::search::derive_display(
                &data,
                &indices,
                sort,
                duration_filter,
                || cancel.is_cancelled(),
            );
            Ok(AppTaskResult::Search {
                generation,
                output: output.map(SearchOutput::Display),
            })
        }) {
            Ok(handle) => {
                self.active_search_kind = Some(PendingSearch::Display);
                Some(handle)
            }
            Err(error) => {
                debug!("app_update: failed to submit search display update: {error:?}");
                self.report_error(format!("Unable to update search results: {error:?}"));
                None
            }
        };
    }

    fn search_duration_filter(&self) -> Option<DurationFilter> {
        self.search
            .selected_histogram_bucket
            .and_then(|index| self.viewer.histogram().buckets.get(index))
            .map(|bucket| DurationFilter {
                minimum: bucket.min_duration,
                maximum: bucket.max_duration,
            })
    }

    pub fn search_active(&self) -> bool {
        self.active_search.is_some()
    }

    fn search_status_visible(&self) -> bool {
        self.search.show_progress
            && (self.search.pending == Some(PendingSearch::Full)
                || self.active_search_kind == Some(PendingSearch::Full))
    }

    fn focus_search(&mut self) {
        self.viewer.set_show_details(true);
        self.search.focus_input = true;
    }

    pub fn draw(&mut self, frame: &Frame<'_>) {
        self.loading.request_update = false;
        if let Some(pending) = self.search.pending.take() {
            match pending {
                PendingSearch::Full => self.apply_search(),
                PendingSearch::Display => self.apply_search_display(),
            }
        }
        let primary_modifier =
            platform::primary_modifier_down(frame.ctrl_down(), frame.super_down());
        if frame.key_pressed(KEY_F) && primary_modifier {
            self.focus_search();
        }
        if !frame.want_text_input() && frame.key_pressed(KEY_SLASH) && frame.shift_down() {
            self.show_shortcuts = !self.show_shortcuts;
        }
        if let Some(menu_bar) = frame.begin_main_menu_bar() {
            let frame = menu_bar.frame();
            if let Some(menu) = frame.begin_menu("View") {
                let frame = menu.frame();
                if frame.menu_item("Reset View", None, false) {
                    self.viewer.reset_view();
                }
                frame.separator();
                frame.menu_item_toggle("Power-save Mode", &mut self.power_save_mode);
                let mut show_details = self.viewer.show_details();
                frame.menu_item_toggle("Details Panel", &mut show_details);
                self.viewer.set_show_details(show_details);
                frame.separator();
                if let Some(theme_menu) = frame.begin_menu("Theme") {
                    let frame = theme_menu.frame();
                    if frame.menu_item("Auto", None, self.theme_mode == ThemeMode::Auto) {
                        self.set_theme_mode(ThemeMode::Auto);
                    }
                    if frame.menu_item("Dark", None, self.theme_mode == ThemeMode::Dark) {
                        self.set_theme_mode(ThemeMode::Dark);
                    }
                    if frame.menu_item("Light", None, self.theme_mode == ThemeMode::Light) {
                        self.set_theme_mode(ThemeMode::Light);
                    }
                    drop(theme_menu);
                }
                drop(menu);
            }
            if let Some(menu) = frame.begin_menu("Tools") {
                let frame = menu.frame();
                if frame.menu_item(
                    "Search Events",
                    Some(if platform::is_mac() {
                        "Cmd+F"
                    } else {
                        "Ctrl+F"
                    }),
                    false,
                ) {
                    self.focus_search();
                }
                frame.menu_item_toggle("Metrics/Debugger", &mut self.show_metrics);
                drop(menu);
            }
            if let Some(menu) = frame.begin_menu("Help") {
                let frame = menu.frame();
                if frame.menu_item("Shortcuts", Some("?"), false) {
                    self.show_shortcuts = true;
                }
                frame.menu_item_toggle("About Dear ImGui", &mut self.show_about);
                drop(menu);
            }
            let memory = format!(
                "{:.1} MB",
                CountingAllocator::live_bytes() as f64 / (1024.0 * 1024.0)
            );
            let width = frame.calc_text_size(&memory).x;
            frame.same_line(frame.window_size().x - width - 16.0, 0.0);
            frame.text_colored(frame.color(self.theme.ui_text_disabled), &memory);
            drop(menu_bar);
        }
        frame.setup_default_dock(self.first_frame);
        self.first_frame = false;
        let window_style = frame.push_window_style();
        if let Some(window) = frame.begin_window("Main Viewport", None, MAIN_VIEWPORT_WINDOW_FLAGS)
        {
            if window.visible {
                let frame = window.frame();
                if self.loading.active {
                    self.draw_loading(frame)
                } else if self
                    .trace_data
                    .as_ref()
                    .is_none_or(|data| data.events.is_empty())
                {
                    self.draw_welcome(frame)
                } else {
                    self.draw_timeline_placeholder(frame)
                }
            }
        }
        // Details inherits the same borderless window style as the main viewport.
        self.draw_details(frame);
        drop(window_style);
        if self.show_metrics {
            frame.show_metrics_window(&mut self.show_metrics);
        }
        if self.show_about {
            frame.show_about_window(&mut self.show_about);
        }
        self.draw_error(frame);
        self.draw_shortcuts(frame);
    }
    fn draw_shortcuts(&mut self, frame: &Frame<'_>) {
        let viewport_size = frame.main_viewport().size();
        frame.set_next_window_position(
            Vec2 {
                x: viewport_size.x * 0.5,
                y: viewport_size.y * 0.5,
            },
            COND_APPEARING,
            Vec2 { x: 0.5, y: 0.5 },
        );
        frame.set_next_window_size(
            Vec2 {
                x: viewport_size.x * 0.7,
                y: viewport_size.y * 0.7,
            },
            COND_APPEARING,
        );
        if self.show_shortcuts {
            frame.open_popup("Shortcuts");
        }
        let _popup_color = frame.push_style_color_u32(COL_POPUP_BG, self.theme.viewport_bg);
        if let Some(_popup) = frame.begin_popup_modal(
            "Shortcuts",
            Some(&mut self.show_shortcuts),
            SHORTCUTS_POPUP_FLAGS,
        ) {
            if frame.mouse_clicked() {
                let mouse = frame.mouse_position();
                let window_pos = frame.window_position();
                let window_size = frame.window_size();
                let inside = mouse.x >= window_pos.x
                    && mouse.x <= window_pos.x + window_size.x
                    && mouse.y >= window_pos.y
                    && mouse.y <= window_pos.y + window_size.y;
                if !inside {
                    self.show_shortcuts = false;
                    self.viewer.set_ignore_next_release();
                    frame.close_current_popup();
                }
            }
            if let Some(child) = frame.begin_child(
                "CheatsheetContent",
                Vec2::default(),
                false,
                WINDOW_FLAGS_NO_SCROLLBAR,
            ) {
                let frame = child.frame();
                let width = frame.content_available().x;
                let gap = 40.0;
                let col_w = (width - gap) * 0.5;

                if let Some(left) = frame.begin_child(
                    "LeftCol",
                    Vec2 { x: col_w, y: 0.0 },
                    false,
                    WINDOW_FLAGS_NO_SCROLLBAR,
                ) {
                    let frame = left.frame();
                    self.draw_cheatsheet_section(
                        frame,
                        "GENERAL",
                        &[
                            ("Toggle Shortcuts", "?"),
                            (
                                "Search Events",
                                if crate::platform::is_mac() {
                                    "Cmd + F"
                                } else {
                                    "Ctrl + F"
                                },
                            ),
                            ("Toggle Details", "Menu > View"),
                            ("Metrics / Debug", "Menu > Tools"),
                        ],
                    );
                    self.draw_cheatsheet_section(
                        frame,
                        "NAVIGATION",
                        &[
                            ("Zoom In/Out", "Ctrl + Scroll"),
                            ("Zoom to Event", "Double Click"),
                            ("Pan Horizontally", "Shift + Scroll"),
                            ("Pan (Any Direction)", "Left Drag"),
                            ("Reset View", "Menu > View"),
                        ],
                    );
                }
                frame.same_line(0.0, gap);
                if let Some(right) = frame.begin_child(
                    "RightCol",
                    Vec2 { x: col_w, y: 0.0 },
                    false,
                    WINDOW_FLAGS_NO_SCROLLBAR,
                ) {
                    let frame = right.frame();
                    self.draw_cheatsheet_section(
                        frame,
                        "SELECTION",
                        &[
                            ("Timeline Selection", "Drag on Ruler"),
                            ("Rectangle Select", "Shift + Drag"),
                            ("Select Event", "Left Click"),
                            ("Clear Selection", "Click Background"),
                            ("Clear Focused", "Click Background"),
                        ],
                    );
                }
            }
        }
    }
    fn draw_cheatsheet_section(&self, frame: &Frame<'_>, title: &str, items: &[(&str, &str)]) {
        let _group = frame.begin_group();
        frame.spacing();
        frame.text_colored(frame.color(self.theme.track_text), title);
        frame.separator();
        frame.spacing();
        if let Some(_table) = frame.begin_table(title, 2, CHEATSHEET_TABLE_FLAGS, Vec2::default()) {
            frame.table_setup_column("Action", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 150.0);
            frame.table_setup_column("Key", TABLE_COLUMN_FLAGS_WIDTH_STRETCH, 0.0);
            for &(action, shortcut) in items {
                frame.table_next_row();
                frame.table_next_column();
                frame.text_unformatted(action);
                frame.table_next_column();
                frame.text_colored(frame.color(self.theme.ruler_text), shortcut);
            }
        }
    }
    fn draw_details(&mut self, frame: &Frame<'_>) {
        if !self.viewer.show_details() {
            return;
        }
        let _window_padding = frame.push_window_padding(10.0);
        let mut open = true;
        if let Some(window) = frame.begin_window(
            "Details",
            Some(&mut open),
            WINDOW_FLAGS_NO_FOCUS_ON_APPEARING,
        ) {
            if window.visible {
                let details = window.frame();
                details.text_disabled("Search Events");
                if self.search.focus_input {
                    details.set_keyboard_focus_here();
                    self.search.focus_input = false;
                }
                let mut search_changed = details.input_text("##search", &mut self.search.query);
                search_changed |= details.item_focused() && details.key_pressed(KEY_ENTER);
                details.spacing();
                let mut threads = self.search.include_threads;
                let mut counters = self.search.include_counters;
                search_changed |= details.checkbox("Threads", &mut threads);
                details.same_line(0.0, -1.0);
                search_changed |= details.checkbox("Counters", &mut counters);
                self.search.include_threads = threads;
                self.search.include_counters = counters;
                if search_changed {
                    self.search.selected_histogram_bucket = None;
                    self.search.pending = Some(PendingSearch::Full);
                    self.search.show_progress = true;
                }
                if !self.search.query.is_empty() {
                    details.text(&format!(
                        "{} events selected",
                        self.viewer.selected_events().len()
                    ));
                    if self.search_status_visible() {
                        details.same_line(0.0, -1.0);
                        details.text_disabled("(searching...)");
                    }
                }
                details.separator();

                let data = self.trace_data.as_ref();
                let has_selection = !self.viewer.selected_events().is_empty();
                if has_selection {
                    if let Some(data) = data {
                        Self::draw_selection_section(
                            details,
                            data,
                            &mut self.viewer,
                            &mut self.search,
                            &self.theme,
                        );
                    }
                }
                if has_selection && self.viewer.focused_event().is_some() {
                    details.separator();
                }
                if let (Some(data), Some(index)) = (data, self.viewer.focused_event()) {
                    details.text_disabled("Focused Event");
                    details.spacing();
                    Self::draw_event_properties(details, data, &self.viewer, &self.theme, index);
                } else if !has_selection {
                    details.text_disabled("Select an event to see details.");
                }
            }
        }
        self.viewer.set_show_details(open);
    }

    fn draw_selection_section(
        details: &Frame<'_>,
        data: &crate::trace::TraceData,
        viewer: &mut Viewer,
        search: &mut SearchState,
        theme: &Theme,
    ) {
        details.text_disabled(&format!(
            "Selection ({} events)",
            viewer.selected_events().len()
        ));
        details.same_line(0.0, -1.0);
        if details.small_button("Clear") {
            viewer.clear_search_results();
            search.selected_histogram_bucket = None;
            return;
        }
        let histogram = viewer.histogram().clone();
        if !histogram.buckets.is_empty() {
            details.spacing();
            details.text_disabled("Duration Distribution");
            let position = details.cursor_screen_position();
            let width = details.content_available().x;
            let height = 80.0;
            details.invisible_button(
                "##dur_histogram",
                Vec2 {
                    x: width,
                    y: height,
                },
            );
            let hovered = details.item_hovered();
            let mouse = details.mouse_position();
            let list = details.draw_list();
            list.rect_filled(
                position,
                Vec2 {
                    x: position.x + width,
                    y: position.y + height,
                },
                theme.search_histogram_bg,
            );
            let spacing = 2.0;
            let bar_width =
                ((width - 10.0 - spacing * (histogram.buckets.len().saturating_sub(1) as f32))
                    / histogram.buckets.len().max(1) as f32)
                    .max(1.0);
            let baseline = position.y + height - 20.0;
            let mut hovered_bucket = None;
            for (index, bucket) in histogram.buckets.iter().enumerate() {
                let ratio = if histogram.max_bucket_count > 0 {
                    bucket.count as f32 / histogram.max_bucket_count as f32
                } else {
                    0.0
                };
                let bar_height = ratio * (height - 35.0);
                let x1 = position.x + 5.0 + index as f32 * (bar_width + spacing);
                let x2 = x1 + bar_width;
                let y1 = baseline - bar_height;
                if hovered
                    && mouse.x >= x1
                    && mouse.x <= x2
                    && mouse.y >= position.y
                    && mouse.y <= baseline
                {
                    hovered_bucket = Some(index);
                }
                if bucket.count > 0 {
                    let color = if search.selected_histogram_bucket == Some(index) {
                        theme.search_histogram_bar_selected
                    } else if hovered_bucket == Some(index) {
                        theme.search_histogram_bar_hovered
                    } else {
                        theme.search_histogram_bar
                    };
                    list.rect_filled(Vec2 { x: x1, y: y1 }, Vec2 { x: x2, y: baseline }, color);
                }
            }
            list.line(
                Vec2 {
                    x: position.x + 5.0,
                    y: baseline,
                },
                Vec2 {
                    x: position.x + width - 5.0,
                    y: baseline,
                },
                theme.ruler_border,
                1.0,
            );
            list.line(
                Vec2 {
                    x: position.x + 5.0,
                    y: position.y + 5.0,
                },
                Vec2 {
                    x: position.x + 5.0,
                    y: baseline,
                },
                theme.ruler_border,
                1.0,
            );
            let font_size = details.font_size() * 0.75;
            list.text_sized(
                Vec2 {
                    x: position.x + 8.0,
                    y: position.y + 4.0,
                },
                theme.ruler_text,
                font_size,
                &histogram.max_bucket_count.to_string(),
            );
            list.text_sized(
                Vec2 {
                    x: position.x + 8.0,
                    y: baseline - 12.0,
                },
                theme.ruler_text,
                font_size,
                "0",
            );
            if let Some(index) = hovered_bucket {
                let bucket = histogram.buckets[index];
                let percent = if histogram.total_count > 0 {
                    bucket.count as f32 / histogram.total_count as f32 * 100.0
                } else {
                    0.0
                };
                details.tooltip(|tooltip| {
                    tooltip.text(&format!(
                        "Duration: {} - {}",
                        crate::format::duration(
                            bucket.min_duration as f64,
                            bucket.max_duration as f64
                        ),
                        crate::format::duration(
                            bucket.max_duration as f64,
                            bucket.max_duration as f64
                        )
                    ));
                    tooltip.text(&format!("Count: {} events ({percent:.1}%)", bucket.count));
                    if bucket.count > 0 {
                        tooltip.text_colored(
                            tooltip.color(theme.ui_text_disabled),
                            "Click to filter table below",
                        );
                    }
                });
                if details.mouse_clicked() && bucket.count > 0 {
                    search.selected_histogram_bucket =
                        if search.selected_histogram_bucket == Some(index) {
                            None
                        } else {
                            Some(index)
                        };
                    search.request_display_update();
                }
            }
            let first = if histogram.buckets.len() > 1
                && histogram.buckets[0].min_duration <= 0
                && histogram.buckets[0].max_duration <= 0
            {
                Some(&histogram.buckets[1])
            } else {
                histogram.buckets.first()
            };
            if let (Some(first), Some(last)) = (first, histogram.buckets.last()) {
                let min_label = crate::format::duration(first.min_duration as f64, 0.0);
                list.text_sized(
                    Vec2 {
                        x: position.x + 5.0,
                        y: baseline + 2.0,
                    },
                    theme.ruler_text,
                    font_size,
                    &min_label,
                );
                let max = crate::format::duration(last.max_duration as f64, 0.0);
                let size = details.calc_text_size(&max);
                list.text_sized(
                    Vec2 {
                        x: position.x + width - size.x * 0.75 - 5.0,
                        y: baseline + 2.0,
                    },
                    theme.ruler_text,
                    font_size,
                    &max,
                );
            }
        }

        if search.selected_histogram_bucket.is_some() {
            details.spacing();
            details.text_colored(
                details.color(theme.ui_text_disabled),
                &format!(
                    "Filtered results: {} / {} events",
                    viewer.filtered_events().len(),
                    viewer.selected_events().len()
                ),
            );
        }

        if let Some(table) = details.begin_table(
            "##multi_select",
            4,
            MULTI_SELECT_TABLE_FLAGS,
            Vec2 { x: 0.0, y: 200.0 },
        ) {
            let table_frame = table.frame();
            table_frame.table_setup_column("Name", TABLE_COLUMN_FLAGS_WIDTH_STRETCH, 0.0);
            table_frame.table_setup_column("Category", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
            table_frame.table_setup_column("Start", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
            table_frame.table_setup_column("Duration", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
            table_frame.table_setup_scroll_freeze(0, 1);
            table_frame.table_headers_row();
            if let Some(update) = table_frame.table_sort_update() {
                let sort = match update {
                    TableSort::Unsorted => None,
                    TableSort::Column { column, ascending } => {
                        let column = match column {
                            0 => SortColumn::Name,
                            1 => SortColumn::Category,
                            2 => SortColumn::Start,
                            3 => SortColumn::Duration,
                            _ => SortColumn::Start,
                        };
                        Some(SearchSort { column, ascending })
                    }
                };
                if search.sort != sort {
                    search.sort = sort;
                    search.request_display_update();
                }
            }
            let filtered = viewer.filtered_events();
            let focused_event = viewer.focused_event();
            let mut target_focused_event = None;
            let clipper = table_frame.list_clipper(filtered.len() as i32, -1.0);
            while clipper.step() {
                let start = clipper.display_start().max(0) as usize;
                let end = (clipper.display_end().max(0) as usize).min(filtered.len());
                for event_index in &filtered[start..end] {
                    let Ok(index) = usize::try_from(*event_index) else {
                        continue;
                    };
                    let Some(event) = data.events.get(index) else {
                        continue;
                    };
                    table_frame.table_next_row();
                    table_frame.table_next_column();
                    let name = data.string_lossy(event.name);
                    let row = format!("{name}##{index}");
                    if table_frame.selectable_flags(
                        &row,
                        focused_event == Some(index),
                        SELECTABLE_SPAN_OVERLAP,
                    ) {
                        target_focused_event = Some(index);
                    }
                    table_frame.table_next_column();
                    table_frame.text(&data.string_lossy(event.category));
                    table_frame.table_next_column();
                    let start = event.timestamp.saturating_sub(viewer.minimum_timestamp());
                    table_frame.text(&crate::format::duration(start as f64, 0.0));
                    table_frame.table_next_column();
                    if event.duration > 0 {
                        table_frame.text(&crate::format::duration(event.duration as f64, 0.0));
                    }
                }
            }
            if target_focused_event.is_some() {
                viewer.request_focus_event(target_focused_event);
            }
        }
    }

    fn draw_event_properties(
        details: &Frame<'_>,
        data: &crate::trace::TraceData,
        viewer: &Viewer,
        theme: &Theme,
        index: usize,
    ) {
        let _theme = theme;
        let Some(event) = data.events.get(index) else {
            return;
        };
        let name = data.string_lossy(event.name).into_owned();
        let category = data.string_lossy(event.category).into_owned();
        let self_duration = viewer.self_duration(index).unwrap_or(event.duration);
        let start = event.timestamp.saturating_sub(viewer.minimum_timestamp());
        details.spacing();
        let Some(_table) = details.begin_table(
            "##focused_event_table_unified",
            3,
            TABLE_FLAGS_NO_SAVED_SETTINGS,
            Vec2::default(),
        ) else {
            return;
        };
        details.table_setup_column("Label", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
        details.table_setup_column("Value", TABLE_COLUMN_FLAGS_WIDTH_STRETCH, 0.0);
        details.table_setup_column("Action", TABLE_COLUMN_FLAGS_WIDTH_FIXED, 0.0);
        let add_row = |label: &str, value: &str, copy_id: Option<&str>| {
            details.table_next_row();
            details.table_next_column();
            details.text_disabled(label);
            details.table_next_column();
            if copy_id.is_some() {
                details.text_wrapped(value);
            } else {
                details.text_unformatted(value);
            }
            details.table_next_column();
            if let Some(copy_id) = copy_id {
                if details.small_button(&format!("Copy##{copy_id}")) {
                    details.set_clipboard(value);
                }
            }
        };
        add_row("Name", &name, Some("Name"));
        if !category.is_empty() {
            add_row("Category", &category, Some("Category"));
        }
        let start_text = crate::format::duration(start as f64, 0.0);
        add_row("Start", &start_text, None);
        if event.duration > 0 {
            let duration_text = crate::format::duration(event.duration as f64, 0.0);
            add_row("Duration", &duration_text, None);
            let self_text = crate::format::duration(self_duration as f64, 0.0);
            add_row("Self Time", &self_text, None);
        }
        let pid_tid = format!("{} / {}", event.process_id, event.thread_id);
        add_row("PID / TID", &pid_tid, None);
        if !data.string(event.id).is_empty() {
            let id = data.string_lossy(event.id).into_owned();
            add_row("ID", &id, Some("id"));
        }
        for arg in data.event_args(event) {
            let key = data.string_lossy(arg.key).into_owned();
            let value = if data.string(arg.value).is_empty() {
                format!("{:.2}", arg.number)
            } else {
                data.string_lossy(arg.value).into_owned()
            };
            add_row(&key, &value, Some(&key));
        }
    }
    fn draw_welcome(&self, frame: &Frame<'_>) {
        let position = frame.cursor_screen_position();
        let available = frame.content_available();
        frame.draw_list().rect_filled(
            position,
            Vec2 {
                x: position.x + available.x,
                y: position.y + available.y,
            },
            self.theme.viewport_bg,
        );
        let message = "Drop a Chrome Trace file here to begin, or";
        let text_size = frame.calc_text_size(message);
        frame.set_cursor_screen_position(Vec2 {
            x: position.x + (available.x - text_size.x) * 0.5,
            y: position.y + (available.y - text_size.y) * 0.5 - 20.0,
        });
        frame.text(message);
        let label = "Select a file";
        let button = Vec2 { x: 120.0, y: 30.0 };
        frame.set_cursor_screen_position(Vec2 {
            x: position.x + (available.x - button.x) * 0.5,
            y: position.y + (available.y - button.y) * 0.5 + 20.0,
        });
        if frame.button(label, button) {
            platform::open_file_dialog()
        }
    }

    fn draw_error(&mut self, frame: &Frame<'_>) {
        let Some(error) = self.error.clone() else {
            return;
        };
        let center = frame.main_viewport().center();
        frame.set_next_window_position(center, COND_APPEARING, Vec2 { x: 0.5, y: 0.5 });
        frame.set_next_window_size(Vec2 { x: 420.0, y: 128.0 }, COND_APPEARING);
        frame.open_popup("Unable to Load Trace");
        let _popup_color = frame.push_style_color_u32(COL_POPUP_BG, self.theme.viewport_bg);
        let Some(_popup) = frame.begin_popup_modal(
            "Unable to Load Trace",
            None,
            WINDOW_FLAGS_NO_MOVE | WINDOW_FLAGS_NO_RESIZE | WINDOW_FLAGS_NO_SCROLLBAR,
        ) else {
            return;
        };
        frame.text_wrapped(&error);

        let button = Vec2 { x: 88.0, y: 26.0 };
        let window_position = frame.window_position();
        let window_size = frame.window_size();
        frame.set_cursor_screen_position(Vec2 {
            x: window_position.x + window_size.x - button.x - 12.0,
            y: window_position.y + window_size.y - button.y - 12.0,
        });
        if frame.button("Dismiss", button) {
            self.error = None;
            frame.close_current_popup();
        }
    }

    fn draw_loading(&self, frame: &Frame<'_>) {
        let position = frame.cursor_screen_position();
        let available = frame.content_available();
        frame.draw_list().rect_filled(
            position,
            Vec2 {
                x: position.x + available.x,
                y: position.y + available.y,
            },
            self.theme.viewport_bg,
        );
        let center = Vec2 {
            x: position.x + available.x * 0.5,
            y: position.y + available.y * 0.5,
        };
        let title = "Loading Trace...";
        let title_size = frame.calc_text_size(title);
        frame.set_cursor_screen_position(Vec2 {
            x: center.x - title_size.x * 0.5,
            y: center.y - 70.0,
        });
        frame.text(title);
        if !self.loading.filename.is_empty() {
            let size = frame.calc_text_size(&self.loading.filename);
            frame.set_cursor_screen_position(Vec2 {
                x: center.x - size.x * 0.5,
                y: center.y - 40.0,
            });
            frame.text_colored(frame.color(self.theme.ruler_text), &self.loading.filename)
        }
        if self.loading.input_total_bytes > 0 {
            let fraction = (self.loading.input_consumed_bytes as f32
                / self.loading.input_total_bytes as f32)
                .min(1.0);
            frame.set_cursor_screen_position(Vec2 {
                x: center.x - 150.0,
                y: center.y - 10.0,
            });
            frame.progress_bar(fraction, Vec2 { x: 300.0, y: 0.0 }, None)
        }
        let progress = format!(
            "Parsed {} events ({:.2} MB)",
            self.loading.event_count,
            self.loading.total_bytes as f64 / (1024.0 * 1024.0)
        );
        let size = frame.calc_text_size(&progress);
        frame.set_cursor_screen_position(Vec2 {
            x: center.x - size.x * 0.5,
            y: center.y + 25.0,
        });
        frame.text_colored(self.theme.status_loading, &progress);
    }
    fn draw_timeline_placeholder(&mut self, frame: &Frame<'_>) {
        let Some(data) = &self.trace_data else { return };
        self.viewer.draw(data, frame, &self.theme);
        if self.viewer.take_box_selection_changed() {
            self.search.selected_histogram_bucket = None;
            self.search.request_display_update();
        }
    }
}
impl Drop for App {
    fn drop(&mut self) {
        self.stop_jobs()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::data::PersistedEvent;
    use base::task::{ExecuteError, Executor, ExecutorJob};
    use std::sync::Mutex;
    use std::time::{Duration, Instant};

    #[derive(Default)]
    struct ManualExecutor {
        job: Mutex<Option<ExecutorJob>>,
    }

    impl ManualExecutor {
        fn run(&self) {
            self.job
                .lock()
                .expect("manual executor mutex poisoned")
                .take()
                .expect("executor job exists")();
        }
    }

    impl Executor for ManualExecutor {
        fn execute(&self, job: ExecutorJob) -> Result<(), ExecuteError> {
            *self.job.lock().expect("manual executor mutex poisoned") = Some(job);
            Ok(())
        }
    }

    fn wait_until(app: &mut App, description: &str, mut complete: impl FnMut(&App) -> bool) {
        let deadline = Instant::now() + Duration::from_secs(5);
        while !complete(app) {
            assert!(
                Instant::now() < deadline,
                "timed out waiting for {description}"
            );
            app.poll_completions();
            std::thread::yield_now();
        }
    }

    fn searchable_data(name: &[u8]) -> Arc<crate::trace::TraceData> {
        let mut data = crate::trace::TraceData::new();
        let name = data.intern(name);
        let phase = data.intern(b"X");
        data.events.push(PersistedEvent {
            name,
            phase,
            duration: 1,
            ..PersistedEvent::default()
        });
        Arc::new(data)
    }

    #[test]
    fn loading_session_adopts_trace() {
        let mut app = App::new();
        assert!(app.begin_session(7, "trace.json", 2));
        app.handle_file_chunk(7, b"[]".to_vec(), 2, true);
        wait_until(&mut app, "trace loading", |app| !app.loading.active);
        assert!(app.trace_data.is_some());
    }

    #[test]
    fn malformed_trace_exposes_its_error_to_the_ui() {
        let mut app = App::new();
        assert!(app.begin_session(7, "broken.json", 9));
        app.handle_file_chunk(7, br#"[{"name""#.to_vec(), 9, true);
        wait_until(&mut app, "malformed trace", |app| !app.loading.active);

        assert!(!app.error().expect("load error").is_empty());
    }

    #[test]
    fn stale_session_error_does_not_replace_current_ui() {
        let mut app = App::new();
        assert!(app.begin_session(7, "current.json", 2));

        app.fail_session(6, "stale failure");
        assert!(app.error().is_none());
        assert!(app.loading.active);

        app.fail_session(7, "current failure");
        assert_eq!(app.error(), Some("current failure"));
        assert!(!app.loading.active);
    }

    #[test]
    fn regression_rapid_loader_session_restart_race() {
        let mut app = App::new();
        let first = br#"[{"name":"Event1","cat":"ui","ph":"B","pid":1,"tid":1,"ts":100}]"#;
        app.begin_session(1, "trace1.json", first.len());
        app.handle_file_chunk(1, first.to_vec(), first.len(), false);
        let second =
            br#"[{"name":"Event2","cat":"ui","ph":"X","pid":1,"tid":1,"ts":200,"dur":50}]"#;
        app.begin_session(2, "trace2.json", second.len());
        app.handle_file_chunk(2, second.to_vec(), second.len(), true);
        wait_until(&mut app, "replacement trace loading", |app| {
            !app.loading.active
        });
        let data = app.trace_data.as_ref().expect("second trace loaded");
        assert_eq!(data.events.len(), 1);
        assert_eq!(data.string_lossy(data.events[0].name), "Event2")
    }
    #[test]
    fn stale_chunks_are_ignored() {
        let mut app = App::new();
        app.begin_session(2, "trace.json", 2);
        assert_eq!(app.handle_file_chunk(1, b"[]".to_vec(), 2, true), 0);
    }

    #[test]
    fn completed_stale_session_cannot_replace_current_session() {
        let mut app = App::new();
        app.begin_session(1, "old.json", 2);
        app.handle_file_chunk(1, b"[]".to_vec(), 2, true);
        let stale = app
            .task_queue
            .wait_completion_timeout(std::time::Duration::from_secs(1))
            .expect("old session completed");

        app.begin_session(2, "current.json", 2);
        app.handle_completion(stale);
        assert!(app.loading.active);
        assert_eq!(app.loading.session_id, 2);
        assert!(app.trace_data.is_none());

        app.handle_file_chunk(2, b"[]".to_vec(), 2, true);
        wait_until(&mut app, "current trace loading", |app| !app.loading.active);
        assert!(app.trace_data.is_some());
    }

    #[test]
    fn replacing_a_session_does_not_block_when_the_task_queue_is_full() {
        let executor = Arc::new(ManualExecutor::default());
        let queue = TaskQueue::new(executor.clone(), 1);
        let mut app = App::with_task_queue(queue);
        assert!(app.begin_session(1, "first.json", 2));
        assert!(app.loading.active);

        let started = Instant::now();
        assert!(!app.begin_session(2, "second.json", 2));

        assert!(started.elapsed() < Duration::from_secs(1));
        assert!(!app.loading.active);
        assert_eq!(app.loading.session_id, 2);
        assert!(app.load_session.is_none());
        assert!(app.loading.request_update);

        executor.run();
        app.poll_completions();
    }

    #[test]
    fn completed_stale_search_cannot_populate_a_new_session() {
        let mut app = App::new();
        app.trace_data = Some(searchable_data(b"old event"));
        app.search.query = "old".to_owned();
        app.apply_search();
        let stale = app
            .task_queue
            .wait_completion_timeout(std::time::Duration::from_secs(1))
            .expect("old search completed");

        app.begin_session(2, "new.json", 2);
        app.handle_completion(stale);

        assert!(app.viewer.selected_events().is_empty());
        assert!(app.viewer.filtered_events().is_empty());
        assert!(app.viewer.histogram().buckets.is_empty());
        assert!(app.loading.active);
    }

    #[test]
    fn only_the_active_search_result_is_adopted() {
        let mut app = App::new();
        app.trace_data = Some(searchable_data(b"first second"));

        app.search.query = "first".to_owned();
        app.apply_search();
        let stale = app
            .task_queue
            .wait_completion_timeout(std::time::Duration::from_secs(1))
            .expect("first search completed");

        app.search.query = "second".to_owned();
        app.apply_search();
        app.handle_completion(stale);
        assert!(app.viewer.selected_events().is_empty());

        wait_until(&mut app, "active search", |app| !app.search_active());
        assert_eq!(app.viewer.selected_events(), [0]);
    }

    #[test]
    fn focusing_search_opens_details_and_requests_keyboard_focus() {
        let mut app = App::new();
        app.viewer.set_show_details(false);

        app.focus_search();

        assert!(app.viewer.show_details());
        assert!(app.search.focus_input);
    }

    #[test]
    fn theme_modes_resolve_and_track_system_changes() {
        assert_eq!(ThemeMode::from_setting("auto"), Some(ThemeMode::Auto));
        assert_eq!(ThemeMode::from_setting("dark"), Some(ThemeMode::Dark));
        assert_eq!(ThemeMode::from_setting("light"), Some(ThemeMode::Light));
        assert_eq!(ThemeMode::from_setting("invalid"), None);

        assert_eq!(ThemeMode::Auto.theme(true), colors::dark());
        assert_eq!(ThemeMode::Auto.theme(false), colors::light());
        assert_eq!(ThemeMode::Dark.theme(false), colors::dark());
        assert_eq!(ThemeMode::Light.theme(true), colors::light());
    }

    #[test]
    fn forced_theme_ignores_system_theme_changes() {
        let mut app = App::new();
        let _ = app.take_theme_changed();

        app.set_theme_mode(ThemeMode::Light);
        assert_eq!(app.theme, colors::light());
        assert!(app.take_theme_changed());
        assert!(app.loading.request_update);

        app.on_theme_changed(true);
        assert_eq!(app.theme, colors::light());
        assert!(!app.take_theme_changed());

        app.set_theme_mode(ThemeMode::Auto);
        app.on_theme_changed(true);
        assert_eq!(app.theme, colors::dark());
        assert!(app.take_theme_changed());
    }

    #[test]
    fn background_table_sorting_does_not_reorder_highlight_indices() {
        let mut app = App::new();
        let mut data = crate::trace::TraceData::new();
        for name in [b"beta".as_slice(), b"alpha".as_slice()] {
            let name = data.intern(name);
            let phase = data.intern(b"X");
            let category = data.intern(b"match");
            data.events.push(PersistedEvent {
                name,
                category,
                phase,
                ..PersistedEvent::default()
            });
        }
        app.trace_data = Some(Arc::new(data));
        app.search.query = "match".to_owned();
        app.apply_search();
        assert!(app.search_status_visible());
        wait_until(&mut app, "initial search", |app| !app.search_active());
        let highlights = app.viewer.selected_events_snapshot();

        app.search.sort = Some(SearchSort {
            column: SortColumn::Name,
            ascending: true,
        });
        app.apply_search_display();
        assert!(!app.search_status_visible());
        wait_until(&mut app, "sorted search display", |app| {
            !app.search_active()
        });

        assert!(Arc::ptr_eq(
            &highlights,
            &app.viewer.selected_events_snapshot()
        ));
        assert_eq!(app.viewer.selected_events(), [0, 1]);
        assert_eq!(app.viewer.filtered_events(), [1, 0]);

        app.search.sort = None;
        app.search.selected_histogram_bucket = Some(0);
        app.apply_search_display();
        assert!(!app.search_status_visible());
        wait_until(&mut app, "filtered search display", |app| {
            !app.search_active()
        });
    }
}
