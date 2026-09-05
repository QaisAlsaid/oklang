#ifndef OK_STRING_H
#define OK_STRING_H

#include <stdbool.h>
#include <stdint.h>

#define UINT56_MAX ((UINT64_C(1) << 56) - 1)

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
  const char* chars;
  uint64_t info; // 56bit length + 1 bit for is_cstring_view (is null terminated).
} ok_string_view;

#define OK_STRING_VIEW_LENGTH_MAX UINT56_MAX
#define OK_STRING_VIEW_CALCULATE_LENGTH (OK_STRING_VIEW_LENGTH_MAX + 1) // implies and requires null terminated.
bool ok_string_view_init(ok_string_view* p_string_view, const char* p_chars, uint64_t p_length, bool p_is_cstr_view);
// not mandatory just cleans stail references, same goes for ok_cstring_view.
void ok_string_view_deinit(ok_string_view* p_string_view);
uint64_t ok_string_view_get_length(const ok_string_view* p_string_view);
bool ok_string_view_is_cstring_view(const ok_string_view* p_string_view);
ok_string_view ok_create_string_view(const char* p_chars, uint64_t p_length, bool p_is_cstr_view);

typedef struct {
  const char* chars;
  uint64_t info; // 56bit length (could be calculated lazily).
} ok_cstring_view;

#define OK_CSTRING_VIEW_LENGTH_MAX UINT56_MAX
#define OK_CSTRING_VIEW_CALCULATE_LENGTH (OK_CSTRING_VIEW_LENGTH_MAX + 1)
#define OK_CSTRING_VIEW_LAZY_CALCULATE_LENGTH (OK_CSTRING_VIEW_LENGTH_MAX + 2)
#define OK_CSTRING_VIEW_LENGTH_OVERFLOW (OK_CSTRING_VIEW_LENGTH_MAX + 3)

bool ok_cstring_view_init(ok_cstring_view* p_cstring_view, const char* p_chars, uint64_t p_length);
void ok_cstring_view_deinit(ok_cstring_view* p_cstring_view);
uint64_t ok_cstring_view_lazy_length(ok_cstring_view* p_cstring_view);
uint64_t ok_cstring_view_get_length(const ok_cstring_view* p_cstring_view);
ok_cstring_view ok_create_cstring_view(const char* p_chars, uint64_t p_length);

ok_cstring_view ok_ok_create_cstring_view_from_string_view(const ok_string_view p_string_view);
ok_string_view ok_ok_create_string_view_from_cstring_view(const ok_cstring_view p_cstring_view);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_STRING_H
