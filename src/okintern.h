#ifndef OK_INTERN_H
#define OK_INTERN_H

#include <stdbool.h>
#include <stdint.h>

#include "okobject.h"
#include "okutils.h"

// the string interner: THE canonicalizer for every ok string (D20) -
// compiler names (identifiers, member spellings, the vm's startup names) AND
// runtime values (literals, concatenation, conversions, host-created strings)
// all settle on one canonical object per content, so identity (===) is content
// truth and every probe table hits the pointer fast path first.
//
// lifetime policy: the table is a WEAK set - it roots nothing. mark never
// walks it; the collector's sweep pre-pass removes unmarked entries
// (backward-shift, no tombstones - the okmap family discipline) BEFORE the
// object walk frees them, so a dead canonical string can never dangle in a
// probe chain. an interned string lives exactly as long as something else
// holds it: a chunk constant, a class table, a register, a host root. the
// one cost of the doctrine: a string created and dropped in a loop re-hashes
// on each rebirth (a probe, no allocation when the content still lives
// somewhere - the common case for names and hot literals).
//
// the table is also the module registry's canonical-name source when the
// modules wave lands (keyed, deduped paths).

typedef struct {
  // open-addressed set of canonical object_string*: parallel slots + cached
  // hashes, linear probing, power-of-two capacity, no tombstones (deletion
  // is the collector's backward-shift, which keeps every survivor reachable
  // from its home slot).
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
// through p_specs). the table is WEAK: the returned string is NOT rooted by
// the interner - the caller roots it (guard, register/constant assignment,
// adopting structure) exactly like any create_object_string result.
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

// remove the entry at p_slot (backward-shift). the collector's sweep
// pre-pass is the only caller: it passes the slot it is standing on, so
// there is no probe and no failure mode. the shifted-in entry (if any)
// lands at p_slot - the caller re-examines the same index.
void string_interner_remove_at(string_interner* p_interner, uint32_t p_slot);

#endif // OK_INTERN_H
