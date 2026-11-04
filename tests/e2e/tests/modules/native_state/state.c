// the patch-49 acceptance lane: a native module whose class carries REAL
// per-instance state in vm fields (no C-side payload, no getter penalty on
// the data plane - the fields are gc-traced values like any script class's).
// the build is a raw gcc command on purpose (M12: whatever toolchain).
//
// Counter(start): payload = start (written by the native init through
// api->value_set_field), step = 3 (a declared initial value, immutable).
// bump() adds step through get/set; count() reads it back.

#include "ok/ok_module.h"

static const ok_api* api = NULL;

// init(start): the receiver rides argv[0], the construction lane's contract
static ok_value counter_init(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc != 2 || api->value_type(p_argv[1]) != OK_TYPE_NUM) {
    api->native_error(p_ok, api->view("Counter expects one number.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  if (!api->value_set_field(p_ok, p_argv[0], api->view("payload", 7), p_argv[1])) {
    api->native_error(p_ok, api->view("Counter init could not write its payload field.",
                                      OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return api->value_null();
}

// bump(): payload = payload + step, answers the new payload
static ok_value counter_bump(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  ok_value payload = api->value_null();
  ok_value step = api->value_null();
  if (!api->value_get_field(p_ok, p_argv[0], api->view("payload", 7), &payload) ||
      !api->value_get_field(p_ok, p_argv[0], api->view("step", 4), &step)) {
    api->native_error(p_ok, api->view("Counter could not read its fields.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  const ok_value next = api->value_number(api->value_as_number(payload) + api->value_as_number(step));
  if (!api->value_set_field(p_ok, p_argv[0], api->view("payload", 7), next)) {
    api->native_error(p_ok, api->view("Counter could not write its payload field.",
                                      OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return next;
}

// count(): the payload read back
static ok_value counter_count(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  ok_value payload = api->value_null();
  if (!api->value_get_field(p_ok, p_argv[0], api->view("payload", 7), &payload)) {
    api->native_error(p_ok, api->view("Counter could not read its payload.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return payload;
}

// step(): the immutable declared default, read-only proof
static ok_value counter_step(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  ok_value step = api->value_null();
  if (!api->value_get_field(p_ok, p_argv[0], api->view("step", 4), &step)) {
    api->native_error(p_ok, api->view("Counter could not read its step.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return step;
}

static bool state_mount(ok* p_ok) {
  ok_value counter = api->class_new(p_ok, api->view("Counter", 7), api->value_null());
  if (api->value_type(counter) != OK_TYPE_CLASS) {
    return false;
  }
  // the patch-49 door: fields on a C-defined class, before the freeze
  if (!api->class_add_field(p_ok, counter, api->view("payload", 7), true, api->value_null())) {
    return false;
  }
  if (!api->class_add_field(p_ok, counter, api->view("step", 4), false, api->value_number(3))) {
    return false;
  }
  if (!api->class_define_native_method(p_ok, counter, api->view("init", 4), 2, counter_init, false)) {
    return false;
  }
  if (!api->class_define_native_method(p_ok, counter, api->view("bump", 4), 1, counter_bump, true)) {
    return false;
  }
  if (!api->class_define_native_method(p_ok, counter, api->view("count", 5), 1, counter_count, true)) {
    return false;
  }
  if (!api->class_define_native_method(p_ok, counter, api->view("step", 4), 1, counter_step, true)) {
    return false;
  }
  if (!api->class_freeze(p_ok, counter)) {
    return false;
  }
  return api->module_export(p_ok, api->view("Counter", 7), counter);
}

static const ok_native_fn_def state_fns[] = {};

OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  if (p_api == NULL || p_api->abi != OK_NATIVE_ABI) {
    return NULL;
  }
  api = p_api;
  static const ok_native_module state_module = {
      OK_NATIVE_ABI,
      "state",
      "0.1.0",
      0,
      state_fns,
      state_mount,
      NULL,
      NULL,
  };
  return &state_module;
}
