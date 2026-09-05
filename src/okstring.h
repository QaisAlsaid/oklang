#ifndef OK_SRC_STRING_H
#define OK_SRC_STRING_H

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok_string.h"
#include "okfwd.h"

#define UINT56_MAX ((UINT64_C(1) << 56) - 1)

// owning string type with length info, always copies, excpet when static string.
typedef struct {
  const char* chars;
  uint64_t info; // 56bit length + 1 bit for is_dynamic
} string;

#define STRING_LENGTH_MAX UINT56_MAX
#define STRING_CALCULATE_LENGTH (STRING_LENGTH_MAX + 1) // implies null terminated.
bool string_init(string* p_string, const char* p_chars, uint64_t p_length, bool p_is_dynamic, allocators* p_alloc);
void string_deinit(string* p_string,
                   allocators* p_alloc); // mandatory call for deallocation (unless it is a static string).
uint64_t string_get_length(const string* p_string);
bool string_is_dynamic(const string* p_string);
string create_dynamic_string(const char* p_chars, uint64_t p_length, allocators* p_alloc);
string create_static_string(const char* p_chars, uint64_t p_length);

ok_string_view ok_create_string_view_from_string(const string p_string);
ok_cstring_view ok_create_cstring_view_from_string(const string p_string);
string create_string_from_string_view(const ok_string_view p_string_view, allocators* p_alloc);
string create_string_from_cstring_view(ok_cstring_view p_cstring_view, allocators* p_alloc);

typedef uint64_t hash_t;
typedef struct {
  string string;
  hash_t hash;
} hashed_string;

void hashed_string_init(hashed_string* p_hashed_string,
                        const ok_string_view p_view,
                        const hash_t p_hash,
                        allocators* p_alloc);
void hashed_string_deinit(hashed_string* p_hashed_string, allocators* p_alloc);
hashed_string create_hashed_string(const ok_string_view p_string, const hash_t p_hash, allocators* p_alloc);
hashed_string create_hashed_string_hash(const ok_string_view p_view, allocators* p_alloc);

#endif // OK_SRC_STRING_H
