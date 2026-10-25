#include <unity/unity.h>

#include <okchunk.h>

void setUp() {
}

void tearDown() {
}

void test_encode_decode_oppabc() {
  {
    word w = encode_oppabc(OP_ADD, false, 0x53, 0x59, 0x52);
    TEST_ASSERT_EQUAL_UINT8(OP_ADD, op_of(w));
    TEST_ASSERT_FALSE(p_of(w));
    TEST_ASSERT_EQUAL_UINT8(0x53, a_of(w));
    TEST_ASSERT_EQUAL_UINT8(0x59, b_of(w));
    TEST_ASSERT_EQUAL_UINT8(0x52, c_of(w));
  }
  {
    word w = encode_oppabc(OP_SUBTRACT, true, 0xff, 0xff, 0xff);
    TEST_ASSERT_EQUAL_UINT8(OP_SUBTRACT, op_of(w));
    TEST_ASSERT_TRUE(p_of(w));
    TEST_ASSERT_EQUAL_UINT8(0xff, a_of(w));
    TEST_ASSERT_EQUAL_UINT8(0xff, b_of(w));
    TEST_ASSERT_EQUAL_UINT8(0xff, c_of(w));
  }
}

void test_encode_decode_oppabprime() {
  {
    word w = encode_oppabprime(OP_MOVE, false, UINT16_C(0x0484), UINT16_C(0x0f54));
    TEST_ASSERT_EQUAL_UINT8(OP_MOVE, op_of(w));
    TEST_ASSERT_FALSE(p_of(w));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0x0484), aprime_of(w));
    TEST_ASSERT_EQUAL_UINT8(UINT16_C(0x0f54), bprime_of(w));
  }
  {
    word w = encode_oppabc(OP_MOVE, true, UINT16_C(0x0fff), UINT16_C(0x0fff));
    TEST_ASSERT_EQUAL_UINT8(OP_MOVE, op_of(w));
    TEST_ASSERT_TRUE(p_of(w));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0x0fff), aprime_of(w));
    TEST_ASSERT_EQUAL_UINT8(UINT16_C(0x0fff), bprime_of(w));
  }
}

void test_encode_decode_oppA() {
  {
    word w = encode_oppA(OP_LOOP, false, UINT16_C(0x4f4b));
    TEST_ASSERT_EQUAL_UINT8(OP_LOOP, op_of(w));
    TEST_ASSERT_FALSE(p_of(w));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0x4f4b), a_of(w));
  }
  {
    word w = encode_oppA(OP_LOOP, true, UINT16_C(0xffff));
    TEST_ASSERT_EQUAL_UINT8(OP_LOOP, op_of(w));
    TEST_ASSERT_TRUE(p_of(w));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0xffff), a_of(w));
  }
}

void test_encode_decode_OPPABC() {
  {
    word2 w2 = encode_OPPABC(OP_ADD_LONG, false, UINT16_C(0x4643), UINT16_C(0x4341), UINT16_C(0x4d50));
    TEST_ASSERT_EQUAL_UINT8(OP_ADD_LONG, OP_of(w2));
    TEST_ASSERT_FALSE(P_of(w2));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0x4643), A_of(w2));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0x4341), B_of(w2));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0x4d50), C_of(w2));
  }
  {
    word2 w2 = encode_OPPABC(OP_SUBTRACT_LONG, true, UINT16_C(0xffff), UINT16_C(0xffff), UINT16_C(0xffff));
    TEST_ASSERT_EQUAL_UINT8(OP_SUBTRACT_LONG, OP_of(w2));
    TEST_ASSERT_TRUE(P_of(w2));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0xffff), A_of(w2));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0xffff), B_of(w2));
    TEST_ASSERT_EQUAL_UINT16(UINT16_C(0xffff), C_of(w2));
  }
}

void test_chunk_write() {
  {
    chunk chunk;
    chunk_init(&chunk, alloc);
    ok_line_info info;
    info.line = 1971;
    info.offset = 14;

    word w = 0xf09f90aa;
    TEST_ASSERT_TRUE(chunk_write_word(&chunk, w, info));
    TEST_ASSERT_EQUAL_UINT32(1, chunk.code.count);
    TEST_ASSERT_EQUAL_UINT32(w, chunk.code.data[0]);
  }
  {
    chunk chunk;
    chunk_init(&chunk, alloc);

    ok_line_info info;
    info.line = 1978;
    info.offset = 9;

    word w1 = 0xf09f8c88;
    word w2 = 0xf09f949a;
    chunk_write_2words(&chunk, w1, w2, info);

    TEST_ASSERT_EQUAL_UINT32(2, chunk.code.count);
    TEST_ASSERT_EQUAL_UINT32(w1, chunk.code.data[0]);
    TEST_ASSERT_EQUAL_UINT32(w2, chunk.code.data[1]);
  }
  {
    chunk chunk;
    chunk_init(&chunk, alloc);

    ok_line_info info;
    info.line = 1974;
    info.offset = 5;

    word2 w = 0x537461726c657373;
    chunk_write_word2(&chunk, w, info);

    TEST_ASSERT_EQUAL_UINT32(2, chunk.code.count);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(w >> 32), chunk.code.data[0]);

    TEST_ASSERT_EQUAL_UINT32((uint32_t)w, chunk.code.data[1]);
    ok_line_info_repeated* _1 = chunk_find_line_info(&chunk, 0);
    ok_line_info_repeated* _2 = chunk_find_line_info(&chunk, 1);

    TEST_ASSERT_NOT_NULL(_1);
    TEST_ASSERT_NOT_NULL(_2);
    TEST_ASSERT_EQUAL_UINT32(_1->reps, _2->reps);
    TEST_ASSERT_EQUAL_UINT32(_1->line_info.line, _2->line_info.line);
    TEST_ASSERT_EQUAL_UINT32(_1->line_info.offset, _2->line_info.offset);
    TEST_ASSERT_EQUAL_UINT32(_1->reps, 2);
    TEST_ASSERT_EQUAL_UINT32(_1->line_info.line, info.line);
    TEST_ASSERT_EQUAL_UINT32(_1->line_info.offset, info.offset);
    TEST_ASSERT_EQUAL_UINT32()
  }
}

void test_line_info_repeate() {
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_encode_decode_oppabc);
  RUN_TEST(test_encode_decode_oppabprime);
  RUN_TEST(test_encode_decode_oppA);
  RUN_TEST(test_encode_decode_OPPABC);
  RUN_TEST(test_chunk_write);
  UNITY_END();
}
