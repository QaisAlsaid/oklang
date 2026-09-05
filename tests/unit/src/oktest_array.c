#include <unity.h>

// TODO: grow overflow.
// injected allocation failure (when we implement custom allocators).

#include <ok/ok_specs.h>
#include <okarray.h>
#include <okspecs.h>

ARRAY_DECLARE(test_array, uint32_t, uint32_t)
ARRAY_DEFINE(test_array, uint32_t, uint32_t, UINT32_MAX, ARRAY_DEFAULT_TYPE_DEINIT)

test_array array;
ok_specs specs = {0};
allocators alloc = {0};

void setUp() {
  patch_specs(&specs);
  alloc = create_raw_allocators_wrapper(&specs.allocators);
  test_array_init(&array, &alloc);
}

void tearDown() {
  test_array_deinit(&array, &alloc);
}

static void test_init_state() {
  TEST_ASSERT_NULL(array.data);
  TEST_ASSERT_EQUAL_UINT32(0, array.count);
  TEST_ASSERT_EQUAL_UINT32(0, array.capacity);
}

static void test_append_1_remove_1() {
  const uint32_t old_count = array.count;
  const uint32_t expected_count_after_append = old_count + 1;
  const uint32_t expected_count_after_remove = old_count;
  TEST_ASSERT_TRUE(test_array_append(&array, 69));
  TEST_ASSERT_NOT_NULL(array.data);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(array.count, array.capacity);
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_append, array.count);
  TEST_ASSERT_EQUAL_UINT32(69, array.data[array.count - 1]);
  TEST_ASSERT_TRUE(test_array_remove(&array, array.count - 1, array.count - 1));
  TEST_ASSERT_NOT_NULL(array.data);
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, array.count);
}

static void test_append_100_remove_100() {
  const uint32_t old_count = array.count;
  const uint32_t expected_count_after_append = old_count + 100;
  const uint32_t expected_count_after_remove = old_count;

  for (uint32_t i = 0; i < 100; ++i) {
    const uint32_t c = i + 1;
    test_array_append(&array, c);
    TEST_ASSERT_NOT_NULL(array.data);
    TEST_ASSERT_EQUAL_UINT32(old_count + c, array.count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(array.count, array.capacity);
  }
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_append, array.count);

  for (uint32_t i = 0; i < 100; ++i) {
    const uint32_t c = i + 1;
    TEST_ASSERT_TRUE(test_array_remove(&array, old_count, old_count));
    TEST_ASSERT_NOT_NULL(array.data);
    TEST_ASSERT_EQUAL_UINT32(expected_count_after_append - c, array.count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(expected_count_after_append - c, array.capacity);
  }
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, array.count);
}

static void test_append_n_1_remove_1() {
  const uint32_t old_count = array.count;
  const uint32_t expected_count_after_append = old_count + 1;
  const uint32_t expected_count_after_remove = old_count;
  const uint32_t elem = 69;
  TEST_ASSERT_TRUE(test_array_append_n(&array, &elem, 1));
  TEST_ASSERT_NOT_NULL(array.data);
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_append, array.count);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(array.count, array.capacity);
  TEST_ASSERT_TRUE(test_array_remove(&array, array.count - 1, array.count - 1));
  TEST_ASSERT_NOT_NULL(array.data);
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, array.count);
}

static void test_append_n_1000_remove_1000() {
  const uint32_t old_count = array.count;
  const uint32_t expected_count_after_append = old_count + 1000;
  const uint32_t expected_count_after_remove = old_count;
  uint32_t elements[1000];
  for (uint32_t i = 0; i < 1000; ++i) {
    elements[i] = i + 1;
  }
  TEST_ASSERT_TRUE(test_array_append_n(&array, elements, 1000));
  TEST_ASSERT_NOT_NULL(array.data);
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_append, array.count);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(array.count, array.capacity);
  const uint32_t old_cap = array.capacity;
  for (uint32_t i = 0; i < 1000; ++i) {
    TEST_ASSERT_EQUAL_UINT32(i + 1, array.data[old_count + i]);
  }
  TEST_ASSERT_TRUE(test_array_remove(&array, old_count, array.count - 1));
  TEST_ASSERT_NOT_NULL(array.data);
  TEST_ASSERT_EQUAL_UINT32(expected_count_after_remove, array.count);
  TEST_ASSERT_EQUAL_UINT32(old_cap, array.capacity);
}

