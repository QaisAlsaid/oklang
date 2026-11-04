// the abi old-minor fixture: a module frozen in the patch-49 era. the
// descriptor hardcodes abi 2 (the field trio's number) while the export
// check uses the CURRENT header - the honest simulation of a stale .so
// meeting a newer ok. the loader must load it unchanged: same major, older
// minor, the additions were append-only. this fixture is the compat
// promise's proof, run on every gate.
#include "ok/ok_module.h"

static const ok_api* api = NULL;

static ok_value fossil_answer(ok* p_ok, uint8_t p_argc, const ok_value* p_argv) {
  (void)p_argc;
  (void)p_argv;
  return api->value_number(42);
}

static const ok_native_fn_def fossil_fns[] = {
    {"answer", 0, fossil_answer},
};

OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  // the module's own both-direction check stays exact: it knows the table
  // it was compiled against, and the host's table is a superset of it
  if (p_api == NULL || p_api->abi != OK_NATIVE_ABI) {
    return NULL;
  }
  api = p_api;
  static const ok_native_module fossil_module = {
      2u, // frozen in the patch-49 era: (0, 2), before the pub-field door
      "fossil",
      "0.1.0",
      1,
      fossil_fns,
      NULL,
      NULL,
      NULL,
  };
  return &fossil_module;
}
