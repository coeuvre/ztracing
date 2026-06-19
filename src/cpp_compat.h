#ifndef ZTRACING_SRC_CPP_COMPAT_H_
#define ZTRACING_SRC_CPP_COMPAT_H_

#include <string_view>
#include "src/array_list.h"
#include "src/hash_table.h"
#include "src/trace_data.h"
#include "src/colors.h"
#include "src/track.h"
#include "src/track_renderer.h"
#include "src/trace_viewer.h"

// ============================================================================
// ArrayList C++ Compatibility Wrapper
// ============================================================================

template <typename T>
struct ArrayList {
  union {
    T* data;
    void* ptr;
  };
  union {
    size_t size;
    size_t len;
  };
  union {
    size_t capacity;
    size_t cap;
  };
  union {
    size_t element_size;
    size_t elem_size;
  };

  T& operator[](size_t index) { return data[index]; }
  const T& operator[](size_t index) const { return data[index]; }
};

template <typename T>
inline ArrayList<T>& as_array_list(array_list_t& al) {
  return reinterpret_cast<ArrayList<T>&>(al);
}

template <typename T>
inline const ArrayList<T>& as_array_list(const array_list_t& al) {
  return reinterpret_cast<const ArrayList<T>&>(al);
}

#define AS_ARRAY_LIST(al, type_t) (as_array_list<type_t>(al))

template <typename T>
inline void array_list_reserve(ArrayList<T>* al, allocator_t a,
                               size_t new_capacity) {
  al->element_size = sizeof(T);
  array_list_reserve(reinterpret_cast<array_list_t*>(al), new_capacity,
                     sizeof(T), a);
}

template <typename T>
inline void array_list_push_back(ArrayList<T>* al, allocator_t a,
                                 const T& item) {
  al->element_size = sizeof(T);
  *reinterpret_cast<T*>(array_list_push_(reinterpret_cast<array_list_t*>(al),
                                         sizeof(T), a)) = item;
}

template <typename T>
inline void array_list_append(ArrayList<T>* al, allocator_t a, const T* items,
                              size_t count) {
  al->element_size = sizeof(T);
  T* dest = reinterpret_cast<T*>(array_list_append_(
      reinterpret_cast<array_list_t*>(al), count, sizeof(T), a));
  if (count > 0) {
    memcpy(dest, items, count * sizeof(T));
  }
}

template <typename T>
inline void array_list_pop_back(ArrayList<T>* al) {
  (void)array_list_pop_(reinterpret_cast<array_list_t*>(al));
}

template <typename T>
inline void array_list_clear(ArrayList<T>* al) {
  array_list_clear(reinterpret_cast<array_list_t*>(al));
}

template <typename T>
inline void array_list_resize(ArrayList<T>* al, allocator_t a,
                              size_t new_size) {
  al->element_size = sizeof(T);
  array_list_resize(reinterpret_cast<array_list_t*>(al), new_size, sizeof(T),
                    a);
}

template <typename T>
inline void array_list_deinit(ArrayList<T>* al, allocator_t a) {
  array_list_deinit(reinterpret_cast<array_list_t*>(al), a);
}

template <typename T>
inline void array_list_push_back(array_list_t* al, allocator_t a, const T& item) {
  al->elem_size = sizeof(T);
  void* slot = array_list_push_(al, sizeof(T), a);
  *static_cast<T*>(slot) = item;
}

template <typename T>
inline void array_list_append(array_list_t* al, allocator_t a, const T* items,
                              size_t count) {
  if (count > 0) {
    al->elem_size = sizeof(T);
    array_list_resize(al, al->len + count, sizeof(T), a);
    char* dst = (char*)al->ptr + (al->len - count) * al->elem_size;
    memcpy(dst, items, count * al->elem_size);
  }
}

// ============================================================================
// HashTable C++ Compatibility Wrapper
// ============================================================================

template <typename T>
struct DefaultHash {
  uint32_t operator()(const T& key) const {
    return static_cast<uint32_t>(key);
  }
};

template <>
struct DefaultHash<std::string_view> {
  uint32_t operator()(std::string_view key) const {
    uint32_t hash = 5381;
    for (char c : key) {
      hash = ((hash << 5) + hash) + c;
    }
    return hash;
  }
};

template <typename T>
struct DefaultEq {
  bool operator()(const T& a, const T& b) const { return a == b; }
};

template <typename K, typename V, typename Hash = DefaultHash<K>,
          typename Eq = DefaultEq<K>>
struct HashTable {
  struct Entry {
    K key;
    V value;
    uint32_t hash;
    bool occupied;
  };

  Entry* entries;
  size_t size;
  size_t capacity;
  size_t capacity_mask;
  size_t key_size;
  size_t value_size;
  size_t key_align;
  size_t value_align;
  size_t entry_size;
  size_t value_offset;
  size_t hash_offset;
  size_t occupied_offset;
  uint32_t (*hash_fn_ptr)(const void* key, void* ctx);
  bool (*eq_fn_ptr)(const void* a, const void* b, void* ctx);
  void* ctx;