static void test_grow() {
  uint32_t old_capacity = array.capacity;
  uint32_t new_capacity = array_grow_capacity(old_capacity, 0, UINT32_MAX);
  TEST_ASSERT_TRUE(test_array_grow(&array, new_capacity));
  TEST_ASSERT_EQUAL_UINT32(new_capacity, array.capacity);

  old_capacity = array.capacity;
  new_capacity = array_grow_capacity(array.capacity, 99, UINT32_MAX);
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(old_capacity + 99, new_capacity);
  TEST_ASSERT_TRUE(test_array_grow(&array, new_capacity));
  TEST_ASSERT_EQUAL_UINT32(new_capacity, array.capacity);

  test_array_deinit(&array, &alloc);
  test_array_init(&array, &alloc);
  test_init_state();
}

static void test_invalid_remove() {
  test_array_deinit(&array, &alloc);
  test_array_init(&array, &alloc);
  test_init_state();

  TEST_ASSERT_FALSE(test_array_remove(&array, 0, 0));
  TEST_ASSERT_FALSE(test_array_remove(&array, 1, 1));
  TEST_ASSERT_FALSE(test_array_remove(&array, 1, 0));
}

ARRAY_DECLARE(bytesized, int, uint8_t)
ARRAY_DEFINE(bytesized, int, uint8_t, UINT8_MAX, ARRAY_DEFAULT_TYPE_DEINIT)

bytesized bs;

static void test_grow_overflow() {
  bytesized_init(&bs, &alloc);
  for (int i = 0; i < UINT8_MAX; ++i) {
    TEST_ASSERT_TRUE(bytesized_append(&bs, i + 1));
  }
  const uint8_t old_capacity = bs.capacity;
  const uint8_t old_count = bs.count;
  int* old_data = bs.data;
  TEST_ASSERT_FALSE(bytesized_append(&bs, -1));
  TEST_ASSERT_EQUAL_UINT8(old_capacity, bs.capacity);
  TEST_ASSERT_EQUAL_UINT8(old_count, bs.count);
  TEST_ASSERT_EQUAL_PTR(old_data, bs.data);
  for (int i = 0; i < UINT8_MAX; ++i) {
    TEST_ASSERT_EQUAL_INT(i + 1, bs.data[i]);
  }
  bytesized_deinit(&bs, &alloc);
}

#include <stdlib.h>

void deinit(int** p_int, allocators* alloc) {
  alloc->release(alloc, *p_int);
  *p_int = NULL;
}

ARRAY_DECLARE_DEFAULT(needsfree, int*)
ARRAY_DEFINE_DEFAULT(needsfree, int*, deinit)

void test_deinit() {
  needsfree arr;
  needsfree_init(&arr, &alloc);
  for (uint32_t i = 0; i < 1000; ++i) {
    TEST_ASSERT_TRUE(needsfree_append(&arr, malloc(sizeof(int))));
  }
  needsfree_deinit(&arr, &alloc); // if test fail there will be a leak and asan will trigger.
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_init_state);
  RUN_TEST(test_append_1_remove_1);
  RUN_TEST(test_append_100_remove_100);
  RUN_TEST(test_append_n_1_remove_1);
  RUN_TEST(test_append_n_1000_remove_1000);
  RUN_TEST(test_grow);
  RUN_TEST(test_invalid_remove);
  RUN_TEST(test_grow_overflow);
  RUN_TEST(test_deinit);

  return UNITY_END();
}
