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
  p_gc->guarded = NULL_AS_VALUE();
  p_gc->is_paused = false;
}

void gc_deinit(gc* p_gc) {
  grays_deinit(&p_gc->grays, &p_gc->raw_alloc_wrapper);
  p_gc->guarded = NULL_AS_VALUE();
  p_gc->compiler = NULL;
  p_gc->globals_store = NULL;
  p_gc->object_store = NULL;
  p_gc->vm = NULL;
  p_gc->allocated_bytes = 0;
  p_gc->next = 1024 * 1024; // 1mb
  p_gc->is_paused = true;
}

bool gc_guard_up(gc* p_gc, value p_value) {
  if (!IS_VALUE_NULL(p_gc->guarded)) {
    return false;
  }
  p_gc->guarded = p_value;
  return true;
}

void gc_guard_down(gc* p_gc) {
  p_gc->guarded = NULL_AS_VALUE();
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

static bool collect(gc* p_gc);
static bool trace(gc* p_gc);

bool gc_collect(gc* p_gc, size_t p_predict, size_t p_old) {
  p_gc->allocated_bytes += p_predict - p_old;

  if (p_gc->is_paused) {
    return true;
  }

  bool result = true;

#ifdef OK_AGRESSIVE_GC
  result = collect(p_gc);
#endif // OK_AGRESSIVE_GC

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
  if (IS_VALUE_OBJECT(p_value) && !mark_object(p_gc, VALUE_AS_OBJECT(p_value))) {
    return false;
  }
  return true;
}

static bool mark_vm(gc* p_gc) {
  vm* vm = p_gc->vm;
  for (uint32_t i = 0; i < vm->stack.top; ++i) {
    if (!mark_value(p_gc, vm->stack.array.data[i])) {
      return false;
    }
  }
  for (uint32_t i = 0; i < vm->call_stack.count; ++i) {
    if (!mark_object(p_gc, (object*)vm->call_stack.data[i].closure)) {
      return false;
    }
  }
  for (object_upvalue* up = vm->open_upvalues; up != NULL; up = up->next) {
    if (!mark_object(p_gc, (object*)up)) {
      return false;
    }
  }
  return true;
}

static bool mark_compiler(gc* p_gc) {
  compiler* compiler = p_gc->compiler;
  for (uint32_t i = 0; i < compiler->functions.count; i++) {
    if (!mark_object(p_gc, (object*)compiler->functions.data[i].function.function)) {
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
  if (!mark_value(p_gc, p_gc->guarded)) {
    return false;
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

bool collect(gc* p_gc) {
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
  }
  return true;
}

static bool trace(gc* p_gc) {
  while (p_gc->grays.count > 0) {
    uint32_t index = p_gc->grays.count - 1;
    object* obj = p_gc->grays.data[index];
    if (!trace_object(p_gc, obj)) {
      return false;
    }
    if (!grays_remove(&p_gc->grays, index, index)) {
      return false;
    }
  }
  return true;
}
