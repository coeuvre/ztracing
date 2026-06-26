#ifndef CORE_JSON_WRITER_H
#define CORE_JSON_WRITER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct json_writer {
  array_list_t* buf;  // Destination buffer (dynamic array of char)
  allocator_t allocator;
  bool first_item[32];  // Nesting stack to track if we need to write commas
  size_t depth;
  bool after_key;  // State flag: true if we just wrote a key name
  bool indent;     // Pretty-print flag: true to indent, false to minify
} json_writer_t;

void json_writer_init(json_writer_t* w, bool indent, array_list_t* out_buf,
                      allocator_t a);
void json_writer_begin_object(json_writer_t* w);
void json_writer_end_object(json_writer_t* w);
void json_writer_begin_array(json_writer_t* w);
void json_writer_end_array(json_writer_t* w);

// Write key names (only valid inside objects)
void json_writer_name(json_writer_t* w, string_view_t name);

// Write values (automatically handles leading commas based on nesting state)
void json_writer_string(json_writer_t* w, string_view_t val);
void json_writer_number_double(json_writer_t* w, double val);
void json_writer_number_int(json_writer_t* w, int64_t val);
void json_writer_bool(json_writer_t* w, bool val);
void json_writer_null(json_writer_t* w);

#ifdef __cplusplus
}
#endif

#endif  // CORE_JSON_WRITER_H
