#ifndef OK_ENV_H
#define OK_ENV_H

// ok_env: the environment door (the P4 audit). libok's own OKPATH read
// and std.sys's env surface ride these, and nothing else in the library
// touches the environment. get() answers the borrowed live entry (the
// libc contract - copy it before mutating); set/unset take effect in the
// process environment the host gave ok.

#include <ok/ok.h>

#include <stdbool.h>
#include <stdint.h>

// the entry's value, or NULL when the name is unset. borrowed.
const char* ok_env_get(const char* p_name);

// false on an invalid name (embedded '=' or NUL - EINVAL's lanes).
bool ok_env_set(const char* p_name, const char* p_value);

// idempotent: unsetting an unset name succeeds.
bool ok_env_unset(const char* p_name);

// the environment's names, sorted by strcmp, deduped (the determinism
// rule). each name and the array itself come from p_alloc; the caller
// releases them all.
bool ok_env_list(const ok_allocators* p_alloc, char*** p_out_names, uint32_t* p_out_count);

#endif // OK_ENV_H