  Hash hash_fn;
  Eq eq_fn;

  static uint32_t cpp_hash_wrapper(const void* key, void* ctx) {
    auto* self = static_cast<HashTable<K, V, Hash, Eq>*>(ctx);
    return self->hash_fn(*static_cast<const K*>(key));
  }

  static bool cpp_eq_wrapper(const void* a, const void* b, void* ctx) {
    auto* self = static_cast<HashTable<K, V, Hash, Eq>*>(ctx);
    return self->eq_fn(*static_cast<const K*>(a), *static_cast<const K*>(b));
  }
};

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_ensure_initialized(HashTable<K, V, Hash, Eq>* ht) {
  if (ht->key_size == 0) {
    ht->key_size = sizeof(K);
    ht->value_size = sizeof(V);
    ht->key_align = alignof(K);
    ht->value_align = alignof(V);
    ht->hash_fn_ptr = HashTable<K, V, Hash, Eq>::cpp_hash_wrapper;
    ht->eq_fn_ptr = HashTable<K, V, Hash, Eq>::cpp_eq_wrapper;
    ht->ctx = ht;
    hash_table_calculate_layout(reinterpret_cast<hash_table_t*>(ht));
  }
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_deinit(HashTable<K, V, Hash, Eq>* ht, allocator_t a) {
  hash_table_deinit(reinterpret_cast<hash_table_t*>(ht), a);
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_resize(HashTable<K, V, Hash, Eq>* ht, allocator_t a,
                              size_t new_capacity) {
  hash_table_ensure_initialized(ht);
  hash_table_resize(reinterpret_cast<hash_table_t*>(ht), new_capacity, a);
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_put_with_hash(HashTable<K, V, Hash, Eq>* ht,
                                     allocator_t a, const K& key,
                                     const V& value, uint32_t h) {
  hash_table_ensure_initialized(ht);
  void* val_slot =
      hash_table_put_with_hash(reinterpret_cast<hash_table_t*>(ht), &key, h, a);
  *static_cast<V*>(val_slot) = value;
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_put(HashTable<K, V, Hash, Eq>* ht, allocator_t a,
                           const K& key, const V& value) {
  hash_table_ensure_initialized(ht);
  void* val_slot = hash_table_put(reinterpret_cast<hash_table_t*>(ht), &key, a);
  *static_cast<V*>(val_slot) = value;
}

template <typename K, typename V, typename Hash, typename Eq>
inline V* hash_table_get_with_hash(HashTable<K, V, Hash, Eq>* ht, const K& key,
                                   uint32_t h) {
  hash_table_ensure_initialized(ht);
  void* val_slot = hash_table_get_with_hash(
      reinterpret_cast<const hash_table_t*>(ht), &key, h);
  return static_cast<V*>(val_slot);
}

template <typename K, typename V, typename Hash, typename Eq>
inline V* hash_table_get(HashTable<K, V, Hash, Eq>* ht, const K& key) {
  hash_table_ensure_initialized(ht);
  void* val_slot =
      hash_table_get(reinterpret_cast<const hash_table_t*>(ht), &key);
  return static_cast<V*>(val_slot);
}

template <typename K, typename V, typename Hash, typename Eq>
inline void hash_table_clear(HashTable<K, V, Hash, Eq>* ht) {
  hash_table_clear(reinterpret_cast<hash_table_t*>(ht));
}

template <typename K, typename V, typename Hash, typename Eq>
inline HashTable<K, V, Hash, Eq>& as_hash_table(hash_table_t& ht) {
  return reinterpret_cast<HashTable<K, V, Hash, Eq>&>(ht);
}

template <typename K, typename V, typename Hash, typename Eq>
inline const HashTable<K, V, Hash, Eq>& as_hash_table(const hash_table_t& ht) {
  return reinterpret_cast<const HashTable<K, V, Hash, Eq>&>(ht);
}

#define AS_HASH_TABLE(ht, key_t, val_t) (as_hash_table<key_t, val_t>(ht))

// ============================================================================
// TraceData C++ Compatibility Wrapper
// ============================================================================

using StringRef = string_ref_t;
using TraceArgPersisted = trace_arg_persisted_t;
using TraceEventPersisted = trace_event_persisted_t;
using StringEntry = string_entry_t;
using TraceData = trace_data_t;
using ActiveEventB = active_event_b_t;
using ThreadStack = thread_stack_t;
using TraceEventMatcher = trace_event_matcher_t;
using Theme = theme_t;

inline StringRef trace_data_push_string(TraceData* td, allocator_t a,
                                        std::string_view str) {
  return trace_data_push_string(td, string_t{str.data(), str.size()}, a);
}

inline void trace_data_add_event(trace_data_t* td, allocator_t a,
                                 const theme_t* theme,
                                 const trace_event_t* event,
                                 trace_event_matcher_t* matcher) {
  trace_data_add_event(td, theme, event, matcher, a);
}

inline std::string_view trace_data_get_string(const TraceData* td,
                                              StringRef ref) {
  string_t s = trace_data_get_string_c(td, ref);
  return std::string_view(s.ptr, s.len);
}

// ============================================================================
// Track C++ Compatibility Wrapper
// ============================================================================

using TrackType = track_type_t;
using Track = track_t;

inline void track_update_colors(ArrayList<Track>* tracks, const trace_data_t* td,
                                const theme_t* theme) {
  track_update_colors((array_list_t*)tracks, td, theme);
}

inline void track_organize(const trace_data_t* td, allocator_t a, const theme_t* theme,
                           ArrayList<Track>* out_tracks, int64_t* out_min_ts,
                           int64_t* out_max_ts) {
  track_organize(td, theme, (array_list_t*)out_tracks, out_min_ts, out_max_ts,
                 a);
}

inline void track_organize(const trace_data_t* td, allocator_t a, const theme_t* theme,
                           array_list_t* out_tracks, int64_t* out_min_ts,
                           int64_t* out_max_ts) {
  track_organize(td, theme, out_tracks, out_min_ts, out_max_ts, a);
}

// ============================================================================
// TrackRenderer C++ Compatibility Wrapper
// ============================================================================

using TrackRenderBlock = track_render_block_t;
using CounterRenderBlock = counter_render_block_t;
using TrackRendererState = track_renderer_state_t;

inline void track_renderer_update_selection_bitset(
    track_renderer_state_t* state, const trace_data_t* trace_data,
    const ArrayList<int64_t>& selected_event_indices, allocator_t a) {
  track_renderer_update_selection_bitset(
      state, trace_data, (const array_list_t*)&selected_event_indices, a);
}

inline void track_compute_render_blocks(
    const track_t* track, const trace_data_t* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t focused_event_idx, track_renderer_state_t* state,
    ArrayList<track_render_block_t>* out_blocks, allocator_t a) {
  track_compute_render_blocks(track, trace_data, viewport_start, viewport_end,
                              inner_width, tracks_canvas_pos_x,
                              focused_event_idx, state,
                              (array_list_t*)out_blocks, a);
}

inline void track_compute_counter_render_blocks(
    const track_t* track, const trace_data_t* trace_data, double viewport_start,
    double viewport_end, float inner_width, float tracks_canvas_pos_x,
    int64_t focused_event_idx, track_renderer_state_t* state,
    ArrayList<counter_render_block_t>* out_blocks, allocator_t a) {
  track_compute_counter_render_blocks(track, trace_data, viewport_start,
                                      viewport_end, inner_width,
                                      tracks_canvas_pos_x, focused_event_idx,
                                      state, (array_list_t*)out_blocks, a);
}

// ============================================================================
// TraceViewer C++ Compatibility Wrapper
// ============================================================================

inline void trace_viewer_calculate_histogram(const ArrayList<int64_t>& results,
                                             const trace_data_t* td,
                                             duration_histogram_t* h) {
  trace_viewer_calculate_histogram((const array_list_t*)&results, td, h);
}

inline void trace_viewer_step(trace_viewer_t* tv, trace_data_t* td,
                              const trace_viewer_input_t& input,
                              allocator_t allocator) {
  trace_viewer_step(tv, td, &input, allocator);
}

// C++ Type Aliases for backward compatibility with unported C++ files
typedef duration_histogram_bucket_t DurationHistogramBucket;
typedef duration_histogram_t DurationHistogram;
typedef search_state_t SearchState;
typedef hover_match_t HoverMatch;
typedef trace_viewer_input_t TraceViewerInput;
typedef track_view_info_t TrackViewInfo;
typedef ruler_tick_t RulerTick;
typedef selection_overlay_layout_t SelectionOverlayLayout;
typedef track_heatmap_t TrackHeatmap;
typedef vertical_minimap_layout_t VerticalMinimapLayout;
typedef vertical_minimap_state_t VerticalMinimapState;
typedef trace_viewer_t TraceViewer;

namespace InteractionDragMode {
const ig_interaction_drag_mode_t NONE = INTERACTION_DRAG_MODE_NONE;
const ig_interaction_drag_mode_t RULER_NEW = INTERACTION_DRAG_MODE_RULER_NEW;
const ig_interaction_drag_mode_t RULER_START = INTERACTION_DRAG_MODE_RULER_START;
const ig_interaction_drag_mode_t RULER_END = INTERACTION_DRAG_MODE_RULER_END;
const ig_interaction_drag_mode_t TRACKS_START = INTERACTION_DRAG_MODE_TRACKS_START;
const ig_interaction_drag_mode_t TRACKS_END = INTERACTION_DRAG_MODE_TRACKS_END;
const ig_interaction_drag_mode_t BOX_SELECT = INTERACTION_DRAG_MODE_BOX_SELECT;
}  // namespace InteractionDragMode

#endif // ZTRACING_SRC_CPP_COMPAT_H_
