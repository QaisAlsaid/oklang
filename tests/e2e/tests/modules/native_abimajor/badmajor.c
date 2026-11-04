// the abi major-mismatch fixture: declares the v1-shaped pair (1, 0). the
// host is pre-1.0 (major 0), so the loader must refuse with the rebuild
// message - the one lane a minor bump can never smooth over.
#include "ok/ok_module.h"

static const ok_api* api = NULL;

static ok_value era_answer(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  (void)p_argv;
  return api->value_null();
}

static const ok_native_fn_def era_fns[] = {
    {"answer", 0, era_answer},
};

OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  if (p_api == NULL) {
    return NULL;
  }
  api = p_api;
  static const ok_native_module era_module = {
      (1u << 16) | 0u, // the future (1, 0): a major this host does not speak
      "era",
      "0.0.1",
      1,
      era_fns,
      NULL,
      NULL,
      NULL,
  };
  return &era_module;
}
