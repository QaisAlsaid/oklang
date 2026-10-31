#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <ok/ok_specs.h>
#include <okarray.h>
#include <okmap.h>
#include <okspecs.h>
#include <okutils.h>

// instantiation 1: the globals_store shape — raw string keys, uint32 values,
// string-view probes (zero-allocation lookups).
#define S_KEY_IS_EMPTY(k) ((k).chars == NULL)
#define S_KEY_SET_EMPTY(k)                                                                                               \
  do {                                                                                                                   \
    (k)->chars = NULL;                                                                                                   \
    (k)->info = 0;                                                                                                       \
  } while (0)
#define S_PROBE_HASH(p) (default_hash_str(p))
#define S_PROBE_MATCHES(p, k)                                                                                            \
  (ok_string_view_get_length(&(p)) == string_get_length(&(k)) &&                                                         \
   memcmp((k).chars, (p).chars, ok_string_view_get_length(&(p))) == 0)
#define S_KEY_CLONE(p, a) (create_string_from_string_view((p), (a)))
#define S_KEY_DEINIT(k, a) (string_deinit((k), (a)))

MAP_DECLARE(str_map, string, uint32_t, ok_string_view)
MAP_DEFINE(str_map, string, uint32_t, ok_string_view, S_KEY_IS_EMPTY, S_KEY_SET_EMPTY, S_PROBE_HASH, S_PROBE_MATCHES, S_KEY_CLONE, S_KEY_DEINIT, ARRAY_DEFAULT_TYPE_DEINIT)

// instantiation 2: adversarial hashing (two buckets regardless of capacity)
// to stress probing and backward-shift deletion under maximal clustering.
#define U_KEY_IS_EMPTY(k) ((k) == UINT32_MAX)
#define U_KEY_SET_EMPTY(k) (*(k) = UINT32_MAX)
#define U_PROBE_HASH(p) ((hash_t)((p) % 2))
#define U_PROBE_MATCHES(p, k) ((p) == (k))
#define U_KEY_CLONE(p, a) (p)
#define U_KEY_DEINIT(k, a)

MAP_DECLARE(bad_map, uint32_t, uint32_t, uint32_t)
MAP_DEFINE(bad_map, uint32_t, uint32_t, uint32_t, U_KEY_IS_EMPTY, U_KEY_SET_EMPTY, U_PROBE_HASH, U_PROBE_MATCHES, U_KEY_CLONE, U_KEY_DEINIT, ARRAY_DEFAULT_TYPE_DEINIT)

// instantiation 3: identity hashing — lets tests place entries at chosen
// home slots (wraparound coverage).
#define W_PROBE_HASH(p) ((hash_t)(p))

MAP_DECLARE(wrap_map, uint32_t, uint32_t, uint32_t)
MAP_DEFINE(wrap_map, uint32_t, uint32_t, uint32_t, U_KEY_IS_EMPTY, U_KEY_SET_EMPTY, W_PROBE_HASH, U_PROBE_MATCHES, U_KEY_CLONE, U_KEY_DEINIT, ARRAY_DEFAULT_TYPE_DEINIT)

static str_map smap;
static bad_map bmap;
static wrap_map wmap;

ok_specs specs = {0};
allocators alloc = {0};

