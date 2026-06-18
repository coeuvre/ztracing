#include "src/trace_data.h"

#include <gtest/gtest.h>

#include "src/colors.h"

static std::string_view to_sv(string_t s) {
  return std::string_view(s.ptr, s.len);
}

TEST(trace_data_test, basic) {
  allocator_t a = allocator_get_default();
  trace_data_t td = {};

  trace_event_t ev = {};
  ev.name = string_lit("event1");
  ev.cat = string_lit("cat1");
  ev.ph = string_lit("X");
  ev.ts = 100;
  ev.dur = 50;
  ev.pid = 1;
  ev.tid = 2;

  trace_arg_t args[2];
  args[0] = {string_lit("key1"), string_lit("val1"), 0.0};
  args[1] = {string_lit("key2"), string_lit("val2"), 0.0};
  ev.args = args;
  ev.args_count = 2;

  trace_event_matcher_t matcher = {};
  trace_data_add_event(&td, theme_get_dark(), &ev, &matcher, a);

  ASSERT_EQ(td.events.len, 1u);
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td.events.ptr;
  const trace_event_persisted_t& p = events[0];
  EXPECT_EQ(to_sv(trace_data_get_string(&td, p.name_ref)), "event1");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, p.cat_ref)), "cat1");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, p.ph_ref)), "X");
  EXPECT_EQ(p.ts, 100);
  EXPECT_EQ(p.dur, 50);
  EXPECT_EQ(p.pid, 1);
  EXPECT_EQ(p.tid, 2);
  EXPECT_EQ(p.args_count, 2u);

  ASSERT_EQ(td.args.len, 2u);
  const trace_arg_persisted_t* td_args =
      (const trace_arg_persisted_t*)td.args.ptr;
  const trace_arg_persisted_t& pa1 = td_args[p.args_offset];
  EXPECT_EQ(to_sv(trace_data_get_string(&td, pa1.key_ref)), "key1");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, pa1.val_ref)), "val1");

  const trace_arg_persisted_t& pa2 = td_args[p.args_offset + 1];
  EXPECT_EQ(to_sv(trace_data_get_string(&td, pa2.key_ref)), "key2");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, pa2.val_ref)), "val2");

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(trace_data_test, de_duplication) {
  allocator_t a = allocator_get_default();
  trace_data_t td = {};

  string_ref_t ref1 = trace_data_push_string(&td, string_lit("foo"), a);
  string_ref_t ref2 = trace_data_push_string(&td, string_lit("bar"), a);
  string_ref_t ref3 = trace_data_push_string(&td, string_lit("foo"), a);

  EXPECT_EQ(ref1, ref3);
  EXPECT_NE(ref1, ref2);
  EXPECT_EQ(td.string_table.len, 2u);

  EXPECT_EQ(to_sv(trace_data_get_string(&td, ref1)), "foo");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, ref2)), "bar");

  trace_data_deinit(&td, a);
}

