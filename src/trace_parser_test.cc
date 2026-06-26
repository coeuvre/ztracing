#include "src/trace_parser.h"

#include <gtest/gtest.h>

#include "core/allocator.h"
#include "core/counting_allocator.h"

TEST(trace_parser_test, basic_array) {
  trace_parser_t p = {};
  allocator_t* a = c_allocator();

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
  allocator_t* a = c_allocator();

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
  allocator_t* a = c_allocator();

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
  allocator_t* a = c_allocator();

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
  allocator_t* a = c_allocator();

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
    allocator_t* a = c_allocator();
    const char* json = "[]";
    trace_parser_feed(&p, json, strlen(json), true, a);
    trace_event_t ev;
    EXPECT_FALSE(trace_parser_next(&p, &ev, a));
    trace_parser_deinit(&p, a);
  }
  {
    trace_parser_t p = {};
    allocator_t* a = c_allocator();
    const char* json = "{\"traceEvents\":[]}";
    trace_parser_feed(&p, json, strlen(json), true, a);
    trace_event_t ev;
    EXPECT_FALSE(trace_parser_next(&p, &ev, a));
    trace_parser_deinit(&p, a);
  }
}

TEST(trace_parser_test, memory_leak) {
  counting_allocator_t ca;
  counting_allocator_init(&ca, c_allocator());
  allocator_t* a = counting_allocator_get_allocator(&ca);

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

TEST(trace_parser_test, float_numbers) {
  trace_parser_t p = {};
  allocator_t* a = c_allocator();

  // "ts", "dur", "pid", "tid" as floating point numbers in JSON
  const char* json =
      "[{\"name\":\"foo\",\"cat\":\"bar\",\"ph\":\"B\",\"ts\":123.45,\"dur\":"
      "12.34,\"pid\":1.0,\"tid\":2.0}]";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "foo");
  EXPECT_EQ(ev.ts, 123);  // Should be truncated to 123, currently corrupts
  EXPECT_EQ(ev.dur, 12);  // Should be truncated to 12, currently corrupts
  EXPECT_EQ(ev.pid, 1);   // Should be truncated to 1, currently corrupts
  EXPECT_EQ(ev.tid, 2);   // Should be truncated to 2, currently corrupts

  EXPECT_FALSE(trace_parser_next(&p, &ev, a));
  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, exponent_numbers) {
  trace_parser_t p = {};
  allocator_t* a = c_allocator();

  const char* json = "[{\"name\":\"foo\",\"ts\":1e2,\"dur\":1e1}]";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.ts, 100);
  EXPECT_EQ(ev.dur, 10);

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, infinite_loop_on_invalid_char_in_skip) {
  trace_parser_t p = {};
  allocator_t* a = c_allocator();

  // "unknown" is an unknown key, its value has an invalid char 'x' inside an
  // object.
  const char* json = "[{\"name\":\"foo\",\"unknown\":{\"a\":x}}]";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  // With the fix, the parser is resilient: it skips the invalid 'x' inside the
  // unknown object and successfully parses the event!
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "foo");

  EXPECT_FALSE(trace_parser_next(&p, &ev, a));
  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, malformed_numbers) {
  trace_parser_t p = {};
  allocator_t* a = c_allocator();

  // "12+34" is tokenized as a single number token because it only contains
  // [0-9.eE+-]. Our custom numeric parser must strictly validate and stop at
  // '+' returning 12.
  const char* json = "[{\"name\":\"foo\",\"ts\":12+34}]";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  EXPECT_FALSE(trace_parser_next(&p, &ev, a));

  trace_parser_deinit(&p, a);
}

TEST(trace_parser_test, integer_overflow) {
  trace_parser_t p = {};
  allocator_t* a = c_allocator();

  // Test numbers exceeding limits
  const char* json =
      "[{\"name\":\"pos\",\"ts\":999999999999999999999999999999,\"pid\":"
      "99999999999},"
      "{\"name\":\"neg\",\"ts\":-999999999999999999999999999999,\"pid\":-"
      "99999999999}]";
  trace_parser_feed(&p, json, strlen(json), true, a);

  trace_event_t ev;
  // 1. Positive overflow
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "pos");
  EXPECT_EQ(ev.ts, INT64_MAX);
  EXPECT_EQ(ev.pid, INT32_MAX);

  // 2. Negative overflow
  EXPECT_TRUE(trace_parser_next(&p, &ev, a));
  EXPECT_EQ(ev.name, "neg");
  EXPECT_EQ(ev.ts, INT64_MIN);
  EXPECT_EQ(ev.pid, INT32_MIN);

  trace_parser_deinit(&p, a);
}
