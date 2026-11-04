#include <unity.h>

// the P2 utf-8 core, exercised directly: decode's check table (the exact
// rejection classes the roadmap ruled), validate's front-door rules,
// count_lossy's replacement discipline, and encode's round trip.

#include <okutf8.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

void test_decode_ascii() {
  uint8_t len = 0;
  TEST_ASSERT_EQUAL_UINT32('a', ok_utf8_decode("a", 1, &len));
  TEST_ASSERT_EQUAL_UINT8(1, len);
}

void test_decode_multibyte() {
  uint8_t len = 0;
  // é = C3 A9
  TEST_ASSERT_EQUAL_UINT32(0xE9u, ok_utf8_decode("\xC3\xA9", 2, &len));
  TEST_ASSERT_EQUAL_UINT8(2, len);
  // α = CE B1
  TEST_ASSERT_EQUAL_UINT32(0x3B1u, ok_utf8_decode("\xCE\xB1", 2, &len));
  TEST_ASSERT_EQUAL_UINT8(2, len);
  // U+1F600 = F0 9F 98 80
  TEST_ASSERT_EQUAL_UINT32(0x1F600u, ok_utf8_decode("\xF0\x9F\x98\x80", 4, &len));
  TEST_ASSERT_EQUAL_UINT8(4, len);
}

void test_decode_rejects_stray_continuation() {
  uint8_t len = 99;
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\x80", 1, &len));
  TEST_ASSERT_EQUAL_UINT8(1, len); // lossy advance: one byte
}

void test_decode_rejects_overlong() {
  uint8_t len = 0;
  // C0 80: two-byte encoding of NUL
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xC0\x80", 2, &len));
  // C1 BF: overlong two-byte
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xC1\xBF", 2, &len));
  // E0 9F 80: overlong three-byte
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xE0\x9F\x80", 3, &len));
  // F0 8F BF BF: overlong four-byte
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xF0\x8F\xBF\xBF", 4, &len));
}

void test_decode_rejects_surrogates() {
  uint8_t len = 0;
  // ED A0 80 = U+D800, a surrogate
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xED\xA0\x80", 3, &len));
  // ED BF BF = U+DFFF, the other edge
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xED\xBF\xBF", 3, &len));
  // but ED 9F BF = U+D7FF is legal
  TEST_ASSERT_EQUAL_UINT32(0xD7FFu, ok_utf8_decode("\xED\x9F\xBF", 3, &len));
}

void test_decode_rejects_past_range() {
  uint8_t len = 0;
  // F4 90 80 80 = U+110000, one past the ceiling
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xF4\x90\x80\x80", 4, &len));
  // F5..FF are not lead bytes at all
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xF5\x80\x80\x80", 4, &len));
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xFF", 1, &len));
  // F4 8F BF BF = U+10FFFF, the last legal codepoint
  TEST_ASSERT_EQUAL_UINT32(0x10FFFFu, ok_utf8_decode("\xF4\x8F\xBF\xBF", 4, &len));
}

void test_decode_rejects_bad_continuation() {
  uint8_t len = 0;
  // C3 41: a two-byte lead followed by 'A'
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xC3\x41", 2, &len));
  // truncated at the region end
  TEST_ASSERT_EQUAL_UINT32(0, ok_utf8_decode("\xC3", 1, &len));
}

void test_validate_accepts_plain_source() {
  uint32_t off = 999;
  ok_utf8_rejection why = OK_UTF8_FINE;
  TEST_ASSERT_TRUE(ok_utf8_validate("let x = 1;\n", 11, &off, &why));
  TEST_ASSERT_EQUAL_UINT32(OK_UTF8_FINE, why);
}

void test_validate_accepts_tab_newline_return() {
  uint32_t off = 999;
  ok_utf8_rejection why = OK_UTF8_FINE;
  TEST_ASSERT_TRUE(ok_utf8_validate("a\tb\nc\rd", 6, &off, &why));
}

void test_validate_rejects_other_c0() {
  uint32_t off = 999;
  ok_utf8_rejection why = OK_UTF8_FINE;
  TEST_ASSERT_FALSE(ok_utf8_validate("a\x01b", 3, &off, &why));
  TEST_ASSERT_EQUAL_UINT32(OK_UTF8_CONTROL, why);
  TEST_ASSERT_EQUAL_UINT32(1, off);
}

