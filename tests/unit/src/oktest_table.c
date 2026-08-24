#include <unity.h>

#include <oktable.h>

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
  const uint32_t old_count = table.count;
  const uint32_t expected_count_after_add = old_count + 1;
  const uint32_t expected_count_after_remove = old_count;
  const uint32_t key = 67;
  const uint32_t value = 69;
  TEST_ASSERT_TRUE(test_table_set(&table, key, value));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_add, table.count);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(table.buckets.count, table.buckets.capacity);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(table.count, table.buckets.count);

  const uint32_t* retrived = test_table_get(&table, key);
  TEST_ASSERT_NOT_NULL(retrived);
  TEST_ASSERT_EQUAL_UINT32(value, *retrived);

  TEST_ASSERT_TRUE(test_table_remove(&table, key));
  TEST_ASSERT_NULL(test_table_get(&table, key));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, table.count);
}

void test_set_set_retrive_remove() {
  const uint32_t old_count = table.count;
  const uint32_t expected_count_after_add = old_count + 1;
  const uint32_t expected_count_after_remove = old_count;

  TEST_ASSERT_TRUE(test_table_set(&table, 67, 69));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_add, table.count);
  {
    const uint32_t* retrived = test_table_get(&table, 67);
    TEST_ASSERT_NOT_NULL(retrived);
    TEST_ASSERT_EQUAL_UINT32(69, *retrived);
  }
  TEST_ASSERT_TRUE(test_table_set(&table, 67, 101));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_add, table.count);
  const uint32_t* retrived = test_table_get(&table, 67);
  TEST_ASSERT_NOT_NULL(retrived);
  TEST_ASSERT_EQUAL_UINT32(101, *retrived);

  TEST_ASSERT_TRUE(test_table_remove(&table, 67));
  TEST_ASSERT_NULL(test_table_get(&table, 67));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, table.count);
}

void test_set_a_set_b_remove_a_retrive_b_clear() {
  const uint32_t old_count = table.count;
  const uint32_t expected_count_after_a_add = old_count + 1;
  const uint32_t expected_count_after_b_add = expected_count_after_a_add + 1;
  const uint32_t expected_count_after_a_remove = expected_count_after_a_add;
  const uint32_t expected_count_after_all_remove = old_count;

  TEST_ASSERT_TRUE(test_table_set(&table, 'a', 'a'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_a_add, table.count);
  TEST_ASSERT_TRUE(test_table_set(&table, 'b', 'b'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_b_add, table.count);
  {
    const uint32_t* a = test_table_get(&table, 'a');
    const uint32_t* b = test_table_get(&table, 'b');
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32('a', *a);
    TEST_ASSERT_EQUAL_UINT32('b', *b);
  }
  TEST_ASSERT_TRUE(test_table_remove(&table, 'a'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_a_remove, table.count);
  const uint32_t* b = test_table_get(&table, 'b');
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_UINT32('b', *b);
  TEST_ASSERT_NULL(test_table_get(&table, 'a'));
  TEST_ASSERT_TRUE(test_table_remove(&table, 'b'));
  TEST_ASSERT_NULL(test_table_get(&table, 'b'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_all_remove, table.count);
}

void test_set_1000_remove_1000() {
  const uint32_t old_count = table.count;
  const uint32_t expected_count_after_add = old_count + 1000;
  const uint32_t expected_count_after_remove = old_count;
  for (uint32_t i = 0; i < 1000; ++i) {
    const uint32_t c = i + 1;
    TEST_ASSERT_TRUE(test_table_set(&table, i, c));
    TEST_ASSERT_EQUAL_UINT32(old_count + c, table.count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(table.buckets.count, table.buckets.capacity);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(table.count, table.buckets.count);
  }
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_add, table.count);

  for (uint32_t i = 0; i < 1000; ++i) {
    const uint32_t c = i + 1;
    const uint32_t* retrived = test_table_get(&table, i);
    TEST_ASSERT_NOT_NULL(retrived);
    TEST_ASSERT_EQUAL_UINT32(c, *retrived);
  }

  for (uint32_t i = 0; i < 1000; ++i) {
    const uint32_t c = i + 1;
    TEST_ASSERT_TRUE(test_table_remove(&table, i));
    TEST_ASSERT_NULL(test_table_get(&table, i));
  }
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, table.count);
}

#define GET_COLLISION_HASH(key) (67)

TABLE_DECLARE_DEFAULT(test_table_collision, uint32_t, uint32_t, uint32_t)
TABLE_DEFINE_DEFAULT(test_table_collision,
                     uint32_t,
                     uint32_t,
                     uint32_t,
                     ARE_U_KEY_EQUAL,
                     IS_U_KEY_NULL,
                     ARRAY_DEFAULT_TYPE_DEINIT,
                     ARRAY_DEFAULT_TYPE_DEINIT,
                     GET_COLLISION_HASH)

void test_set_a_set_b_retrive_a_retrive_b_clear_collision() {
  const uint32_t old_count = table.count;
  const uint32_t expected_count_after_a_add = old_count + 1;
  const uint32_t expected_count_after_b_add = expected_count_after_a_add + 1;
  const uint32_t expected_count_after_a_remove = expected_count_after_a_add;
  const uint32_t expected_count_after_all_remove = old_count;
  const uint32_t expected_bucket_count = 1;

  TEST_ASSERT_TRUE(test_table_set(&table, 'a', 'a'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_a_add, table.count);
  TEST_ASSERT_TRUE(test_table_set(&table, 'b', 'b'));
  // TEST_ASSERT_TRUE(test_table_set(&table, 'm', 'b'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_b_add, table.count);
  // TEST_ASSERT_EQUAL_UINT32(expected_bucket_count, table.buckets.count);
  {
    const uint32_t* a = test_table_get(&table, 'a');
    const uint32_t* b = test_table_get(&table, 'b');
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT32('a', *a);
    TEST_ASSERT_EQUAL_UINT32('b', *b);
  }
  TEST_ASSERT_TRUE(test_table_remove(&table, 'a'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_a_remove, table.count);
  TEST_ASSERT_TRUE(test_table_remove(&table, 'b'));
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_all_remove, table.count);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_state);
  RUN_TEST(test_set_1_remove_1);
  RUN_TEST(test_set_set_retrive_remove);
  RUN_TEST(test_set_a_set_b_remove_a_retrive_b_clear);
  RUN_TEST(test_set_1000_remove_1000);
  RUN_TEST(test_set_a_set_b_retrive_a_retrive_b_clear_collision);
  return UNITY_END();
}
