#ifndef ZTRACING_SRC_CHANNEL_H_
#define ZTRACING_SRC_CHANNEL_H_

#include <stdbool.h>
#include <stddef.h>

#include "src/allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque struct definition for strict encapsulation.
// Internal fields are hidden in the .c file.
typedef struct channel channel_t;

// 1. Underlying Generic API (Marked with trailing underscore, do not call
// directly)
channel_t* channel_create_(size_t item_size, size_t capacity,
                           allocator_t allocator);
void channel_destroy(channel_t* chan, allocator_t allocator);
bool channel_send_(channel_t* chan, const void* item, size_t item_size);
bool channel_recv_(channel_t* chan, void* out_item, size_t item_size);
bool channel_try_send_(channel_t* chan, const void* item, size_t item_size);
bool channel_try_recv_(channel_t* chan, void* out_item, size_t item_size);
void channel_close_and_drain_(channel_t* chan,
                              void (*destructor)(void* item,
                                                 allocator_t allocator),
                              allocator_t allocator);
size_t channel_get_size(const channel_t* chan);

// 2. Type-Safe Public Macro API (Replaces void* with compile-time type
// extraction)

// Creates a channel for a specific type.
// Example: channel_t* chan = channel_create(msg_t, 64, allocator);
#define channel_create(type_t, capacity, allocator) \
  channel_create_(sizeof(type_t), (capacity), (allocator))

// BLOCKING Send. Blocks if the channel is full.
// CRITICAL: FORBIDDEN on the WebAssembly UI/Main thread! Use only in background
// tasks.
#define channel_send(chan, item_ptr) \
  channel_send_((chan), (item_ptr), sizeof(*(item_ptr)))

// BLOCKING Receive. Blocks if the channel is empty.
// CRITICAL: FORBIDDEN on the WebAssembly UI/Main thread! Use only in background
// tasks.
#define channel_recv(chan, out_item_ptr) \
  channel_recv_((chan), (out_item_ptr), sizeof(*(out_item_ptr)))

// NON-BLOCKING Send. Returns immediately. Returns false if the channel is full.
// Safe for use on the WebAssembly UI/Main thread.
#define channel_try_send(chan, item_ptr) \
  channel_try_send_((chan), (item_ptr), sizeof(*(item_ptr)))

// NON-BLOCKING Receive. Returns immediately. Returns false if the channel is
// empty. Safe for use on the WebAssembly UI/Main thread.
#define channel_try_recv(chan, out_item_ptr) \
  channel_try_recv_((chan), (out_item_ptr), sizeof(*(out_item_ptr)))

// Closes and drains all remaining items in a channel using a destructor.
#define channel_close_and_drain(chan, type_t, destructor, allocator)           \
  channel_close_and_drain_((chan), (void (*)(void*, allocator_t))(destructor), \
                           (allocator))

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_CHANNEL_H_
