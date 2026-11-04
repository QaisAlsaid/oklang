#include <stdio.h>

#define OK_GC_EXPOSE
#include "okgc.h"

#include "okcompiler.h"
#include "okglobals_store.h"
#include "okmodules.h"
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
  p_gc->interner = NULL;
  p_gc->modules = NULL;
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
  p_gc->interner = NULL;
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

void gc_watch_interner(gc* p_gc, string_interner* p_interner) {
  p_gc->interner = p_interner;
}

void gc_watch_modules(gc* p_gc, module_system* p_modules) {
  p_gc->modules = p_modules;
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
  printf("marked object: %p ", (void*)p_object);
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
  // a pending defer holds its thunk closure in the payload. the slot is a
  // live register from mark's point of view, so the closure must root like
  // any other object reference (the defer tag is not OBJ_*-typed).
  if (IS_VALUE_STRICTLY_DEFER(p_value) && !mark_object(p_gc, (object*)VALUE_AS_DEFER(p_value))) {
    return false;
  }
  return true;
}

static bool mark_execution_state(gc* p_gc, execution_state* exe) {
  // a script-thrown value sits in the error lane from raise until the host
  // clears it (ok_last_error keeps it readable past the failed run/call).
  // across C boundaries where allocation happens, so it must root like a
  // register (a thrown object surviving a collection mid-unwind).
  if (exe->has_error && exe->error_is_script) {
    if (!mark_value(p_gc, exe->error_value)) {
      return false;
    }
  }
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

static bool mark_vm(gc* p_gc) {
  vm* vm = p_gc->vm;
  execution_state* exe = vm->current_execution_state;
  // the hierarchy roots: object, type, and the cached lookup names are
  // referenced only here, so this mark is what keeps them immortal.
  if (!mark_object(p_gc, (object*)vm->object_singleton) || !mark_object(p_gc, (object*)vm->class_singleton) ||
      !mark_object(p_gc, (object*)vm->static_call_name) || !mark_object(p_gc, (object*)vm->init_method_name)) {
    return false;
  }
  for (uint32_t i = 0; i < CLASS_OP_COUNT; ++i) {
    // [CALL] aliases static_call_name; marking twice is a no-op either way.
    if (!mark_object(p_gc, (object*)vm->op_names[i])) {
      return false;
    }
  }
  // the builtin singletons root here exactly like object and type:
  // nothing else references them at startup, and every classof() answer
  // funnels through one of them.
  for (uint32_t i = 0; i < OK_BUILTIN_COUNT; ++i) {
    if (!mark_object(p_gc, (object*)vm->builtin_classes[i])) {
      return false;
    }
  }
  if (!mark_execution_state(p_gc, exe)) {
    return false;
  }
  // the parked resumers: while a resume runs a coroutine's state, the states
  // it suspended root here - their registers and call stacks hold live
  // values the coro still reads when it yields back (a swapped-out state is
  // invisible to the current-state walk above, this is its stand-in).
  for (execution_state* parked = vm->resume_parked; parked != NULL; parked = parked->next_parked) {
    if (!mark_execution_state(p_gc, parked)) {
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

static bool mark_value_array(gc* p_gc, value_array* p_array) {
  for (uint32_t i = 0; i < p_array->count; ++i) {
    if (!mark_value(p_gc, p_array->data[i])) {
      return false;
    }
  }
  return true;
}

// OR-1: the module registry's namespaces and the pending-roots window (the
// roots a TU built but its constants have not adopted yet). the canonical
// path strings and the namespace classes ride through the instances.
// OR-3: the native mount window's staged values ride here too (between
// api->module_export and the namespace freeze).
static bool mark_modules(gc* p_gc) {
  module_system* modules = p_gc->modules;
  for (uint32_t i = 0; i < modules->loaded.count; ++i) {
    if (!mark_value(p_gc, modules->loaded.data[i].namespace)) {
      return false;
    }
  }
  if (!mark_value_array(p_gc, &modules->pending_roots)) {
    return false;
  }
  if (modules->mounting != NULL && !mark_value_array(p_gc, &modules->mounting->values)) {
    return false;
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
  if (p_gc->modules != NULL && !mark_modules(p_gc)) {
    return false;
  }
  // the interner is deliberately NOT marked: it is a weak set (D20). its
  // dead entries are removed by sweep's pre-pass instead, and its live
  // entries stay alive through whatever else references them (chunk
  // constants, class tables, registers, host roots).
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

// the interner's dead-entry pass (D20): the table is a WEAK set - mark
// never walks it - so its unmarked entries must leave BEFORE the object walk
// frees them (a freed canonical string would dangle in a probe chain).
// removal is backward-shift: a shifted-in entry can land in the slot just
// examined, so the walk re-examines the same index after a removal. each
// removal decrements count, so the loop always terminates.
static bool sweep_interner(gc* p_gc) {
  string_interner* interner = p_gc->interner;
  uint32_t i = 0;
  while (i < interner->capacity) {
    object_string* entry = interner->slots[i];
    if (entry == NULL || object_is_marked((object*)entry)) {
      i += 1u;
      continue;
    }
    string_interner_remove_at(interner, i);
  }
  return true;
}

static bool sweep(gc* p_gc) {
  if (p_gc->object_store == NULL) {
    return false;
  }
  if (p_gc->interner != NULL && !sweep_interner(p_gc)) {
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
      printf("sweeping object: %p: ", (void*)unreached);
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
  printf("tracing object: %p ", (void*)p_object);
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
    // the per-site inline caches hold class values (and through them, the
    // frozen tables their raw entry pointers index): the chunk owns their
    // rooting, a class can outlive every register that named it. every way
    // of the probe chain roots its own key.
    for (uint32_t i = 0; i < fu->chunk.mic.count; ++i) {
      for (uint32_t w = 0; w < OK_MIC_WAYS; ++w) {
        if (!mark_value(p_gc, fu->chunk.mic.data[i].ways[w].klass)) {
          return false;
        }
      }
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
    // the closed snapshot, plus the owner pin: an open cell captured in a
    // coro keeps that coro (and its register file) alive through the
    // upvalue, exactly like a live register would
    object_upvalue* upvalue = (object_upvalue*)p_object;
    if (!mark_value(p_gc, upvalue->closed) || !mark_value(p_gc, upvalue->owner)) {
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
  case OBJ_MAP: {
    // walk occupied slots only, marking key and value
    // (both are gc refs, strings are heap objects). empty slots hold the strict-null key
    // sentinel, and their value slots are stale garbage from backward-shift
    // moves and overwrites: marking those could resurrect a swept object's
    // dangling pointer. the keys array is the occupancy authority.
    object_map* map = (object_map*)p_object;
    for (uint32_t i = 0; i < map->entries.capacity; ++i) {
      if (!IS_VALUE_STRICTLY_NULL(map->entries.keys[i])) {
        if (!mark_value(p_gc, map->entries.keys[i]) || !mark_value(p_gc, map->entries.values[i])) {
          return false;
        }
      }
    }
    break;
  }
  case OBJ_CLASS: {
    object_class* klass = (object_class*)p_object;
    // the name strings are shared with the defining chunk's constants, but a
    // class routinely outlives the frame that defined it: the class owns its
    // tables, so it traces every key it holds, shared or not.
    if (!mark_object(p_gc, (object*)klass->name) || !mark_object(p_gc, (object*)klass->parent)) {
      return false;
    }
    for (uint32_t i = 0; i < klass->own_names.count; ++i) {
      if (!mark_object(p_gc, (object*)klass->own_names.data[i])) {
        return false;
      }
    }
    for (uint32_t i = 0; i < klass->own_inits.count; ++i) {
      if (klass->own_inits.data[i] != NULL && !mark_object(p_gc, (object*)klass->own_inits.data[i])) {
        return false;
      }
    }
    // patch 49: the C-side declared defaults are values (immediates or refs)
    for (uint32_t i = 0; i < klass->own_defaults.count; ++i) {
      if (!mark_value(p_gc, klass->own_defaults.data[i])) {
        return false;
      }
    }
    for (uint32_t i = 0; i < klass->methods.capacity; ++i) {
      if (klass->methods.keys[i] != NULL) {
        if (!mark_object(p_gc, (object*)klass->methods.keys[i]) ||
            !mark_object(p_gc, (object*)klass->methods.values[i].name) ||
            (klass->methods.values[i].closure != NULL &&
             !mark_object(p_gc, (object*)klass->methods.values[i].closure))) {
          return false;
        }
      }
    }
    for (uint32_t i = 0; i < klass->own_static_names.count; ++i) {
      if (!mark_object(p_gc, (object*)klass->own_static_names.data[i])) {
        return false;
      }
    }
    if (!mark_value_array(p_gc, &klass->static_values)) {
      return false;
    }
    for (uint32_t i = 0; i < klass->statics.capacity; ++i) {
      if (klass->statics.keys[i] != NULL) {
        if (!mark_object(p_gc, (object*)klass->statics.keys[i]) ||
            !mark_object(p_gc, (object*)klass->statics.values[i].name) ||
            (klass->statics.values[i].closure != NULL &&
             !mark_object(p_gc, (object*)klass->statics.values[i].closure))) {
          return false;
        }
      }
    }
    for (uint32_t i = 0; i < klass->static_fields.capacity; ++i) {
      if (klass->static_fields.keys[i] != NULL && !mark_object(p_gc, (object*)klass->static_fields.keys[i])) {
        return false;
      }
    }
    for (uint32_t i = 0; i < klass->fields.capacity; ++i) {
      if (klass->fields.keys[i] != NULL && !mark_object(p_gc, (object*)klass->fields.keys[i])) {
        return false;
      }
    }
    break;
  }
  case OBJ_INSTANCE: {
    object_instance* inst = (object_instance*)p_object;
    if (!mark_object(p_gc, (object*)inst->klass)) {
      return false;
    }
    for (uint32_t i = 0; i < inst->klass->nfields; ++i) {
      if (!mark_value(p_gc, inst->fields[i])) {
        return false;
      }
    }
    break;
  }
  case OBJ_BOUND_METHOD: {
    // the entry lives inside the receiver's (or the receiver class's) frozen
    // table: marking the receiver roots the table that owns the entry.
    if (!mark_value(p_gc, ((object_bound_method*)p_object)->receiver)) {
      return false;
    }
    break;
  }
  case OBJ_CORO: {
    // a parked (or ready) coroutine: the body closure, plus the whole owned
    // execution state walked exactly like a live vm's - registers, call
    // stack closures, open upvalues, the error lane. a RUNNING coro's state
    // is the vm's current one (marked as itself, twice-marked is a no-op).
    object_coro* coro = (object_coro*)p_object;
    if (!mark_object(p_gc, (object*)coro->body)) {
      return false;
    }
    if (coro->state != NULL && !mark_execution_state(p_gc, coro->state)) {
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
