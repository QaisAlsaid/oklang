// the ok build acceptance lane: a native target with a declared build
// command, built through `ok build`, imported by name.
#include "ok/ok_module.h"

static const ok_api* api = NULL;

static ok_value adder_add(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  if (p_argc != 2 || api->value_type(p_argv[0]) != OK_TYPE_NUM || api->value_type(p_argv[1]) != OK_TYPE_NUM) {
    api->native_error(p_ok, api->view("add expects two numbers.", OK_STRING_VIEW_CALCULATE_LENGTH));
    return api->value_null();
  }
  return api->value_number(api->value_as_number(p_argv[0]) + api->value_as_number(p_argv[1]));
}

static const ok_native_fn_def adder_fns[] = {
    {"add", 2, adder_add},
};

OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  if (p_api == NULL || p_api->abi != OK_NATIVE_ABI) {
    return NULL;
  }
  api = p_api;
  static const ok_native_module adder_module = {
      OK_NATIVE_ABI,
      "adder",
      "0.1.0",
      1,
      adder_fns,
      NULL,
      NULL,
      NULL,
  };
  return &adder_module;
}
