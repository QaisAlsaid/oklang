// the name mismatch fixture: the shared object declares a different name
// than the import stem.
#include "ok/ok_module.h"
static const ok_native_module badname_module = {
    OK_NATIVE_ABI,
    "impostor", // not "badname": the check must catch it
    "0.0.1",
    0,
    NULL,
    NULL,
    NULL,
    NULL,
};
OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  (void)p_api;
  return &badname_module;
}
