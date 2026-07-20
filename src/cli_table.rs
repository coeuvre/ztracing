use std::io::{self, IsTerminal};

#[derive(Clone, Copy)]
pub enum Align {
    Left,
    Right,
}
struct Column {
    header: String,
    align: Align,
    width: usize,
    dynamic: bool,
}
#[derive(Default)]
pub struct Table {
    columns: Vec<Column>,
    rows: Vec<Vec<String>>,
}

impl Table {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn column(&mut self, header: &str, align: Align, width: usize, dynamic: bool) {
        self.columns.push(Column {
            header: header.to_owned(),
            align,
            width,
            dynamic,
        });
    }
    pub fn row<I, S>(&mut self, cells: I)
    where
        I: IntoIterator<Item = S>,
        S: ToString,
    {
        self.rows
            .push(cells.into_iter().map(|cell| cell.to_string()).collect());
    }
    pub fn render(self) -> String {
        let terminal_width = terminal_width();
        self.render_with_width(terminal_width)
    }

    fn render_with_width(mut self, terminal_width: Option<usize>) -> String {
        for (index, column) in self.columns.iter_mut().enumerate() {
            if column.dynamic {
                column.width = column.width.max(column.header.chars().count());
                for row in &self.rows {
                    column.width = column
                        .width
                        .max(row.get(index).map_or(0, |cell| cell.chars().count()));
                }
            }
        }
        let requested = self
            .columns
            .iter()
            .map(|column| column.width)
            .sum::<usize>()
            + self.columns.len().saturating_sub(1) * 3;
        if let Some(width) = terminal_width.filter(|width| requested > *width) {
            let separator = self.columns.len().saturating_sub(1) * 3;
            let fixed = self
                .columns
                .iter()
                .filter(|column| !column.dynamic)
                .map(|column| column.width)
                .sum::<usize>();
            let dynamic = self
                .columns
                .iter()
                .filter(|column| column.dynamic)
                .map(|column| column.width)
                .sum::<usize>();
            let available = width.saturating_sub(separator + fixed);
            for column in self.columns.iter_mut().filter(|column| column.dynamic) {
                column.width = if available == 0 {
                    3
                } else {
                    (column.width * available / dynamic.max(1)).max(3)
                };
            }
        }
        let total = self
            .columns
            .iter()
            .map(|column| column.width)
            .sum::<usize>()
            + self.columns.len().saturating_sub(1) * 3;
        let mut output = String::new();
        output.push_str(&self.render_row(self.columns.iter().map(|column| column.header.as_str())));
        output.push('\n');
        output.extend(std::iter::repeat_n('-', total));
        output.push('\n');
        for row in &self.rows {
            output.push_str(&self.render_row(
                (0..self.columns.len()).map(|index| row.get(index).map_or("", String::as_str)),
            ));
            output.push('\n');
        }
        output
    }
    fn render_row<'a>(&self, cells: impl IntoIterator<Item = &'a str>) -> String {
        cells
            .into_iter()
            .zip(&self.columns)
            .map(|(cell, column)| format_cell(cell, column.width, column.align))
            .collect::<Vec<_>>()
            .join(" | ")
    }
}

fn terminal_width() -> Option<usize> {
    if let Some(width) = columns_override() {
        return Some(width);
    }
    if !io::stdout().is_terminal() {
        return None;
    }
    native_terminal_width().or(Some(80))
}

fn columns_override() -> Option<usize> {
    std::env::var("COLUMNS")
        .ok()
        .and_then(|value| value.parse::<usize>().ok())
        .filter(|width| *width > 0)
}

#[cfg(any(target_os = "linux", target_os = "macos"))]
fn native_terminal_width() -> Option<usize> {
    use std::os::fd::AsRawFd;

    #[repr(C)]
    #[derive(Default)]
    struct WindowSize {
        rows: u16,
        columns: u16,
        x_pixels: u16,
        y_pixels: u16,
    }

    #[cfg(target_os = "linux")]
    const TIOCGWINSZ: usize = 0x5413;
    #[cfg(target_os = "macos")]
    const TIOCGWINSZ: usize = 0x40087468;

    unsafe extern "C" {
        fn ioctl(fd: std::ffi::c_int, request: usize, ...) -> std::ffi::c_int;
    }

    let mut size = WindowSize::default();
    let result = unsafe { ioctl(io::stdout().as_raw_fd(), TIOCGWINSZ, &mut size) };
    (result == 0 && size.columns > 0).then_some(usize::from(size.columns))
}

#[cfg(not(any(target_os = "linux", target_os = "macos")))]
fn native_terminal_width() -> Option<usize> {
    None
}

