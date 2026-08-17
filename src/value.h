#ifndef OK_VALUE_H
#define OK_VALUE_H

#include "array.h"
#include <stdbool.h>
#include <stdint.h>

#define QNAN ((uint64_t)0x7ffc000000000000)
#define SIGN_BIT ((uint64_t)0x8000000000000000)

#define NULL_BIT 1
#define FALSE_BIT 2
#define TRUE_BIT 3

#define VALUE_NULL ((value)(QNAN | NULL_BIT))
#define VALUE_FALSE ((value)(QNAN | FALSE_BIT))
#define VALUE_TRUE ((value)(QNAN | TRUE_BIT))

#define IS_VALUE_NULL(value) ((value) == VALUE_NULL)
#define IS_VALUE_BOOL(value) ((value) == VALUE_FALSE || (value) == VALUE_TRUE)
#define IS_VALUE_NUMBER(value) (((value) & QNAN) != QNAN)
#define IS_VALUE_OBJECT(value) (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define NULL_AS_VALUE() VALUE_NULL
#define BOOL_AS_VALUE(bool) (bool ? VALUE_TRUE : VALUE_FALSE)
#define NUMBER_AS_VALUE(number) number_to_value(number)
#define OBJECT_AS_VALUE(object) ((value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(object)))

#define VALUE_AS_NULL(value) NULL
#define VALUE_AS_BOOL(value) ((value) == VALUE_TRUE)
#define VALUE_AS_NUMBER(value) value_to_number(value)
#define VALUE_AS_OBJECT(value) ((object*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

typedef uint64_t value;

static value inline number_to_value(double p_number) {
  union pun {
    value v;
    double n;
  };
  union pun val = {.n = p_number};
  return val.v;
}

static double inline value_to_number(value p_value) {
  union pun {
    double n;
    value v;
  };
  union pun val = {.v = p_value};
  return val.n;
}

void value_debug_print(value p_value);

ARRAY_DECLARE(value, value, uint32_t)

#endif // OK_VALUE_H
