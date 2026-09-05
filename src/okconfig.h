#ifndef OK_CONFIG_H
#define OK_CONFIG_H

#include <stddef.h>
#include <stdint.h>

typedef void* (*ok_reallocate)(void* p_ptr, size_t p_old_size, size_t p_new_size);
typedef void* (*ok_allocate)(size_t p_size);
typedef void (*ok_release)(void* p_ptr);

typedef struct {
  ok_reallocate reallocate;
  ok_allocate allocate;
  ok_release release;
} ok_allocator_config;

typedef struct {
  ok_allocator_config allocators;
} ok_config;

#endif // OK_CONFIG_H
