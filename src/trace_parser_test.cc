#include "src/trace_parser.h"

#include <gtest/gtest.h>

#include "src/allocator.h"

TEST(trace_parser_test, basic_array) {
  trace_parser_t p = {};
  allocator_t a = allocator_get_default();

  const char* json =
      "[{\"name\":\"foo\",\"cat\":\"bar\",\"ph\":\"B\",\"ts\":123,\"pid\":1,"
      "\"tid\":2}]";
  // Exercise the new allocator-last C API!
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "foo");
  EXPECT_EQ(ev.cat, "bar");
  EXPECT_EQ(ev.ph, "B");
  EXPECT_EQ(ev.ts, 123);
  EXPECT_EQ(ev.pid, 1);
  EXPECT_EQ(ev.tid, 2);

  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, basic_object) {
  trace_parser_t p = {};
  allocator_t a = allocator_get_default();

  const char* json = "{\"traceEvents\":[{\"name\":\"foo\"}],\"other\":123}";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "foo");

  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, streaming) {
  trace_parser_t p = {};
  allocator_t a = allocator_get_default();

  const char* chunk1 = "[{\"name\":\"fo";
  const char* chunk2 = "o\"},{\"name\":\"bar\"}]";

  trace_parser_feed(&p, chunk1, strlen(chunk1), false, a);
  trace_event_t ev;
  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_feed(&p, chunk2, strlen(chunk2), true, a);
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "foo");

  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "bar");

  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, streaming_middle_of_second_event) {
  trace_parser_t p = {};
  allocator_t a = allocator_get_default();

  const char* chunk1 = "[{\"name\":\"foo\"},{\"name\":\"ba";
  const char* chunk2 = "r\"}]";

  trace_parser_feed(&p, chunk1, strlen(chunk1), false, a);
  trace_event_t ev;
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "foo");
  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_feed(&p, chunk2, strlen(chunk2), true, a);
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "bar");

  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, args) {
  trace_parser_t p = {};
  allocator_t a = allocator_get_default();

  const char* json =
      "[{\"name\":\"a\",\"args\":{\"url\":\"http://"
      "foo\",\"id\":123,\"obj\":{\"x\":1}}}]";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.args_count, 3u);
  EXPECT_EQ(ev.args[0].key, "url");
  EXPECT_EQ(ev.args[0].val, "http://foo");
  EXPECT_EQ(ev.args[1].key, "id");
  EXPECT_EQ(ev.args[1].val, "");
  EXPECT_DOUBLE_EQ(ev.args[1].val_double, 123.0);
  EXPECT_EQ(ev.args[2].key, "obj");
  EXPECT_EQ(ev.args[2].val, "{\"x\":1}");

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, empty) {
  {
    trace_parser_t p = {};
    allocator_t a = allocator_get_default();
    const char* json = "[]";
    trace_parser_feed(&p, json, strlen(json), true, a);
    trace_event_t ev;
    EXPECT_FALSE(trace_parser_next(&p, &ev, a));
    trace_parser_deinit(&p, a);
  }
  {
    trace_parser_t p = {};
    allocator_t a = allocator_get_default();
    const char* json = "{\"traceEvents\":[]}";
    trace_parser_feed(&p, json, strlen(json), true, a);
    trace_event_t ev;
    EXPECT_FALSE(trace_parser_next(&p, &ev, a));
    trace_parser_deinit(&p, a);
  }
}

TEST(trace_parser_test, memory_leak) {
  counting_allocator_t ca = counting_allocator_init(allocator_get_default());
  allocator_t a = counting_allocator_get_allocator(&ca);

  {
    trace_parser_t p = {};

    const char* json =
        "[{\"name\":\"foo\",\"args\":{\"x\":1}},{\"name\":\"bar\"}]";
    trace_parser_feed(&p, json, strlen(json), true, a);

    trace_event_t ev;
    while (trace_parser_next(&p, &ev, a)) {
      // process
    }

    trace_parser_deinit(&p, a);
  }

  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
