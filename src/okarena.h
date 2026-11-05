#ifndef OK_ARENA_H
#define OK_ARENA_H

#include <stddef.h>

#include "okfwd.h"
#include "okmm.h"

// the parse arena (P5, the front-door patch): a region allocator the whole
// AST rides. parse-time nodes, list buffers and diagnostic temporaries
// bump-allocate out of chained blocks; release is a no-op; one deinit frees
// the world. two hardening classes die here at once: a failed parse can
// never leak a half-built statement (the orphan nodes die with the arena
// whenever the caller drops the result), and teardown is a block walk, not
// a recursive one (a deep tree can never overflow the C stack on its way
// OUT either - the depth guard bounds the way in, the arena the way out).
//
// what keeps this honest: nothing the compiler keeps survives the parse.
// string literals intern into copies (D20), names ride interned constants,
// locals are compile-time views into the source (which outlives the AST in
// every driver). the arena frees exactly where the per-node deinits freed
// before - at parse_result_deinit - so no lifetime moves, only the shape of
// the free changes.
typedef struct ok_arena_block ok_arena_block;

typedef struct {
  ok_arena_block* head; // the newest block; allocations bump down its payload
  allocators* source;   // where blocks come from (the caller's allocators)
  size_t total;         // bytes handed out (telemetry; blocks free by walk)
} ok_arena;

void ok_arena_init(ok_arena* p_arena, allocators* p_source);

// ownership handoff: p_from's blocks become p_to's, p_from restarts empty.
// the parse hands its arena to the result this way - the parser itself is
// stack-scoped in every caller, the result outlives it.
void ok_arena_move(ok_arena* p_to, ok_arena* p_from);

// frees every block. the struct is init-shaped afterwards (empty, reusable).
void ok_arena_deinit(ok_arena* p_arena);

// the allocators vtable over an arena. allocate bumps; reallocate bumps and
// copies (the old bytes stay behind until the bulk free - growth wastes a
// little, parse-time never minds); release is a no-op. the arena is reached
// through p_alloc->impl.
void* ok_arena_allocate(allocators* p_alloc, size_t p_size);
void* ok_arena_reallocate(allocators* p_alloc, void* p_ptr, size_t p_old_size, size_t p_new_size);
void ok_arena_release(allocators* p_alloc, void* p_ptr);

// build the vtable. the returned allocators carry no gc hook (arena bytes
// are raw AST memory, not collector objects; the BLOCK allocations tickle
// the source's hook on their own).
allocators ok_arena_allocators(ok_arena* p_arena);

#endif // OK_ARENA_H