// deterministic LCG so failures reproduce.
static uint32_t rng_state = 0x12345678u;
static uint32_t rng() {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

// tiny shadow model: the "reference python dict" from the spec, C-ified.
#define SHADOW_KEYS 32
typedef struct {
  char key[8];
  uint32_t val;
  bool used;
} shadow_slot;
static shadow_slot shadow[SHADOW_KEYS];

static int32_t shadow_find(const char* p_key) {
  for (uint32_t i = 0; i < SHADOW_KEYS; ++i) {
    if (shadow[i].used && strcmp(shadow[i].key, p_key) == 0) {
      return (int32_t)i;
    }
  }
  return -1;
}

static void shadow_set(const char* p_key, uint32_t p_val) {
  int32_t i = shadow_find(p_key);
  if (i < 0) {
    for (uint32_t j = 0; j < SHADOW_KEYS; ++j) {
      if (!shadow[j].used) {
        i = (int32_t)j;
        strcpy(shadow[i].key, p_key);
        shadow[i].used = true;
        break;
      }
    }
  }
  shadow[i].val = p_val;
}

static bool shadow_remove(const char* p_key) {
  int32_t i = shadow_find(p_key);
  if (i < 0) {
    return false;
  }
  shadow[i].used = false;
  return true;
}

static uint32_t shadow_count() {
  uint32_t n = 0;
  for (uint32_t i = 0; i < SHADOW_KEYS; ++i) {
    if (shadow[i].used) {
      n++;
    }
  }
  return n;
}

// invariant: every occupied slot's entry is findable from its home slot.
static void assert_str_map_invariants() {
  TEST_ASSERT_TRUE(smap.capacity == 0 || (smap.capacity >= 8 && (smap.capacity & (smap.capacity - 1)) == 0));
  uint32_t live = 0;
  for (uint32_t i = 0; i < smap.capacity; ++i) {
    if (S_KEY_IS_EMPTY(smap.keys[i])) {
      continue;
    }
    live++;
    const ok_string_view probe = ok_create_string_view_from_string(smap.keys[i]);
    uint32_t* v = str_map_get(&smap, probe);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT32(smap.values[i], *v);
  }
  TEST_ASSERT_EQUAL_UINT32(live, smap.count);
}

void setUp() {
  patch_specs(&specs);
  alloc = create_raw_allocators_wrapper(&specs.allocators);
  str_map_init(&smap, &alloc);
  bad_map_init(&bmap, &alloc);
  wrap_map_init(&wmap, &alloc);
  memset(shadow, 0, sizeof(shadow));
}

void tearDown() {
  str_map_deinit(&smap);
  bad_map_deinit(&bmap);
  wrap_map_deinit(&wmap);
}

void test_init_state() {
  TEST_ASSERT_EQUAL_UINT32(0, smap.count);
  TEST_ASSERT_EQUAL_UINT32(0, smap.capacity);
  TEST_ASSERT_NULL(smap.keys);
  TEST_ASSERT_NULL(str_map_get(&smap, ok_create_string_view("absent", 6, true)));
  TEST_ASSERT_FALSE(str_map_remove(&smap, ok_create_string_view("absent", 6, true)));
}

void test_set_get_roundtrip() {
  TEST_ASSERT_TRUE(str_map_set(&smap, ok_create_string_view("alpha", 5, true), 1));
  TEST_ASSERT_TRUE(str_map_set(&smap, ok_create_string_view("beta", 4, true), 2));
  TEST_ASSERT_EQUAL_UINT32(2, smap.count);
  TEST_ASSERT_EQUAL_UINT32(1, *str_map_get(&smap, ok_create_string_view("alpha", 5, true)));
  TEST_ASSERT_EQUAL_UINT32(2, *str_map_get(&smap, ok_create_string_view("beta", 4, true)));
  TEST_ASSERT_NULL(str_map_get(&smap, ok_create_string_view("gamma", 5, true)));
}

void test_set_updates_existing() {
  TEST_ASSERT_TRUE(str_map_set(&smap, ok_create_string_view("k", 1, true), 10));
  TEST_ASSERT_TRUE(str_map_set(&smap, ok_create_string_view("k", 1, true), 20));
  TEST_ASSERT_EQUAL_UINT32(1, smap.count);
  TEST_ASSERT_EQUAL_UINT32(20, *str_map_get(&smap, ok_create_string_view("k", 1, true)));
}

void test_remove_then_get() {
  TEST_ASSERT_TRUE(str_map_set(&smap, ok_create_string_view("k", 1, true), 7));
  TEST_ASSERT_TRUE(str_map_remove(&smap, ok_create_string_view("k", 1, true)));
  TEST_ASSERT_EQUAL_UINT32(0, smap.count);
  TEST_ASSERT_NULL(str_map_get(&smap, ok_create_string_view("k", 1, true)));
  TEST_ASSERT_FALSE(str_map_remove(&smap, ok_create_string_view("k", 1, true)));
}

// the worked example from the design doc: three keys sharing one home slot,
// deleting the middle one must slide the third back or lose it.
void test_backward_shift_worked_example() {
  TEST_ASSERT_TRUE(bad_map_set(&bmap, 2, 200));
  TEST_ASSERT_TRUE(bad_map_set(&bmap, 4, 400));
  TEST_ASSERT_TRUE(bad_map_set(&bmap, 6, 600));
  // all three hash to bucket 0: they occupy slots 0, 1, 2.
  TEST_ASSERT_EQUAL_UINT32(2, bmap.keys[0]);
  TEST_ASSERT_EQUAL_UINT32(4, bmap.keys[1]);
  TEST_ASSERT_EQUAL_UINT32(6, bmap.keys[2]);
  TEST_ASSERT_TRUE(bad_map_remove(&bmap, 4));
  TEST_ASSERT_EQUAL_UINT32(2, bmap.count);
  // entry 6 slid back into slot 1; slot 2 is empty again.
  TEST_ASSERT_EQUAL_UINT32(2, bmap.keys[0]);
  TEST_ASSERT_EQUAL_UINT32(6, bmap.keys[1]);
  TEST_ASSERT_TRUE(U_KEY_IS_EMPTY(bmap.keys[2]));
  TEST_ASSERT_EQUAL_UINT32(200, *bad_map_get(&bmap, 2));
  TEST_ASSERT_EQUAL_UINT32(600, *bad_map_get(&bmap, 6));
  TEST_ASSERT_NULL(bad_map_get(&bmap, 4));
}

// cluster wrapping past the end of the array: homes at cap-1 and 0.
void test_backward_shift_wraparound() {
  TEST_ASSERT_TRUE(wrap_map_set(&wmap, 7, 70));
  TEST_ASSERT_TRUE(wrap_map_set(&wmap, 15, 150)); // home 7 is taken, wraps to slot 0
  TEST_ASSERT_TRUE(wrap_map_set(&wmap, 0, 0)); // home 0 is taken, lands at slot 1
  TEST_ASSERT_EQUAL_UINT32(7, wmap.keys[7]);
  TEST_ASSERT_EQUAL_UINT32(15, wmap.keys[0]);
  TEST_ASSERT_EQUAL_UINT32(0, wmap.keys[1]);
  TEST_ASSERT_TRUE(wrap_map_remove(&wmap, 7));
  TEST_ASSERT_EQUAL_UINT32(150, *wrap_map_get(&wmap, 15));
  TEST_ASSERT_EQUAL_UINT32(0, *wrap_map_get(&wmap, 0));
  TEST_ASSERT_NULL(wrap_map_get(&wmap, 7));
}

void test_grow_path() {
  char key[16];
  for (uint32_t i = 0; i < 1000; ++i) {
    snprintf(key, sizeof(key), "key_%u", i);
    TEST_ASSERT_TRUE(str_map_set(&smap, ok_create_string_view(key, strlen(key), true), i));
  }
  TEST_ASSERT_EQUAL_UINT32(1000, smap.count);
  TEST_ASSERT_TRUE(smap.capacity >= 16 && (smap.capacity & (smap.capacity - 1)) == 0);
  for (uint32_t i = 0; i < 1000; i += 97) {
    snprintf(key, sizeof(key), "key_%u", i);
    TEST_ASSERT_EQUAL_UINT32(i, *str_map_get(&smap, ok_create_string_view(key, strlen(key), true)));
  }
}

// random operations against the shadow model, with the invariant checker
// run periodically. the backward-shift correctness property is exactly this:
// after any sequence of sets/removes, every live key is still findable.
void test_property_vs_shadow_model() {
  char key[8];
  for (uint32_t op = 0; op < 2000; ++op) {
    const uint32_t k = rng() % SHADOW_KEYS;
    const uint32_t action = rng() % 10;
    snprintf(key, sizeof(key), "k%u", k);
    const ok_string_view probe = ok_create_string_view(key, strlen(key), true);
    if (action < 6) {
      const uint32_t val = rng() % 1000000;
      TEST_ASSERT_TRUE(str_map_set(&smap, probe, val));
      shadow_set(key, val);
    } else {
      const bool map_removed = str_map_remove(&smap, probe);
      const bool shadow_removed = shadow_remove(key);
      TEST_ASSERT_EQUAL_INT(shadow_removed ? 1 : 0, map_removed ? 1 : 0);
    }
    uint32_t* got = str_map_get(&smap, probe);
    const int32_t idx = shadow_find(key);
    if (idx < 0) {
      TEST_ASSERT_NULL(got);
    } else {
      TEST_ASSERT_NOT_NULL(got);
      TEST_ASSERT_EQUAL_UINT32(shadow[idx].val, *got);
    }
    if (op % 100 == 99) {
      TEST_ASSERT_EQUAL_UINT32(shadow_count(), smap.count);
      assert_str_map_invariants();
    }
  }
  TEST_ASSERT_EQUAL_UINT32(shadow_count(), smap.count);
  assert_str_map_invariants();
}

// the adversarial instantiation under the same regime: everything collides.
void test_property_adversarial_hash() {
  for (uint32_t op = 0; op < 1000; ++op) {
    const uint32_t k = rng() % 24;
    const uint32_t action = rng() % 10;
    if (action < 6) {
      TEST_ASSERT_TRUE(bad_map_set(&bmap, k, k * 3));
      uint32_t* got = bad_map_get(&bmap, k);
      TEST_ASSERT_NOT_NULL(got);
      TEST_ASSERT_EQUAL_UINT32(k * 3, *got);
    } else {
      (void)bad_map_remove(&bmap, k);
      // presence now depends on history: if present, the value must hold.
      uint32_t* got = bad_map_get(&bmap, k);
      if (got != NULL) {
        TEST_ASSERT_EQUAL_UINT32(k * 3, *got);
      }
    }
  }
  // final invariant: every occupied slot's entry is findable and correct.
  uint32_t live = 0;
  for (uint32_t i = 0; i < bmap.capacity; ++i) {
    if (U_KEY_IS_EMPTY(bmap.keys[i])) {
      continue;
    }
    live++;
    uint32_t* got = bad_map_get(&bmap, bmap.keys[i]);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_UINT32(bmap.values[i], *got);
  }
  TEST_ASSERT_EQUAL_UINT32(live, bmap.count);
  TEST_ASSERT_TRUE(bmap.capacity == 0 || (bmap.capacity & (bmap.capacity - 1)) == 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_init_state);
  RUN_TEST(test_set_get_roundtrip);
  RUN_TEST(test_set_updates_existing);
  RUN_TEST(test_remove_then_get);
  RUN_TEST(test_backward_shift_worked_example);
  RUN_TEST(test_backward_shift_wraparound);
  RUN_TEST(test_grow_path);
  RUN_TEST(test_property_vs_shadow_model);
  RUN_TEST(test_property_adversarial_hash);
  return UNITY_END();
}
