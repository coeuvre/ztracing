#ifndef ZTRACING_SRC_TRACK_H_
#define ZTRACING_SRC_TRACK_H_

#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/trace_data.h"

struct Track {
  int32_t pid;
  int32_t tid;
  StringRef name_ref;
  int32_t sort_index;
  ArrayList<size_t> event_indices;
  ArrayList<uint32_t> depths;
  int64_t max_dur;
  uint32_t max_depth;
};

void track_deinit(Track* t, Allocator a);
void track_sort_events(Track* t, const TraceData* td);
void track_update_max_dur(Track* t, const TraceData* td);
void track_calculate_depths(Track* t, const TraceData* td, Allocator a);
size_t track_find_visible_start_index(const Track* t, const TraceData* td,
                                      int64_t viewport_start_ts);
void track_organize(const TraceData* td, Allocator a,
                    ArrayList<Track>* out_tracks, int64_t* out_min_ts,
                    int64_t* out_max_ts);

#endif  // ZTRACING_SRC_TRACK_H_
