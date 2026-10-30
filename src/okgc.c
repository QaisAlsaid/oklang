#include <stdio.h>

#define OK_GC_EXPOSE
#include "okgc.h"

#include "okcompiler.h"
#include "okglobals_store.h"
#include "okobject_store.h"
#include "okvalue.h"
#include "okvm.h"

#if defined(OK_PARANOID)
#define OK_LOG_GC
#endif

#define GC_INITIAL_THRESHOLD (1024 * 1024)
#define GC_GROWTH_FACTOR 2

ARRAY_DEFINE_DEFAULT(grays, object*, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(gc_root_freelist, uint32_t, ARRAY_DEFAULT_TYPE_DEINIT)

static bool collect(gc* p_gc);
static bool trace(gc* p_gc);
static bool mark_value(gc* p_gc, value p_value);

void gc_init(gc* p_gc, object_store* p_heap, allocators* p_alloc) {
  p_gc->compiler = NULL;
  p_gc->globals_store = NULL;
  p_gc->object_store = p_heap;
  p_gc->vm = NULL;
  p_gc->alloc = p_alloc;
  p_gc->raw_alloc_wrapper = create_raw_allocators_wrapper(p_alloc->raw_allocators);
  p_gc->allocated_bytes = 0;
  p_gc->next = 1024 * 1024; // 1mb
  grays_init(&p_gc->grays, &p_gc->raw_alloc_wrapper);
  // raw allocator on purpose: rooting must never itself trigger the collector.
  value_array_init(&p_gc->roots, &p_gc->raw_alloc_wrapper);
  gc_root_freelist_init(&p_gc->root_freelist, &p_gc->raw_alloc_wrapper);
  for (uint32_t i = 0; i < OK_GC_GUARD_CAPACITY; ++i) {
    p_gc->guarded[i] = NULL_AS_VALUE();
  }
  p_gc->guard_count = 0;
  p_gc->is_paused = false;
  p_gc->is_tracing = false;
}

void gc_deinit(gc* p_gc) {
  grays_deinit(&p_gc->grays, &p_gc->raw_alloc_wrapper);
  value_array_deinit(&p_gc->roots, &p_gc->raw_alloc_wrapper);
  gc_root_freelist_deinit(&p_gc->root_freelist, &p_gc->raw_alloc_wrapper);
  for (uint32_t i = 0; i < OK_GC_GUARD_CAPACITY; ++i) {
    p_gc->guarded[i] = NULL_AS_VALUE();
  }
  p_gc->guard_count = 0;
  p_gc->compiler = NULL;
  p_gc->globals_store = NULL;
  p_gc->object_store = NULL;
  p_gc->vm = NULL;
  p_gc->allocated_bytes = 0;
  p_gc->next = 1024 * 1024; // 1mb
  p_gc->is_paused = true;
  p_gc->is_tracing = false;
}

bool gc_guard_up(gc* p_gc, value p_value) {
  // lifo stack: nested pairs compose. overflow is a programming error the
  // caller must treat as "could not root" (returns false), never ignore.
  if (p_gc->guard_count >= OK_GC_GUARD_CAPACITY) {
    return false;
  }
  p_gc->guarded[p_gc->guard_count++] = p_value;
  return true;
}

void gc_guard_down(gc* p_gc) {
  // underflow is a no-op rather than releasing someone else's guard
  if (p_gc->guard_count > 0) {
    p_gc->guard_count--;
    p_gc->guarded[p_gc->guard_count] = NULL_AS_VALUE();
  }
}

uint32_t gc_root(gc* p_gc, value p_value) {
  // caller rejects strictly-null values: a null slot is the free marker.
  if (p_gc->root_freelist.count > 0) {
    const uint32_t slot = p_gc->root_freelist.data[p_gc->root_freelist.count - 1];
    gc_root_freelist_pop(&p_gc->root_freelist);
    p_gc->roots.data[slot] = p_value;
    return slot + 1;
  }
  // the roots array uses the raw allocator: appending cannot collect, so
  // p_value cannot be swept between entry and publication.
  if (!value_array_append(&p_gc->roots, p_value)) {
    return 0;
  }
  return p_gc->roots.count;
}

bool gc_unroot(gc* p_gc, uint32_t p_root) {
  if (p_root == 0 || p_root > p_gc->roots.count) {
    return false;
  }
  value* slot = &p_gc->roots.data[p_root - 1];
  if (IS_VALUE_STRICTLY_NULL(*slot)) {
    return false; // already freed (double unroot)
  }
  *slot = NULL_AS_VALUE();
  return gc_root_freelist_append(&p_gc->root_freelist, p_root - 1);
}

bool gc_mark_value_user(gc* p_gc, value p_value) {
  if (!p_gc->is_tracing) {
    return false;
  }
  return mark_value(p_gc, p_value);
}

void gc_pause(gc* p_gc) {
  p_gc->is_paused = true;
}

void gc_resume(gc* p_gc) {
  p_gc->is_paused = false;
}

void gc_watch_vm(gc* p_gc, vm* p_vm) {
  p_gc->vm = p_vm;
}

void gc_watch_compiler(gc* p_gc, compiler* p_compiler) {
  p_gc->compiler = p_compiler;
}

void gc_watch_globals(gc* p_gc, globals_store* p_globals) {
  p_gc->globals_store = p_globals;
}

bool gc_collect(gc* p_gc, size_t p_predict, size_t p_old) {
  p_gc->allocated_bytes += p_predict - p_old;

  if (p_gc->is_paused) {
    return true;
  }

  bool result = true;

#ifdef OK_AGGRESSIVE_GC
  result = collect(p_gc);
#endif // OK_AGGRESSIVE_GC

  if (p_gc->allocated_bytes > p_gc->next) {
    result = collect(p_gc);
  }

  return result;
}

static bool mark_object(gc* p_gc, object* p_object) {
  if (p_object == NULL || object_is_marked(p_object)) {
    return true;
  }
  object_set_marked(p_object, true);
#ifdef OK_LOG_GC
  printf("marked object: %p ", p_object);
  object_debug_print(p_object);
  puts("\n");
#endif
  return grays_append(&p_gc->grays, p_object);
}

static bool mark_value(gc* p_gc, value p_value) {
  // strict check is required here: marked values come from registers, constants,
  // globals, closed upvalues. and raw doubles whose payload bits alias the
  // tag space (e.g. 19.0, 23.0, 27.0...) would otherwise be treated as object
  // pointers and crash the marker (found via OK_AGGRESSIVE_GC on *_long tests).
  if (IS_VALUE_STRICTLY_OBJECT(p_value) && !mark_object(p_gc, VALUE_AS_OBJECT(p_value))) {
    return false;
  }
  return true;
}

static bool mark_vm(gc* p_gc) {
  vm* vm = p_gc->vm;
  execution_state* exe = vm->current_execution_state;
  for (uint32_t i = 0; i < exe->registers.top; ++i) {
    if (!mark_value(p_gc, exe->registers.array.data[i])) {
      return false;
    }
  }
  for (uint32_t i = 0; i < exe->call_stack.count; ++i) {
    if (!mark_object(p_gc, (object*)exe->call_stack.data[i].closure)) {
      return false;
    }
  }
  for (object_upvalue* up = exe->open_upvalues; up != NULL; up = up->next) {
    if (!mark_object(p_gc, (object*)up)) {
      return false;
    }
  }
  return true;
}

static bool mark_compiler(gc* p_gc) {
  compiler* compiler = p_gc->compiler;
  for (uint32_t i = 0; i < compiler->contexts.count; i++) {
    if (!mark_object(p_gc, (object*)compiler->contexts.data[i].function.function)) {
      return false;
    }
  }
  return true;
}

static bool mark_globals_store(gc* p_gc) {
  for (uint32_t i = 0; i < p_gc->globals_store->global_values.count; ++i) {
    if (!mark_value(p_gc, p_gc->globals_store->global_values.data[i])) {
      return false;
    }
  }
  return true;
}

static bool mark(gc* p_gc) {
  if (p_gc->vm != NULL && !mark_vm(p_gc)) {
    return false;
  }
  if (p_gc->compiler != NULL && !mark_compiler(p_gc)) {
    return false;
  }
  if (p_gc->globals_store != NULL && !mark_globals_store(p_gc)) {
    return false;
  }
  for (uint32_t i = 0; i < p_gc->guard_count; ++i) {
    if (!mark_value(p_gc, p_gc->guarded[i])) {
      return false;
    }
  }
  for (uint32_t i = 0; i < p_gc->roots.count; ++i) {
    // freed slots hold tagged null and mark_value's strict check skips them.
    if (!mark_value(p_gc, p_gc->roots.data[i])) {
      return false;
    }
  }
  return true;
}

static bool mark_value_array(gc* p_gc, value_array* p_array) {
  for (uint32_t i = 0; i < p_array->count; ++i) {
    if (!mark_value(p_gc, p_array->data[i])) {
      return false;
    }
  }
  return true;
}

static bool sweep(gc* p_gc) {
  if (p_gc->object_store == NULL) {
    return false;
  }
  object* prev = NULL;
  object* curr = p_gc->object_store->objects;
  while (curr != NULL) {
    if (object_is_marked(curr)) {
      object_set_marked(curr, false);
      prev = curr;
      curr = curr->next;
    } else {
      object* unreached = curr;
      curr = curr->next;
      if (prev != NULL) {
        prev->next = curr;
      } else {
        p_gc->object_store->objects = curr;
      }
#ifdef OK_LOG_GC
      printf("sweeping object: %p: ", unreached);
      object_debug_print(unreached);
      puts("\n");
#endif // OK_LOG_GC
      object_specs specs = {p_gc->alloc, p_gc->object_store};
      object_dispatch_deinit(unreached, &specs);
      p_gc->alloc->release(p_gc->alloc, unreached);
    }
  }
  return true;
}

static bool collect(gc* p_gc) {
#ifdef OK_LOG_GC
  size_t previous = p_gc->allocated_bytes;
  printf("--- gc ---\n");
#endif // OK_LOG_GC

  bool res = mark(p_gc);
  res = res && trace(p_gc);
  res = res && sweep(p_gc);
  p_gc->next = p_gc->allocated_bytes * GC_GROWTH_FACTOR;

  if (p_gc->next < GC_INITIAL_THRESHOLD) {
    p_gc->next = GC_INITIAL_THRESHOLD;
  }
#ifdef OK_LOG_GC
  size_t reclaimed = previous - p_gc->allocated_bytes;
  if (reclaimed > 0) {
    printf("          reclaimed %zu bytes (from %zu to %zu) next at %zu\n",
           reclaimed,
           previous,
           p_gc->allocated_bytes,
           p_gc->next);
  }
  printf("--- gc ---\n");
#endif // OK_LOG_GC
  return res;
}

static bool trace_object(gc* p_gc, object* p_object) {
#ifdef OK_LOG_GC
  printf("tracing object: %p ", p_object);
  object_debug_print(p_object);
  puts("\n");
#endif // OK_LOG_GC
  switch (object_get_type(p_object)) {
  case OBJ_STRING: {
    break;
  }
  case OBJ_FUNCTION: {
    object_function* fu = (object_function*)p_object;
    if (!mark_object(p_gc, (object*)fu->name) || !mark_value_array(p_gc, &fu->chunk.constants)) {
      return false;
    }
    break;
  }
  case OBJ_CLOSURE: {
    object_closure* closure = (object_closure*)p_object;
    if (!mark_object(p_gc, (object*)closure->function)) {
      return false;
    }
    for (uint32_t i = 0; i < closure->upvalues.count; ++i) {
      if (!mark_object(p_gc, (object*)closure->upvalues.data[i])) {
        return false;
      }
    }
    break;
  }
  case OBJ_UPVALUE: {
    if (!mark_value(p_gc, ((object_upvalue*)p_object)->closed)) {
      return false;
    }
    break;
  }
  case OBJ_NATIVE_VALUE: {
    // the embedder's structure is opaque to the collector: the tracer walks
    // it and reports every ok_value it still holds through ok_gc_mark_value.
    object_native_value* native = (object_native_value*)p_object;
    if (native->tracer != NULL && p_gc->vm != NULL) {
      if (!native->tracer(p_gc->vm->ok, native->user_data)) {
        return false;
      }
    }
    break;
  }
  case OBJ_ARRAY: {
    if (!mark_value_array(p_gc, &((object_array*)p_object)->values)) {
      return false;
    }
    break;
  }
  }
  return true;
}

static bool trace(gc* p_gc) {
  bool res = true;
  p_gc->is_tracing = true;
  while (res && p_gc->grays.count > 0) {
    uint32_t index = p_gc->grays.count - 1;
    object* obj = p_gc->grays.data[index];
    res = trace_object(p_gc, obj);
    res = res && grays_remove(&p_gc->grays, index, index);
  }
  p_gc->is_tracing = false;
  return res;
}