TEST(trace_data_test, clear) {
  allocator_t a = allocator_get_default();
  trace_data_t td = {};

  trace_data_push_string(&td, string_lit("foo"), a);
  trace_event_t ev = {};
  ev.name = string_lit("foo");
  trace_event_matcher_t matcher = {};
  trace_data_add_event(&td, theme_get_dark(), &ev, &matcher, a);

  trace_data_clear(&td, a);

  EXPECT_EQ(td.string_buffer.len, 0u);
  EXPECT_EQ(td.string_table.len, 0u);
  EXPECT_EQ(td.events.len, 0u);
  EXPECT_EQ(td.string_lookup.size, 0u);

  // Should still be usable
  string_ref_t ref = trace_data_push_string(&td, string_lit("foo"), a);
  EXPECT_EQ(ref, 1u);
  EXPECT_EQ(to_sv(trace_data_get_string(&td, ref)), "foo");

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(trace_data_test, begin_end_events_basic) {
  allocator_t a = allocator_get_default();
  trace_data_t td = {};
  trace_event_matcher_t matcher = {};

  // 1. Thread 1 Begin event
  trace_event_t b1 = {};
  b1.name = string_lit("event1");
  b1.cat = string_lit("cat1");
  b1.ph = string_lit("B");
  b1.ts = 100;
  b1.pid = 1;
  b1.tid = 2;
  trace_data_add_event(&td, theme_get_dark(), &b1, &matcher, a);

  ASSERT_EQ(td.events.len, 1u);
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td.events.ptr;
  EXPECT_EQ(events[0].dur, 0);

  // 2. Thread 1 End event
  trace_event_t e1 = {};
  e1.ph = string_lit("E");
  e1.ts = 150;
  e1.pid = 1;
  e1.tid = 2;
  trace_data_add_event(&td, theme_get_dark(), &e1, &matcher, a);

  // End event should not create a new event, but resolve duration of B
  ASSERT_EQ(td.events.len, 1u);
  events = (const trace_event_persisted_t*)td.events.ptr;
  EXPECT_EQ(events[0].dur, 50);

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(trace_data_test, begin_end_events_nested_and_thread_isolated) {
  allocator_t a = allocator_get_default();
  trace_data_t td = {};
  trace_event_matcher_t matcher = {};

  // 1. Thread 1 B1
  trace_event_t b1 = {.name = string_lit("parent"),
                      .ph = string_lit("B"),
                      .ts = 100,
                      .pid = 1,
                      .tid = 1};
  trace_data_add_event(&td, theme_get_dark(), &b1, &matcher, a);

  // 2. Thread 2 B2 (Different thread)
  trace_event_t b2 = {.name = string_lit("other"),
                      .ph = string_lit("B"),
                      .ts = 110,
                      .pid = 1,
                      .tid = 2};
  trace_data_add_event(&td, theme_get_dark(), &b2, &matcher, a);

  // 3. Thread 1 B3 (Nested on thread 1)
  trace_event_t b3 = {.name = string_lit("child"),
                      .ph = string_lit("B"),
                      .ts = 120,
                      .pid = 1,
                      .tid = 1};
  trace_data_add_event(&td, theme_get_dark(), &b3, &matcher, a);

  ASSERT_EQ(td.events.len, 3u);
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td.events.ptr;
  EXPECT_EQ(to_sv(trace_data_get_string(&td, events[0].name_ref)), "parent");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, events[1].name_ref)), "other");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, events[2].name_ref)), "child");

  // 4. Thread 1 End (Should match B3 "child")
  trace_event_t e3 = {.ph = string_lit("E"), .ts = 130, .pid = 1, .tid = 1};
  trace_data_add_event(&td, theme_get_dark(), &e3, &matcher, a);

  events = (const trace_event_persisted_t*)td.events.ptr;
  EXPECT_EQ(events[2].dur, 10);
  EXPECT_EQ(events[0].dur, 0);  // Still 0

  // 5. Thread 2 End (Should match B2 "other")
  trace_event_t e2 = {.ph = string_lit("E"), .ts = 140, .pid = 1, .tid = 2};
  trace_data_add_event(&td, theme_get_dark(), &e2, &matcher, a);

  events = (const trace_event_persisted_t*)td.events.ptr;
  EXPECT_EQ(events[1].dur, 30);

  // 6. Thread 1 End (Should match B1 "parent")
  trace_event_t e1 = {.ph = string_lit("E"), .ts = 150, .pid = 1, .tid = 1};
  trace_data_add_event(&td, theme_get_dark(), &e1, &matcher, a);

  events = (const trace_event_persisted_t*)td.events.ptr;
  EXPECT_EQ(events[0].dur, 50);

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}

TEST(trace_data_test, begin_end_events_args_merging) {
  allocator_t a = allocator_get_default();
  trace_data_t td = {};
  trace_event_matcher_t matcher = {};

  // 1. Begin with arguments
  trace_event_t b = {};
  b.name = string_lit("ev");
  b.ph = string_lit("B");
  b.ts = 100;
  b.pid = 1;
  b.tid = 1;
  trace_arg_t b_args[2];
  b_args[0] = {string_lit("arg1"), string_lit("val1"), 0.0};
  b_args[1] = {string_lit("arg2"), string_lit(""), 42.0};
  b.args = b_args;
  b.args_count = 2;
  trace_data_add_event(&td, theme_get_dark(), &b, &matcher, a);

  // 2. End with arguments (one duplicate, one new)
  trace_event_t e = {};
  e.ph = string_lit("E");
  e.ts = 200;
  e.pid = 1;
  e.tid = 1;
  trace_arg_t e_args[2];
  e_args[0] = {string_lit("arg2"), string_lit(""), 99.0};     // Overrides arg2
  e_args[1] = {string_lit("arg3"), string_lit("val3"), 0.0};  // New arg
  e.args = e_args;
  e.args_count = 2;
  trace_data_add_event(&td, theme_get_dark(), &e, &matcher, a);

  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td.events.ptr;
  const trace_event_persisted_t& p = events[0];
  EXPECT_EQ(p.args_count, 3u);

  // Check the merged args
  const trace_arg_persisted_t* td_args =
      (const trace_arg_persisted_t*)td.args.ptr;
  const trace_arg_persisted_t& arg1 = td_args[p.args_offset];
  EXPECT_EQ(to_sv(trace_data_get_string(&td, arg1.key_ref)), "arg1");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, arg1.val_ref)), "val1");

  const trace_arg_persisted_t& arg2 = td_args[p.args_offset + 1];
  EXPECT_EQ(to_sv(trace_data_get_string(&td, arg2.key_ref)), "arg2");
  EXPECT_EQ(arg2.val_double, 99.0);

  const trace_arg_persisted_t& arg3 = td_args[p.args_offset + 2];
  EXPECT_EQ(to_sv(trace_data_get_string(&td, arg3.key_ref)), "arg3");
  EXPECT_EQ(to_sv(trace_data_get_string(&td, arg3.val_ref)), "val3");

  trace_event_matcher_deinit(&matcher, a);
  trace_data_deinit(&td, a);
}
