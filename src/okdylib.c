// OR-3 "O Resolution" - the dynamic-library platform wrapper + the default
// native loader lane.
//
// two real implementations, one ifdef (the plan's line): the dlopen family
// (linux, the bsds, solaris/illumos, aix, haiku, and macos, whose mach-o
// speaks the same api) and windows LoadLibrary/GetProcAddress/FreeLibrary.
// everything else (consoles, bare newlib, wasi without a dynamic linker)
// has NOTHING, which is exactly the case the host-provided ok_dylib hooks
// exist for: the host installs its own table in ok_specs, or accepts the
// structural refusal.
//
// loading discipline (MODULES.md section 7): open with the local,
// resolve-now mode so a module's symbols never leak into another's
// namespace, resolve exactly one symbol (ok_native_export), and never
// dlclose mid-run. the default lane's handles close at ok_free, after every
// on_unload has fired.

#define _XOPEN_SOURCE 700 // dlopen (XSI) + dlerror

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ok/ok.h"
#include "ok/ok_module.h"

#include "okstate.h"

// one formatted error into the caller's lane, NULL-safe (a NULL p_err just
// formats nowhere, the caller only wanted the boolean)
static void err_set(char* p_err, size_t p_capacity, const char* p_format, ...) {
  if (p_err == NULL || p_capacity == 0) {
    return;
  }
  va_list args;
  va_start(args, p_format);
  vsnprintf(p_err, p_capacity, p_format, args);
  va_end(args);
}

// ---------------------------------------------------------------- the wrapper

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static void* dylib_open_windows(void* p_ctx, const char* p_path, char* p_err, size_t p_err_capacity) {
  (void)p_ctx;
  HMODULE handle = LoadLibraryA(p_path);
  if (handle == NULL) {
    err_set(p_err, p_err_capacity, "LoadLibrary failed with GetLastError %lu.", (unsigned long)GetLastError());
    return NULL;
  }
  return (void*)handle;
}

static void* dylib_sym_windows(void* p_ctx, void* p_handle, const char* p_name, char* p_err, size_t p_err_capacity) {
  (void)p_ctx;
  FARPROC sym = GetProcAddress((HMODULE)p_handle, p_name);
  if (sym == NULL) {
    err_set(p_err, p_err_capacity, "GetProcAddress('%s') failed with GetLastError %lu.", p_name,
            (unsigned long)GetLastError());
    return NULL;
  }
  return (void*)sym;
}

static void dylib_close_windows(void* p_ctx, void* p_handle) {
  (void)p_ctx;
  FreeLibrary((HMODULE)p_handle);
}

static const ok_dylib g_dylib_windows = {
    dylib_open_windows,
    dylib_sym_windows,
    dylib_close_windows,
    NULL,
};

const ok_dylib* ok_dylib_platform(void) {
  return &g_dylib_windows;
}

const char* ok_dylib_extension(void) {
  return ".dll";
}

#elif defined(__unix__) || defined(__APPLE__) || defined(__HAIKU__)

#include <dlfcn.h>

static void* dylib_open_posix(void* p_ctx, const char* p_path, char* p_err, size_t p_err_capacity) {
  (void)p_ctx;
  // RTLD_LOCAL (default on most, explicit everywhere): a module's symbols
  // never enter another's namespace. RTLD_NOW: resolve at open, missing
  // symbols refuse the load instead of firing mid-run.
  void* handle = dlopen(p_path, RTLD_NOW | RTLD_LOCAL);
  if (handle == NULL) {
    const char* reason = dlerror();
    err_set(p_err, p_err_capacity, "%s", reason != NULL ? reason : "dlopen failed.");
    return NULL;
  }
  return handle;
}

static void* dylib_sym_posix(void* p_ctx, void* p_handle, const char* p_name, char* p_err, size_t p_err_capacity) {
  (void)p_ctx;
  (void)dlerror(); // clear any stale error before the probe
  void* sym = dlsym(p_handle, p_name);
  if (sym == NULL) {
    const char* reason = dlerror();
    err_set(p_err, p_err_capacity, "%s", reason != NULL ? reason : "dlsym failed.");
    return NULL;
  }
  return sym;
}

static void dylib_close_posix(void* p_ctx, void* p_handle) {
  (void)p_ctx;
  dlclose(p_handle);
}

