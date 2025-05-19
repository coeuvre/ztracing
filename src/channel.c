#include "src/channel.h"

#include "src/assert.h"
#include "src/list.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/types.h"

Channel *Channel_Create(u32 cap, usize item_size, Allocator allocator) {
  ASSERT(cap > 0);
  Arena *arena = Arena_Create(&(ArenaOptions){
      .allocator = allocator,
  });
  Platform_Mutex *mutex = Platform_Mutex_Create();
  Platform_Condition *condition = Platform_Condition_Create();
  Channel *channel = Arena_PushStruct(arena, Channel);
  *channel = (Channel){
      .arena = arena,
      .mutex = mutex,
      .condition = condition,
      .cap = cap,
      .item_size = item_size,
  };
  return channel;
}

static void Channel_Destroy(Channel *self) {
  ASSERT(self->rx_closed && self->tx_closed);
  Platform_Condition_Destroy(self->condition);
  Platform_Mutex_Destroy(self->mutex);
  Arena_Destroy(self->arena);
}

static void DoSend(Channel *self, usize item_size, void *item) {
  ASSERT(self->len < self->cap);
  ChannelItem *ci = self->free_first;
  if (ci) {
    DLL_REMOVE(self->free_first, self->free_last, ci, prev, next);
  } else {
    ci = Arena_Push(self->arena, sizeof(ChannelItem) + item_size, 1);
  }
  DLL_APPEND(self->first, self->last, ci, prev, next);

  CopyMemory(ci + 1, item, item_size);
  self->len += 1;

  Platform_Condition_Broadcast(self->condition);
}

bool Channel_Send_(Channel *self, usize item_size, void *item) {
  ASSERT(self->item_size == item_size);

  bool sent = false;

  Platform_Mutex_Lock(self->mutex);

  while (!(self->len < self->cap || self->rx_closed)) {
    Platform_Condition_Wait(self->condition, self->mutex);
  }

  if (!self->rx_closed) {
    DoSend(self, item_size, item);
    sent = true;
  }

  Platform_Mutex_Unlock(self->mutex);

  return sent;
}

bool Channel_TrySend_(Channel *self, usize item_size, void *item) {
  ASSERT(self->item_size == item_size);

  bool sent = false;

  Platform_Mutex_Lock(self->mutex);
  if (self->len < self->cap) {
    DoSend(self, item_size, item);
    sent = true;
  }
  Platform_Mutex_Unlock(self->mutex);

  return sent;
}

bool Channel_Receive_(Channel *self, usize item_size, void *item) {
  ASSERT(self->item_size == item_size);

  bool received = false;

  Platform_Mutex_Lock(self->mutex);

  while (!(self->len || self->tx_closed)) {
    Platform_Condition_Wait(self->condition, self->mutex);
  }

  if (self->len) {
    received = true;
    ChannelItem *ci = self->first;
    ASSERT(ci);
    DLL_REMOVE(self->first, self->last, ci, prev, next);
    self->len -= 1;

    CopyMemory(item, ci + 1, item_size);

    DLL_PREPEND(self->free_first, self->free_last, ci, prev, next);

    Platform_Condition_Broadcast(self->condition);
  }

  Platform_Mutex_Unlock(self->mutex);

  return received;
}

bool Channel_CloseRx(Channel *self) {
  Platform_Mutex_Lock(self->mutex);

  ASSERT(!self->rx_closed);
  self->rx_closed = true;
  Platform_Condition_Broadcast(self->condition);

  bool all_closed = self->rx_closed && self->tx_closed;

  Platform_Mutex_Unlock(self->mutex);

  if (all_closed) {
    Channel_Destroy(self);
  }

  return all_closed;
}

bool Channel_CloseTx(Channel *self) {
  Platform_Mutex_Lock(self->mutex);

  ASSERT(!self->tx_closed);
  self->tx_closed = true;
  Platform_Condition_Broadcast(self->condition);

  bool all_closed = self->rx_closed && self->tx_closed;

  Platform_Mutex_Unlock(self->mutex);

  if (all_closed) {
    Channel_Destroy(self);
  }

  return all_closed;
}
