#include "src/channel.h"

#include <pthread.h>
#include <string.h>

#include "src/assert.h"
#include "src/platform.h"

struct channel {
  pthread_mutex_t mutex;
  pthread_cond_t cv_recv;
  pthread_cond_t cv_send;
  void* buffer;      // Raw circular buffer memory (capacity * item_size bytes)
  size_t item_size;  // Size of each item in bytes
  size_t capacity;   // Maximum items the buffer can hold
  size_t head;       // Index of the next item to read (pop)
  size_t tail;       // Index of the next slot to write (push)
  size_t size;       // Current number of items in the channel
  bool tx_closed;    // True if the sender side has been closed
  bool rx_closed;    // True if the receiver side has been closed
  channel_item_destructor_t destructor;
  allocator_t allocator;
};

channel_t* channel_create_(size_t item_size, size_t capacity,
                           channel_item_destructor_t destructor,
                           allocator_t allocator) {
  CHECK(item_size > 0);
  CHECK(capacity > 0);

  channel_t* chan = allocator_alloc(allocator, sizeof(channel_t));
  CHECK(chan != nullptr);

  pthread_mutex_init(&chan->mutex, nullptr);
  pthread_cond_init(&chan->cv_recv, nullptr);
  pthread_cond_init(&chan->cv_send, nullptr);

  chan->item_size = item_size;
  chan->capacity = capacity;
  chan->head = 0;
  chan->tail = 0;
  chan->size = 0;
  chan->tx_closed = false;
  chan->rx_closed = false;
  chan->destructor = destructor;
  chan->allocator = allocator;

  chan->buffer = allocator_alloc(allocator, capacity * item_size);
  CHECK(chan->buffer != nullptr);

  return chan;
}

void channel_destroy(channel_t* chan) {
  CHECK(chan != nullptr);

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);
  CHECK(chan->tx_closed);
  CHECK(chan->rx_closed);

  // Drain every remaining item and destroy it using the registered destructor
  if (chan->buffer != nullptr) {
    if (chan->destructor != nullptr && chan->size > 0) {
      void* item = allocator_alloc(chan->allocator, chan->item_size);
      CHECK(item != nullptr);
      while (chan->size > 0) {
        const void* src =
            (const char*)chan->buffer + chan->head * chan->item_size;
        memcpy(item, src, chan->item_size);
        chan->head = (chan->head + 1) % chan->capacity;
        chan->size--;
        chan->destructor(item);
      }
      allocator_free(chan->allocator, item, chan->item_size);
    }
    allocator_free(chan->allocator, chan->buffer,
                   chan->capacity * chan->item_size);
  }

  // Gravestone: invalidate item_size last, after all reads are done.
  chan->item_size = 0;
  pthread_mutex_unlock(&chan->mutex);

  pthread_mutex_destroy(&chan->mutex);
  pthread_cond_destroy(&chan->cv_recv);
  pthread_cond_destroy(&chan->cv_send);

  allocator_free(chan->allocator, chan, sizeof(channel_t));
}

bool channel_send_(channel_t* chan, void* item, size_t item_size) {
#ifdef __EMSCRIPTEN__
  CHECK(!platform_is_main_thread() &&
        "FATAL: Blocking channel_send called on UI thread!");
#endif
  CHECK(chan != nullptr);
  CHECK(item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);

  while (chan->size == chan->capacity && !chan->tx_closed && !chan->rx_closed) {
    pthread_cond_wait(&chan->cv_send, &chan->mutex);
  }

  if (chan->tx_closed || chan->rx_closed) {
    pthread_mutex_unlock(&chan->mutex);
    if (chan->destructor != nullptr) {
      chan->destructor(item);
    }
    return false;
  }

  void* dest = (char*)chan->buffer + chan->tail * chan->item_size;
  memcpy(dest, item, chan->item_size);

  chan->tail = (chan->tail + 1) % chan->capacity;
  chan->size++;

  pthread_cond_signal(&chan->cv_recv);
  pthread_mutex_unlock(&chan->mutex);

  return true;
}

