#ifndef SRC_CLI_TABLE_H
#define SRC_CLI_TABLE_H

#include <stddef.h>
#include <stdbool.h>

#include "core/darray.h"
#include "core/string.h"
#include "core/arena.h"

typedef enum {
  CLI_ALIGN_LEFT,
  CLI_ALIGN_RIGHT,
} cli_table_align_t;

typedef struct {
  string_t header;
  cli_table_align_t align;
  int width; // Current width of the column
  bool dynamic;
} cli_table_column_t;

typedef struct {
  darray_t(string_t) cells;
} cli_table_row_t;

typedef struct {
  darray_t(cli_table_column_t) columns;
  darray_t(cli_table_row_t) rows;
  arena_t* arena;
} cli_table_t;

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the table.
void cli_table_init(cli_table_t* t);

// Deinitializes the table and frees all associated memory (via its arena).
void cli_table_deinit(cli_table_t* t);

// Adds a column to the table.
void cli_table_add_column(cli_table_t* t, string_view_t header,
                          cli_table_align_t align, int default_width,
                          bool dynamic);

// Adds a new row and returns its index.
size_t cli_table_add_row(cli_table_t* t);

// Sets the cell value for the last added row.
void cli_table_set_cell(cli_table_t* t, size_t col_idx, string_view_t value);

// Sets the formatted cell value for the last added row.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void cli_table_set_cell_fmt(cli_table_t* t, size_t col_idx, const char* fmt, ...);

// Prints the table to stdout, adjusting column widths to fit the terminal.
void cli_table_print(cli_table_t* t);

#ifdef __cplusplus
}
#endif

#endif // SRC_CLI_TABLE_H
