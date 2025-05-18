#ifndef ZTRACING_SRC_CHANNEL_H_
#define ZTRACING_SRC_CHANNEL_H_

#include "src/memory.h"
#include "src/platform.h"
#include "src/types.h"

typedef struct ChannelItem ChannelItem;
struct ChannelItem {
  ChannelItem *prev;
  ChannelItem *next;
};

typedef struct Channel {
  Arena arena;
  Platform_Mutex *mutex;
  Platform_Condition *condition;
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

Channel *Channel_Create(u32 cap, usize item_size);
/// Returns false if the rx is closed.
bool Channel_Send_(Channel *self, usize item_size, void *item);
#define Channel_Send(self, item) Channel_Send_(self, sizeof(*item), item)

// Returns false if no item in the buffer and tx is closed. out_item must has at
// least item_size space.
bool Channel_Receive_(Channel *self, usize item_size, void *item);
#define Channel_Receive(self, item) Channel_Receive_(self, sizeof(*item), item)

/// Returns true if the channel is destroyed after this call.
bool Channel_CloseRx(Channel *self);
/// Returns true if the channel is destroyed after this call.
bool Channel_CloseTx(Channel *self);

#endif  // ZTRACING_SRC_CHANNEL_H_
