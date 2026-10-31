#ifndef OK_MAP_H
#define OK_MAP_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "okmm.h"
#include "okstring.h"

// open-addressed hash map: linear probing, parallel key/hash/value arrays,
// backward-shift deletion (no tombstones). replaces oktable.h's separate
// chaining
//
// MAP_DECLARE(name, key_type, value_type, probe_type)
// MAP_DEFINE(name, key_type, value_type, probe_type,
//            KEY_IS_EMPTY(k), KEY_SET_EMPTY(k),
//            PROBE_HASH(p), PROBE_MATCHES(p, k),
//            KEY_CLONE(p, a), KEY_DEINIT(k, a), VALUE_DEINIT(v, a))
//
// contract:
//   - an EMPTY slot is one whose key satisfies KEY_IS_EMPTY. the empty
//     sentinel must never be a storable key (the language's map object uses
//     the strict-null value for this; this table uses chars == NULL).
//   - PROBE_HASH is computed once per operation, on the PROBE. the hash of a
//     stored entry is cached in the parallel hashes[] array at insert time
//     and is NEVER recomputed: growing re-inserts from the cache. probe keys
//     do not have to be stored keys (globals_store probes with a string view,
//     zero allocations; interning will probe with raw char*).
//   - PROBE_MATCHES(p, k) compares a probe against a stored key. it must
//     agree with PROBE_HASH: equal probes imply equal hashes.
//   - KEY_CLONE(p, a) builds an OWNED stored key from a probe. it may
//     allocate; it is only called on the insert path, after the table has
//     successfully made room, so a failed clone never leaves partial state.
//   - KEY_DEINIT/VALUE_DEINIT release owned keys/values at remove/deinit
//     time. both are no-ops for the current users.
//   - capacity is 0 before the first insert, then a power of two >= 8. load
//     factor is kept <= 0.7 ((count + 1) * 10 > capacity * 7 grows x2).
//     because an empty slot always exists, probes terminate.
//   - remove uses backward-shift deletion: the freed slot pulls backward any
//     following entry in its cluster that is allowed to live there, so every
//     entry stays reachable from its home slot. no tombstones, no rehash.

#define MAP_DECLARE(name, key_type, value_type, probe_type)                                                            \
  typedef struct name name;                                                                                            \
  struct name {                                                                                                        \
    allocators* alloc;                                                                                                 \
    uint32_t count;    /* live entries */                                                                              \
    uint32_t capacity; /* 0, or power of two >= 8 */                                                                   \
    key_type* keys;    /* parallel arrays: probes touch hashes + keys only */                                          \
    hash_t* hashes;    /* cached; garbage in empty slots, read after the key check */                                  \
    value_type* values;                                                                                                \
  };                                                                                                                   \
  void name##_init(name* p_map, allocators* p_alloc);                                                                  \
  void name##_deinit(name* p_map);                                                                                     \
  value_type* name##_get(const name* p_map, probe_type p_probe);                                                       \
  bool name##_set(name* p_map, probe_type p_probe, value_type p_value); /* update-or-insert; false = OOM only */       \
  bool name##_remove(name* p_map, probe_type p_probe);                  /* false = absent */

