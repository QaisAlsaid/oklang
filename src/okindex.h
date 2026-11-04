#ifndef OK_INDEX_H
#define OK_INDEX_H

#include <stddef.h>
#include <stdint.h>

// fuck strings.h
typedef uint16_t ok_index;

#define INDEX_MAX UINT16_MAX
#define IS_INDEX_VALID(iw) ((iw.errors) == INDEX_OK)

typedef enum { INDEX_OK = 0 } index_error;

typedef struct {
  ok_index index;
  // the two anonymous unions are ok's one chosen C11-ism: context-named
  // views over the same bytes (a kind for the writer, a flag for the
  // reader). naming them would redesign the tagged-index idiom the whole
  // tree leans on, so the deviation is marked exactly here instead, and
  // the C99 pedantic gate stays on everywhere else.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
  union {
    uint8_t flags;
    uint8_t kind;
    uint8_t type;
  };
  union {
    uint8_t errors;
    uint8_t error;
  };
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} index_wrapper;

#endif // OK_INDEX_H