static const ok_dylib g_dylib_posix = {
    dylib_open_posix,
    dylib_sym_posix,
    dylib_close_posix,
    NULL,
};

const ok_dylib* ok_dylib_platform(void) {
  return &g_dylib_posix;
}

const char* ok_dylib_extension(void) {
  return ".so";
}

#else

// nothing: this platform has no dynamic loader. the host-provided ok_dylib
// hooks (ok_specs) are the lane here, or natives stay off entirely.
const ok_dylib* ok_dylib_platform(void) {
  return NULL;
}

const char* ok_dylib_extension(void) {
  return ".so"; // inert: no probe ever runs without a dylib table
}

#endif

// ---------------------------------------------------------------- the default loader lane

// the api table lives in okapi.c; the loader hands it to ok_native_export.
const ok_api* ok_api_table(void);

// the default lane's handle bookkeeping: every handle this loader opens is
// appended to the instance's list and closed at ok_free (after the
// on_unload sweep). host-installed load_native implementations manage their
// own handles, we never see theirs.
static bool track_handle(module_system* p_modules, void* p_handle) {
  if (p_handle == NULL) {
    return true;
  }
  if (p_modules->native_handles_count == p_modules->native_handles_capacity) {
    const uint32_t grown = p_modules->native_handles_capacity == 0 ? 4 : p_modules->native_handles_capacity * 2;
    void** handles = p_modules->ok->raw_alloc.reallocate(p_modules->native_handles, sizeof(void*) * grown);
    if (handles == NULL) {
      return false;
    }
    p_modules->native_handles = handles;
    p_modules->native_handles_capacity = grown;
  }
  p_modules->native_handles[p_modules->native_handles_count++] = p_handle;
  return true;
}

bool ok_load_native_default(ok* p_ok,
                            const char* p_path,
                            const ok_native_module** p_out,
                            void** p_handle,
                            char* p_err,
                            size_t p_err_capacity) {
  err_set(p_err, p_err_capacity, "%s", "");
  const ok_dylib* dylib = p_ok->dylib;
  if (dylib == NULL) {
    err_set(p_err, p_err_capacity,
            "this ok instance has no dynamic loading: the platform provides none and ok_specs.dylib is NULL.");
    return false;
  }
  // the existence probe FIRST, so an ordinary miss stays clean: the loader's
  // contract distinguishes "nothing native here" (empty message, the search
  // continues) from "a native candidate exists but is broken" (a message,
  // the hard error). a file that is not a regular file is a miss, not a
  // broken native (the ladder's directory candidates die here).
  struct stat info;
  if (stat(p_path, &info) != 0 || !S_ISREG(info.st_mode)) {
    return false; // clean miss: no message
  }
  char err[256];
  void* handle = dylib->open(dylib->p_ctx, p_path, err, sizeof(err));
  if (handle == NULL) {
    err_set(p_err, p_err_capacity, "%s", err);
    return false;
  }
  ok_native_export_fn export_fn =
      (ok_native_export_fn)(uintptr_t)dylib->sym(dylib->p_ctx, handle, "ok_native_export", err, sizeof(err));
  if (export_fn == NULL) {
    dylib->close(dylib->p_ctx, handle);
    err_set(p_err, p_err_capacity, "%s", err);
    return false;
  }
  const ok_native_module* descriptor = export_fn(ok_api_table());
  if (descriptor == NULL) {
    dylib->close(dylib->p_ctx, handle);
    err_set(p_err, p_err_capacity, "ok_native_export returned NULL.");
    return false;
  }
  if (!track_handle(&p_ok->modules, handle)) {
    dylib->close(dylib->p_ctx, handle);
    err_set(p_err, p_err_capacity, "out of memory while tracking the module handle.");
    return false;
  }
  *p_out = descriptor;
  *p_handle = handle;
  return true;
}

// the loader-vtable shape of the default lane, installed by ok_modules_init
// next to the fs read (okmodules.c): p_ctx is the module system (both
// halves reach what they need through it).
bool ok_load_native_thunk(void* p_ctx,
                          const char* p_path,
                          const ok_api* p_api,
                          const ok_native_module** p_out,
                          void** p_handle,
                          char* p_err,
                          size_t p_err_capacity) {
  (void)p_api; // the default lane always hands out the shared static table
  return ok_load_native_default(((module_system*)p_ctx)->ok, p_path, p_out, p_handle, p_err, p_err_capacity);
}
