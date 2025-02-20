#ifndef ZTRACING_SRC_CHANNEL_H_
#define ZTRACING_SRC_CHANNEL_H_

#include "src/memory.h"
#include "src/platform.h"

typedef struct ChannelItem ChannelItem;
struct ChannelItem {
  ChannelItem *prev;
  ChannelItem *next;
};

typedef struct Channel {
  Arena arena;
  PlatformMutex *mutex;
  PlatformCondition *condition;
  u32 cap;
  u32 len;
  usize item_size;
  ChannelItem *first;
  ChannelItem *last;
  ChannelItem *free_first;
  ChannelItem *free_last;
  bool rx_closed;
  bool tx_closed;
} Channel;

Channel *channel_alloc(u32 cap, usize item_size);
/// Returns false if the rx is closed.
bool channel_send_(Channel *self, usize item_size, void *item);
#define channel_send(self, item) channel_send_(self, sizeof(*item), item)

// Returns false if no item in the buffer and tx is closed. out_item must has at
// least item_size space.
bool channel_receive_(Channel *self, usize item_size, void *item);
#define channel_receive(self, item) \
  channel_receive_(self, sizeof(*item), item)

/// Returns true if the channel is destroyed after this call.
bool channel_close_rx(Channel *self);
/// Returns true if the channel is destroyed after this call.
bool channel_close_tx(Channel *self);

#endif  // ZTRACING_SRC_CHANNEL_H_
