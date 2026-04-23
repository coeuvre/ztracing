#include "src/trace_parser.h"

#include <gtest/gtest.h>

TEST(TraceParserTest, BasicArray) {
  TraceParser p = {};
  Allocator a = allocator_get_default();

  const char* json =
      "[{\"name\":\"foo\",\"cat\":\"bar\",\"ph\":\"B\",\"ts\":123,\"pid\":1,"
      "\"tid\":2}]";
  trace_parser_feed(&p, a, json, strlen(json), true);

  TraceEvent ev;
  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
  EXPECT_EQ(ev.name, "foo");
  EXPECT_EQ(ev.cat, "bar");
  EXPECT_EQ(ev.ph, "B");
  EXPECT_EQ(ev.ts, 123);
  EXPECT_EQ(ev.pid, 1);
  EXPECT_EQ(ev.tid, 2);

  EXPECT_FALSE(trace_parser_next(&p, a, &ev));

  trace_parser_deinit(&p, a);
}

TEST(TraceParserTest, BasicObject) {
  TraceParser p = {};
  Allocator a = allocator_get_default();

  const char* json = "{\"traceEvents\":[{\"name\":\"foo\"}],\"other\":123}";
  trace_parser_feed(&p, a, json, strlen(json), true);

  TraceEvent ev;
  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
  EXPECT_EQ(ev.name, "foo");

  EXPECT_FALSE(trace_parser_next(&p, a, &ev));

  trace_parser_deinit(&p, a);
}

TEST(TraceParserTest, Streaming) {
  TraceParser p = {};
  Allocator a = allocator_get_default();

  const char* chunk1 = "[{\"name\":\"fo";
  const char* chunk2 = "o\"},{\"name\":\"bar\"}]";

  trace_parser_feed(&p, a, chunk1, strlen(chunk1), false);
  TraceEvent ev;
  EXPECT_FALSE(trace_parser_next(&p, a, &ev));

  trace_parser_feed(&p, a, chunk2, strlen(chunk2), true);
  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
  EXPECT_EQ(ev.name, "foo");

  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
  EXPECT_EQ(ev.name, "bar");

  EXPECT_FALSE(trace_parser_next(&p, a, &ev));

  trace_parser_deinit(&p, a);
}

TEST(TraceParserTest, StreamingMiddleOfSecondEvent) {
  TraceParser p = {};
  Allocator a = allocator_get_default();

  const char* chunk1 = "[{\"name\":\"foo\"},{\"name\":\"ba";
  const char* chunk2 = "r\"}]";

  trace_parser_feed(&p, a, chunk1, strlen(chunk1), false);
  TraceEvent ev;
  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
  EXPECT_EQ(ev.name, "foo");
  EXPECT_FALSE(trace_parser_next(&p, a, &ev));

  trace_parser_feed(&p, a, chunk2, strlen(chunk2), true);
  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
  EXPECT_EQ(ev.name, "bar");

  EXPECT_FALSE(trace_parser_next(&p, a, &ev));

  trace_parser_deinit(&p, a);
}

TEST(TraceParserTest, Args) {
  TraceParser p = {};
  Allocator a = allocator_get_default();

  const char* json =
      "[{\"name\":\"a\",\"args\":{\"url\":\"http://"
      "foo\",\"id\":123,\"obj\":{\"x\":1}}}]";
  trace_parser_feed(&p, a, json, strlen(json), true);

  TraceEvent ev;
  EXPECT_TRUE(trace_parser_next(&p, a, &ev));
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

TEST(TraceParserTest, Empty) {
  {
    TraceParser p = {};
    Allocator a = allocator_get_default();
    const char* json = "[]";
    trace_parser_feed(&p, a, json, strlen(json), true);
    TraceEvent ev;
    EXPECT_FALSE(trace_parser_next(&p, a, &ev));
    trace_parser_deinit(&p, a);
  }
  {
    TraceParser p = {};
    Allocator a = allocator_get_default();
    const char* json = "{\"traceEvents\":[]}";
    trace_parser_feed(&p, a, json, strlen(json), true);
    TraceEvent ev;
    EXPECT_FALSE(trace_parser_next(&p, a, &ev));
    trace_parser_deinit(&p, a);
  }
}

TEST(TraceParserTest, MemoryLeak) {
  CountingAllocator ca = counting_allocator_init(allocator_get_default());
  Allocator a = counting_allocator_get_allocator(&ca);

  {
    TraceParser p = {};

    const char* json =
        "[{\"name\":\"foo\",\"args\":{\"x\":1}},{\"name\":\"bar\"}]";
    trace_parser_feed(&p, a, json, strlen(json), true);

    TraceEvent ev;
    while (trace_parser_next(&p, a, &ev)) {
      // process
    }

    trace_parser_deinit(&p, a);
  }

  EXPECT_EQ(counting_allocator_get_allocated_bytes(&ca), 0u);
}
