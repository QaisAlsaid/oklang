#ifndef OK_VALUE_H
#define OK_VALUE_H

#include <stdint.h>

typedef double value;

void value_debug_print(value p_value);

typedef struct {
  uint32_t count;
  uint32_t capacity;
  value* value_array;
} values;

void values_init(values* p_values);
void values_just_write(values* p_values, value p_value);
uint32_t values_write(values* p_values, value p_value);
void values_free(values* p_values);

#endif // OK_VALUE_H
