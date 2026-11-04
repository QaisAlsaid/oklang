// the abi mismatch fixture: built against a wrong OK_NATIVE_ABI.
#include "ok/ok_module.h"
static const ok_native_module badabi_module = {
    99, // not OK_NATIVE_ABI: the loader must refuse with the rebuild message
    "badabi",
    "0.0.1",
    0,
    NULL,
    NULL,
    NULL,
    NULL,
};
OK_API const ok_native_module* ok_native_export(const ok_api* p_api) {
  (void)p_api;
  return &badabi_module;
}
