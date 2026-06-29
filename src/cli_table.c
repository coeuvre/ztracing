#include "src/cli_table.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "core/allocator.h"

static size_t utf8_strlen(string_view_t sv) {
  size_t len = 0;
  for (size_t i = 0; i < sv.len; ) {
    unsigned char c = (unsigned char)sv.ptr[i];
    if (c < 0x80) {
      i += 1;
    } else if ((c & 0xe0) == 0xc0) {
      i += 2;
    } else if ((c & 0xf0) == 0xe0) {
      i += 3;
    } else if ((c & 0xf8) == 0xf0) {
      i += 4;
    } else {
      i += 1;
    }
    len++;
  }
  return len;
}

static int get_terminal_width(void) {
  char* cols = getenv("COLUMNS");
  if (cols) {
    int val = atoi(cols);
    if (val > 0) {
      return val;
    }
  }

  if (!isatty(STDOUT_FILENO)) {
    return 0; // Unlimited when redirected
  }

  int width = 80;
#ifdef TIOCGWINSZ
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    width = w.ws_col;
  }
#endif
  return width;
}

// Prints at most max_width visual characters of sv.
// If truncated, and max_width > 3, prints max_width - 1 chars and then "…".
// Returns the actual visual width printed.
static size_t print_truncated_utf8(string_view_t sv, size_t max_width) {
  size_t total_visual_len = utf8_strlen(sv);
  
  if (total_visual_len <= max_width) {
    printf("%.*s", (int)sv.len, sv.ptr);
    return total_visual_len;
  }
  
  if (max_width == 0) {
    return 0;
  }
  
  size_t target_width = max_width;
  bool use_ellipsis = max_width > 3;
  if (use_ellipsis) {
    target_width = max_width - 1;
  }
  
  size_t printed_bytes = 0;
  size_t visual_len = 0;
  for (size_t i = 0; i < sv.len && visual_len < target_width; ) {
    unsigned char c = (unsigned char)sv.ptr[i];
    size_t char_bytes = 1;
    if (c < 0x80) char_bytes = 1;
    else if ((c & 0xe0) == 0xc0) char_bytes = 2;
    else if ((c & 0xf0) == 0xe0) char_bytes = 3;
    else if ((c & 0xf8) == 0xf0) char_bytes = 4;
    
    printed_bytes += char_bytes;
    i += char_bytes;
    visual_len++;
  }
  
  printf("%.*s", (int)printed_bytes, sv.ptr);
  if (use_ellipsis) {
    printf("…");
    visual_len++;
  }
  return visual_len;
}

void cli_table_init(cli_table_t* t) {
  if (!t) return;
  memset(t, 0, sizeof(*t));
  t->arena = arena_create();
}

void cli_table_deinit(cli_table_t* t) {
  if (!t) return;
  arena_destroy(t->arena);
}

void cli_table_add_column(cli_table_t* t, string_view_t header,
                          cli_table_align_t align, int default_width,
                          bool dynamic) {
  if (!t) return;
  allocator_t* a = arena_get_allocator(t->arena);
  cli_table_column_t col = {
    .header = string_from_view(header, a),
    .align = align,
    .width = default_width,
    .dynamic = dynamic,
  };
  darray_push(&t->columns, col, a);
}

size_t cli_table_add_row(cli_table_t* t) {
  if (!t) return 0;
  allocator_t* a = arena_get_allocator(t->arena);
  cli_table_row_t row = {};
  darray_push(&t->rows, row, a);
  return t->rows.len - 1;
}

void cli_table_set_cell(cli_table_t* t, size_t col_idx, string_view_t value) {
  if (!t || t->rows.len == 0) return;
  allocator_t* a = arena_get_allocator(t->arena);
  cli_table_row_t* row = &t->rows.ptr[t->rows.len - 1];
  
  while (row->cells.len <= col_idx) {
    string_t empty = {};
    darray_push(&row->cells, empty, a);
  }
  
  row->cells.ptr[col_idx] = string_from_view(value, a);
}

void cli_table_set_cell_fmt(cli_table_t* t, size_t col_idx, const char* fmt, ...) {
  if (!t || t->rows.len == 0) return;
  allocator_t* a = arena_get_allocator(t->arena);
  cli_table_row_t* row = &t->rows.ptr[t->rows.len - 1];

  while (row->cells.len <= col_idx) {
    string_t empty = {};
    darray_push(&row->cells, empty, a);
  }

  string_t s = {};
  va_list args;
  va_start(args, fmt);
  string_vprintf(&s, a, fmt, args);
  va_end(args);
  
  row->cells.ptr[col_idx] = s;
}

