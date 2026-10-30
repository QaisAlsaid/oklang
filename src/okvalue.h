#ifndef OK_VALUE_H
#define OK_VALUE_H

#include <stdbool.h>
#include <stdint.h>

#include "okarray.h"

/*=============================boxed memory layout=================================
 *
 * LSB                                                                     MSB
 *  ↓                                                                       ↓
 *  00000000000000000000000000000000000000000000000 0  0  0  0 000000000000 0
 *                          ↓                       ↓  ↓  ↓  ↓       ↓      ↓
 *                       payload                    T3 T2 R  Q      exp     T1
 *=================================================================================*/

/*==========tag============
 *
 * tag = { T3, T2, T1 }
 *
 *=========================*/

#define QNAN (UINT64_C(0x7ffc000000000000))
#define SIGN_BIT (UINT64_C(0x8000000000000000))

#define PAYLOAD_MASK (UINT64_C(0x0000ffffffffffff))
#define TAG_T1_MASK SIGN_BIT
#define TAG_T3_MASK (UINT64_C(0x0003000000000000))
#define TAG_MASK (TAG_T3_MASK | TAG_T1_MASK)

#define TAG_X_MASK(t) ((((uint64_t)(t) & UINT64_C(0x3)) << 48) | (((uint64_t)(t) & UINT64_C(0x4)) << 61))

#define IS_VALUE_TAG(v, tag_bits) (((v) & (QNAN | TAG_MASK)) == (QNAN | tag_bits))

#define TAG_NULL 0
#define TAG_FALSE 1
#define TAG_TRUE 2
#define TAG_OBJECT 3
#define TAG_INT 4
#define TAG_USER 5       // pointer or whatever as long as it is 48bits
#define TAG_RESERVED_1 6 // not used yet
#define TAG_RESERVED_2 7 // not used yet

#define TAG_NULL_MASK UINT64_C(0x0000000000000000)
#define TAG_FALSE_MASK UINT64_C(0x0001000000000000)
#define TAG_TRUE_MASK UINT64_C(0x0002000000000000)
#define TAG_OBJECT_MASK UINT64_C(0x0003000000000000)
#define TAG_INT_MASK UINT64_C(0x8000000000000000)
#define TAG_USER_MASK UINT64_C(0x8001000000000000)
#define TAG_RESERVED_1_MASK UINT64_C(0x8002000000000000)
#define TAG_RESERVED_2_MASK UINT64_C(0x8003000000000000)

#define VALUE_TAG(v) ((v) & TAG_MASK)
#define VALUE_PAYLOAD(v) ((v) & PAYLOAD_MASK)

#define TAGGED_VALUE(t, p) (QNAN | (t) | ((p) & PAYLOAD_MASK))

#define IS_VALUE_NUMBER(v) (((v) & QNAN) != QNAN)
#define IS_VALUE_NULL(v) (VALUE_TAG(v) == TAG_NULL_MASK)
#define IS_VALUE_BOOL(v) is_value_bool(v)
#define IS_VALUE_OBJECT(v) (VALUE_TAG(v) == TAG_OBJECT_MASK)
#define IS_VALUE_INT(v) (VALUE_TAG(v) == TAG_INT_MASK)
#define IS_VALUE_USER(v) (VALUE_TAG(v) == TAG_USER_MASK)
#define IS_VALUE_RESERVED_1(v) (VALUE_TAG(v) == TAG_RESERVED_1_MASK)
#define IS_VALUE_RESERVED_2(v) (VALUE_TAG(v) == TAG_RESERVED_2_MASK)

#define IS_VALUE_STRICTLY_NULL(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_NULL(v))
#define IS_VALUE_STRICTLY_BOOL(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_BOOL(v))
#define IS_VALUE_STRICTLY_OBJECT(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_OBJECT(v))
#define IS_VALUE_STRICTLY_INT(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_INT(v))
#define IS_VALUE_STRICTLY_USER(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_USER(v))
#define IS_VALUE_STRICTLY_RESERVED_1(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_RESERVED_1(v))
#define IS_VALUE_STRICTLY_RESERVED_2(v) (!IS_VALUE_NUMBER(v) && IS_VALUE_RESERVED_2(v))

