#ifndef OK_GC_H
#define OK_GC_H

#include "okfwd.h"
#include "okvalue.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef OK_GC_EXPOSE
#include "okarray.h"
ARRAY_DECLARE_DEFAULT(grays, object*)
struct gc {
  size_t allocated_bytes;
  size_t next;
  vm* vm;
  compiler* compiler;
  object_store* object_store;
  globals_store* globals_store;
  grays grays;
  value guarded;
  allocators* alloc;
  allocators raw_alloc_wrapper;
  bool is_paused;
};
#endif // OK_GC_EXPOSE

void gc_init(gc* p_gc, object_store* p_heap, allocators* p_alloc);
void gc_deinit(gc* p_gc);

bool gc_collect(gc* p_gc, const size_t p_predict, const size_t p_old);
bool gc_guard_up(gc* p_gc, value p_value);
void gc_guard_down(gc* p_gc);
void gc_pause(gc* p_gc);
void gc_resume(gc* p_gc);

void gc_watch_vm(gc* p_gc, vm* p_vm);
void gc_watch_compiler(gc* p_gc, compiler* p_compiler);
void gc_watch_globals(gc* p_gc, globals_store* p_globals);

#endif // OK_GC_H
