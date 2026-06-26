#include "core/json_reader.h"

#include <gtest/gtest.h>

#include <string_view>

#include "src/string.h"

TEST(json_reader_test, basic) {
  const char* json = "{\"key\": [1, 2.5, true, false, null]}";
  json_reader_t r;
  json_reader_init(&r, json, strlen(json));

  json_token_t tok;

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_OBJECT_START);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_STRING);
  EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "key");

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_COLON);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER_I64);
  EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "1");
  EXPECT_EQ(tok.val.i64, 1);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER_F64);
  EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "2.5");
  EXPECT_DOUBLE_EQ(tok.val.f64, 2.5);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_TRUE);
  EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "true");

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_FALSE);
  EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "false");

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_NULL);
  EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "null");

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_END);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_OBJECT_END);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_EOF);
}
TEST(json_reader_test, string_edge_cases) {
  json_token_t tok;

  // 1. Unclosed string
  {
    const char* json = "{\"key\": \"unclosed}";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_OBJECT_START);
    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_STRING);
    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_COLON);
    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ERROR);  // unclosed string
  }

  // 2. Escaped backslash and quote
  {
    const char* json = "[\"hello\\\\world\", \"hello\\\"world\"]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_STRING);
    EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len),
              "hello\\\\world");

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_STRING);
    EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len),
              "hello\\\"world");

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_END);
  }

  // 3. Trailing backslash at EOF
  {
    const char* json = "[\"hello\\";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);
    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ERROR);  // trailing backslash
  }

  // 4. Empty string
  {
    const char* json = "[\"\"]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_STRING);
    EXPECT_EQ(tok.val.str.len, 0u);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_END);
  }
}

TEST(json_reader_test, number_edge_cases) {
  json_token_t tok;

  // 1. Negative float and exponents
  {
    const char* json = "[-123.456, 1e9, 2.5e-4, 3.14e+2]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER_F64);
    EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "-123.456");
    EXPECT_DOUBLE_EQ(tok.val.f64, -123.456);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER_F64);
    EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "1e9");
    EXPECT_DOUBLE_EQ(tok.val.f64, 1e9);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER_F64);
    EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "2.5e-4");
    EXPECT_DOUBLE_EQ(tok.val.f64, 2.5e-4);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_COMMA);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_NUMBER_F64);
    EXPECT_EQ(std::string_view(tok.val.str.ptr, tok.val.str.len), "3.14e+2");
    EXPECT_DOUBLE_EQ(tok.val.f64, 3.14e+2);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_END);
  }

  // 2. Standalone minus sign
  {
    const char* json = "[-]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ERROR);  // malformed number
  }
}

TEST(json_reader_test, literal_edge_cases) {
  json_token_t tok;

  // 1. Malformed literals
  const char* malformed[] = {"[trud]", "[falsy]"};
  for (const char* json : malformed) {
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ERROR);
  }

  // "nulle" is parsed as "null" followed by "e]" (which causes an error on the
  // second token)
  {
    const char* json = "[nulle]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_NULL);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ERROR);  // error on "e]"
  }

  // 2. Unexpected characters
  {
    const char* json = "[@]";
    json_reader_t r;
    json_reader_init(&r, json, strlen(json));

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

    json_reader_next(&r, &tok);
    EXPECT_EQ(tok.type, JSON_TOKEN_ERROR);  // @ is invalid
  }
}

TEST(json_reader_test, whitespace) {
  const char* json = "  \n  \t  \r  [  \n  \t  \r  ]  \n  \t  \r  ";
  json_reader_t r;
  json_reader_init(&r, json, strlen(json));
  json_token_t tok;

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_START);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_ARRAY_END);

  json_reader_next(&r, &tok);
  EXPECT_EQ(tok.type, JSON_TOKEN_EOF);
}