void cli_table_print(cli_table_t* t) {
  if (!t) return;

  // 1. Calculate dynamic widths based on content
  cli_table_column_t* cols = t->columns.ptr;
  for (size_t c = 0; c < t->columns.len; c++) {
    cli_table_column_t* col = &cols[c];
    if (col->dynamic) {
      size_t max_w = utf8_strlen(string_get_view(&col->header));
      cli_table_row_t* rows = t->rows.ptr;
      for (size_t r = 0; r < t->rows.len; r++) {
        if (c < rows[r].cells.len) {
          size_t cell_w = utf8_strlen(string_get_view(&rows[r].cells.ptr[c]));
          if (cell_w > max_w) {
            max_w = cell_w;
          }
        }
      }
      if ((int)max_w > col->width) {
        col->width = (int)max_w;
      }
    }
  }

  // 2. Adjust widths to fit terminal
  int W = get_terminal_width();
  size_t total_width = 0;
  for (size_t c = 0; c < t->columns.len; c++) {
    total_width += (size_t)cols[c].width;
  }
  if (t->columns.len > 0) {
    total_width += (t->columns.len - 1) * 3; // " | "
  }

  if (W > 0 && total_width > (size_t)W) {
    int separators_width = (int)(t->columns.len > 0 ? (t->columns.len - 1) * 3 : 0);
    int fixed_width = 0;
    int req_dyn_width = 0;
    for (size_t c = 0; c < t->columns.len; c++) {
      if (cols[c].dynamic) {
        req_dyn_width += cols[c].width;
      } else {
        fixed_width += cols[c].width;
      }
    }
    
    int available_dyn = W - fixed_width - separators_width;
    if (available_dyn < 0) {
      // Terminal is extremely narrow. Set all dynamic to minimum width of 3.
      for (size_t c = 0; c < t->columns.len; c++) {
        if (cols[c].dynamic) {
          cols[c].width = 3;
        }
      }
    } else if (req_dyn_width > 0) {
      // Distribute available width proportionally
      for (size_t c = 0; c < t->columns.len; c++) {
        if (cols[c].dynamic) {
          cols[c].width = (int)((double)cols[c].width * available_dyn / req_dyn_width);
          if (cols[c].width < 3) {
            cols[c].width = 3; // Enforce minimum width
          }
        }
      }
    }
  }

  // 3. Print header
  for (size_t c = 0; c < t->columns.len; c++) {
    cli_table_column_t* col = &cols[c];
    string_view_t header_view = string_get_view(&col->header);
    
    if (col->align == CLI_ALIGN_LEFT) {
      size_t printed = print_truncated_utf8(header_view, (size_t)col->width);
      for (int i = 0; i < col->width - (int)printed; i++) {
        putchar(' ');
      }
    } else {
      size_t header_len = utf8_strlen(header_view);
      if (header_len > (size_t)col->width) {
        print_truncated_utf8(header_view, (size_t)col->width);
      } else {
        for (int i = 0; i < col->width - (int)header_len; i++) {
          putchar(' ');
        }
        printf("%.*s", (int)header_view.len, header_view.ptr);
      }
    }

    if (c < t->columns.len - 1) {
      printf(" | ");
    }
  }
  printf("\n");

  // 4. Print separator
  total_width = 0;
  for (size_t c = 0; c < t->columns.len; c++) {
    total_width += (size_t)cols[c].width;
  }
  if (t->columns.len > 0) {
    total_width += (t->columns.len - 1) * 3;
  }
  for (size_t i = 0; i < total_width; i++) {
    putchar('-');
  }
  printf("\n");

  // 5. Print rows
  cli_table_row_t* rows = t->rows.ptr;
  for (size_t r = 0; r < t->rows.len; r++) {
    cli_table_row_t* row = &rows[r];
    for (size_t c = 0; c < t->columns.len; c++) {
      cli_table_column_t* col = &cols[c];
      string_view_t cell_view = SV("");
      if (c < row->cells.len) {
        cell_view = string_get_view(&row->cells.ptr[c]);
      }

      if (col->align == CLI_ALIGN_LEFT) {
        size_t printed = print_truncated_utf8(cell_view, (size_t)col->width);
        for (int i = 0; i < col->width - (int)printed; i++) {
          putchar(' ');
        }
      } else {
        size_t cell_len = utf8_strlen(cell_view);
        if (cell_len > (size_t)col->width) {
          print_truncated_utf8(cell_view, (size_t)col->width);
        } else {
          for (int i = 0; i < col->width - (int)cell_len; i++) {
            putchar(' ');
          }
          printf("%.*s", (int)cell_view.len, cell_view.ptr);
        }
      }

      if (c < t->columns.len - 1) {
        printf(" | ");
      }
    }
    printf("\n");
  }
}
