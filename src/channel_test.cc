#include "src/channel.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "src/allocator.h"

// Generic 64-byte structure to test struct copying and boundary alignments
struct mock_64_byte_t {
  uint64_t fields[8];
};

TEST(channel_test, basic_fifo_int) {
  allocator_t allocator = allocator_get_default();

  // Create channel for int
  channel_t* chan = channel_create(int, 10, nullptr, allocator);
  EXPECT_EQ(channel_get_size(chan), 0u);

  // Push sequential values
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(channel_send(chan, &i));
    EXPECT_EQ(channel_get_size(chan), (size_t)(i + 1));
  }

  // Pop and verify FIFO order
  for (int i = 0; i < 5; ++i) {
    int out = -1;
    EXPECT_TRUE(channel_recv(chan, &out));
    EXPECT_EQ(out, i);
    EXPECT_EQ(channel_get_size(chan), (size_t)(4 - i));
  }

  channel_destroy(chan);
}

TEST(channel_test, basic_fifo_double) {
  allocator_t allocator = allocator_get_default();

  // Create channel for double
  channel_t* chan = channel_create(double, 5, nullptr, allocator);

  double val1 = 3.14;
  double val2 = 2.718;

  EXPECT_TRUE(channel_send(chan, &val1));
  EXPECT_TRUE(channel_send(chan, &val2));

  double out = 0.0;
  EXPECT_TRUE(channel_recv(chan, &out));
  EXPECT_DOUBLE_EQ(out, 3.14);

  EXPECT_TRUE(channel_recv(chan, &out));
  EXPECT_DOUBLE_EQ(out, 2.718);

  channel_destroy(chan);
}

TEST(channel_test, basic_fifo_struct) {
  allocator_t allocator = allocator_get_default();

  // Create channel for generic 64-byte struct
  channel_t* chan = channel_create(mock_64_byte_t, 3, nullptr, allocator);

  mock_64_byte_t msg1 = {.fields = {1, 2, 3, 4, 5, 6, 7, 8}};
  mock_64_byte_t msg2 = {.fields = {9, 10, 11, 12, 13, 14, 15, 16}};

  EXPECT_TRUE(channel_send(chan, &msg1));
  EXPECT_TRUE(channel_send(chan, &msg2));
  EXPECT_EQ(channel_get_size(chan), 2u);

  mock_64_byte_t out = {};
  EXPECT_TRUE(channel_recv(chan, &out));
  EXPECT_EQ(out.fields[0], 1u);
  EXPECT_EQ(out.fields[7], 8u);

  EXPECT_TRUE(channel_recv(chan, &out));
  EXPECT_EQ(out.fields[0], 9u);
  EXPECT_EQ(out.fields[7], 16u);

  channel_destroy(chan);
}

TEST(channel_test, boundary_conditions) {
  allocator_t allocator = allocator_get_default();

  // Saturated bounded channel of size 2
  channel_t* chan = channel_create(int, 2, nullptr, allocator);

  int val = 42;
  EXPECT_TRUE(channel_try_send(chan, &val));
  EXPECT_TRUE(channel_try_send(chan, &val));

  // Sending on a full channel must fail instantly (non-blocking)
  EXPECT_FALSE(channel_try_send(chan, &val));

  int out = 0;
  EXPECT_TRUE(channel_try_recv(chan, &out));
  EXPECT_EQ(out, 42);

  EXPECT_TRUE(channel_try_recv(chan, &out));
  EXPECT_EQ(out, 42);

  // Receiving from an empty channel must fail instantly (non-blocking)
  EXPECT_FALSE(channel_try_recv(chan, &out));

  channel_destroy(chan);
}

