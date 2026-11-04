// OR-3 "O Resolution" - the ok_api vtable, the native module ABI's whole
// reach into the vm.
//
// one STATIC, STATELESS table per process (Q's ruling: "support everything
// we have and expose to the abi"): every entry trampolines to its ok.h
// counterpart and routes through the ok* (or value) argument it is handed,
// so the table captures no instance state. a module stashes the pointer at
// export time and it stays valid forever; the same table serves every ok
// instance in the process, and freeing one instance never dangles another
// module's stash.
//
// the one entry with state on the side is module_export: it appends to the
// module system's active mount window (okmodules.h), which exists only
// between a native's mount_classes start and its namespace freeze.

#include "ok/ok.h"
#include "ok/ok_module.h"

#include "okobject.h"
#include "okstate.h"
#include "okstring.h"
#include "okvalue.h"
#include "okvm.h"

// ---------------------------------------------------------------- view construction

static ok_string_view api_view(const char* p_chars, uint64_t p_length) {
  if (p_length == OK_STRING_VIEW_CALCULATE_LENGTH) {
    // the NUL-terminated literal lane: the module spells it with the same
    // sentinel ok.h's constructors use
    return ok_create_string_view(p_chars, OK_STRING_VIEW_CALCULATE_LENGTH, true);
  }
  return ok_create_string_view(p_chars, p_length, false);
}

// ---------------------------------------------------------------- values: write

static ok_value api_value_null(void) {
  return ok_value_null();
}

static ok_value api_value_string(ok* p_ok, ok_string_view p_string) {
  return ok_value_string(p_ok, p_string);
}

static ok_value api_value_number(double p_value) {
  return ok_value_number(p_value);
}

static ok_value api_value_bool(bool p_value) {
  return ok_value_bool(p_value);
}

static ok_value api_array_new(ok* p_ok, const ok_value* p_items, uint32_t p_count) {
  return ok_array_new(p_ok, p_items, p_count);
}

static ok_value api_map_new(ok* p_ok, const ok_value* p_pairs, uint32_t p_pair_count) {
  return ok_map_new(p_ok, p_pairs, p_pair_count);
}

static bool api_map_set(ok* p_ok, ok_value p_map, ok_value p_key, ok_value p_value) {
  return ok_map_set(p_ok, p_map, p_key, p_value);
}

static ok_value api_native_value_new(ok* p_ok, void* p_data, ok_native_destructor p_destructor,
                                     ok_native_tracer p_tracer) {
  return ok_native_value_new(p_ok, p_data, p_destructor, p_tracer);
}

// ---------------------------------------------------------------- values: read

// the one-call discriminator. OK_TYPE_FN covers every callable value (a
// closure, a native function, a bound method); OK_TYPE_NATIVE is the native
// DATA wrapper (ok_native_value_new's product), the thing a raylib-shaped
// module wraps its handles in.
static ok_type_tag api_value_type(ok_value p_value) {
  if (IS_VALUE_STRICTLY_NULL(p_value)) {
    return OK_TYPE_NIL;
  }
  if (IS_VALUE_STRICTLY_BOOL(p_value)) {
    return OK_TYPE_BOOL;
  }
  if (IS_VALUE_NUMBER(p_value)) {
    return OK_TYPE_NUM;
  }
  if (IS_VALUE_STRING(p_value)) {
    return OK_TYPE_STR;
  }
  if (IS_VALUE_ARRAY(p_value)) {
    return OK_TYPE_ARRAY;
  }
  if (IS_VALUE_MAP(p_value)) {
    return OK_TYPE_MAP;
  }
  if (IS_VALUE_CLASS(p_value)) {
    return OK_TYPE_CLASS;
  }
  if (IS_VALUE_INSTANCE(p_value)) {
    return OK_TYPE_INSTANCE;
  }
  if (IS_VALUE_CORO(p_value)) {
    return OK_TYPE_CORO;
  }
  if (IS_VALUE_CLOSURE(p_value) || IS_VALUE_NATIVE_FUNCTION(p_value) || IS_VALUE_BOUND_METHOD(p_value)) {
    return OK_TYPE_FN;
  }
  if (IS_VALUE_NATIVE_VALUE(p_value)) {
    return OK_TYPE_NATIVE;
  }
  return OK_TYPE_NIL; // defensive: the space is covered above
}

