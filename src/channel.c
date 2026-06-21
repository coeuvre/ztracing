#include "src/channel.h"

#include <pthread.h>
#include <string.h>

#include "src/assert.h"

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
  bool closed;       // True if the channel has been closed
};

channel_t* channel_create_(size_t item_size, size_t capacity,
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
  chan->closed = false;

  chan->buffer = allocator_alloc(allocator, capacity * item_size);
  CHECK(chan->buffer != nullptr);

  return chan;
}

void channel_destroy(channel_t* chan, allocator_t allocator) {
  CHECK(chan != nullptr);

  if (chan->buffer != nullptr) {
    allocator_free(allocator, chan->buffer, chan->capacity * chan->item_size);
  }

  pthread_mutex_destroy(&chan->mutex);
  pthread_cond_destroy(&chan->cv_recv);
  pthread_cond_destroy(&chan->cv_send);

  allocator_free(allocator, chan, sizeof(channel_t));
}

bool channel_send_(channel_t* chan, const void* item, size_t item_size) {
  CHECK(chan != nullptr);
  CHECK(item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);

  while (chan->size == chan->capacity && !chan->closed) {
    pthread_cond_wait(&chan->cv_send, &chan->mutex);
  }

  if (chan->closed) {
    pthread_mutex_unlock(&chan->mutex);
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
  CHECK(chan != nullptr);
  CHECK(out_item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);

  while (chan->size == 0 && !chan->closed) {
    pthread_cond_wait(&chan->cv_recv, &chan->mutex);
  }

  // If the channel is closed and empty, we cannot pop anymore.
  if (chan->size == 0 && chan->closed) {
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

bool channel_try_send_(channel_t* chan, const void* item, size_t item_size) {
  CHECK(chan != nullptr);
  CHECK(item != nullptr);
  // Runtime Type-Size Safety Assertion
  CHECK(chan->item_size == item_size);
  (void)item_size;

  pthread_mutex_lock(&chan->mutex);

  if (chan->size == chan->capacity || chan->closed) {
    pthread_mutex_unlock(&chan->mutex);
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

  if (chan->size == 0) {
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

static void channel_close(channel_t* chan) {
  CHECK(chan != nullptr);

  pthread_mutex_lock(&chan->mutex);
  chan->closed = true;
  pthread_cond_broadcast(&chan->cv_recv);
  pthread_cond_broadcast(&chan->cv_send);
  pthread_mutex_unlock(&chan->mutex);
}

void channel_close_and_drain_(channel_t* chan,
                              void (*destructor)(void* item,
                                                 allocator_t allocator),
                              allocator_t allocator) {
  CHECK(chan != nullptr);
  channel_close(chan);

  // Allocate stack space for one item on the heap
  void* item = allocator_alloc(allocator, chan->item_size);
  CHECK(item != nullptr);

  // Drain every remaining item and destroy it
  while (channel_try_recv_(chan, item, chan->item_size)) {
    if (destructor != nullptr) {
      destructor(item, allocator);
    }
  }

  allocator_free(allocator, item, chan->item_size);
}

size_t channel_get_size(const channel_t* chan) {
  CHECK(chan != nullptr);

  // We cast away const for the lock, as mutex locking modifies the mutex state,
  // but it is conceptually a const query.
  channel_t* non_const_chan = (channel_t*)chan;
  pthread_mutex_lock(&non_const_chan->mutex);
  size_t size = chan->size;
  pthread_mutex_unlock(&non_const_chan->mutex);

  return size;
}

bool channel_is_closed(const channel_t* chan) {
  CHECK(chan != nullptr);

  channel_t* non_const_chan = (channel_t*)chan;
  pthread_mutex_lock(&non_const_chan->mutex);
  bool closed = chan->closed;
  pthread_mutex_unlock(&non_const_chan->mutex);

  return closed;
}
