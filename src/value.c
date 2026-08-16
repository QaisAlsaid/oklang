#include "value.h"
#include "array.h"
#include "mm.h"
#include <stdio.h>

void value_debug_print(value p_value) {
  if (IS_VALUE_NUMBER(p_value)) {
    printf("%g", VALUE_AS_NUMBER(p_value));
  } else if (IS_VALUE_BOOL(p_value)) {
    printf("%s", VALUE_AS_BOOL(p_value) ? "true" : "false");
  } else if (IS_VALUE_NULL(p_value)) {
    printf("null");
  }
}

void value_array_init(value_array* p_values) {
  OK_ARRAY_INIT(p_values->count, p_values->capacity, p_values->value_array);
}

bool value_array_append(value_array* p_values, value p_value) {
  OK_ARRAY_APPEND(value, uint32_t, p_values->count, p_values->capacity, p_values->value_array, p_value);
  return p_values != NULL;
}

void value_array_deinit(value_array* p_values) {
  OK_ARRAY_FREE(value, p_values->capacity, p_values->value_array);
  value_array_init(p_values);
}
