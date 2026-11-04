// the force-load fixture: registered under its declared name, imported bare.
#include <stdio.h>
#include <string.h>

#include "ok/ok_module.h"
static const ok_api* api = NULL;
static ok_value forced_greet(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  ok_cstring_view name = api->value_as_string(p_argv[0]);
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "forced says %s", name.chars);
  return api->value_string(p_ok, api->view(buffer, OK_STRING_VIEW_CALCULATE_LENGTH));
}
static const ok_native_fn_def forced_fns[] = {
    {"greet", 1, forced_greet},
};
static const ok_native_module forced_module = {
    OK_NATIVE_ABI,
    "forced",
    "1.0.0",
    1,
    forced_fns,
    NULL,
    NULL,
    NULL,
};
OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  if (p_api == NULL || p_api->abi != OK_NATIVE_ABI) {
    return NULL;
  }
  api = p_api;
  return &forced_module;
}
