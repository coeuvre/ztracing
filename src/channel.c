#include "src/channel.h"

#include "src/assert.h"
#include "src/list.h"
#include "src/memory.h"
#include "src/platform.h"
#include "src/types.h"

Channel *channel_alloc(u32 cap, usize item_size) {
  ASSERT(cap > 0);
  Arena arena_ = {0};
  PlatformMutex *mutex = platform_mutex_alloc();
  PlatformCondition *condition = platform_condition_alloc();
  Channel *channel = arena_push_struct_no_zero(&arena_, Channel);
  *channel = (Channel){
      .arena = arena_,
      .mutex = mutex,
      .condition = condition,
      .cap = cap,
      .item_size = item_size,
  };
  return channel;
}

static void channel__free(Channel *self) {
  ASSERT(self->rx_closed && self->tx_closed);
  platform_condition_free(self->condition);
  platform_mutex_free(self->mutex);
  arena_free(&self->arena);
}

bool channel_send_(Channel *self, usize item_size, void *item) {
  ASSERT(self->item_size == item_size);

  bool sent = false;

  platform_mutex_lock(self->mutex);

  while (!(self->len < self->cap || self->rx_closed)) {
    platform_condition_wait(self->condition, self->mutex);
  }

  if (!self->rx_closed) {
    ASSERT(self->len < self->cap);
    sent = true;

    ChannelItem *ci = self->free_first;
    if (ci) {
      DLL_REMOVE(self->free_first, self->free_last, ci, prev, next);
    } else {
      ci = arena_push(&self->arena, sizeof(ChannelItem) + item_size,
                      ARENA_PUSH_NO_ZERO);
      DLL_APPEND(self->first, self->last, ci, prev, next);
    }

    memory_copy(ci + 1, item, item_size);
    self->len += 1;

    platform_condition_broadcast(self->condition);
  }

  platform_mutex_unlock(self->mutex);

  return sent;
}

bool channel_receive_(Channel *self, usize item_size, void *item) {
  ASSERT(self->item_size == item_size);

  bool received = false;

  platform_mutex_lock(self->mutex);

  while (!(self->len || self->tx_closed)) {
    platform_condition_wait(self->condition, self->mutex);
  }

  if (self->len) {
    received = true;
    ChannelItem *ci = self->first;
    ASSERT(ci);
    DLL_REMOVE(self->first, self->last, ci, prev, next);
    self->len -= 1;

    memory_copy(item, ci + 1, item_size);

    DLL_PREPEND(self->free_first, self->free_last, ci, prev, next);

    platform_condition_broadcast(self->condition);
  }

  platform_mutex_unlock(self->mutex);

  return received;
}

bool channel_close_rx(Channel *self) {
  platform_mutex_lock(self->mutex);

  ASSERT(!self->rx_closed);
  self->rx_closed = true;
  platform_condition_broadcast(self->condition);

  bool all_closed = self->rx_closed && self->tx_closed;

  platform_mutex_unlock(self->mutex);

  if (all_closed) {
    channel__free(self);
  }

  return all_closed;
}

bool channel_close_tx(Channel *self) {
  platform_mutex_lock(self->mutex);

  ASSERT(!self->tx_closed);
  self->tx_closed = true;
  platform_condition_broadcast(self->condition);

  bool all_closed = self->rx_closed && self->tx_closed;

  platform_mutex_unlock(self->mutex);

  if (all_closed) {
    channel__free(self);
  }

  return all_closed;
}
