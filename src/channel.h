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

// Destructor function pointer type for channel items.
typedef void (*channel_item_destructor_t)(void* item);

// 1. Underlying Generic API (Marked with trailing underscore, do not call
// directly)
channel_t* channel_create_(size_t item_size, size_t capacity,
                           channel_item_destructor_t destructor,
                           allocator_t allocator);
// Destroys the channel. Requires channel_close_tx AND channel_close_rx to have
// been called first (CHECKs otherwise).
void channel_destroy(channel_t* chan);
bool channel_send_(channel_t* chan, void* item, size_t item_size);
bool channel_recv_(channel_t* chan, void* out_item, size_t item_size);
bool channel_try_send_(channel_t* chan, void* item, size_t item_size);
bool channel_try_recv_(channel_t* chan, void* out_item, size_t item_size);

// Closes the sender side. Further send ops fail (and destruct the item).
// Receivers still drain remaining items; once empty, recv returns false.
void channel_close_tx(channel_t* chan);

// Closes the receiver side. Further recv ops fail immediately; send ops also
// fail (and destruct the item) since no one will consume.
void channel_close_rx(channel_t* chan);

size_t channel_get_size(channel_t* chan);
size_t channel_get_capacity(channel_t* chan);
bool channel_is_tx_closed(channel_t* chan);
bool channel_is_rx_closed(channel_t* chan);

// 2. Type-Safe Public Macro API (Replaces void* with compile-time type
// extraction)

// Creates a channel for a specific type.
// If destructor is non-null, it will be used to clean up items:
// 1. When channel_destroy is called on a non-empty channel.
// 2. When a send operation (blocking or non-blocking) fails (returns false).
// Example: channel_t* chan = channel_create(msg_t, 64, msg_deinit, allocator);
#define channel_create(type_t, capacity, destructor, allocator) \
  channel_create_(sizeof(type_t), (capacity),                   \
                  (channel_item_destructor_t)(destructor), (allocator))

// BLOCKING Send. Blocks if the channel is full.
// CRITICAL: FORBIDDEN on the WebAssembly UI/Main thread! Use only in background
// tasks.
// If the channel is closed and the send fails, the registered destructor is
// automatically called on the item.
#define channel_send(chan, item_ptr) \
  channel_send_((chan), (item_ptr), sizeof(*(item_ptr)))

// BLOCKING Receive. Blocks if the channel is empty.
// CRITICAL: FORBIDDEN on the WebAssembly UI/Main thread! Use only in background
// tasks.
#define channel_recv(chan, out_item_ptr) \
  channel_recv_((chan), (out_item_ptr), sizeof(*(out_item_ptr)))

// NON-BLOCKING Send. Returns immediately. Returns false if the channel is full
// or closed.
// Safe for use on the WebAssembly UI/Main thread.
// If the send fails, the registered destructor is automatically called on the
// item.
#define channel_try_send(chan, item_ptr) \
  channel_try_send_((chan), (item_ptr), sizeof(*(item_ptr)))

// NON-BLOCKING Receive. Returns immediately. Returns false if the channel is
// empty. Safe for use on the WebAssembly UI/Main thread.
#define channel_try_recv(chan, out_item_ptr) \
  channel_try_recv_((chan), (out_item_ptr), sizeof(*(out_item_ptr)))

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_CHANNEL_H_
