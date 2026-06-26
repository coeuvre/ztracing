#include "core/json_writer.h"

#include <gtest/gtest.h>

#include <string_view>

#include "core/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

TEST(json_writer_test, basic) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, false, &buf, a);

  // {"a":1,"b":true,"c":"hello"}
  json_writer_begin_object(&w);
  json_writer_name(&w, string_view_from_cstr("a"));
  json_writer_number_int(&w, 1);
  json_writer_name(&w, string_view_from_cstr("b"));
  json_writer_bool(&w, true);
  json_writer_name(&w, string_view_from_cstr("c"));
  json_writer_string(&w, string_view_from_cstr("hello"));
  json_writer_end_object(&w);

  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  EXPECT_EQ(res, "{\"a\":1,\"b\":true,\"c\":\"hello\"}");

  array_list_deinit(&buf, a);
}

TEST(json_writer_test, escaping) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, false, &buf, a);

  // {"text":"line1\nline2\ttab\"quote"}
  json_writer_begin_object(&w);
  json_writer_name(&w, string_view_from_cstr("text"));
  json_writer_string(&w, string_view_from_cstr("line1\nline2\ttab\"quote"));
  json_writer_end_object(&w);

  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  EXPECT_EQ(res, "{\"text\":\"line1\\nline2\\ttab\\\"quote\"}");

  array_list_deinit(&buf, a);
}

TEST(json_writer_test, nested) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, false, &buf, a);

  // {"array":[1,{"nested":true},null]}
  json_writer_begin_object(&w);
  json_writer_name(&w, string_view_from_cstr("array"));
  json_writer_begin_array(&w);
  json_writer_number_int(&w, 1);
  json_writer_begin_object(&w);
  json_writer_name(&w, string_view_from_cstr("nested"));
  json_writer_bool(&w, true);
  json_writer_end_object(&w);
  json_writer_null(&w);
  json_writer_end_array(&w);
  json_writer_end_object(&w);

  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  EXPECT_EQ(res, "{\"array\":[1,{\"nested\":true},null]}");

  array_list_deinit(&buf, a);
}

TEST(json_writer_test, escaping_edge_cases) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, false, &buf, a);

  // String containing all 2-character escapes and control chars
  // \b \f \r \\ and control chars \x01, \x1f
  const char text[] = "back\bform\ffeed\rslash\\\\control\x01\x1f";
  json_writer_begin_array(&w);
  json_writer_string(&w, string_view_from_parts(text, sizeof(text) - 1));
  json_writer_end_array(&w);

  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  EXPECT_EQ(res,
            "[\"back\\bform\\ffeed\\rslash\\\\\\\\control\\u0001\\u001f\"]");

  array_list_deinit(&buf, a);
}

TEST(json_writer_test, double_and_bool) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, false, &buf, a);

  json_writer_begin_array(&w);
  json_writer_number_double(&w, 3.14159);
  json_writer_number_double(&w, -0.000123);
  json_writer_bool(&w, false);
  json_writer_end_array(&w);

  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  EXPECT_EQ(res, "[3.14159,-0.000123,false]");

  array_list_deinit(&buf, a);
}

TEST(json_writer_test, depth_clamping) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, false, &buf, a);

  // Nest objects 35 levels deep. The depth limit is 32.
  // After level 32, begin_object should clamp and not advance depth.
  for (int i = 0; i < 35; i++) {
    json_writer_begin_object(&w);
  }
  for (int i = 0; i < 35; i++) {
    json_writer_end_object(&w);
  }

  // Sane state check: we should be able to write a flat value afterwards.
  json_writer_null(&w);

  // Let's verify that we wrote 32 '{', then 3 sibling '{' separated by commas,
  // 35 '}', and then a 'null'.
  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  std::string expected;
  for (int i = 0; i < 32; i++) expected += "{";
  expected += ",{,{,{";
  for (int i = 0; i < 35; i++) expected += "}";
  expected += "null";

  EXPECT_EQ(res, expected);

  array_list_deinit(&buf, a);
}

TEST(json_writer_test, indentation) {
  allocator_t* a = c_allocator();
  array_list_t buf = {};
  json_writer_t w;
  json_writer_init(&w, true, &buf, a);

  // Write:
  // {
  //   "a": 1,
  //   "b": [
  //     2,
  //     true
  //   ]
  // }
  json_writer_begin_object(&w);
  json_writer_name(&w, string_view_from_cstr("a"));
  json_writer_number_int(&w, 1);
  json_writer_name(&w, string_view_from_cstr("b"));
  json_writer_begin_array(&w);
  json_writer_number_int(&w, 2);
  json_writer_bool(&w, true);
  json_writer_end_array(&w);
  json_writer_end_object(&w);

  std::string_view res(reinterpret_cast<const char*>(buf.ptr), buf.len);
  const char* expected =
      "{\n"
      "  \"a\": 1,\n"
      "  \"b\": [\n"
      "    2,\n"
      "    true\n"
      "  ]\n"
      "}";
  EXPECT_EQ(res, expected);

  array_list_deinit(&buf, a);
}
