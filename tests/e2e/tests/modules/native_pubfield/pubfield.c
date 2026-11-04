// the patch-51 acceptance lane: the pub-field door from C. class_add_field_ex
// opens the member ring - script reads and writes the native class's fields
// as slot loads, no getter round-trips. the private counter stays ringed:
// script reaches it only through its methods.
#include "ok/ok_module.h"

static const ok_api* api = NULL;

// init(x, y): the receiver rides argv[0], the construction lane's contract
static ok_value vec_init(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc != 3 || api->value_type(p_argv[1]) != OK_TYPE_NUM || api->value_type(p_argv[2]) != OK_TYPE_NUM) {
    api->native_error(p_ok, api->view("Vec expects two numbers.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  if (!api->value_set_field(p_ok, p_argv[0], api->view("x", 1), p_argv[1]) ||
      !api->value_set_field(p_ok, p_argv[0], api->view("y", 1), p_argv[2])) {
    api->native_error(p_ok, api->view("Vec init could not write its fields.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return api->value_null();
}

// length_sq(): x*x + y*y, counted. the pub fields read through the trusted
// lane (the ring never applied to C), the counter proves per-instance state.
static ok_value vec_length_sq(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  ok_value x = api->value_null();
  ok_value y = api->value_null();
  ok_value taps = api->value_null();
  if (!api->value_get_field(p_ok, p_argv[0], api->view("x", 1), &x) ||
      !api->value_get_field(p_ok, p_argv[0], api->view("y", 1), &y) ||
      !api->value_get_field(p_ok, p_argv[0], api->view("taps", 4), &taps)) {
    api->native_error(p_ok, api->view("Vec could not read its fields.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  if (!api->value_set_field(p_ok, p_argv[0], api->view("taps", 4), api->value_number(api->value_as_number(taps) + 1))) {
    api->native_error(p_ok, api->view("Vec could not count.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return api->value_number(api->value_as_number(x) * api->value_as_number(x) +
                           api->value_as_number(y) * api->value_as_number(y));
}

// reads(): the private counter read back through a pub method
static ok_value vec_reads(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  ok_value taps = api->value_null();
  if (!api->value_get_field(p_ok, p_argv[0], api->view("taps", 4), &taps)) {
    api->native_error(p_ok, api->view("Vec could not read its counter.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return taps;
}

static bool pubfield_mount(ok* p_ok) {
  ok_value vec = api->class_new(p_ok, api->view("Vec", 3), api->value_null());
  if (api->value_type(vec) != OK_TYPE_CLASS) {
    return false;
  }
  // the patch-51 door: pub fields, script-visible, mut for the write side
  if (!api->class_add_field_ex(p_ok, vec, api->view("x", 1), true, true, api->value_number(0)) ||
      !api->class_add_field_ex(p_ok, vec, api->view("y", 1), true, true, api->value_number(0))) {
    return false;
  }
  // the private counter: the patch-49 entry, the ring keeps it
  if (!api->class_add_field(p_ok, vec, api->view("taps", 4), true, api->value_number(0))) {
    return false;
  }
  if (!api->class_define_native_method(p_ok, vec, api->view("init", 4), 3, vec_init, false) ||
      !api->class_define_native_method(p_ok, vec, api->view("length_sq", 9), 1, vec_length_sq, true) ||
      !api->class_define_native_method(p_ok, vec, api->view("reads", 5), 1, vec_reads, true)) {
    return false;
  }
  if (!api->class_freeze(p_ok, vec)) {
    return false;
  }
  return api->module_export(p_ok, api->view("Vec", 3), vec);
}

static const ok_native_fn_def pubfield_fns[] = {};

OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  if (p_api == NULL || p_api->abi != OK_NATIVE_ABI) {
    return NULL;
  }
  api = p_api;
  static const ok_native_module pubfield_module = {
      OK_NATIVE_ABI,
      "pubfield",
      "0.1.0",
      0,
      pubfield_fns,
      pubfield_mount,
      NULL,
      NULL,
  };
  return &pubfield_module;
}
