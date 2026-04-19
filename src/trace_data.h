#ifndef ZTRACING_SRC_TRACE_DATA_H_
#define ZTRACING_SRC_TRACE_DATA_H_

#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/hash_table.h"
#include "src/str.h"
#include "src/trace_parser.h"

// A reference to a string in the TraceData string pool.
typedef uint32_t StringRef;

struct TraceArgPersisted {
  StringRef key_ref;
  StringRef val_ref;
  double val_double;
};

struct TraceEventPersisted {
  StringRef name_ref;
  StringRef cat_ref;
  StringRef ph_ref;
  StringRef cname_ref;
  StringRef id_ref;
  uint32_t color;
  int64_t ts;
  int64_t dur;
  int32_t pid;
  int32_t tid;
  uint32_t args_offset;
  uint32_t args_count;
};

struct StringEntry {
  uint32_t offset;
  uint32_t len;
  uint32_t hash;
};

struct TraceData {
  ArrayList<char> string_buffer;
  ArrayList<StringEntry> string_table;

  struct StringLookupHash {
    const TraceData* td;
    uint32_t operator()(uint32_t index) const;
  };
  struct StringLookupEq {
    const TraceData* td;
    bool operator()(uint32_t a, uint32_t b) const;
  };

  HashTable<uint32_t, uint32_t, StringLookupHash, StringLookupEq> string_lookup;

  ArrayList<TraceEventPersisted> events;
  ArrayList<TraceArgPersisted> args;

  // Temporary storage for hashing during push
  struct {
    Str current_str;
    uint32_t current_hash;
  } tmp;
};

struct Theme;

void trace_data_init(TraceData* td, Allocator a);
void trace_data_deinit(TraceData* td, Allocator a);
void trace_data_clear(TraceData* td, Allocator a);

StringRef trace_data_push_string(TraceData* td, Allocator a, Str s);

void trace_data_add_event(TraceData* td, Allocator a, const Theme* theme,
                          const TraceEvent* event);
void trace_data_update_event_color(TraceData* td, uint32_t event_idx,
                                   const Theme* theme);

// Helper to get a string from a reference.
inline Str trace_data_get_string(const TraceData* td, StringRef ref) {
  if (ref == 0 || ref > td->string_table.size) return {nullptr, 0};
  const StringEntry& e = td->string_table[ref - 1];
  return {&td->string_buffer[e.offset], (size_t)e.len};
}

#endif  // ZTRACING_SRC_TRACE_DATA_H_
