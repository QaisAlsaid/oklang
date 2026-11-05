#include <string.h>

#include "okarena.h"

// block sizing: 16 KiB covers a few hundred nodes, so ordinary statements
// ride one block; a pathological file just chains more. anything larger
// than the standard block gets a private one.
#define OK_ARENA_BLOCK_SIZE ((size_t)16384)

// alignment: twice the pointer width (16 on every host we build for today)
// is enough for the AST's structs and the lists' element arrays.
#define OK_ARENA_ALIGN ((size_t)(sizeof(void*) * 2))

struct ok_arena_block {
  ok_arena_block* next;
  size_t used; // payload bytes consumed (always kept aligned)
  size_t cap;  // payload bytes available
  // the payload rides the struct's tail: (char*)(block + 1)
};

static size_t arena_align(size_t p_size) {
  return (p_size + (OK_ARENA_ALIGN - 1)) & ~(OK_ARENA_ALIGN - 1);
}

void ok_arena_init(ok_arena* p_arena, allocators* p_source) {
  p_arena->head = NULL;
  p_arena->source = p_source;
  p_arena->total = 0;
}

void ok_arena_move(ok_arena* p_to, ok_arena* p_from) {
  p_to->head = p_from->head;
  p_to->source = p_from->source;
  p_to->total = p_from->total;
  p_from->head = NULL;
  p_from->total = 0;
}

static ok_arena_block* arena_new_block(ok_arena* p_arena, size_t p_payload) {
  const size_t cap = p_payload > OK_ARENA_BLOCK_SIZE ? arena_align(p_payload) : OK_ARENA_BLOCK_SIZE;
  ok_arena_block* block = (ok_arena_block*)p_arena->source->allocate(p_arena->source, sizeof(ok_arena_block) + cap);
  if (block == NULL) {
    return NULL;
  }
  block->next = p_arena->head;
  block->used = 0;
  block->cap = cap;
  return block;
}

void* ok_arena_allocate(allocators* p_alloc, size_t p_size) {
  ok_arena* arena = (ok_arena*)p_alloc->impl;
  size_t need = arena_align(p_size);
  if (need == 0) {
    need = OK_ARENA_ALIGN; // a zero-size ask still deserves a real pointer
  }
  if (arena->head == NULL || arena->head->cap - arena->head->used < need) {
    ok_arena_block* fresh = arena_new_block(arena, need);
    if (fresh == NULL) {
      return NULL;
    }
    arena->head = fresh;
  }
  void* out = (char*)(arena->head + 1) + arena->head->used;
  arena->head->used += need;
  arena->total += need;
  return out;
}

void* ok_arena_reallocate(allocators* p_alloc, void* p_ptr, size_t p_old_size, size_t p_new_size) {
  if (p_new_size == 0) {
    ok_arena_release(p_alloc, p_ptr);
    return NULL;
  }
  if (p_ptr == NULL) {
    return ok_arena_allocate(p_alloc, p_new_size);
  }
  void* fresh = ok_arena_allocate(p_alloc, p_new_size);
  if (fresh == NULL) {
    return NULL;
  }
  memcpy(fresh, p_ptr, p_old_size < p_new_size ? p_old_size : p_new_size);
  return fresh;
}

void ok_arena_release(allocators* p_alloc, void* p_ptr) {
  (void)p_alloc;
  (void)p_ptr; // the bulk free owns everything
}

void ok_arena_deinit(ok_arena* p_arena) {
  ok_arena_block* block = p_arena->head;
  while (block != NULL) {
    ok_arena_block* next = block->next;
    p_arena->source->release(p_arena->source, block);
    block = next;
  }
  p_arena->head = NULL;
  p_arena->total = 0;
}

allocators ok_arena_allocators(ok_arena* p_arena) {
  allocators alloc;
  alloc.reallocate = ok_arena_reallocate;
  alloc.allocate = ok_arena_allocate;
  alloc.release = ok_arena_release;
  alloc.gc = NULL;
  alloc.raw_allocators = p_arena->source != NULL ? p_arena->source->raw_allocators : NULL;
  alloc.impl = p_arena;
  return alloc;
}