#define NUMBER_AS_VALUE(n) number_to_value(n)
#define NULL_AS_VALUE() TAGGED_VALUE(TAG_NULL_MASK, 0)
#define BOOL_AS_VALUE(b) TAGGED_VALUE((b) ? TAG_TRUE_MASK : TAG_FALSE_MASK, 0)
#define OBJECT_AS_VALUE(o) TAGGED_VALUE(TAG_OBJECT_MASK, (uint64_t)(uintptr_t)o)
#define INT_AS_VALUE(i) TAGGED_VALUE(TAG_INT_MASK, int_to_payload(i))
#define USER_AS_VALUE(u) TAGGED_VALUE(TAG_USER_MASK, (uint64_t)u)
#define USER_PTR_AS_VALUE(u) TAGGED_VALUE(TAG_USER_MASK, (uint64_t)(uintptr_t)(u))
#define RESERVED_1_AS_VALUE(r1) TAGGED_VALUE(TAG_RESERVED_1_MASK, (uint64_t)(r1))
#define RESERVED_2_AS_VALUE(r2) TAGGED_VALUE(TAG_RESERVED_2_MASK, (uint64_t)(r2))

#define VALUE_AS_NUMBER(v) value_to_number(v)
#define VALUE_AS_NULL(v) NULL
#define VALUE_AS_BOOL(v) (VALUE_TAG(v) == TAG_TRUE_MASK)
#define VALUE_AS_OBJECT(v) ((object*)(uintptr_t)(VALUE_PAYLOAD(v)))
#define VALUE_AS_INT(v) (payload_to_int(VALUE_PAYLOAD(v)))
#define VALUE_AS_USER(v) VALUE_PAYLOAD(v)
#define VALUE_AS_USER_PTR(v) ((void*)(uintptr_t)VALUE_PAYLOAD(v))
#define VALUE_AS_RESERVED_1(v) VALUE_PAYLOAD(v)
#define VALUE_AS_RESERVED_2(v) VALUE_PAYLOAD(v)

typedef uint64_t value;

static inline value make_tagged_value(uint8_t tag, uint64_t payload) {
  return QNAN | ((uint64_t)(tag & 0x3u) << 48u) | ((uint64_t)(tag & 0x4u) << 61u) | (payload & PAYLOAD_MASK);
}

static inline uint64_t value_payload(value p_value) {
  return p_value & PAYLOAD_MASK;
}

static inline bool is_value_bool(value p_value) {
  return (VALUE_TAG(p_value) == TAG_TRUE_MASK || VALUE_TAG(p_value) == TAG_FALSE_MASK);
}

static inline value number_to_value(double p_number) {
  union pun {
    value v;
    double n;
  };
  union pun val = {.n = p_number};
  return val.v;
}

static inline double value_to_number(value p_value) {
  union pun {
    double n;
    value v;
  };
  union pun val = {.v = p_value};
  return val.n;
}

#define INT_PAYLOAD_SIGN UINT64_C(0x0000800000000000)
#define INT_PAYLOAD_MASK UINT64_C(0x0000ffffffffffff)

static inline uint64_t int_to_payload(int64_t p_int) {
  return (uint64_t)p_int & INT_PAYLOAD_MASK;
}

static inline int64_t payload_to_int(uint64_t p_payload) {
  p_payload &= INT_PAYLOAD_MASK;
  if (p_payload & INT_PAYLOAD_SIGN) {
    p_payload |= ~INT_PAYLOAD_MASK;
  }
  return p_payload;
}

void value_debug_print(value p_value);

ARRAY_DECLARE(value_array, value, uint32_t)

#endif // OK_VALUE_H
