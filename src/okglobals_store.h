#ifndef OK_GLOBALS_STORE_H
#define OK_GLOBALS_STORE_H

#include "okarray.h"
#include "oktable.h"
#include "okutils.h"
#include "okvalue.h"

#define GLOBAL_ALLOCATION_FAILED UINT32_MAX
#define GLOBAL_OVERFLOW (GLOBAL_ALLOCATION_FAILED - 1)
#define GLOBAL_NOT_FOUND (GLOBAL_OVERFLOW - 1)
#define GLOBAL_ILL_MUTATION (GLOBAL_NOT_FOUND - 1)
#define GLOBAL_ERROR (GLOBAL_ILL_MUTATION - 1)
#define IS_GLOBAL_VALID(global) ((global) < GLOBAL_ERROR)
#define GLOBAL_MAX UINT24_MAX

typedef enum {
  GLOBAL_FLAG_MUTABLE = 1,
} global_flags;

ARRAY_DECLARE(global_values, value, uint32_t)
TABLE_DECLARE(globals_table, uint32_t, uint32_t, hashed_string, uint32_t, hash_t)

#if defined(OK_DEBUG) // TODO debug meta data to get a name from an index
TABLE_DECLARE(debug_table, uint32_t, uint32_t, uint32_t, hashed_string, hash_t)
#endif

typedef struct {
  global_values global_values;
  globals_table table;
} globals_store;

void globals_store_init(globals_store* p_store);
void globals_store_deinit(globals_store* p_store);
uint32_t globals_store_add(globals_store* p_store, const string_view p_identifier, uint8_t p_flags /* 4 bits */);
uint32_t globals_store_get(globals_store* p_store, const string_view p_identifier);

#endif // OK_GLOBALS_STORE_H
