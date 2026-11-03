#ifndef OK_INTERN_H
#define OK_INTERN_H

#include <stdbool.h>
#include <stdint.h>

#include "okobject.h"
#include "okutils.h"

// the string interner: canonicalizes compiler-made strings (identifiers,
// literals, member names, the vm's startup names) so name lookups can hit
// pointer equality before falling back to content comparison. RUNTIME-built
// strings (concatenation, number formatting, host-created values) are never
// interned: their population is unbounded and equality stays content-based,
// which the probe fast paths handle by falling through on pointer mismatch.
//
// lifetime policy: the table holds STRONG references and the gc roots it
// (gc_watch_interner / mark_interner). interned strings are therefore
// immortal - identical to the lifetime they already have as chunk constants
// or class-table keys, so this changes no observable lifetime. when the
// pipeline-isolation spec lands, interning moves to its commit phase and the
// table becomes the module registry's canonical-name source.

typedef struct {
  // open-addressed set of canonical object_string*: parallel slots + cached
  // hashes, linear probing, power-of-two capacity, no tombstones (eviction
  // never happens - the table only grows).
  object_string** slots;
  hash_t* hashes; // cached; garbage in empty slots, read after the pointer check
  uint32_t count;
  uint32_t capacity; // always a power of two (or zero pre-first-insert)
  allocators* alloc;
} string_interner;

void string_interner_init(string_interner* p_interner, allocators* p_alloc);
void string_interner_deinit(string_interner* p_interner);

// the canonical string for p_view: an existing entry when the content is
// already interned, otherwise a freshly created one (created + inserted
// through p_specs). the returned string is rooted by the interner itself.
//
// gc discipline: the insert may grow the table, whose allocation can collect;
// the fresh string is created BEFORE inserting and is NOT yet reachable, so
// callers that create-then-intern through a cold path must guard across this
// call. (the compiler sites follow the existing add_string_constant shape and
// already do; the vm's singleton sites assign into rooted slots immediately.)
object_string* string_interner_intern(string_interner* p_interner, ok_string_view p_view, object_specs* p_specs);

// probe only: the canonical string when one exists, NULL otherwise. no
// allocation, no side effects - the freeze pass and diagnostics use this to
// reuse spellings without creating.
object_string* string_interner_find(const string_interner* p_interner, ok_string_view p_view);

#endif // OK_INTERN_H
