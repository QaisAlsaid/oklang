#include <stdio.h>
#include <stdlib.h>

#define OK_GC_EXPOSE
#include "ok/ok.h"
#include "okcompiler.h"
#include "okdebug.h"
#include "okgc.h"
#include "okobject.h"
#include "okobject_store.h"
#include "okparser.h"
#include "okspecs.h"
#include "okutils.h"
#include "okvalue.h"
#include "okvm.h"

struct ok {
  object_store objects_store;
  globals_store globals_store;
  string_interner interner;
  compiler compiler;
  vm vm;
  gc gc;
  ok_source source;
  allocators alloc;
  ok_allocators raw_alloc;
  bool dump_bytecode;
  bool dump_skip_run;
};

// the vm's natives receive the ok* per the native ABI and reach the vm through
// here (the struct above is this file's own business)
vm* ok_vm(ok* p_ok) {
  return &p_ok->vm;
}

// the core builtins are receiver-first native functions: `len(a)` and
// `a.len()` are one C entry, mounted twice (free function + method on the
// array/map class singletons). str carries no methods in v0.5.0 - nothing
// existed as a str function builtin to mirror, and the ruling is no new stdlib.
// errors go through the supported path ok_native_error -> vm_native_error.

static ok_value native_len(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (IS_VALUE_ARRAY(p_argv[0])) {
    // f64, never an int, numbers are the only public numeric type
    return NUMBER_AS_VALUE((double)VALUE_AS_ARRAY(p_argv[0])->values.count);
  }
  if (IS_VALUE_MAP(p_argv[0])) {
    return NUMBER_AS_VALUE((double)VALUE_AS_MAP(p_argv[0])->entries.count);
  }
  ok_native_error(p_ok,
                  ok_create_string_view(
                      "invalid argument: 'len' expects an array or a map.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
  return NULL_AS_VALUE();
}

// classof(v) (C12): the one class every value is an instance of. never
// fails - the answer is total over the value space.
static ok_value native_classof(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  return OBJECT_AS_VALUE(vm_classof(&p_ok->vm, p_argv[0]));
}

// isinstance(v, C) (C12): the chain walk - true when C is classof(v) or one
// of its ancestors (object at the root matches everything, including null
// through nil). the exact-class check is `classof(v) == C` or the `is`
// operator; this is the family question.
static ok_value native_isinstance(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_CLASS(p_argv[1])) {
    ok_native_error(p_ok,
                    ok_create_string_view(
                        "invalid argument: 'isinstance' expects a class.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return BOOL_AS_VALUE(vm_is_instance(&p_ok->vm, p_argv[0], VALUE_AS_CLASS(p_argv[1])));
}

// get(m, k) / get(m, k, default)
// absent yields null (or the given default), where [] hard-errors.
// the key rules are the map's: null and unhashable keys error, only absence is lax.
static ok_value native_get(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc < 1) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid call to 'get': expected 2 arguments (map, key) or 3 (map, key, default).",
                              OK_STRING_VIEW_CALCULATE_LENGTH,
                              true));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_MAP(p_argv[0])) {
    ok_native_error(
        p_ok, ok_create_string_view("invalid argument: 'get' expects a map.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (p_argc != 2 && p_argc != 3) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid call to 'get': expected 2 arguments (map, key) or 3 (map, key, default).",
                              OK_STRING_VIEW_CALCULATE_LENGTH,
                              true));
    return NULL_AS_VALUE();
  }
  if (IS_VALUE_STRICTLY_NULL(p_argv[1])) {
    ok_native_error(p_ok, ok_create_string_view("map key cannot be null.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (!map_key_is_legal(p_argv[1])) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid map key: only strings, numbers and booleans can be keys.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  value* slot = value_map_get(&VALUE_AS_MAP(p_argv[0])->entries, p_argv[1]);
  if (slot != NULL) {
    return *slot;
  }
  return p_argc == 3 ? p_argv[2] : NULL_AS_VALUE();
}

static ok_value native_push(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'push' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  object_array* arr = VALUE_AS_ARRAY(p_argv[0]);
  // no gc guard needed here: the array is rooted through the caller's argument
  // window [0, top), and value_array_append only reallocs the array's
  // own buffer. it never triggers a collection. do not "fix" this in the future.
  if (!value_array_append(&arr->values, p_argv[1])) {
    ok_native_error(
        p_ok, ok_create_string_view("out of memory: failed to grow array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  return NULL_AS_VALUE(); // inert
}

static ok_value native_pop(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid argument: 'pop' expects an array.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  object_array* arr = VALUE_AS_ARRAY(p_argv[0]);
  if (arr->values.count == 0) {
    return NULL_AS_VALUE(); // pop on empty is null.
  }
  const value popped = arr->values.data[arr->values.count - 1];
  value_array_pop(&arr->values); // does not shrink capacity.
  return popped;
}

// remove: arrays take the half open range remove(a, m, n); maps take the key remove(m, k).
// removing an absent map key is a legal no-op (the array form's empty range is legal for the same reason).
static ok_value native_remove(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc < 1) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid argument: 'remove' expects an array or a map.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  if (IS_VALUE_MAP(p_argv[0])) {
    if (p_argc != 2) {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid call to 'remove' on a map: expected 2 arguments (map, key).",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
    if (IS_VALUE_STRICTLY_NULL(p_argv[1])) {
      ok_native_error(p_ok, ok_create_string_view("map key cannot be null.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
      return NULL_AS_VALUE();
    }
    if (!map_key_is_legal(p_argv[1])) {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid map key: only strings, numbers and booleans can be keys.",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
    object_map_remove(VALUE_AS_MAP(p_argv[0]), p_argv[1]); // absent = no-op
    return NULL_AS_VALUE();
  }
  if (p_argc != 3) {
    ok_native_error(
        p_ok,
        ok_create_string_view("invalid call to 'remove' on an array: expected 3 arguments (array, start, end).",
                              OK_STRING_VIEW_CALCULATE_LENGTH,
                              true));
    return NULL_AS_VALUE();
  }
  if (!IS_VALUE_ARRAY(p_argv[0])) {
    ok_native_error(p_ok,
                    ok_create_string_view("invalid argument: 'remove' expects an array or a map.",
                                          OK_STRING_VIEW_CALCULATE_LENGTH,
                                          true));
    return NULL_AS_VALUE();
  }
  uint32_t range[2] = {0, 0};
  for (uint8_t i = 1; i < 3; ++i) {
    const value v = p_argv[i];
    if (IS_VALUE_NUMBER(v)) {
      const double d = VALUE_AS_NUMBER(v);
      // cast based integrality. the range guard runs first
      if (!(d >= MIN_SAFE_INTEGER && d <= MAX_SAFE_INTEGER) || d != (double)(int64_t)d) {
        ok_native_error(p_ok,
                        ok_create_string_view("invalid range for 'remove': bounds must be whole numbers.",
                                              OK_STRING_VIEW_CALCULATE_LENGTH,
                                              true));
        return NULL_AS_VALUE();
      }
      if (d < 0 || d > (double)UINT32_MAX) {
        ok_native_error(p_ok,
                        ok_create_string_view("invalid range for 'remove'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL_AS_VALUE();
      }
      range[i - 1] = (uint32_t)d;
    } else if (IS_VALUE_INT(v)) {
      const int64_t iv = VALUE_AS_INT(v);
      if (iv < 0 || iv > (int64_t)UINT32_MAX) {
        ok_native_error(p_ok,
                        ok_create_string_view("invalid range for 'remove'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
        return NULL_AS_VALUE();
      }
      range[i - 1] = (uint32_t)iv;
    } else {
      ok_native_error(p_ok,
                      ok_create_string_view("invalid range for 'remove': bounds must be numbers.",
                                            OK_STRING_VIEW_CALCULATE_LENGTH,
                                            true));
      return NULL_AS_VALUE();
    }
  }
  object_array* arr = VALUE_AS_ARRAY(p_argv[0]);
  if (range[0] > range[1] || range[1] > arr->values.count) {
    ok_native_error(p_ok, ok_create_string_view("invalid range for 'remove'.", OK_STRING_VIEW_CALCULATE_LENGTH, true));
    return NULL_AS_VALUE();
  }
  if (range[0] < range[1]) {                                  // empty range is a legal
    value_array_remove(&arr->values, range[0], range[1] - 1); // inclusive end underlying impl
  }
  return NULL_AS_VALUE();
}

static bool ok_define_core_natives(ok* p_ok) {
  bool status = true;
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" len", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_len);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" push", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_push);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" pop", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_pop);
  // ' remove' and ' get' validate argc themselves.
  // remove is 3 for arrays / 2 for maps, get is 2 or 3
  status &= ok_global_define_native_function(p_ok,
                                             ok_create_string_view(" remove", OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                             OK_NATIVE_ARITY_ANY,
                                             native_remove);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" get", OK_STRING_VIEW_CALCULATE_LENGTH, true), OK_NATIVE_ARITY_ANY, native_get);
  // the class-surface builtins (C12): classof/isinstance are plain native
  // functions like the rest, rebindable like the rest. the `is` operator and
  // `===` are the primitive lanes that never consult bindings.
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" classof", OK_STRING_VIEW_CALCULATE_LENGTH, true), 1, native_classof);
  status &= ok_global_define_native_function(
      p_ok, ok_create_string_view(" isinstance", OK_STRING_VIEW_CALCULATE_LENGTH, true), 2, native_isinstance);
  return status;
}

// the natives-as-classes step: mount the method surfaces on the builtin
// singletons, then seal them. the C handlers are the SAME functions the
// free-function spellings use - they were receiver-first all along
// (`len(x)` always took its receiver as argv[0]) - so `len(a)` and `a.len()`
// are one entry, and the "no new stdlib" ruling holds by construction:
// only what already existed as a function builtin gets a method spelling
// (array: len/push/pop/remove; map: len/remove/get; str/function/num/bool/
// nil carry nothing in v0.5.0). the sealing happens here, after the tables
// fill - which is why vm_init_singletons leaves the builtins unfrozen.
static bool ok_define_builtin_surfaces(ok* p_ok) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  struct {
    builtin_class_id klass;
    const char* name;
    uint16_t arity; // counts the receiver
    ok_native_fn fn;
  } const methods[] = {
      {OK_BUILTIN_ARRAY, "len", 1, native_len},
      {OK_BUILTIN_ARRAY, "push", 2, native_push},
      {OK_BUILTIN_ARRAY, "pop", 1, native_pop},
      {OK_BUILTIN_ARRAY, "remove", OK_NATIVE_ARITY_ANY, native_remove},
      {OK_BUILTIN_MAP, "len", 1, native_len},
      {OK_BUILTIN_MAP, "remove", OK_NATIVE_ARITY_ANY, native_remove},
      {OK_BUILTIN_MAP, "get", OK_NATIVE_ARITY_ANY, native_get},
  };
  bool status = true;
  for (uint32_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
    status &= object_class_define_native_method(p_ok->vm.builtin_classes[methods[i].klass],
                                                ok_create_string_view(methods[i].name, OK_STRING_VIEW_CALCULATE_LENGTH, true),
                                                methods[i].arity,
                                                methods[i].fn,
                                                /*p_pub*/ true,
                                                &specs);
  }
  // the coro surface (R20-R24): the native static `operator ()`
  // (construction), `resume`, and `status`, mounted before the seal like
  // every other builtin's methods
  status &= vm_define_coro_surface(&p_ok->vm);
  for (uint32_t i = 0; i < OK_BUILTIN_COUNT; ++i) {
    // the seal (C15): from here the builtin tables are frozen like every
    // script class's, which is what the fast lanes depend on.
    status &= object_class_freeze(p_ok->vm.builtin_classes[i]);
  }
  return status;
}

// the seeding table: ONE list both sides walk, so the visible-name array and
// the value array can never drift apart (the old two hand-built arrays were
// position-paired by discipline alone). natives pull their hidden " name"
// global (the space prefix is what hides them); builtin classes pull the vm
// singletons; object and type ride the same walk.
typedef enum {
  SEED_NATIVE,       // the value is hidden-global " <name>" (a native fn)
  SEED_BUILTIN_CLASS, // the value is vm->builtin_classes[class_id]
  SEED_OBJECT,       // the root singleton
  SEED_CLASS,        // the metaclass singleton
} seed_kind;

typedef struct {
  const char* name;   // the visible top-level binding
  seed_kind kind;
  builtin_class_id class_id; // SEED_BUILTIN_CLASS only, else OK_BUILTIN_COUNT
} seed_entry;

static const seed_entry top_level_seeds[] = {
    {"len", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"push", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"pop", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"remove", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"get", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"isinstance", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"classof", SEED_NATIVE, OK_BUILTIN_COUNT},
    {"str", SEED_BUILTIN_CLASS, OK_BUILTIN_STR},
    {"num", SEED_BUILTIN_CLASS, OK_BUILTIN_NUM},
    {"bool", SEED_BUILTIN_CLASS, OK_BUILTIN_BOOL},
    {"array", SEED_BUILTIN_CLASS, OK_BUILTIN_ARRAY},
    {"map", SEED_BUILTIN_CLASS, OK_BUILTIN_MAP},
    {"function", SEED_BUILTIN_CLASS, OK_BUILTIN_FUNCTION},
    {"nil", SEED_BUILTIN_CLASS, OK_BUILTIN_NIL},
    {"coro", SEED_BUILTIN_CLASS, OK_BUILTIN_CORO},
    {"object", SEED_OBJECT, OK_BUILTIN_COUNT},
    {"type", SEED_CLASS, OK_BUILTIN_COUNT},
};

static top_level_declarations ok_get_top_level_declarations(ok* p_ok, bool* p_error) {
  top_level_declarations decls;
  top_level_declarations_init(&decls, &p_ok->alloc);
  for (uint32_t i = 0; i < sizeof(top_level_seeds) / sizeof(top_level_seeds[0]); ++i) {
    variable_declaration decl = {
        ok_create_string_view(top_level_seeds[i].name, OK_STRING_VIEW_CALCULATE_LENGTH, true)};
    if (!top_level_declarations_append(&decls, decl)) {
      *p_error = true;
      break;
    }
  }
  return decls;
}

static value_array ok_get_top_level_values(ok* p_ok, bool* p_error) {
  value_array vals;
  value_array_init(&vals, &p_ok->alloc);

  for (uint32_t i = 0; i < sizeof(top_level_seeds) / sizeof(top_level_seeds[0]) && !*p_error; ++i) {
    const seed_entry* seed = &top_level_seeds[i];
    value temp;
    switch (seed->kind) {
    case SEED_NATIVE: {
      // the hidden global is " " + the visible name; the names are short
      // static strings, so a bounded stack buffer is the whole story
      char hidden[32];
      const int written = snprintf(hidden, sizeof(hidden), " %s", seed->name);
      if (written <= 0 || (size_t)written >= sizeof(hidden) ||
          !ok_global_get(p_ok, ok_create_string_view(hidden, (uint32_t)written, true), &temp)) {
        *p_error = true;
      }
      break;
    }
    case SEED_BUILTIN_CLASS:
      temp = OBJECT_AS_VALUE(p_ok->vm.builtin_classes[seed->class_id]);
      break;
    case SEED_OBJECT:
      // object and type have no global storage; the vm singletons are
      // the values (seeded fresh every run, rooted by mark_vm in between)
      temp = OBJECT_AS_VALUE(p_ok->vm.object_singleton);
      break;
    case SEED_CLASS:
      temp = OBJECT_AS_VALUE(p_ok->vm.class_singleton);
      break;
    }
    if (*p_error || !value_array_append(&vals, temp)) {
      *p_error = true;
      break;
    }
  }
  return vals;
}

ok* ok_create(ok_specs p_specs) {
  patch_specs(&p_specs);
  ok* okay = p_specs.allocators.allocate(sizeof(ok));
  if (okay == NULL) {
    return NULL;
  }
  okay->raw_alloc = p_specs.allocators;
  okay->alloc = create_allocators(&okay->raw_alloc);
  okay->dump_bytecode = false;
  okay->dump_skip_run = false;

  object_store_init(&okay->objects_store, &okay->alloc);

  gc_init(&okay->gc, &okay->objects_store, &okay->alloc);
  okay->alloc.gc = &okay->gc;

  globals_store_init(&okay->globals_store, &okay->alloc);
  gc_watch_globals(&okay->gc, &okay->globals_store);
  // the interner precedes both consumers (compiler + vm): they only store
  // the pointer, but the gc watch must be live before either interns.
  string_interner_init(&okay->interner, &okay->alloc);
  gc_watch_interner(&okay->gc, &okay->interner);
  compiler_specs compiler_specs = {.alloc = &okay->alloc,
                                   .objects_store = &okay->objects_store,
                                   .globals_store = &okay->globals_store,
                                   .interner = &okay->interner};
  compiler_init(&okay->compiler, compiler_specs);
  gc_watch_compiler(&okay->gc, &okay->compiler);
  vm_specs vm_specs = {.alloc = &okay->alloc,
                       .globals_store = &okay->globals_store,
                       .objects_store = &okay->objects_store,
                       .ok = okay,
                       .interner = &okay->interner};
  vm_init(&okay->vm, vm_specs);
  gc_watch_vm(&okay->gc, &okay->vm);
  // the singletons need mark_vm live before they exist: their only root is
  // the vm itself, and the in-flight pieces ride the guard stack.
  if (!vm_init_singletons(&okay->vm)) {
    return NULL;
  }
  if (!ok_define_core_natives(okay)) { // beware we not compiling yet
    ok_free(okay);
    return NULL;
  }
  if (!ok_define_builtin_surfaces(okay)) { // mount the method tables, then seal
    ok_free(okay);
    return NULL;
  }
  return okay;
}

static ok_result ok_run_inner(ok* p_ok, uint32_t p_source_id);

// stamp the freshly compiled tree with its source id: the main function and
// every nested proto (function protos live in the constants arrays).
// proto trees grow downward only (a proto's constants are built before it is finished),
// so the walk terminates. shared protos stamp idempotently.
static void stamp_source_ids(object_function* p_function, uint32_t p_source_id) {
  p_function->chunk.source_id = p_source_id;
  for (uint32_t i = 0; i < p_function->chunk.constants.count; ++i) {
    const value v = p_function->chunk.constants.data[i];
    if (IS_VALUE_FUNCTION(v)) {
      stamp_source_ids(VALUE_AS_FUNCTION(v), p_source_id);
    }
  }
}

ok_result ok_run(ok* p_ok, ok_source p_source) {
  // a fresh run starts with a clean error lane: ok_last_error reads the
  // most recent failed run, and this run hasn't failed yet.
  vm_clear_error(&p_ok->vm);
  const uint32_t source_id = vm_begin_source(&p_ok->vm, p_source.code, p_source.path);
  if (source_id == VM_SOURCE_UNSTAMPED) {
    return OK_INIT_ERROR;
  }
  p_ok->source = p_source;
  const ok_result result = ok_run_inner(p_ok, source_id);
  vm_end_source(&p_ok->vm, source_id);
  return result;
}

void ok_retain_source(ok* p_ok, bool p_retain) {
  p_ok->vm.retain_source = p_retain;
}

/* classes (embedder side) */

bool ok_value_is_class(ok_value p_value) {
  return IS_VALUE_CLASS(p_value);
}

// a C-defined class: created unfrozen (define its native methods, then
// ok_class_freeze). p_parent is a class value, or a null value for object.
// returns a null value on failure. the returned class is NOT rooted - the
// definition-sequence calls below guard it across their own allocations,
// but if you allocate anything else in between (or keep it past the
// sequence), root it with ok_root first, exactly like an ok_array_new result.
ok_value ok_class_new(ok* p_ok, ok_string_view p_name, ok_value p_parent) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_class* parent = p_ok->vm.object_singleton;
  if (!IS_VALUE_STRICTLY_NULL(p_parent)) {
    if (!IS_VALUE_CLASS(p_parent)) {
      return NULL_AS_VALUE();
    }
    parent = VALUE_AS_CLASS(p_parent);
  }
  object_string* name = create_object_string(p_name, &specs);
  if (name == NULL) {
    return NULL_AS_VALUE();
  }
  if (!gc_guard_up(p_ok->alloc.gc, OBJECT_AS_VALUE(name))) {
    return NULL_AS_VALUE();
  }
  object_class* klass = create_object_class(name, parent, &specs);
  gc_guard_down(p_ok->alloc.gc); // the class (and its trace) owns the name
  if (klass == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(klass);
}

// define one native method on an unfrozen class. the arity COUNTS the
// receiver (`fu m(x)` on the script side is arity 2 here). OK_NATIVE_ARITY_ANY
// defers the check to the callback. false = p_class is not a class value, the
// class is sealed, a same-name redeclaration, a private override of a pub
// ancestor, an arity disagreement with an ancestor, or OOM.
bool ok_class_define_native_method(ok* p_ok,
                                    ok_value p_class,
                                    ok_string_view p_name,
                                    uint16_t p_arity_including_receiver,
                                    ok_native_fn p_cb,
                                    bool p_pub) {
  if (!IS_VALUE_CLASS(p_class)) {
    return false;
  }
  // the class may be unrooted C state: guard it across the name allocation
  // and the map insert (both can collect under aggressive gc).
  if (!gc_guard_up(p_ok->alloc.gc, p_class)) {
    return false;
  }
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  const bool defined = object_class_define_native_method(
      VALUE_AS_CLASS(p_class), p_name, p_arity_including_receiver, p_cb, p_pub, &specs);
  gc_guard_down(p_ok->alloc.gc);
  return defined;
}

// seal the class (C15): builds the flat field map and freezes both tables.
// after this, ok_class_define_native_method is refused. script sees the class
// like any other: store it in a global and construct with C(args).
bool ok_class_freeze(ok* p_ok, ok_value p_class) {
  if (!IS_VALUE_CLASS(p_class)) {
    return false;
  }
  if (!gc_guard_up(p_ok->alloc.gc, p_class)) {
    return false;
  }
  const bool frozen = object_class_freeze(VALUE_AS_CLASS(p_class));
  gc_guard_down(p_ok->alloc.gc);
  return frozen;
}

ok_value ok_last_error(ok* p_ok) {
  return vm_last_error(&p_ok->vm);
}

void ok_clear_error(ok* p_ok) {
  vm_clear_error(&p_ok->vm);
}

static ok_result ok_run_inner(ok* p_ok, uint32_t p_source_id) {
  parser parser;
  parser_specs specs = {.alloc = &p_ok->alloc, .source = &p_ok->source};
  parser_init(&parser, specs);
  parse_result parse_result = parser_parse(&parser);
  if (parse_result.status != PARSE_OK) {
    parse_result_deinit(&parse_result);
    return OK_PARSE_ERROR;
  }

#if defined(OK_TRACE_AST)
  gc_pause(&p_ok->gc);
  printf("--- ast ---\n");
  ast_dispatch_print((ast_node*)parse_result.root);
  printf("--- ast ---");
  fflush(stdout);
  gc_resume(&p_ok->gc);
#endif // defined(OK_TRACE_AST)

  compile_specs compile_specs;
  compile_specs.root = parse_result.root;
  compile_specs.source = &p_ok->source;
  bool err = false;
  compile_specs.top_level_declarations = ok_get_top_level_declarations(p_ok, &err);
  if (err) {
    return OK_INIT_ERROR;
  }
  compile_result compile_result = compiler_compile(&p_ok->compiler, compile_specs);
  top_level_declarations_deinit(&compile_specs.top_level_declarations, &p_ok->alloc);
  parse_result_deinit(&parse_result);
  if (compile_result.status != COMPILE_OK) {
    return OK_COMPILE_ERROR;
  }
  // the freshly compiled tree remembers its source:
  // main + every nested proto. runtime reports read it back per frame
  stamp_source_ids(compile_result.function, p_source_id);
  if (p_ok->dump_bytecode) {
    gc_pause(&p_ok->gc);
    disassembler disassembler;
    disassembler_specs disassembler_specs = {.chunk = &compile_result.function->chunk,
                                             .globals_store = &p_ok->globals_store};
    disassembler_init(&disassembler, disassembler_specs);
    debug_disassemble_function(&disassembler, compile_result.function, "<main>");
    disassembler_deinit(&disassembler);
    gc_resume(&p_ok->gc);
    if (p_ok->dump_skip_run) {
      return OK;
    }
  }
  interpret_specs interpret_specs;
  interpret_specs.function = compile_result.function;
  interpret_specs.source_id = p_source_id;
  if (!gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(compile_result.function))) {
    return OK_INIT_ERROR;
  }
  err = false;
  interpret_specs.top_level_values = ok_get_top_level_values(p_ok, &err);
  if (err) {
    gc_guard_down(&p_ok->gc);
    return OK_INIT_ERROR;
  }
  interpret_result interpret_result = vm_interpret(&p_ok->vm, interpret_specs);
  gc_guard_down(&p_ok->gc);
  value_array_deinit(&interpret_specs.top_level_values, &p_ok->alloc);
  if (interpret_result.status != RUNTIME_OK) {
    interpret_result_deinit(&interpret_result);
    return OK_RUNTIME_ERROR;
  }

  return OK;
}

void ok_debug_dump_bytecode(ok* p_ok, bool p_enabled, bool p_skip_run) {
  p_ok->dump_bytecode = p_enabled;
  p_ok->dump_skip_run = p_skip_run;
}

void ok_set_print(ok* p_ok, ok_print_fn p_cb, void* p_ctx) {
  p_ok->vm.print_cb = p_cb;
  p_ok->vm.print_ctx = p_ctx;
}

void ok_free(ok* p_ok) {
  vm_deinit(&p_ok->vm);
  compiler_deinit(&p_ok->compiler);
  string_interner_deinit(&p_ok->interner);
  globals_store_deinit(&p_ok->globals_store);
  object_store_deinit(&p_ok->objects_store);
  gc_deinit(&p_ok->gc);
  p_ok->alloc.release(&p_ok->alloc, p_ok);
}

ok_value ok_value_null(void) {
  return NULL_AS_VALUE();
}

ok_value ok_value_bool(bool p_value) {
  return BOOL_AS_VALUE(p_value);
}

ok_value ok_value_number(double p_value) {
  return NUMBER_AS_VALUE(p_value);
}

ok_value ok_value_string(ok* p_ok, ok_string_view p_string) {
  object_specs specs = {&p_ok->alloc, &p_ok->objects_store};
  object_string* str = create_object_string(p_string, &specs);
  if (str == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(str);
}

bool ok_value_is_null(ok_value p_value) {
  return IS_VALUE_STRICTLY_NULL(p_value);
}

bool ok_value_is_bool(ok_value p_value) {
  return IS_VALUE_STRICTLY_BOOL(p_value);
}

bool ok_value_is_number(ok_value p_value) {
  return IS_VALUE_NUMBER(p_value);
}

bool ok_value_is_object(ok_value p_value) {
  return IS_VALUE_STRICTLY_OBJECT(p_value);
}

bool ok_value_is_string(ok_value p_value) {
  return IS_VALUE_STRING(p_value);
}

bool ok_value_is_coro(ok_value p_value) {
  return IS_VALUE_CORO(p_value);
}

bool ok_value_as_bool(ok_value p_value) {
  return VALUE_AS_BOOL(p_value);
}

double ok_value_as_number(ok_value p_value) {
  return VALUE_AS_NUMBER(p_value);
}

ok_cstring_view ok_value_as_string(ok_value p_value) {
  return ok_create_cstring_view_from_string(VALUE_AS_STRING(p_value)->string);
}

bool ok_call(ok* p_ok, ok_value p_callee, uint8_t p_argc, const ok_value* p_argv, ok_value* p_result) {
  execution_state* exe = p_ok->vm.current_execution_state;
  register_file* reg = &exe->registers;
  const uint32_t base = reg->top;

  // stage the call window at the top of the register file: [base] = callee, [base+1..] = args.
  // the vm can grow the file later when the callee frame needs more registers.
  if ((uint32_t)(base + p_argc + 1) > reg->array.capacity) {
    if (!register_array_grow(&reg->array, base + p_argc + 1)) {
      *p_result = NULL_AS_VALUE();
      return false;
    }
  }
  reg->array.data[base] = p_callee;
  memcpy(&reg->array.data[base + 1], p_argv, p_argc * sizeof(*p_argv));
  reg->top = base + p_argc + 1;

  interpret_result result = vm_call(&p_ok->vm, p_callee, p_argc);
  if (result.status != RUNTIME_OK) {
    // runtime_error already reset the execution state.
    *p_result = NULL_AS_VALUE();
    return false;
  }
  *p_result = reg->array.data[base];
  return true;
}

bool ok_global_define(ok* p_ok, ok_string_view p_name, ok_value p_value, bool p_is_mutable) {
  gc_guard_up(&p_ok->gc, p_value);
  global_id id = globals_store_add(&p_ok->globals_store, p_name, p_is_mutable ? GLOBAL_FLAG_MUTABLE : GLOBAL_FLAG_NONE);
  if (!IS_GLOBAL_VALID(id)) {
    gc_guard_down(&p_ok->gc);
    return false;
  }
  p_ok->globals_store.global_values.data[id.index] = p_value;
  gc_guard_down(&p_ok->gc);
  return true;
}

bool ok_global_get(ok* p_ok, ok_string_view p_name, ok_value* p_value) {
  global_id id = globals_store_get(&p_ok->globals_store, p_name);
  if (!IS_GLOBAL_VALID(id)) {
    *p_value = NULL_AS_VALUE();
    return false;
  }
  *p_value = p_ok->globals_store.global_values.data[id.index];
  return true;
}

bool ok_global_define_native_function(ok* p_ok, ok_string_view p_name, uint8_t p_arity, ok_native_fn p_cb) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_native_function* native = create_object_native_function(p_name, p_arity, p_cb, &s);
  if (native == NULL) {
    return false;
  }
  return ok_global_define(p_ok, p_name, OBJECT_AS_VALUE(native), false);
}

bool ok_native_error(ok* p_ok, ok_string_view p_message) {
  return vm_native_error(&p_ok->vm, p_message);
}

bool ok_native_throw(ok* p_ok, ok_value p_value) {
  return vm_native_throw(&p_ok->vm, p_value);
}

ok_value
ok_native_value_new(ok* p_ok, void* p_user_data, ok_native_destructor p_destructor, ok_native_tracer p_tracer) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_native_value* native = create_object_native_value(p_user_data, p_destructor, p_tracer, &s);
  if (native == NULL) {
    return NULL_AS_VALUE();
  }
  return OBJECT_AS_VALUE(native);
}

void* ok_value_as_native_value(ok_value p_value) {
  if (!IS_VALUE_STRICTLY_OBJECT(p_value)) {
    return NULL;
  }

  object* obj = VALUE_AS_OBJECT(p_value);

  if (object_get_type(obj) != OBJ_NATIVE_VALUE) {
    return NULL;
  }

  object_native_value* native = (object_native_value*)obj;
  return native->user_data;
}

bool ok_value_is_native_value(ok_value p_value) {
  return IS_VALUE_NATIVE_VALUE(p_value);
}

bool ok_gc_mark_value(ok* p_ok, ok_value p_value) {
  return gc_mark_value_user(&p_ok->gc, p_value);
}

uint32_t ok_root(ok* p_ok, ok_value p_value) {
  // null is the roots' free-slot marker, so rooting it cannot be supported.
  if (IS_VALUE_STRICTLY_NULL(p_value)) {
    return OK_ROOT_INVALID;
  }
  return gc_root(&p_ok->gc, p_value);
}

bool ok_unroot(ok* p_ok, uint32_t p_root) {
  return gc_unroot(&p_ok->gc, p_root);
}

ok_value ok_array_new(ok* p_ok, const ok_value* p_items, uint32_t p_count) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_array* arr = create_object_array(&s);
  if (arr == NULL) {
    return NULL_AS_VALUE();
  }
  // mirror OP_ARRAY_NEW: between creation and publication the array is linked
  // in the store yet unreachable, and every append grows through the gc-aware
  // allocator (collects). the items are host locals, equally invisible: guard
  // the array for the whole loop and each item across its own append. guard
  // depth peaks at 2 (capacity 16), mirroring the VM's unchecked pattern.
  gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(arr));
  for (uint32_t i = 0; i < p_count; ++i) {
    gc_guard_up(&p_ok->gc, p_items[i]);
    const bool appended = value_array_append(&arr->values, p_items[i]);
    gc_guard_down(&p_ok->gc);
    if (!appended) {
      gc_guard_down(&p_ok->gc);
      return NULL_AS_VALUE();
    }
  }
  gc_guard_down(&p_ok->gc);
  return OBJECT_AS_VALUE(arr);
}

bool ok_value_is_array(ok_value p_value) {
  return IS_VALUE_ARRAY(p_value);
}

uint32_t ok_array_len(ok_value p_value) {
  if (!IS_VALUE_ARRAY(p_value)) {
    return 0;
  }
  return VALUE_AS_ARRAY(p_value)->values.count;
}

ok_value ok_array_get(ok_value p_value, uint32_t p_index) {
  if (!IS_VALUE_ARRAY(p_value) || p_index >= VALUE_AS_ARRAY(p_value)->values.count) {
    return NULL_AS_VALUE();
  }
  return VALUE_AS_ARRAY(p_value)->values.data[p_index];
}

bool ok_array_set(ok_value p_value, uint32_t p_index, ok_value p_new_value) {
  if (!IS_VALUE_ARRAY(p_value) || p_index >= VALUE_AS_ARRAY(p_value)->values.count) {
    return false;
  }
  VALUE_AS_ARRAY(p_value)->values.data[p_index] = p_new_value;
  return true;
}

bool ok_array_push(ok* p_ok, ok_value p_array, ok_value p_value) {
  if (!IS_VALUE_ARRAY(p_array)) {
    return false;
  }
  // unlike the ' push' core native (whose arguments are rooted through the caller's register window),
  // a host-held array and value are invisible to the collector,
  // thus both need the guard while the append may grow (collect).
  gc_guard_up(&p_ok->gc, p_array);
  gc_guard_up(&p_ok->gc, p_value);
  const bool appended = value_array_append(&VALUE_AS_ARRAY(p_array)->values, p_value);
  gc_guard_down(&p_ok->gc);
  gc_guard_down(&p_ok->gc);
  return appended;
}

/* maps */

// builds a new map from p_pair_count (key, value) pairs laid out flat {k0, v0, k1, v1, ...}.
// p_pairs may be NULL when p_pair_count is 0.
// duplicate keys: the later pair wins, same as the literal.
// returns a null value on allocation failure or an illegal key (null / unhashable).
ok_value ok_map_new(ok* p_ok, const ok_value* p_pairs, uint32_t p_pair_count) {
  object_specs s = {.alloc = &p_ok->alloc, .store = &p_ok->objects_store};
  object_map* map = create_object_map(&s);
  if (map == NULL) {
    return NULL_AS_VALUE();
  }
  // the constructor guard pattern, mirroring ok_array_new: the map is linked
  // in the store yet unreachable, and every set may rehash (grow) under the
  // collector. so it stays rooted across the whole fill and each pair across
  // its own set. guard depth peaks at 3 (capacity 16).
  gc_guard_up(&p_ok->gc, OBJECT_AS_VALUE(map));
  for (uint32_t i = 0; i < p_pair_count; ++i) {
    const ok_value key = p_pairs[2 * i];
    const ok_value val = p_pairs[2 * i + 1];
    if (IS_VALUE_STRICTLY_NULL(key) || !map_key_is_legal(key)) {
      gc_guard_down(&p_ok->gc);
      return NULL_AS_VALUE(); // null return: only strings, numbers, bools are valid keys
    }
    gc_guard_up(&p_ok->gc, key);
    gc_guard_up(&p_ok->gc, val);
    const bool set = object_map_set(map, key, val);
    gc_guard_down(&p_ok->gc);
    gc_guard_down(&p_ok->gc);
    if (!set) {
      gc_guard_down(&p_ok->gc);
      return NULL_AS_VALUE();
    }
  }
  gc_guard_down(&p_ok->gc);
  return OBJECT_AS_VALUE(map);
}

bool ok_value_is_map(ok_value p_value) {
  return IS_VALUE_MAP(p_value);
}

uint32_t ok_map_len(ok_value p_value) {
  if (!IS_VALUE_MAP(p_value)) {
    return 0;
  }
  return VALUE_AS_MAP(p_value)->entries.count;
}

// read only and allocation free (probes never allocate).
// return false when type is not a map, or illegal key, or absent.
// the stored value (even a stored null) lands in *p_out only on success (true).
bool ok_map_get(ok_value p_map, ok_value p_key, ok_value* p_out) {
  if (!IS_VALUE_MAP(p_map) || IS_VALUE_STRICTLY_NULL(p_key) || !map_key_is_legal(p_key)) {
    return false;
  }
  value* slot = value_map_get(&VALUE_AS_MAP(p_map)->entries, p_key);
  if (slot == NULL) {
    return false;
  }
  *p_out = *slot;
  return true;
}

// insert or overwrite. unlike the ' get'/' remove' core natives
// (whose arguments are rooted through the caller's register window),
// a host-held map, key and value are invisible to the collector
// thus all three need the guard while the set may grow (collect).
bool ok_map_set(ok* p_ok, ok_value p_map, ok_value p_key, ok_value p_value) {
  if (!IS_VALUE_MAP(p_map) || IS_VALUE_STRICTLY_NULL(p_key) || !map_key_is_legal(p_key)) {
    return false;
  }
  gc_guard_up(&p_ok->gc, p_map);
  gc_guard_up(&p_ok->gc, p_key);
  gc_guard_up(&p_ok->gc, p_value);
  const bool set = object_map_set(VALUE_AS_MAP(p_map), p_key, p_value);
  gc_guard_down(&p_ok->gc);
  gc_guard_down(&p_ok->gc);
  gc_guard_down(&p_ok->gc);
  return set;
}

// backward-shift removal, no allocation. false = not a map, illegal key, or absent (in which case nothing changed).
bool ok_map_remove(ok_value p_map, ok_value p_key) {
  if (!IS_VALUE_MAP(p_map) || IS_VALUE_STRICTLY_NULL(p_key) || !map_key_is_legal(p_key)) {
    return false;
  }
  return object_map_remove(VALUE_AS_MAP(p_map), p_key);
}
