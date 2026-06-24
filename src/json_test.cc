#include "src/json.h"

#include <gtest/gtest.h>

#include <string_view>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

TEST(json_test, reader_basic) {
  const char* json = "{\"key\": [1, 2.5, true, false, null]}";
  json_reader_t r;
  json_reader_init(&r, json, strlen(json));

  json_token_t tok;

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_OBJECT_START);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_STRING);
  EXPECT_EQ(std::string_view(tok.val.ptr, tok.val.len), "key");

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_COLON);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER);
  EXPECT_EQ(std::string_view(tok.val.ptr, tok.val.len), "1");

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER);
  EXPECT_EQ(std::string_view(tok.val.ptr, tok.val.len), "2.5");

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_TRUE);
  EXPECT_EQ(std::string_view(tok.val.ptr, tok.val.len), "true");

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_FALSE);
  EXPECT_EQ(std::string_view(tok.val.ptr, tok.val.len), "false");

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_NULL);
  EXPECT_EQ(std::string_view(tok.val.ptr, tok.val.len), "null");

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_END);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_OBJECT_END);

  tok = json_reader_next(&r);
  EXPECT_EQ(tok.type, JSON_TOKEN_EOF);
}

TEST(json_test, writer_basic) {
  allocator_t a = allocator_get_default();
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

TEST(json_test, writer_escaping) {
  allocator_t a = allocator_get_default();
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

TEST(json_test, writer_nested) {
  allocator_t a = allocator_get_default();
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

TEST(json_test, reader_string_edge_cases) {
  // 1. Unclosed string
  {
    const char* json = "{\"key\": \"unclosed}";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_OBJECT_START);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_STRING);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_COLON);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ERROR);  // unclosed string
  }

  // 2. Escaped backslash and quote
  {
    const char* json = "[\"hello\\\\world\", \"hello\\\"world\"]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);

    json_token_t t1 = json_reader_next(&r);
    EXPECT_EQ(t1.type, JSON_TOKEN_STRING);
    EXPECT_EQ(std::string_view(t1.val.ptr, t1.val.len), "hello\\\\world");

    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_COMMA);

    json_token_t t2 = json_reader_next(&r);
    EXPECT_EQ(t2.type, JSON_TOKEN_STRING);
    EXPECT_EQ(std::string_view(t2.val.ptr, t2.val.len), "hello\\\"world");

    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_END);
  }

  // 3. Trailing backslash at EOF
  {
    const char* json = "[\"hello\\";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
    EXPECT_EQ(json_reader_next(&r).type,
              JSON_TOKEN_ERROR);  // trailing backslash
  }

  // 4. Empty string
  {
    const char* json = "[\"\"]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
    json_token_t t = json_reader_next(&r);
    EXPECT_EQ(t.type, JSON_TOKEN_STRING);
    EXPECT_EQ(t.val.len, 0u);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_END);
  }
}

TEST(json_test, reader_number_edge_cases) {
  // 1. Negative float and exponents
  {
    const char* json = "[-123.456, 1e9, 2.5e-4, 3.14e+2]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);

    json_token_t t1 = json_reader_next(&r);
    EXPECT_EQ(t1.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(std::string_view(t1.val.ptr, t1.val.len), "-123.456");
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_COMMA);

    json_token_t t2 = json_reader_next(&r);
    EXPECT_EQ(t2.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(std::string_view(t2.val.ptr, t2.val.len), "1e9");
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_COMMA);

    json_token_t t3 = json_reader_next(&r);
    EXPECT_EQ(t3.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(std::string_view(t3.val.ptr, t3.val.len), "2.5e-4");
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_COMMA);

    json_token_t t4 = json_reader_next(&r);
    EXPECT_EQ(t4.type, JSON_TOKEN_NUMBER);
    EXPECT_EQ(std::string_view(t4.val.ptr, t4.val.len), "3.14e+2");

    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_END);
  }

  // 2. Standalone minus sign
  {
    const char* json = "[-]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ERROR);  // malformed number
  }
}

TEST(json_test, reader_literal_edge_cases) {
  // 1. Malformed literals
  const char* malformed[] = {"[trud]", "[falsy]"};
  for (const char* json : malformed) {
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ERROR);
  }

  // "nulle" is parsed as "null" followed by "e]" (which causes an error on the
  // second token)
  {
    const char* json = "[nulle]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_NULL);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ERROR);  // error on "e]"
  }

  // 2. Unexpected characters
  {
    const char* json = "[@]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
    EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ERROR);  // @ is invalid
  }
}

TEST(json_test, reader_whitespace) {
  const char* json = "  \n  \t  \r  [  \n  \t  \r  ]  \n  \t  \r  ";
  json_reader_t r;
  json_reader_init(&r, json, strlen(json));
  EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_START);
  EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_ARRAY_END);
  EXPECT_EQ(json_reader_next(&r).type, JSON_TOKEN_EOF);
}

TEST(json_test, writer_escaping_edge_cases) {
  allocator_t a = allocator_get_default();
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

TEST(json_test, writer_double_and_bool) {
  allocator_t a = allocator_get_default();
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

TEST(json_test, writer_depth_clamping) {
  allocator_t a = allocator_get_default();
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

TEST(json_test, writer_indentation) {
  allocator_t a = allocator_get_default();
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
