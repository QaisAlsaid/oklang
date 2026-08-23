#include <table.h>
#include <unity.h>

// set A
// get A
// remove A
// get A
//
// set A
// set A again
// get A
//
// set A
// set B
// remove A
// get B
//
// 1000 unique keys
// 1000 keys with identical hashes
// rehash
// remove everything

#define ARE_U_KEY_EQUAL(lhs, rhs) (lhs == rhs)
#define IS_U_KEY_NULL(key) (key == UINT32_MAX)
#define GET_U_HASH(key) (key)

TABLE_DECLARE_DEFAULT(test_table, uint32_t, uint32_t, uint32_t)
TABLE_DEFINE_DEFAULT(test_table,
                     uint32_t,
                     uint32_t,
                     uint32_t,
                     ARE_U_KEY_EQUAL,
                     IS_U_KEY_NULL,
                     ARRAY_DEFAULT_TYPE_DEINIT,
                     ARRAY_DEFAULT_TYPE_DEINIT,
                     GET_U_HASH)

test_table table;

void setUp() {
  test_table_init(&table);
}

void tearDown() {
  test_table_deinit(&table);
}

void test_init_state() {
  TEST_ASSERT_EQUAL_UINT32(0, table.count);
  TEST_ASSERT_EQUAL_UINT32(0, table.buckets.count);
  TEST_ASSERT_EQUAL_UINT32(0, table.buckets.capacity);
  TEST_ASSERT_NULL(table.buckets.data);
}

void test_set_1_remove_1() {
  const uint32_t key = 67;
  const uint32_t value = 69;
  TEST_ASSERT_TRUE(test_table_set(&table, key, value));
  TEST_ASSERT_EQUAL_UINT32(1, table.count);
  TEST_ASSERT_EQUAL_UINT32(1, table.buckets.count);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(table.buckets.count, table.buckets.capacity);

  const uint32_t* retrived = test_table_get(&table, key);
  TEST_ASSERT_NOT_NULL(retrived);
  TEST_ASSERT_EQUAL_UINT32(value, *retrived);

  TEST_ASSERT_TRUE(test_table_remove(&table, key));
  TEST_ASSERT_NULL(test_table_get(&table, key));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_state);
  RUN_TEST(test_set_1_remove_1);
  return UNITY_END();
}
