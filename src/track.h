#ifndef ZTRACING_SRC_TRACK_H_
#define ZTRACING_SRC_TRACK_H_

#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/colors.h"
#include "src/trace_data.h"

typedef enum track_type {
  TRACK_TYPE_COUNTER,
  TRACK_TYPE_THREAD,
} track_type_t;

#define TRACK_BLOCK_SIZE 1024

// Represents a timeline track of events (either thread events or counters)
typedef struct track {
  track_type_t type;
  int32_t pid;
  int32_t tid;
  string_ref_t name_ref;
  string_ref_t id_ref;
  int32_t sort_index;
#ifdef __cplusplus
  ArrayList<size_t> event_indices;
  ArrayList<uint32_t> depths;
  ArrayList<string_ref_t> counter_series;
  ArrayList<uint32_t> counter_colors;
  ArrayList<int64_t> block_max_durs;
#else
  array_list_t event_indices;   // Element type: size_t
  array_list_t depths;          // Element type: uint32_t
  array_list_t counter_series;  // Element type: string_ref_t
  array_list_t counter_colors;  // Element type: uint32_t
  array_list_t block_max_durs;  // Element type: int64_t
#endif
  double counter_max_total;
  int64_t max_dur;
  uint32_t max_depth;
} track_t;

#ifdef __cplusplus
extern "C" {
#endif

void track_deinit(track_t* t, allocator_t a);
void track_sort_events(track_t* t, const trace_data_t* td, allocator_t a);
void track_update_max_dur(track_t* t, const trace_data_t* td, allocator_t a);
void track_calculate_depths(track_t* t, const trace_data_t* td, allocator_t a);
size_t track_find_visible_start_index(const track_t* t, const trace_data_t* td,
                                      int64_t viewport_start_ts);

void track_update_colors(array_list_t* tracks, const trace_data_t* td,
                         const theme_t* theme);
void track_organize(const trace_data_t* td, const theme_t* theme,
                    array_list_t* out_tracks, int64_t* out_min_ts,
                    int64_t* out_max_ts, allocator_t a);

#ifdef __cplusplus
}
#endif

// C++ Compatibility Wrapper
#ifdef __cplusplus
using TrackType = track_type_t;
using Track = track_t;

inline void track_update_colors(ArrayList<Track>* tracks, const TraceData* td,
                                const Theme* theme) {
  track_update_colors((array_list_t*)tracks, td, theme);
}

inline void track_organize(const TraceData* td, Allocator a, const Theme* theme,
                           ArrayList<Track>* out_tracks, int64_t* out_min_ts,
                           int64_t* out_max_ts) {
  track_organize(td, theme, (array_list_t*)out_tracks, out_min_ts, out_max_ts,
                 a);
}
#endif

#endif  // ZTRACING_SRC_TRACK_H_