TEST(channel_test, thread_blocking_recv) {
  allocator_t allocator = allocator_get_default();
  channel_t* chan = channel_create(int, 5, nullptr, allocator);

  std::atomic<bool> thread_started{false};
  std::atomic<int> received_val{-1};

  std::thread t([&]() {
    thread_started = true;
    int val = -1;
    // This will block until the main thread sends a value
    EXPECT_TRUE(channel_recv(chan, &val));
    received_val = val;
  });

  // Wait for background thread to start and block
  while (!thread_started) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_EQ(received_val, -1);

  // Send value to unblock the thread
  int send_val = 100;
  EXPECT_TRUE(channel_send(chan, &send_val));

  t.join();
  EXPECT_EQ(received_val, 100);

  channel_destroy(chan);
}

TEST(channel_test, thread_blocking_send) {
  allocator_t allocator = allocator_get_default();
  // Bounded channel of size 1
  channel_t* chan = channel_create(int, 1, nullptr, allocator);

  int fill = 99;
  EXPECT_TRUE(channel_send(chan, &fill));

  std::atomic<bool> thread_started{false};
  std::atomic<bool> send_completed{false};

  std::thread t([&]() {
    thread_started = true;
    int val = 200;
    // This will block because the channel is full
    EXPECT_TRUE(channel_send(chan, &val));
    send_completed = true;
  });

  // Wait for background thread to start and block
  while (!thread_started) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_FALSE(send_completed);

  // Pop the filling item to make room and unblock the thread
  int out = -1;
  EXPECT_TRUE(channel_recv(chan, &out));
  EXPECT_EQ(out, 99);

  t.join();
  EXPECT_TRUE(send_completed);

  // Verify the blocked item was successfully queued and popped
  EXPECT_TRUE(channel_recv(chan, &out));
  EXPECT_EQ(out, 200);

  channel_destroy(chan);
}

TEST(channel_test, thread_wakeup_close_recv) {
  allocator_t allocator = allocator_get_default();
  channel_t* chan = channel_create(int, 5, nullptr, allocator);

  std::atomic<bool> thread_started{false};
  std::atomic<bool> recv_returned_false{false};

  std::thread t([&]() {
    thread_started = true;
    int val = -1;
    // Blocks on empty channel, must wake up and return false when closed
    if (!channel_recv(chan, &val)) {
      recv_returned_false = true;
    }
  });

  while (!thread_started) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_FALSE(recv_returned_false);

  // Close channel to wake up and cancel the blocked receiver
  channel_close(chan);

  t.join();
  EXPECT_TRUE(recv_returned_false);

  channel_destroy(chan);
}

TEST(channel_test, thread_wakeup_close_send) {
  allocator_t allocator = allocator_get_default();
  // Bounded channel of size 1
  channel_t* chan = channel_create(int, 1, nullptr, allocator);

  int fill = 50;
  EXPECT_TRUE(channel_send(chan, &fill));

  std::atomic<bool> thread_started{false};
  std::atomic<bool> send_returned_false{false};

  std::thread t([&]() {
    thread_started = true;
    int val = 60;
    // Blocks on full channel, must wake up and return false when closed
    if (!channel_send(chan, &val)) {
      send_returned_false = true;
    }
  });

  while (!thread_started) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_FALSE(send_returned_false);

  // Close channel to wake up and cancel the blocked writer
  channel_close(chan);

  t.join();
  EXPECT_TRUE(send_returned_false);

  channel_destroy(chan);
}