static double api_value_as_number(ok_value p_value) {
  return ok_value_as_number(p_value);
}

static bool api_value_as_bool(ok_value p_value) {
  return ok_value_as_bool(p_value);
}

static ok_cstring_view api_value_as_string(ok_value p_value) {
  return ok_value_as_string(p_value);
}

static uint32_t api_array_len(ok_value p_value) {
  return ok_array_len(p_value);
}

static ok_value api_array_get(ok_value p_value, uint32_t p_index) {
  return ok_array_get(p_value, p_index);
}

static uint32_t api_map_len(ok_value p_value) {
  return ok_map_len(p_value);
}

static bool api_map_get(ok_value p_map, ok_value p_key, ok_value* p_out) {
  return ok_map_get(p_map, p_key, p_out);
}

static void* api_value_as_native_value(ok_value p_value) {
  return ok_value_as_native_value(p_value);
}

// ---------------------------------------------------------------- type + class identity

static ok_value api_classof(ok* p_ok, ok_value p_value) {
  return OBJECT_AS_VALUE(vm_classof(&p_ok->vm, p_value));
}

static bool api_is_instance(ok* p_ok, ok_value p_value, ok_value p_class) {
  if (!IS_VALUE_CLASS(p_class)) {
    return false;
  }
  return vm_is_instance(&p_ok->vm, p_value, VALUE_AS_CLASS(p_class));
}

static ok_cstring_view api_class_name(ok* p_ok, ok_value p_class) {
  (void)p_ok;
  const ok_cstring_view empty = {NULL, 0};
  if (!IS_VALUE_CLASS(p_class)) {
    return empty;
  }
  const object_string* name = VALUE_AS_CLASS(p_class)->name;
  return ok_create_cstring_view(name->string.chars, string_get_length(&name->string));
}

// ---------------------------------------------------------------- calls

static bool api_call(ok* p_ok, ok_value p_callee, uint8_t p_argc, const ok_value* p_argv, ok_value* p_out) {
  return ok_call(p_ok, p_callee, p_argc, p_argv, p_out);
}

// ---------------------------------------------------------------- classes

static ok_value api_class_new(ok* p_ok, ok_string_view p_name, ok_value p_parent) {
  return ok_class_new(p_ok, p_name, p_parent);
}

static bool api_class_define_native_method(ok* p_ok,
                                           ok_value p_class,
                                           ok_string_view p_name,
                                           uint16_t p_arity_including_receiver,
                                           ok_native_fn p_cb,
                                           bool p_pub) {
  return ok_class_define_native_method(p_ok, p_class, p_name, p_arity_including_receiver, p_cb, p_pub);
}

static bool api_class_freeze(ok* p_ok, ok_value p_class) {
  return ok_class_freeze(p_ok, p_class);
}

// ---------------------------------------------------------------- module building

static bool api_module_export(ok* p_ok, ok_string_view p_name, ok_value p_value) {
  native_mount* mount = p_ok->modules.mounting;
  if (mount == NULL || mount->failure != NULL) {
    return false;
  }
  if (p_name.chars == NULL || ok_string_view_get_length(&p_name) == 0) {
    mount->failure = "a mount export needs a name.";
    return false;
  }
  // a duplicate export name is a hard load failure (M13's collision rule)
  for (uint32_t i = 0; i < mount->count; ++i) {
    if (strlen(mount->names[i]) == ok_string_view_get_length(&p_name) &&
        memcmp(mount->names[i], p_name.chars, ok_string_view_get_length(&p_name)) == 0) {
      mount->failure = "duplicate export name in the mount callback.";
      return false;
    }
  }
  if (mount->count == mount->capacity) {
    const uint32_t grown = mount->capacity == 0 ? 4 : mount->capacity * 2;
    char** names = p_ok->raw_alloc.reallocate(mount->names, sizeof(char*) * grown);
    if (names == NULL) {
      mount->failure = "out of memory while staging a mount export.";
      return false;
    }
    mount->names = names;
    mount->capacity = grown;
  }
  const uint32_t length = (uint32_t)ok_string_view_get_length(&p_name);
  char* copy = p_ok->raw_alloc.allocate(length + 1);
  if (copy == NULL) {
    mount->failure = "out of memory while staging a mount export.";
    return false;
  }
  memcpy(copy, p_name.chars, length);
  copy[length] = '\0';
  // the value rides the marked mount window once appended; the append itself
  // can collect (aggressive gc), and until it lands the value is only a C
  // local in the mount callback. guard across exactly that call - the same
  // dance the fn-staging loop does. the hole was always here; patch 51's
  // extra field-array append shifted the allocation order enough to reuse
  // the swept class's memory before the namespace read it back.
  if (!gc_guard_up(p_ok->alloc.gc, p_value)) {
    p_ok->raw_alloc.release(copy);
    mount->failure = "out of memory while staging a mount export.";
    return false;
  }
  const bool appended = value_array_append(&mount->values, p_value);
  gc_guard_down(p_ok->alloc.gc);
  if (!appended) {
    p_ok->raw_alloc.release(copy);
    mount->failure = "out of memory while staging a mount export.";
    return false;
  }
  mount->names[mount->count++] = copy;
  return true;
}

