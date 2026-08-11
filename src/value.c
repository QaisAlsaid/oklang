#include <stdio.h>
#include "value.h"
#include "mm.h"
#include "array.h"

void value_debug_print(value p_value) {
  printf("%g", p_value);
}

void values_init(values* p_values) {
  OK_ARRAY_INIT(p_values->count, p_values->capacity, p_values->value_array);
}

void values_just_write(values* p_values, value p_value) {
  OK_ARRAY_APPEND(value, p_values->count, p_values->capacity, p_values->value_array, p_value);
}

uint32_t values_write(values* p_values, value p_value) {
  values_just_write(p_values, p_value);
  return p_values->count - 1;
}

void values_free(values* p_values) {
  OK_ARRAY_FREE(value, p_values->capacity, p_values->value_array);
  values_init(p_values);
}