TEST(channel_test, concurrent_producers_consumers) {
  allocator_t allocator = allocator_get_default();
  // Bounded channel with high contention
  channel_t* chan = channel_create(int, 10, nullptr, allocator);

  const int num_producers = 4;
  const int num_consumers = 4;
  const int items_per_producer = 1000;

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;

  std::atomic<int> total_sum_sent{0};
  std::atomic<int> total_sum_received{0};

  // Spawn producers
  for (int p = 0; p < num_producers; ++p) {
    producers.emplace_back([&, p]() {
      int local_sum = 0;
      for (int i = 0; i < items_per_producer; ++i) {
        int item = p * items_per_producer + i;
        EXPECT_TRUE(channel_send(chan, &item));
        local_sum += item;
      }
      total_sum_sent += local_sum;
    });
  }

  // Spawn consumers
  for (int c = 0; c < num_consumers; ++c) {
    consumers.emplace_back([&]() {
      int local_sum = 0;
      int item = 0;
      while (channel_recv(chan, &item)) {
        local_sum += item;
      }
      total_sum_received += local_sum;
    });
  }

  // Wait for all producers to finish pushing
  for (auto& t : producers) {
    t.join();
  }

  // Close the channel to signal consumers that no more items are coming
  channel_close(chan);

  // Wait for consumers to finish draining the channel
  for (auto& t : consumers) {
    t.join();
  }

  // Mathematical validation: no data lost or duplicated!
  EXPECT_EQ(total_sum_received.load(), total_sum_sent.load());

  channel_destroy(chan);
}

TEST(channel_test, memory_leak_verification) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    channel_t* chan = channel_create(mock_64_byte_t, 100, nullptr, a);

    mock_64_byte_t msg = {};
    for (int i = 0; i < 50; ++i) {
      EXPECT_TRUE(channel_send(chan, &msg));
    }
    EXPECT_EQ(channel_get_size(chan), 50u);

    mock_64_byte_t out;
    for (int i = 0; i < 50; ++i) {
      EXPECT_TRUE(channel_recv(chan, &out));
    }

    channel_destroy(chan);
  }

  // Verify that all allocated memory was completely freed
  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}

// === New Safety & Automatic Cleanup Tests ===

struct mock_resource_t {
  int* ref_count;
};

static void mock_resource_deinit(void* item) {
  mock_resource_t* res = (mock_resource_t*)item;
  if (res->ref_count != nullptr) {
    (*res->ref_count)--;
    res->ref_count = nullptr;
  }
}

TEST(channel_test, auto_destruction_on_closed_send) {
  allocator_t a = allocator_get_default();

  channel_t* chan = channel_create(mock_resource_t, 5, mock_resource_deinit, a);

  int ref_count = 1;
  mock_resource_t res = {&ref_count};

  // Close the channel
  channel_close(chan);

  // Sending on closed channel must fail and AUTOMATICALLY call
  // mock_resource_deinit
  EXPECT_FALSE(channel_send(chan, &res));
  EXPECT_EQ(ref_count, 0);

  channel_destroy(chan);
}

TEST(channel_test, auto_destruction_on_full_try_send) {
  allocator_t a = allocator_get_default();

  channel_t* chan = channel_create(mock_resource_t, 1, mock_resource_deinit, a);

  int ref_count_fill = 1;
  mock_resource_t res_fill = {&ref_count_fill};
  EXPECT_TRUE(channel_try_send(chan, &res_fill));

  int ref_count_fail = 1;
  mock_resource_t res_fail = {&ref_count_fail};

  // Sending on full channel via try_send must fail and AUTOMATICALLY call
  // mock_resource_deinit
  EXPECT_FALSE(channel_try_send(chan, &res_fail));
  EXPECT_EQ(ref_count_fail, 0);

  // Destroying the channel must automatically drain the filling item and deinit
  // it
  channel_destroy(chan);
  EXPECT_EQ(ref_count_fill, 0);
}

TEST(channel_test, auto_drain_on_destroy) {
  allocator_t a = allocator_get_default();

  channel_t* chan = channel_create(mock_resource_t, 5, mock_resource_deinit, a);

  int ref1 = 1;
  int ref2 = 1;
  mock_resource_t res1 = {&ref1};
  mock_resource_t res2 = {&ref2};

  EXPECT_TRUE(channel_send(chan, &res1));
  EXPECT_TRUE(channel_send(chan, &res2));
  EXPECT_EQ(channel_get_size(chan), 2u);

  // Destroying the channel must automatically drain all remaining items and
  // call the destructor on each
  channel_destroy(chan);

  EXPECT_EQ(ref1, 0);
  EXPECT_EQ(ref2, 0);
}
