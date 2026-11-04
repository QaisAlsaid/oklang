#ifndef OK_SPECS_H
#define OK_SPECS_H

#include "ok_fwd.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef void* (*ok_reallocate)(void* p_ptr, size_t p_new_size);
typedef void* (*ok_allocate)(size_t p_size);
typedef void (*ok_release)(void* p_ptr);

struct ok_allocators {
  ok_reallocate reallocate;
  ok_allocate allocate;
  ok_release release;
};

// OR-3 "O Resolution" - the capability families. they fail differently, so
// they gate differently: LOAD is arbitrary native code in-process (the
// escape hatch), BUILD is arbitrary code out-of-process (build commands,
// OR-4's half), HOST is data-plane leakage (fs/env/clock, the stdlib as it
// lands). the mask is a bitmask; presets are sugar.
typedef enum {
  OK_CAP_NATIVE_LOAD = 1u << 0,  // dlopen natives (import + force-load)
  OK_CAP_NATIVE_BUILD = 1u << 1, // run declared native build commands (OR-4)
  OK_CAP_HOST = 1u << 2,         // host-touching stdlib (fs/env/clock, later)
} ok_cap;

// every capability bit at once. a macro, not an enumerator: C99 keeps
// enumerator values inside int and the full mask does not fit (the P1
// warning sweep's find; the value is unchanged).
#define OK_CAP_ALL ((ok_cap)0xffffffffu)

struct ok_specs {
  ok_allocators allocators;
  // capabilities refused at birth (the ratchet's starting point). 0, the
  // {0} default, refuses nothing: every existing embedder keeps the full
  // surface. an embedder that wants the locked stance writes OK_CAP_ALL.
  // after ok_create the mask only ever grows (ok_caps_drop); nothing, not a
  // native, not a script, not a manifest, can shrink it.
  uint32_t caps_dropped;
  // host-provided dynamic-loading hooks (malloc-style): NULL means the
  // platform default (the dlopen family, LoadLibrary on windows). a host on
  // a platform with no loader (consoles, bare newlib, wasm without dlopen)
  // installs its own table here, or leaves natives off entirely (drop
  // OK_CAP_NATIVE_LOAD, or let the default refusal speak).
  const ok_dylib* dylib;
};

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // OK_SPECS_H