// ---------------------------------------------------------------- errors + roots

static bool api_native_error(ok* p_ok, ok_string_view p_message) {
  return ok_native_error(p_ok, p_message);
}

static bool api_native_throw(ok* p_ok, ok_value p_value) {
  return ok_native_throw(p_ok, p_value);
}

static uint32_t api_root(ok* p_ok, ok_value p_value) {
  return ok_root(p_ok, p_value);
}

static bool api_unroot(ok* p_ok, uint32_t p_root) {
  return ok_unroot(p_ok, p_root);
}

// ---------------------------------------------------------------- coro primitives

static ok_value api_coro_new(ok* p_ok, ok_value p_body) {
  return ok_coro_new(p_ok, p_body);
}

static bool api_coro_resume(ok* p_ok, ok_value p_coro, ok_value p_arg, bool p_has_arg, ok_value* p_out) {
  return ok_coro_resume(p_ok, p_coro, p_arg, p_has_arg, p_out);
}

static uint32_t api_coro_status(ok* p_ok, ok_value p_coro) {
  return ok_coro_status(p_ok, p_coro);
}

static ok_value api_coro_current(ok* p_ok) {
  return ok_coro_current(p_ok);
}

static bool api_native_suspend(ok* p_ok) {
  return ok_native_suspend(p_ok);
}

// ---------------------------------------------------------------- the field-access trio (patch 49)

static bool api_class_add_field(ok* p_ok, ok_value p_class, ok_string_view p_name, bool p_mut, ok_value p_initial) {
  return ok_class_add_field(p_ok, p_class, p_name, p_mut, p_initial);
}

// the pub form (patch 51, ABI 3): appended at the tail, the append-only way.
static bool api_class_add_field_ex(
    ok* p_ok, ok_value p_class, ok_string_view p_name, bool p_mut, bool p_pub, ok_value p_initial) {
  return ok_class_add_field_ex(p_ok, p_class, p_name, p_mut, p_pub, p_initial);
}

static bool api_value_get_field(ok* p_ok, ok_value p_instance, ok_string_view p_name, ok_value* p_out) {
  return ok_value_get_field(p_ok, p_instance, p_name, p_out);
}

static bool api_value_set_field(ok* p_ok, ok_value p_instance, ok_string_view p_name, ok_value p_new_value) {
  return ok_value_set_field(p_ok, p_instance, p_name, p_new_value);
}

// ---------------------------------------------------------------- the table

static const ok_api g_ok_api = {
    OK_NATIVE_ABI,
    api_view,
    api_value_null,
    api_value_string,
    api_value_number,
    api_value_bool,
    api_array_new,
    api_map_new,
    api_map_set,
    api_native_value_new,
    api_value_type,
    api_value_as_number,
    api_value_as_bool,
    api_value_as_string,
    api_array_len,
    api_array_get,
    api_map_len,
    api_map_get,
    api_value_as_native_value,
    api_classof,
    api_is_instance,
    api_class_name,
    api_call,
    api_class_new,
    api_class_define_native_method,
    api_class_freeze,
    api_module_export,
    api_native_error,
    api_native_throw,
    api_root,
    api_unroot,
    api_coro_new,
    api_coro_resume,
    api_coro_status,
    api_coro_current,
    api_native_suspend,
    api_class_add_field,
    api_value_get_field,
    api_value_set_field,
    api_class_add_field_ex,
};

const ok_api* ok_api_table(void) {
  return &g_ok_api;
}
