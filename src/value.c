#include <stdio.h>
#include "value.h"
#include "mm.h"
#include "array.h"

void value_debug_print(value p_value) {
  printf("%g", p_value);
}

void value_array_init(value_array* p_values) {
  OK_ARRAY_INIT(p_values->count, p_values->capacity, p_values->value_array);
}

void value_array_append(value_array* p_values, value p_value) {
  OK_ARRAY_APPEND(value, uint32_t, p_values->count, p_values->capacity, p_values->value_array, p_value);
}

void value_array_deinit(value_array* p_values) {
  OK_ARRAY_FREE(value, p_values->capacity, p_values->value_array);
  value_array_init(p_values);
}