#define MAP_DEFINE(name,                                                                                               \
                   key_type,                                                                                           \
                   value_type,                                                                                         \
                   probe_type,                                                                                         \
                   KEY_IS_EMPTY,                                                                                       \
                   KEY_SET_EMPTY,                                                                                      \
                   PROBE_HASH,                                                                                         \
                   PROBE_MATCHES,                                                                                      \
                   KEY_CLONE,                                                                                          \
                   KEY_DEINIT,                                                                                         \
                   VALUE_DEINIT)                                                                                       \
                                                                                                                       \
  void name##_init(name* p_map, allocators* p_alloc) {                                                                 \
    p_map->alloc = p_alloc;                                                                                            \
    p_map->count = 0;                                                                                                  \
    p_map->capacity = 0;                                                                                               \
    p_map->keys = NULL;                                                                                                \
    p_map->hashes = NULL;                                                                                              \
    p_map->values = NULL;                                                                                              \
  }                                                                                                                    \
                                                                                                                       \
  void name##_deinit(name* p_map) {                                                                                    \
    if (p_map->keys != NULL) {                                                                                         \
      for (uint32_t i = 0; i < p_map->capacity; ++i) {                                                                 \
        if (!KEY_IS_EMPTY(p_map->keys[i])) {                                                                           \
          KEY_DEINIT(&p_map->keys[i], p_map->alloc);                                                                   \
          VALUE_DEINIT(&p_map->values[i], p_map->alloc);                                                               \
        }                                                                                                              \
      }                                                                                                                \
      p_map->alloc->release(p_map->alloc, p_map->keys);                                                                \
      p_map->alloc->release(p_map->alloc, p_map->hashes);                                                              \
      p_map->alloc->release(p_map->alloc, p_map->values);                                                              \
    }                                                                                                                  \
    p_map->keys = NULL;                                                                                                \
    p_map->hashes = NULL;                                                                                              \
    p_map->values = NULL;                                                                                              \
    p_map->capacity = 0;                                                                                               \
    p_map->count = 0;                                                                                                  \
  }                                                                                                                    \
                                                                                                                       \
  static uint32_t name##_find_slot(const name* p_map, hash_t p_hash, probe_type p_probe) {                             \
    if (p_map->capacity == 0) {                                                                                        \
      return UINT32_MAX;                                                                                               \
    }                                                                                                                  \
    const uint32_t mask = p_map->capacity - 1;                                                                         \
    uint32_t i = (uint32_t)p_hash & mask;                                                                              \
    while (!KEY_IS_EMPTY(p_map->keys[i])) {                                                                            \
      if (p_map->hashes[i] == p_hash && PROBE_MATCHES(p_probe, p_map->keys[i])) {                                      \
        return i;                                                                                                      \
      }                                                                                                                \
      i = (i + 1) & mask;                                                                                              \
    }                                                                                                                  \
    return UINT32_MAX;                                                                                                 \
  }                                                                                                                    \
                                                                                                                       \
  static bool name##_grow(name* p_map) {                                                                               \
    const uint32_t new_capacity = p_map->capacity == 0 ? 8 : p_map->capacity * 2;                                      \
    key_type* new_keys = (key_type*)p_map->alloc->allocate(p_map->alloc, new_capacity * sizeof(key_type));             \
    hash_t* new_hashes = (hash_t*)p_map->alloc->allocate(p_map->alloc, new_capacity * sizeof(hash_t));                 \
    value_type* new_values = (value_type*)p_map->alloc->allocate(p_map->alloc, new_capacity * sizeof(value_type));     \
    if (new_keys == NULL || new_hashes == NULL || new_values == NULL) {                                                \
      if (new_keys != NULL) {                                                                                          \
        p_map->alloc->release(p_map->alloc, new_keys);                                                                 \
      }                                                                                                                \
      if (new_hashes != NULL) {                                                                                        \
        p_map->alloc->release(p_map->alloc, new_hashes);                                                               \
      }                                                                                                                \
      if (new_values != NULL) {                                                                                        \
        p_map->alloc->release(p_map->alloc, new_values);                                                               \
      }                                                                                                                \
      return false;                                                                                                    \
    }                                                                                                                  \
    for (uint32_t i = 0; i < new_capacity; ++i) {                                                                      \
      KEY_SET_EMPTY(&new_keys[i]);                                                                                     \
    }                                                                                                                  \
    /* re-insert from the cached hashes: stored keys are never re-hashed */                                            \
    for (uint32_t i = 0; i < p_map->capacity; ++i) {                                                                   \
      if (KEY_IS_EMPTY(p_map->keys[i])) {                                                                              \
        continue;                                                                                                      \
      }                                                                                                                \
      uint32_t j = (uint32_t)p_map->hashes[i] & (new_capacity - 1);                                                    \
      while (!KEY_IS_EMPTY(new_keys[j])) {                                                                             \
        j = (j + 1) & (new_capacity - 1);                                                                              \
      }                                                                                                                \
      new_keys[j] = p_map->keys[i];                                                                                    \
      new_hashes[j] = p_map->hashes[i];                                                                                \
      new_values[j] = p_map->values[i];                                                                                \
    }                                                                                                                  \
    if (p_map->keys != NULL) {                                                                                         \
      p_map->alloc->release(p_map->alloc, p_map->keys);                                                                \
      p_map->alloc->release(p_map->alloc, p_map->hashes);                                                              \
      p_map->alloc->release(p_map->alloc, p_map->values);                                                              \
    }                                                                                                                  \
    p_map->keys = new_keys;                                                                                            \
    p_map->hashes = new_hashes;                                                                                        \
    p_map->values = new_values;                                                                                        \
    p_map->capacity = new_capacity;                                                                                    \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  value_type* name##_get(const name* p_map, probe_type p_probe) {                                                      \
    const uint32_t slot = name##_find_slot(p_map, PROBE_HASH(p_probe), p_probe);                                       \
    return slot == UINT32_MAX ? NULL : &p_map->values[slot];                                                           \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_set(name* p_map, probe_type p_probe, value_type p_value) {                                               \
    const hash_t hash = PROBE_HASH(p_probe);                                                                           \
    uint32_t slot = name##_find_slot(p_map, hash, p_probe);                                                            \
    if (slot != UINT32_MAX) {                                                                                          \
      VALUE_DEINIT(&p_map->values[slot], p_map->alloc);                                                                \
      p_map->values[slot] = p_value;                                                                                   \
      return true;                                                                                                     \
    }                                                                                                                  \
    if (p_map->capacity == 0 || (uint64_t)(p_map->count + 1) * 10 > (uint64_t)p_map->capacity * 7) {                   \
      if (!name##_grow(p_map)) {                                                                                       \
        return false;                                                                                                  \
      }                                                                                                                \
      slot = name##_find_slot(p_map, hash, p_probe);                                                                   \
      if (slot != UINT32_MAX) { /* grew into an existing key: update */                                                \
        VALUE_DEINIT(&p_map->values[slot], p_map->alloc);                                                              \
        p_map->values[slot] = p_value;                                                                                 \
        return true;                                                                                                   \
      }                                                                                                                \
    }                                                                                                                  \
    const uint32_t mask = p_map->capacity - 1;                                                                         \
    uint32_t i = (uint32_t)hash & mask;                                                                                \
    while (!KEY_IS_EMPTY(p_map->keys[i])) { /* an empty slot exists: load <= 0.7 */                                    \
      i = (i + 1) & mask;                                                                                              \
    }                                                                                                                  \
    p_map->keys[i] = KEY_CLONE(p_probe, p_map->alloc);                                                                 \
    p_map->hashes[i] = hash;                                                                                           \
    p_map->values[i] = p_value;                                                                                        \
    p_map->count++;                                                                                                    \
    return true;                                                                                                       \
  }                                                                                                                    \
                                                                                                                       \
  bool name##_remove(name* p_map, probe_type p_probe) {                                                                \
    const hash_t hash = PROBE_HASH(p_probe);                                                                           \
    const uint32_t slot = name##_find_slot(p_map, hash, p_probe);                                                      \
    if (slot == UINT32_MAX) {                                                                                          \
      return false;                                                                                                    \
    }                                                                                                                  \
    KEY_DEINIT(&p_map->keys[slot], p_map->alloc);                                                                      \
    VALUE_DEINIT(&p_map->values[slot], p_map->alloc);                                                                  \
    const uint32_t mask = p_map->capacity - 1;                                                                         \
    uint32_t hole = slot;                                                                                              \
    for (;;) {                                                                                                         \
      KEY_SET_EMPTY(&p_map->keys[hole]);                                                                               \
      uint32_t k = hole;                                                                                               \
      for (;;) {                                                                                                       \
        k = (k + 1) & mask;                                                                                            \
        if (KEY_IS_EMPTY(p_map->keys[k])) { /* cluster ended: the hole is final */                                     \
          p_map->count--;                                                                                              \
          return true;                                                                                                 \
        }                                                                                                              \
        const uint32_t home = (uint32_t)p_map->hashes[k] & mask;                                                       \
        /* the entry at k may fill the hole iff its home is at-or-before the hole: */                                  \
        /* distance(home -> k) >= distance(hole -> k), cyclically. otherwise keep walking. */                          \
        if (((k - home) & mask) >= ((k - hole) & mask)) {                                                              \
          break;                                                                                                       \
        }                                                                                                              \
      }                                                                                                                \
      p_map->keys[hole] = p_map->keys[k];                                                                              \
      p_map->hashes[hole] = p_map->hashes[k];                                                                          \
      p_map->values[hole] = p_map->values[k];                                                                          \
      hole = k;                                                                                                        \
    }                                                                                                                  \
  }

#endif // OK_MAP_H
