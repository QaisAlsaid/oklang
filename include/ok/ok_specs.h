#ifndef OK_SPECS_H
#define OK_SPECS_H

#include "ok_fwd.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef void* (*ok_reallocate)(void* p_ptr, size_t p_new_size);
typedef void* (*ok_allocate)(size_t p_size);
typedef void (*ok_release)(void* p_ptr);

struct ok_allocators {
  ok_reallocate reallocate;
  ok_allocate allocate;
  ok_release release;
};

struct ok_specs {
  ok_allocators allocators;
};

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_SPECS_H