fn format_cell(value: &str, width: usize, align: Align) -> String {
    let count = value.chars().count();
    let display = if count <= width {
        value.to_owned()
    } else if width == 0 {
        String::new()
    } else {
        let ellipsis = width > 3;
        let take = width - usize::from(ellipsis);
        let mut value: String = value.chars().take(take).collect();
        if ellipsis {
            value.push('…');
        }
        value
    };
    let padding = width.saturating_sub(display.chars().count());
    match align {
        Align::Left => format!("{display}{}", " ".repeat(padding)),
        Align::Right => format!("{}{display}", " ".repeat(padding)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    static ENVIRONMENT: Mutex<()> = Mutex::new(());

    #[test]
    fn basic() {
        let mut table = Table::new();
        table.column("Col1", Align::Left, 0, true);
        table.column("Col2", Align::Right, 10, false);
        table.row(["value1", "num 42"]);
        assert_eq!(
            table.render_with_width(None),
            "Col1   |       Col2\n-------------------\nvalue1 |     num 42\n"
        );
    }

    #[test]
    fn utf8_width() {
        let mut table = Table::new();
        table.column("Bar", Align::Left, 0, true);
        table.row(["████"]);
        assert_eq!(table.render_with_width(None), "Bar \n----\n████\n");
    }

    #[test]
    fn terminal_width_truncation() {
        let mut table = Table::new();
        table.column("FixedCol", Align::Left, 8, false);
        table.column("DynamicColLongName", Align::Left, 0, true);
        table.row(["12345678", "VeryLongValueThatWillBeTruncated"]);
        let output = table.render_with_width(Some(20));
        assert!(output.lines().all(|line| line.chars().count() <= 20));
        assert!(output.contains("FixedCol | DynamicC…"));
    }

    #[test]
    fn truncates_left_and_right_aligned_cells() {
        let mut table = Table::new();
        table.column("Left header", Align::Left, 5, false);
        table.column("Right header", Align::Right, 5, false);
        table.row(["abcdef", "uvwxyz"]);
        assert_eq!(
            table.render_with_width(None),
            "Left… | Righ…\n-------------\nabcd… | uvwx…\n"
        );
    }

    #[test]
    fn renders_missing_and_empty_cells() {
        let mut table = Table::new();
        table.column("A", Align::Left, 3, false);
        table.column("B", Align::Right, 3, false);
        table.row([""]);
        table.row(std::iter::empty::<&str>());
        assert_eq!(
            table.render_with_width(None),
            "A   |   B\n---------\n    |    \n    |    \n"
        );
    }

    #[test]
    fn distributes_multiple_dynamic_columns() {
        let mut table = Table::new();
        table.column("first-column", Align::Left, 0, true);
        table.column("second-column-longer", Align::Left, 0, true);
        table.row(["first-value", "second-value-that-is-longer"]);
        let output = table.render_with_width(Some(20));
        assert!(output.lines().all(|line| line.chars().count() <= 20));
        assert_eq!(output.lines().next(), Some("firs… | second-col…"));
    }

    #[test]
    fn dynamic_columns_keep_minimum_width_on_extremely_narrow_terminals() {
        let mut table = Table::new();
        table.column("fixed", Align::Left, 8, false);
        table.column("dynamic", Align::Left, 0, true);
        table.row(["12345678", "abcdef"]);
        assert_eq!(
            table.render_with_width(Some(5)),
            "fixed    | dyn\n--------------\n12345678 | abc\n"
        );
    }

    #[test]
    fn short_widths_truncate_without_ellipsis() {
        for width in 0..=3 {
            let rendered = format_cell("abcdef", width, Align::Left);
            assert_eq!(rendered, "abcdef".chars().take(width).collect::<String>());
        }
        assert_eq!(format_cell("abcdef", 4, Align::Left), "abc…");
    }

    #[test]
    fn unicode_truncation_does_not_split_code_points() {
        assert_eq!(format_cell("é界█z", 3, Align::Left), "é界█");
        assert_eq!(format_cell("é界█z", 4, Align::Left), "é界█z");
    }

    #[test]
    fn columns_override_requires_a_strict_positive_integer() {
        let _guard = ENVIRONMENT.lock().unwrap();
        let previous = std::env::var_os("COLUMNS");
        for value in ["0", "-1", "20px", " 20"] {
            // SAFETY: access is serialized for this test and restored below.
            unsafe { std::env::set_var("COLUMNS", value) };
            assert_eq!(columns_override(), None);
        }
        // SAFETY: access is serialized for this test and restored below.
        unsafe { std::env::set_var("COLUMNS", "20") };
        assert_eq!(columns_override(), Some(20));
        // SAFETY: access is serialized for this test and restored before releasing the lock.
        unsafe {
            if let Some(previous) = previous {
                std::env::set_var("COLUMNS", previous);
            } else {
                std::env::remove_var("COLUMNS");
            }
        }
    }
}