void test_validate_rejects_bidi() {
  uint32_t off = 999;
  ok_utf8_rejection why = OK_UTF8_FINE;
  // U+202E (RIGHT-TO-LEFT OVERRIDE) = E2 80 AE - the trojan-source trick
  TEST_ASSERT_FALSE(ok_utf8_validate("a" "\xE2\x80\xAE" "b", 5, &off, &why));
  TEST_ASSERT_EQUAL_UINT32(OK_UTF8_BIDI, why);
  TEST_ASSERT_EQUAL_UINT32(1, off);
  // U+2066 (LRI) = E2 81 A6
  TEST_ASSERT_FALSE(ok_utf8_validate("x" "\xE2\x81\xA6" "y", 5, &off, &why));
  TEST_ASSERT_EQUAL_UINT32(OK_UTF8_BIDI, why);
}

void test_validate_rejects_malformed_with_offset() {
  uint32_t off = 999;
  ok_utf8_rejection why = OK_UTF8_FINE;
  TEST_ASSERT_FALSE(ok_utf8_validate("print \"ok\";\n\xFF", 13, &off, &why));
  TEST_ASSERT_EQUAL_UINT32(OK_UTF8_MALFORMED, why);
  TEST_ASSERT_EQUAL_UINT32(12, off);
}

void test_validate_rejects_nul_for_length_carrying_callers() {
  uint32_t off = 999;
  ok_utf8_rejection why = OK_UTF8_FINE;
  TEST_ASSERT_FALSE(ok_utf8_validate("a\0b", 3, &off, &why));
  TEST_ASSERT_EQUAL_UINT32(OK_UTF8_NUL, why);
  TEST_ASSERT_EQUAL_UINT32(1, off);
}

void test_count_lossy_counts_replacements() {
  // 2 codepoints + 2 malformed bytes = 4, one U+FFFD per bad byte
  TEST_ASSERT_TRUE(ok_utf8_count_lossy("a" "\xFF\xFE" "b", 4) == 4);
  TEST_ASSERT_TRUE(ok_utf8_count_lossy("", 0) == 0);
  TEST_ASSERT_TRUE(ok_utf8_count_lossy("h\xC3\xA9llo", 6) == 5);
  TEST_ASSERT_TRUE(ok_utf8_count_lossy("\xF0\x9F\x98\x80", 4) == 1);
}

void test_encode_round_trips() {
  char buf[4];
  TEST_ASSERT_EQUAL_UINT8(1, ok_utf8_encode('A', buf));
  {
    uint8_t len = 0;
    TEST_ASSERT_EQUAL_UINT32('A', ok_utf8_decode(buf, 1, &len));
  }
  TEST_ASSERT_EQUAL_UINT8(2, ok_utf8_encode(0xE9u, buf));
  TEST_ASSERT_EQUAL_UINT8(3, ok_utf8_encode(0x202Fu, buf));
  TEST_ASSERT_EQUAL_UINT8(4, ok_utf8_encode(0x1F600u, buf));
  // the full decode(encode(cp)) round trip
  uint8_t len = 0;
  TEST_ASSERT_EQUAL_UINT32(0x1F600u, ok_utf8_decode(buf, 4, &len));
  TEST_ASSERT_EQUAL_UINT8(4, len);
}

void test_encode_rejects_bad_codepoints() {
  char buf[4];
  TEST_ASSERT_EQUAL_UINT8(0, ok_utf8_encode(0x110000u, buf));
  TEST_ASSERT_EQUAL_UINT8(0, ok_utf8_encode(0xD800u, buf));
  TEST_ASSERT_EQUAL_UINT8(0, ok_utf8_encode(0xDFFFu, buf));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decode_ascii);
  RUN_TEST(test_decode_multibyte);
  RUN_TEST(test_decode_rejects_stray_continuation);
  RUN_TEST(test_decode_rejects_overlong);
  RUN_TEST(test_decode_rejects_surrogates);
  RUN_TEST(test_decode_rejects_past_range);
  RUN_TEST(test_decode_rejects_bad_continuation);
  RUN_TEST(test_validate_accepts_plain_source);
  RUN_TEST(test_validate_accepts_tab_newline_return);
  RUN_TEST(test_validate_rejects_other_c0);
  RUN_TEST(test_validate_rejects_bidi);
  RUN_TEST(test_validate_rejects_malformed_with_offset);
  RUN_TEST(test_validate_rejects_nul_for_length_carrying_callers);
  RUN_TEST(test_count_lossy_counts_replacements);
  RUN_TEST(test_encode_round_trips);
  RUN_TEST(test_encode_rejects_bad_codepoints);
  return UNITY_END();
}
