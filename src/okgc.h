#ifndef OK_GC_H
#define OK_GC_H

#include "okfwd.h"
#include "okvalue.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef OK_GC_EXPOSE
#include "okarray.h"
ARRAY_DECLARE_DEFAULT(grays, object*)
ARRAY_DECLARE_DEFAULT(gc_root_freelist, uint32_t)
#define OK_GC_GUARD_CAPACITY 16
struct gc {
  size_t allocated_bytes;
  size_t next;
  vm* vm;
  compiler* compiler;
  object_store* object_store;
  globals_store* globals_store;
  grays grays;
  // embedder-owned strong references (ok_root). unlike the guard stack these
  // are long lived and unordered, so: growable (raw allocators), null-slot + freelist recycling.
  value_array roots;
  gc_root_freelist root_freelist;
  value guarded[OK_GC_GUARD_CAPACITY];
  uint32_t guard_count;
  allocators* alloc;
  allocators raw_alloc_wrapper;
  bool is_paused;
  // true only while trace() drains the gray stack: the window in which
  // ok_gc_mark_value (from ok_native_tracer callbacks) is legal.
  bool is_tracing;
};
#endif // OK_GC_EXPOSE

void gc_init(gc* p_gc, object_store* p_heap, allocators* p_alloc);
void gc_deinit(gc* p_gc);

bool gc_collect(gc* p_gc, const size_t p_predict, const size_t p_old);
bool gc_guard_up(gc* p_gc, value p_value);
void gc_guard_down(gc* p_gc);

// embedder rooting: ids are slot + 1, 0 is invalid, slots recycle via freelist.
uint32_t gc_root(gc* p_gc, value p_value);
bool gc_unroot(gc* p_gc, uint32_t p_root);

// ok_gc_mark_value's engine: refuses to run outside a trace (stray gray
// entries would be walked by the next collect against swept objects).
bool gc_mark_value_user(gc* p_gc, value p_value);
void gc_pause(gc* p_gc);
void gc_resume(gc* p_gc);

void gc_watch_vm(gc* p_gc, vm* p_vm);
void gc_watch_compiler(gc* p_gc, compiler* p_compiler);
void gc_watch_globals(gc* p_gc, globals_store* p_globals);

#endif // OK_GC_H
