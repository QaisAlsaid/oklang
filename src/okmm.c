#include <stdlib.h>

#include "okmm.h"

#include "ok/ok_specs.h"
#include "okgc.h"
#include <stdio.h>

void release_impl(allocators* p_alloc, void* p_ptr) {
  return p_alloc->raw_allocators->release(p_ptr);
}

void* reallocate_impl(allocators* p_alloc, void* p_ptr, const size_t p_old_size, const size_t p_new_size) {
  if (p_new_size == 0) {
    release_impl(p_alloc, p_ptr);
    return NULL;
  }
  if (p_alloc->gc != NULL && !gc_collect(p_alloc->gc, p_new_size, p_old_size)) {
    return NULL;
  }
  void* alloc = p_alloc->raw_allocators->reallocate(p_ptr, p_new_size);
  return alloc;
}

void* allocate_impl(allocators* p_alloc, const size_t p_size) {
  if (p_alloc->gc != NULL && !gc_collect(p_alloc->gc, p_size, 0)) {
    return NULL;
  }
  return p_alloc->raw_allocators->allocate(p_size);
}

allocators create_allocators(ok_allocators* p_alloc) {
  allocators alloc;
  alloc.raw_allocators = p_alloc;
  alloc.reallocate = reallocate_impl;
  alloc.allocate = allocate_impl;
  alloc.release = release_impl;
  alloc.gc = NULL;
  return alloc;
}

void release_wrapper_impl(allocators* p_alloc, void* p_ptr) {
  return p_alloc->raw_allocators->release(p_ptr);
}

void* reallocate_wrapper_impl(allocators* p_alloc, void* p_ptr, const size_t p_old_size, const size_t p_new_size) {
  if (p_new_size == 0) {
    release_impl(p_alloc, p_ptr);
    return NULL;
  }
  void* alloc = p_alloc->raw_allocators->reallocate(p_ptr, p_new_size);
  return alloc;
}

void* allocate_wrapper_impl(allocators* p_alloc, const size_t p_size) {
  return p_alloc->raw_allocators->allocate(p_size);
}

allocators create_raw_allocators_wrapper(ok_allocators* p_alloc) {
  allocators alloc;
  alloc.raw_allocators = p_alloc;
  alloc.reallocate = reallocate_wrapper_impl;
  alloc.allocate = allocate_wrapper_impl;
  alloc.release = release_wrapper_impl;
  alloc.gc = NULL;
  return alloc;
}
