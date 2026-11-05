#ifndef OK_MM_H
#define OK_MM_H

#include <stddef.h>
#include <stdint.h>

#include "okfwd.h"

typedef void* (*reallocate)(allocators* p_alloc, void* p_ptr, const size_t p_old_size, const size_t p_new_size);
typedef void* (*allocate)(allocators* p_alloc, const size_t p_size);
typedef void (*release)(allocators* p_alloc, void* p_ptr);

struct allocators {
  reallocate reallocate;
  allocate allocate;
  release release;
  gc* gc;
  ok_allocators* raw_allocators;
  // the impl lane (P5): allocator vtables with their own backing store -
  // the parse arena - reach it through here. NULL on the plain wrappers.
  void* impl;
};

allocators create_allocators(ok_allocators* p_alloc);

allocators create_raw_allocators_wrapper(ok_allocators* p_alloc);

#endif // OK_MM_H
