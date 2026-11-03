#include <stddef.h>
#include <string.h>

#include "okintern.h"
#include "okobject.h"

// the set's load factor ceiling: grow at 2/3 full, same family as okmap.
#define INTERN_MAX_LOAD_NUM 2
#define INTERN_MAX_LOAD_DEN 3

static uint32_t interner_capacity_for(uint32_t p_count) {
  uint32_t capacity = 8;
  while (capacity * INTERN_MAX_LOAD_NUM / INTERN_MAX_LOAD_DEN <= p_count) {
    capacity <<= 1u;
  }
  return capacity;
}

void string_interner_init(string_interner* p_interner, allocators* p_alloc) {
  p_interner->slots = NULL;
  p_interner->hashes = NULL;
  p_interner->count = 0;
  p_interner->capacity = 0;
  p_interner->alloc = p_alloc;
}

void string_interner_deinit(string_interner* p_interner) {
  // entries themselves are object_store-owned; only the index dies here
  if (p_interner->slots != NULL) {
    p_interner->alloc->release(p_interner->alloc, p_interner->slots);
    p_interner->slots = NULL;
  }
  if (p_interner->hashes != NULL) {
    p_interner->alloc->release(p_interner->alloc, p_interner->hashes);
    p_interner->hashes = NULL;
  }
  p_interner->count = 0;
  p_interner->capacity = 0;
}

// probe for p_hash + p_view in a table of p_capacity slots. returns the slot
// index: occupied + content-equal = hit; the first empty slot = miss target.
static uint32_t interner_probe(object_string** p_slots,
                               const hash_t* p_hashes,
                               uint32_t p_capacity,
                               hash_t p_hash,
                               ok_string_view p_view) {
  uint32_t i = (uint32_t)p_hash & (p_capacity - 1u);
  for (;;) {
    object_string* entry = p_slots[i];
    if (entry == NULL) {
      return i; // miss
    }
    if (p_hashes[i] == p_hash) {
      const uint64_t length = ok_string_view_get_length(&p_view);
      if (string_get_length(&entry->string) == length &&
          memcmp(entry->string.chars, p_view.chars, length) == 0) {
        return i; // hit
      }
    }
    i = (i + 1u) & (p_capacity - 1u);
  }
}

static bool interner_grow(string_interner* p_interner) {
  const uint32_t new_capacity = interner_capacity_for(p_interner->count);
  object_string** new_slots = (object_string**)p_interner->alloc->allocate(p_interner->alloc, new_capacity * sizeof(object_string*));
  hash_t* new_hashes = (hash_t*)p_interner->alloc->allocate(p_interner->alloc, new_capacity * sizeof(hash_t));
  if (new_slots == NULL || new_hashes == NULL) {
    if (new_slots != NULL) {
      p_interner->alloc->release(p_interner->alloc, new_slots);
    }
    if (new_hashes != NULL) {
      p_interner->alloc->release(p_interner->alloc, new_hashes);
    }
    return false;
  }
  for (uint32_t i = 0; i < new_capacity; ++i) {
    new_slots[i] = NULL;
  }
  // re-insert from the cached hashes: stored strings are never re-hashed
  for (uint32_t i = 0; i < p_interner->capacity; ++i) {
    const object_string* entry = p_interner->slots[i];
    if (entry == NULL) {
      continue;
    }
    const uint32_t j = (uint32_t)p_interner->hashes[i] & (new_capacity - 1u);
    // the new table is fresh: the first probe miss is the insert point
    uint32_t k = j;
    while (new_slots[k] != NULL) {
      k = (k + 1u) & (new_capacity - 1u);
    }
    new_slots[k] = (object_string*)entry;
    new_hashes[k] = p_interner->hashes[i];
  }
  if (p_interner->slots != NULL) {
    p_interner->alloc->release(p_interner->alloc, p_interner->slots);
  }
  if (p_interner->hashes != NULL) {
    p_interner->alloc->release(p_interner->alloc, p_interner->hashes);
  }
  p_interner->slots = new_slots;
  p_interner->hashes = new_hashes;
  p_interner->capacity = new_capacity;
  return true;
}

object_string* string_interner_find(const string_interner* p_interner, ok_string_view p_view) {
  if (p_interner->capacity == 0) {
    return NULL;
  }
  const hash_t hash = default_hash_str(p_view);
  const uint32_t i = interner_probe(p_interner->slots,
                                    p_interner->hashes,
                                    p_interner->capacity,
                                    hash,
                                    p_view);
  return p_interner->slots[i];
}

object_string* string_interner_intern(string_interner* p_interner, ok_string_view p_view, object_specs* p_specs) {
  // growth first when needed, so the insert below never grows mid-flight
  if (p_interner->capacity == 0 ||
      (p_interner->count + 1u) * INTERN_MAX_LOAD_DEN > p_interner->capacity * INTERN_MAX_LOAD_NUM) {
    if (!interner_grow(p_interner)) {
      return NULL;
    }
  }
  const hash_t hash = default_hash_str(p_view);
  uint32_t i = interner_probe(p_interner->slots, p_interner->hashes, p_interner->capacity, hash, p_view);
  if (p_interner->slots[i] != NULL) {
    return p_interner->slots[i];
  }
  // miss: create the canonical object, then insert. the creation may collect
  // (aggressive mode); the new string is unrooted until the slot assignment
  // below, and the assignment itself cannot allocate. the bare creator is
  // load-bearing here: the public create_object_string routes through the
  // interner, and a canonical object must not re-enter it.
  object_string* fresh = create_object_string_uninterned(p_view, p_specs);
  if (fresh == NULL) {
    return NULL;
  }
  // the probe index is stale-proof: nothing inserted between probe and here
  // (the only allocation was the string itself, and inserts are ours alone)
  p_interner->slots[i] = fresh;
  p_interner->hashes[i] = hash;
  p_interner->count += 1u;
  return fresh;
}

void string_interner_remove_at(string_interner* p_interner, uint32_t p_slot) {
  // the collector's deletion: p_slot is a slot the sweep is standing on (its
  // entry is known unmarked), so there is nothing to probe. the algorithm is
  // the okmap family's backward-shift: the freed slot pulls backward any
  // following entry whose probe chain crosses it, keeping every survivor
  // reachable from its home slot. no tombstones, no rehash.
  if (p_interner->capacity == 0 || p_slot >= p_interner->capacity || p_interner->slots[p_slot] == NULL) {
    return; // defensive only: the sweep never hands us an empty slot
  }
  const uint32_t mask = p_interner->capacity - 1u;
  uint32_t hole = p_slot;
  for (;;) {
    p_interner->slots[hole] = NULL;
    uint32_t k = hole;
    for (;;) {
      k = (k + 1u) & mask;
      if (p_interner->slots[k] == NULL) { // cluster ended: the hole is final
        p_interner->count -= 1u;
        return;
      }
      const uint32_t home = (uint32_t)p_interner->hashes[k] & mask;
      // the entry at k may fill the hole iff its home is at-or-before the
      // hole: distance(home -> k) >= distance(hole -> k), cyclically.
      // otherwise keep walking.
      if (((k - home) & mask) >= ((k - hole) & mask)) {
        break;
      }
    }
    p_interner->slots[hole] = p_interner->slots[k];
    p_interner->hashes[hole] = p_interner->hashes[k];
    hole = k;
  }
}
