#include <stdio.h>

#include "okarray.h"
#include "okobject.h"
#include "okvalue.h"

void value_debug_print(value p_value) {
  if (IS_VALUE_NUMBER(p_value)) {
    printf("%g", VALUE_AS_NUMBER(p_value));
  } else if (IS_VALUE_BOOL(p_value)) {
    printf("%s", VALUE_AS_BOOL(p_value) ? "true" : "false");
  } else if (IS_VALUE_OBJECT(p_value)) {
    object_debug_print(VALUE_AS_OBJECT(p_value));
  } else {
    printf("null");
  }
}

ARRAY_DEFINE(value_array, value, uint32_t, UINT32_MAX, ARRAY_DEFAULT_TYPE_DEINIT)
