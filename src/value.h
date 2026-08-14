#ifndef OK_VALUE_H
#define OK_VALUE_H

#include <stdint.h>

typedef double value;

void value_debug_print(value p_value);

typedef struct {
  uint32_t count;
  uint32_t capacity;
  value* value_array;
} value_array;

void value_array_init(value_array* p_values);
void value_array_deinit(value_array* p_values);
void value_array_append(value_array* p_values, value p_value);

#endif // OK_VALUE_H
