#ifndef OK_GLOBALS_STORE_H
#define OK_GLOBALS_STORE_H

#include "okarray.h"
#include "okchunk.h" // for UINT24_MAX
#include "okfwd.h"
#include "okstring.h"
#include "oktable.h"
#include "okvalue.h"

#define GLOBAL_ALLOCATION_FAILED UINT32_MAX
#define GLOBAL_OVERFLOW (GLOBAL_ALLOCATION_FAILED - 1)
#define GLOBAL_NOT_FOUND (GLOBAL_OVERFLOW - 1)
#define GLOBAL_ILL_MUTATION (GLOBAL_NOT_FOUND - 1)
#define GLOBAL_ERROR (GLOBAL_ILL_MUTATION - 1)
#define IS_GLOBAL_VALID(global) (((global) & 0xf0000000) == 0)
#define GLOBAL_MAX UINT24_MAX

typedef enum {
  GLOBAL_FLAG_MUTABLE = 0,
} global_flags;

ARRAY_DECLARE(global_values, value, uint32_t)
TABLE_DECLARE(globals_table, uint32_t, uint32_t, hashed_string, uint32_t, hash_t)

#if defined(OK_DEBUG) // TODO debug meta data to get a name from an index
TABLE_DECLARE(debug_table, uint32_t, uint32_t, uint32_t, hashed_string, hash_t)
#endif

struct globals_store {
  global_values global_values;
  globals_table table;
  allocators* alloc;
};

void globals_store_init(globals_store* p_store, allocators* p_alloc);
void globals_store_deinit(globals_store* p_store);
uint32_t globals_store_add(globals_store* p_store, const ok_string_view p_identifier, bool p_is_mutable);
uint32_t globals_store_get(globals_store* p_store, const ok_string_view p_identifier);

static inline uint32_t global_get_raw_index(uint32_t p_packed) {
  return p_packed & 0x00ffffff;
}

static inline bool global_test_flag(uint32_t p_packed, uint8_t p_flag) {
  return (p_packed & UINT32_C(1) << (24 + p_flag)) != 0;
}

static inline void global_set_flag(uint32_t* p_packed, uint8_t p_flag) {
  *p_packed |= (UINT32_C(1) << (24 + p_flag));
}

static inline void global_clear_flag(uint32_t* p_packed, uint8_t p_flag) {
  *p_packed &= ~(UINT32_C(1) << (24 + p_flag));
}

#endif // OK_GLOBALS_STORE_H