bool channel_recv_(channel_t* chan, void* out_item, size_t item_size) {
#ifdef __EMSCRIPTEN__
  CHECK(!platform_is_main_thread() &&
        "FATAL: Blocking channel_recv called on UI thread!");
#endif
  CHECK(chan != nullptr);
  CHECK(out_item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);

  while (chan->size == 0 && !chan->tx_closed && !chan->rx_closed) {
    pthread_cond_wait(&chan->cv_recv, &chan->mutex);
  }

  // If the channel is closed and empty, we cannot pop anymore.
  if ((chan->size == 0 && chan->tx_closed) || chan->rx_closed) {
    pthread_mutex_unlock(&chan->mutex);
    return false;
  }

  const void* src = (const char*)chan->buffer + chan->head * chan->item_size;
  memcpy(out_item, src, chan->item_size);

  chan->head = (chan->head + 1) % chan->capacity;
  chan->size--;

  pthread_cond_signal(&chan->cv_send);
  pthread_mutex_unlock(&chan->mutex);

  return true;
}

bool channel_try_send_(channel_t* chan, void* item, size_t item_size) {
  CHECK(chan != nullptr);
  CHECK(item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);

  if (chan->size == chan->capacity || chan->tx_closed || chan->rx_closed) {
    pthread_mutex_unlock(&chan->mutex);
    if (chan->destructor != nullptr) {
      chan->destructor(item);
    }
    return false;
  }

  void* dest = (char*)chan->buffer + chan->tail * chan->item_size;
  memcpy(dest, item, chan->item_size);

  chan->tail = (chan->tail + 1) % chan->capacity;
  chan->size++;

  pthread_cond_signal(&chan->cv_recv);
  pthread_mutex_unlock(&chan->mutex);

  return true;
}

bool channel_try_recv_(channel_t* chan, void* out_item, size_t item_size) {
  CHECK(chan != nullptr);
  CHECK(out_item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);

  if (chan->size == 0 || chan->rx_closed) {
    pthread_mutex_unlock(&chan->mutex);
    return false;
  }

  const void* src = (const char*)chan->buffer + chan->head * chan->item_size;
  memcpy(out_item, src, chan->item_size);

  chan->head = (chan->head + 1) % chan->capacity;
  chan->size--;

  pthread_cond_signal(&chan->cv_send);
  pthread_mutex_unlock(&chan->mutex);

  return true;
}

void channel_close_tx(channel_t* chan) {
  CHECK(chan != nullptr);

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);
  CHECK(!chan->tx_closed);
  chan->tx_closed = true;
  pthread_cond_broadcast(&chan->cv_recv);
  pthread_cond_broadcast(&chan->cv_send);
  pthread_mutex_unlock(&chan->mutex);
}

void channel_close_rx(channel_t* chan) {
  CHECK(chan != nullptr);

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);
  CHECK(!chan->rx_closed);
  chan->rx_closed = true;
  pthread_cond_broadcast(&chan->cv_recv);
  pthread_cond_broadcast(&chan->cv_send);
  pthread_mutex_unlock(&chan->mutex);
}

size_t channel_get_size(channel_t* chan) {
  CHECK(chan != nullptr);

  // We lock the mutex even though this is conceptually a const query.
  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);
  size_t size = chan->size;
  pthread_mutex_unlock(&chan->mutex);

  return size;
}

bool channel_is_tx_closed(channel_t* chan) {
  CHECK(chan != nullptr);

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);
  bool closed = chan->tx_closed;
  pthread_mutex_unlock(&chan->mutex);

  return closed;
}

bool channel_is_rx_closed(channel_t* chan) {
  CHECK(chan != nullptr);

  pthread_mutex_lock(&chan->mutex);
  CHECK(chan->item_size);
  bool closed = chan->rx_closed;
  pthread_mutex_unlock(&chan->mutex);

  return closed;
}
