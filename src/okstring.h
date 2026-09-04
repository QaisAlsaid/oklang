#ifndef OK_STRING_H
#define OK_STRING_H

#include <stdbool.h>
#include <stdint.h>

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

typedef struct {
  const char* chars;
  uint64_t info; // 56bit length + 1 bit for is_cstring_view (is null terminated).
} string_view;

#define STRING_VIEW_LENGTH_MAX UINT56_MAX
#define STRING_VIEW_CALCULATE_LENGTH (STRING_LENGTH_MAX + 1) // implies and requires null terminated.
bool string_view_init(string_view* p_string_view, const char* p_chars, uint64_t p_length, bool p_is_cstr_view);
void string_view_deinit(
    string_view* p_string_view); // not mandatory just cleans stail references, same goes for cstring_view.
uint64_t string_view_get_length(const string_view* p_string_view);
bool string_view_is_cstring_view(const string_view* p_string_view);
string_view create_string_view(const char* p_chars, uint64_t p_length, bool p_is_cstr_view);

typedef struct {
  const char* chars;
  uint64_t info; // 56bit length (could be calculated lazily).
} cstring_view;

#define CSTRING_VIEW_LENGTH_MAX UINT56_MAX
#define CSTRING_VIEW_CALCULATE_LENGTH (STRING_LENGTH_MAX + 1)
#define CSTRING_VIEW_LAZY_CALCULATE_LENGTH (STRING_LENGTH_MAX + 2)
#define CSTRING_VIEW_LENGTH_OVERFLOW (STRING_LENGTH_MAX + 3)

bool cstring_view_init(cstring_view* p_cstring_view, const char* p_chars, uint64_t p_length);
void cstring_view_deinit(cstring_view* p_cstring_view);
uint64_t cstring_view_lazy_length(cstring_view* p_cstring_view);
uint64_t cstring_view_get_length(const cstring_view* p_cstring_view);
cstring_view create_cstring_view(const char* p_chars, uint64_t p_length);

string_view create_string_view_from_string(const string p_string);
cstring_view create_cstring_view_from_string(const string p_string);

string create_string_from_string_view(const string_view p_string_view, allocators* p_alloc);
cstring_view create_cstring_view_from_string_view(const string_view p_string_view);

string create_string_from_cstring_view(cstring_view p_cstring_view, allocators* p_alloc);
string_view create_string_view_from_cstring_view(const cstring_view p_cstring_view);

typedef uint64_t hash_t;
typedef struct {
  string string;
  hash_t hash;
} hashed_string;

void hashed_string_init(hashed_string* p_hashed_string,
                        const string_view p_view,
                        const hash_t p_hash,
                        allocators* p_alloc);
void hashed_string_deinit(hashed_string* p_hashed_string, allocators* p_alloc);
hashed_string create_hashed_string(const string_view p_string, const hash_t p_hash, allocators* p_alloc);
hashed_string create_hashed_string_hash(const string_view p_view, allocators* p_alloc);

#endif // OK_STRING_H
