#ifndef OK_CHUNK_HPP
#define OK_CHUNK_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ok/ok_source.h"
#include "okarray.h"
#include "okdef.h"
#include "okfwd.h"
#include "okindex.h"
#include "okstring.h"
#include "okvalue.h"

typedef index_wrapper constant;

typedef enum {
  CONSTANT_OK = 0,
  CONSTANT_ALLOCATION_FAILED,
  CONSTANT_OVERFLOW,
} constant_errors;

typedef enum {
  CONSTANT_FLAG_NONE = 0,
} constant_flags;

typedef index_wrapper identifier;

typedef enum {
  IDENTIFIER_OK = 0,
  IDENTIFIER_ALLOCATION_FAILED,
  IDENTIFIER_OVERFLOW,
} identifier_errors;

typedef enum {
  IDENTIFIER_FLAG_NONE = 0,
} identifier_flags;

#define IS_CONSTANT_VALID(cw) (cw.errors == CONSTANT_OK)
#define CONSTANT_RAW_INDEX(cw) (cw.index)

#define IS_IDENTIFIER_VALID(iw) (iw.errors == IDENTIFIER_OK)
#define IDENTIFIER_RAW_INDEX(iw) (iw.index)

#define CONSTANTS_MAX INDEX_MAX
#define IDENTIFIERS_MAX INDEX_MAX

// if unicode isn't rendering it's a down arrow

/*===========32bit word memory layout==========
 *
 *            (narrow mode)
 * MSB                                LSB
 *  ↓                                  ↓
 *  0000000 0 00000000 00000000 00000000
 *     ↓    ↓    ↓        ↓        ↓
 *     op   p    a        b        c
 *
 *           (wide mode)
 * MSB                               LSB
 *  ↓                                 ↓
 *  0000000 0 000000000000 000000000000
 *     ↓    ↓      ↓            ↓
 *     op   p      a'           b'
 *
 *            (large mode)
 * MSB                               LSB
 *  ↓                                 ↓
 *  0000000 0 0000000000000000 00000000
 *     ↓    ↓        ↓           ↓
 *     op   p        A          free
 *
 *            (large-ex mode)
 * MSB                               LSB
 *  ↓                                 ↓
 *  0000000 0 0000000000000000 000 00000
 *     ↓    ↓        ↓          ↓    ↓
 *     op   p        A          e  reserved

 *
 *=============================================*/

/*========================64bit 2 word memory layout============================
 *
 * MSB                                                                   LSB
 *  ↓                                                                     ↓
 *  0000000 0 0000000000000000 0000000000000000 0000000000000000 0 00000000
 *     ↓    ↓        ↓                ↓                ↓         ↓    ↓
 *     op   p        A                B                C         q   free
 *
 *==============================================================================*/

/*====================================notation===========================================
 *
 * BINARY ALWAYS READS LSB => MSB. (i.e. op always starts at offset 0 exactly).
 *
 * word(w):           32bit word size, also the maximum size of the code array.
 * op(op')(OP):       7bit, the actual opcode being dispatched.
 * p(p')(P), q, s, r: 1bit, meaning defined by each op.
 * a, b, c:           8bit, meaning defined by each op.
 * a', b':            12bit, meaning defined by each op.
 * A, B, C:           16bit, meaning defined by each op.
 * e:                 3bit, value defined by the RETURNED enum
 *
 * top:  frame top (in active calls is the same as register top but the vm separates the 2 concepts)
 * r[x]: register (*relevant* frame window)
 * k[x]: constant
 * u[x]: upvalue
 * g[x]: global (by index)
 *=======================================================================================*/

/*=========================================notes=========================================
 *
 * index max: 16bit (to any table)
 * jump max:  16bit (any jump type)
 * ip starts at an offset of exactly 1 word ahead of the current decoded instruction.
   thus the _LONG variants should increment ip by 1 word.
 *
 * jump offsets (OP_JUMP / OP_JUMP_IF / OP_JUMP_IF_SET / OP_LOOP) are measured
 * relative to the word after the last word of the jump instruction itself:
 *   target = (instruction_index + instruction_words) + offset   for forward jumps
 *   target = (instruction_index + instruction_words) - offset   for OP_LOOP
 * (instruction_words = 1 for OP_JUMP/OP_LOOP, 2 for OP_JUMP_IF/OP_JUMP_IF_SET)
 *
 * 2-word instructions are stored low 32 bits first, then high 32 bits.
 * the p flag on OP_JUMP_IF / OP_JUMP_IF_SET selects jump polarity:
 *   p == 1 -> jump when r[B] is truthy
 *   p == 0 -> jump when r[B] is falsy
 *
 *=======================================================================================*/

typedef uint32_t word;
typedef uint64_t word2;

// one monomorphic inline-cache slot, owned by the chunk and addressed by the
// third word of OP_INVOKE / OP_GET_MEMBER. klass roots the class (its tables
// are frozen, so the raw entry pointer beneath it can never dangle); method
// serves INVOKE, field_slot serves GET_MEMBER (is_field picks the lane).
struct method_entry;

typedef struct {
  value klass;
  const struct method_entry* method;
  uint16_t field_slot;
  uint8_t is_field;
  // the cache key's lane: 0 = the receiver was an instance (klass is its
  // class), 1 = the receiver was the class value itself (klass is recv).
  // one site can see both across executions, and the two lanes read
  // different storage, so the key alone must not decide.
  uint8_t recv_is_class;
} mic_entry;

// the site's storage: a small probe chain instead of a single entry, so a
// polymorphic site (two receiver classes alternating at one call) stops
// thrashing. probe order is declaration order; a miss fills the first empty
// way, and a full set evicts round-robin via the hand. ways stay valid
// forever under the freeze seal (entries are read-only snapshots of sealed
// tables), so eviction is a performance decision, never a correctness one.
#define OK_MIC_WAYS 3

typedef struct {
  mic_entry ways[OK_MIC_WAYS];
  uint8_t hand;
} mic_cache;

typedef index_wrapper mic_slot;

enum {
  MIC_OK = 0,
  MIC_OVERFLOW,
};

#define IS_MIC_VALID(mw) ((mw).errors == MIC_OK)

ARRAY_DECLARE_DEFAULT(mic_caches, mic_cache)

// clang-format off
typedef enum {
  // name                   size  operands        runtime
  OP_NOP,                // w:    ()              -> NOP
  OP_MOVE,               // w:    (a', b')        -> r[a'] = r[b']
  OP_MOVE_LONG,          // 2w:   (A, B)          -> r[A] = r[B]; ip += w
  OP_LOAD_CONSTANT,      // w:    (a', b')        -> r[a'] = k[b']
  OP_LOAD_CONSTANT_LONG, // 2w:   (A, B)          -> r[A] = k[B]; ip += w
  OP_JUMP,               // w:    (A)             -> ip += A
  OP_LOOP,               // w:    (A)             -> ip -= A
  OP_JUMP_IF,            // 2w:   (p, A, B)       -> ip += A when (p == 1) == truthy(r[B])
  OP_JUMP_IF_SET,        // 2w:   (p, A, B, C)    -> when (p == 1) == truthy(r[B]): ip += A and r[C] = r[B]
  OP_LOAD_NULL,          // 2w:   (A, B)          -> r[A] ... r[A + B - 1] = null
  OP_LOAD_FALSE,         // w:    (A)             -> r[A] = false
  OP_LOAD_TRUE,          // w:    (A)             -> r[A] = true
  OP_LOAD_GLOBAL,        // w:    (a', b')        -> r[a'] = g[b']
  OP_LOAD_GLOBAL_LONG,   // 2w:   (A, B)          -> r[A] = g[B];
  OP_STORE_GLOBAL,       // w:    (p, a', b')     -> g[a'] = p then k[b'] otherwise r[b']
  OP_STORE_GLOBAL_LONG,  // 2w:   (p, A, B)       -> g[A] = p then k[B] otherwise r[B]
  OP_LOAD_UPVALUE,       // w:    (a', b')        -> r[a'] = u[b']
  OP_LOAD_UPVALUE_LONG,  // 2w:   (A, B)          -> r[A] = u[B];
  OP_STORE_UPVALUE,      // w:    (p, a', b')     -> u[a'] = p then k[b'] otherwise r[b']
  OP_STORE_UPVALUE_LONG, // 2w:   (p, A, B)       -> u[A] = p then k[B] otherwise r[B];
  OP_CLOSE_UPVALUES,     // w:    (A)             -> close upvalues where index >= A
  OP_CALL,               // w:    (p, a, b, c)    -> r[a] ... r[a + c - 2] = r[a](r[a + 1] ... r[a + b]); { p == 1 => argc = r[a + 1] ... r[top - 1] } { c == 0 => take all returned, else take c - 1 }
  OP_CALL_LONG,          // 2w:   (p, A, B, C)    -> r[A] ... r[A + C - 2] = r[A](r[A + 1] ... r[A + B - 1]); { same as narrow variant }
  OP_RETURN,             // w:    (a', b')        -> return r[a'] ... r[a' + b' - 1] { b' == 0 => return r[a'] ... r[top - 1] (passthrough) }
  OP_RETURN_LONG,        // 2w:   (A, B)          -> return r[A] ... r[A + B - 1]; { same as narrow variant }
  OP_RETURN_1,           // w:    (A, e)          -> return switch RETURNED(e)
  OP_CLOSURE,            // w:    (a', b')        -> r[a'] = closure(k[b'])
  OP_CLOSURE_LONG,       // 2w:   (A, B)          -> r[A] = closure(k[B])
  OP_NOT,                // w:    (p, a', b')     -> r[a'] = p == 0 then !r[b'] otherwise !k[b'] 
  OP_NOT_LONG,           // 2w:   (p, A, B)       -> r[A] = p == 0 then !r[B] otherwise !k[B]
  OP_NEGATE,             // w:    (p, a', b')     -> r[a'] = p == 0 then -r[b'] otherwise -k[b']
  OP_NEGATE_LONG,        // 2w:   (p, A, B)       -> r[A] = p == 0 then -r[B] otherwise -k[B]
  OP_ADD,                // w:    (p, a, b, c)    -> r[a] = r[b] + p then k[c] otherwise r[c]
  OP_ADD_LONG,           // 2w:   (p, A, B, C, q) -> r[A] = q then k[B] otherwise r[B] + p then k[C] otherwise r[C];
  OP_SUBTRACT,           // w:    (p, a, b, c)    -> r[a] = r[b] - (p then k[c] otherwise r[c])
  OP_SUBTRACT_LONG,      // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) - (p then k[C] otherwise r[C]); ip += w
  OP_MULTIPLY,           // w:    (p, a, b, c)    -> r[a] = r[b] * (p then k[c] otherwise r[c])
  OP_MULTIPLY_LONG,      // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) * (p then k[C] otherwise r[C]); ip += w
  OP_DIVIDE,             // w:    (p, a, b, c)    -> r[a] = r[b] / (p then k[c] otherwise r[c])
  OP_DIVIDE_LONG,        // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) / (p then k[C] otherwise r[C]); ip += w
  OP_MODULUS,            // w:    (p, a, b, c)    -> r[a] = r[b] % (p then k[c] otherwise r[c]); floored remainder
  OP_AS,                 // w:    (p, a, b, c)    -> r[a] = r[b] as (p then g[c] otherwise r[c])
  OP_AS_LONG,            // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) as (p then g[C] otherwise r[C]); ip += w
  OP_MODULUS_LONG,       // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) % (p then k[C] otherwise r[C]); ip += w
  OP_EQUALS,             // w:    (p, a, b, c)    -> r[a] = r[b] == (p then k[c] otherwise r[c])
  OP_EQUALS_LONG,        // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) == (p then k[C] otherwise r[C]); ip += w
  OP_NOT_EQUAL,          // w:    (p, a, b, c)    -> r[a] = r[b] != (p then k[c] otherwise r[c])
  OP_NOT_EQUAL_LONG,     // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) != (p then k[C] otherwise r[C]); ip += w
  OP_GREATER,            // w:    (p, a, b, c)    -> r[a] = r[b] > (p then k[c] otherwise r[c])
  OP_GREATER_LONG,       // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) > (p then k[C] otherwise r[C]); ip += w
  OP_LESS,               // w:    (p, a, b, c)    -> r[a] = r[b] < (p then k[c] otherwise r[c])
  OP_LESS_LONG,          // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) < (p then k[C] otherwise r[C]); ip += w
  OP_GREATER_EQUAL,      // w:    (p, a, b, c)    -> r[a] = r[b] >= (p then k[c] otherwise r[c])
  OP_GREATER_EQUAL_LONG, // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) >= (p then k[C] otherwise r[C]); ip += w
  OP_LESS_EQUAL,         // w:    (p, a, b, c)    -> r[a] = r[b] <= (p then k[c] otherwise r[c])
  OP_LESS_EQUAL_LONG,    // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) <= (p then k[C] otherwise r[C]); ip += w
  OP_PRINT,              // w:    (p, A)          -> print r[A]
  OP_ARRAY_NEW,          // w:    (a, b, c)       -> r[a] = c != 0 then Array(r[b]..r[b + c - 1]) otherwise Array()
  OP_ARRAY_NEW_LONG,     // 2w:   (A, B, C)       -> same; ip += w
  OP_MAP_NEW,            // w:    (a, b, c)       -> r[a] = c != 0 then Map(r[b], r[b + 1], .. r[b + 2c - 1]) otherwise Map()
                         //                          c is the pair count: [b, b + 2c) is k0, v0, k1, v1, ...
  OP_MAP_NEW_LONG,       // 2w:   (A, B, C)       -> same; ip += w
  OP_INDEX_GET,          // w:    (p, a, b, c)    -> r[a] = r[b][p then k[c] otherwise r[c]]
  OP_INDEX_GET_LONG,     // 2w:   (p, A, B, C)    -> r[A] = r[B][p then k[C] otherwise r[C]]; ip += w
                         //                          r[B] array: whole-number index, out of bounds errors.
                         //                          r[B] map: key lookup, missing key errors
  OP_INDEX_SET,          // w:    (p, a, b, c)    -> r[a][p then k[b] otherwise r[b]] = r[c] (value is a register in narrow)
  OP_INDEX_SET_LONG,     // 2w:   (p, q, A, B, C) -> r[A][p then k[B] : r[B]] = (q otherwise k[C] : r[C]); ip += w
                         //                          r[A] array: bounds checked store. r[A] map: insert or overwrite
  OP_JUMP_IF_ARGC,       // 2w:   (p, A, B)       -> ip += A when (p == 1) == (frame->argc > B); B is the 0-based parameter position
  OP_FOR_PREP,           // 2w:   (A, B)          -> numeric for entry: r[B] is the counter, r[B + 1] the limit, r[B + 2] the step
                         //                          (compiler allocates the three adjacently and asserts it). validates all
                         //                          three are numbers, errors when the step is zero, normalizes the triple to
                         //                          number-tagged doubles in place (int-tagged operands widen once here), then
                         //                          runs the first bounds test (step > 0 then r[B] < r[B + 1] otherwise r[B] > r[B + 1]):
                         //                          when it fails ip += A (skip the whole loop, A is a forward offset like OP_JUMP_IF's).
                         //                          (p unused, always false)
  OP_FOR_LOOP,           // 2w:   (p, A, B)       -> numeric for back edge: r[B] = r[B] + r[B + 2] (counter += step), then
                         //                          when still in range (step > 0 then r[B] < r[B + 1] otherwise r[B] > r[B + 1])
                         //                          ip -= A (jump back to the body top, A is a backward offset like OP_LOOP's).
                         //                          p == 1: the compiler proved the counter immutable (plain `let`, or the for-in
                         //                          hidden counter) - all three operands read as raw doubles, no per-trip tag
                         //                          checks (FOR_PREP's normalization holds: the limit/step are nameless, the
                         //                          counter is statically unwritable). p == 0: `let mut` - the counter is gated
                         //                          each trip, the body may reseat it to any value.
  OP_ITER_PREP,          // 2w:   (p, A, B)       -> for-in entry (arrays and maps, runtime dispatched). r[B] is the
                         //                          iterable, r[B + 1] the cursor, r[B + 2] the end (arrays: element
                         //                          count; maps: the version snapshot). arrays: cursor = 0, first bounds
                         //                          test, empty skips the loop (ip += A like OP_JUMP_IF's forward offset).
                         //                          maps: snapshot the version, advance the cursor to the first occupied
                         //                          slot, empty skips. p == 1: the two-binding form (k, v) - arrays error
                         //                          ("arrays bind one loop variable"), maps write both loop registers.
                         //                          anything else: "can only iterate over arrays and maps".
  OP_ITER_GET,           // 2w:   (p, A, B, C)    -> for-in body top: r[C] = current element. arrays: r[C] =
                         //                          r[B] values[cursor]. maps: r[C] = keys[cursor], and when p == 1 also
                         //                          r[C + 1] = values[cursor] (the compiler declares k, v adjacent).
                         //                          (A unused, always 0)
  OP_ITER_LOOP,          // 2w:   (p, A, B)       -> for-in back edge. arrays: cursor + 1, still < end -> ip -= A.
                         //                          maps: the version snapshot must match (a size change during iteration
                         //                          is a hard error, overwrites are fine), advance the cursor to the next
                         //                          occupied slot, found -> ip -= A. (p unused, always false)
  OP_VA_PACK,            // w:    (a', b')        -> r[a'] = Array(r[b'] ... r[frame->argc]); the varargs prologue.
                         //                          b' is the varargs parameter's register (the first
                         //                          trailing argument); the count is argc - b' + 1 when argc >= b',
                         //                          else 0 (the fixed/defaulted params swallowed everything).
                         //                          reads before writing: a' == b' is the normal self-packing shape.
  OP_CALL_SPREAD,        // w:    (a, b, c)       -> r[a] stays the callable, r[a + 1] ... r[a + b] the fixed args;
                         //                          r[a + b + 1] must be an array: its elements stage at
                         //                          [a + b + 1, a + b + len] (overwriting the array slot as they go,
                         //                          the object stays alive through the C-side reference), then CALL
                         //                          with argc = b + len and retc = c - 1 (c == 0 => dynamic), like OP_CALL.
  OP_CALL_SPREAD_LONG,   // 2w:   (A, B, C)       -> same; ip += w
  OP_TRY_ENTER,          // 2w:   (A, B, C)       -> push handler record {catch_ip = ip + A (like OP_JUMP_IF's forward offset),
                         //                          park = B, floor = C} onto the vm-wide handler stack, tagged with the current frame.
                         //                          B is the register the unwinder parks the error value into 
                         //                          (the catch binder's own register, or a hidden one for bare catch).
                         //                          so delivery needs no separate bind opcode. C is the upvalue floor
                         //                          (the try statement's scope checkpoint): the unwinder closes
                         //                          everything above it. the register watermark restores to the
                         //                          frame's full window (temps die, locals stay).
  OP_TRY_EXIT,           // w:    ()              -> pop the top handler record (leaving the protected region:
                         //                          normal completion, break/continue jumping out, or the re-dispatch).
                         //                          break/continue emit one per active record between the jump and its loop's entry.
  OP_THROW,              // 2w:   (A)             -> raise r[A]: the value is the error (any value). 
                         //                          walk the handler stack top-down: first live record wins.
                         //                          pop frames above it (closing their upvalues), close upvalues above the
                         //                          record's floor, restore the watermark, park the value at the
                         //                          record's register, jump to its catch_ip.
                         //                          no record: the uncaught lane (report at the outermost C boundary).
                         //                          bare `throw;` inside a catch compiles to OP_THROW on the binder's register.
  OP_DEFER_MARK,         // w:    (A)             -> r[A] = defer_tagged(r[A]). the compiler's only writer of the defer tag:
                         //                          the register must hold the thunk closure just built by OP_CLOSURE 
                         //                          (an internal error otherwise. the tag is never user-forgeable).
                         //                          a marked slot is a pending defer: OP_DEFER_RUN consumes it, the unwind scan consumes it, and nothing else can ever see it.
  OP_DEFER_RUN,          // w:    (A)             -> if r[A] is defer-tagged: nil the slot 
                         //                          (consumed, a scope instance runs each defer exactly once), 
                         //                          stage the thunk at the register file top (above every compile-time register, so nothing live is clobbered)
                         //                          and call it, discard lane. a null slot is a no-op: the defer statement never executed on this
                         //                          path (conditional defer, loop iteration that skipped it).
  OP_CLASS,              // 2w:   (A, B, C)       -> r[A] = new class named k[B], parent r[C] (a class value;
                         //                          null means object). tables start empty and unfrozen.
  OP_DEFINE_FIELD,       // 2w:   (p, A, B, C)    -> class r[A] gains field k[C]; p == 1: no initializer,
                         //                          else r[B] is a thunk closure run with the fresh instance
                         //                          as its receiver at construction time.
  OP_DEFINE_METHOD,      // 2w:   (p, A, B, C)    -> class r[A] gains method k[C] from closure r[B]; p == 1
                         //                          marks it pub (default private). refused once frozen.
  OP_CLASS_END,          // w:    (a')            -> freeze class r[a']: build the flat field map, patch the
                         //                          method bodies' field ops to baked slots (the freeze pass
                         //                          in okvm.c), then seal the tables forever.
  OP_GET_FIELD,          // w:    (a', b')        -> r[a'] = fields of `this` at slot b'
  OP_GET_FIELD_LONG,     // 2w:   (A, B)          -> r[A] = fields of `this` at slot B; ip += w
  OP_STORE_FIELD,        // w:    (p, a', b')     -> fields of `this` at slot b' = p then k[a'] otherwise r[a']
  OP_STORE_FIELD_LONG,   // 2w:   (p, A, B)       -> fields of `this` at slot B = p then k[A] otherwise r[A]; ip += w
  OP_GET_FIELD_NAME,     // w:    (a', b')        -> r[a'] = field k[b'] of `this`, else global k[b']. the
                         //                          pre-patch form: the freeze pass rewrites it in place to
                         //                          GET_FIELD (slot) or LOAD_GLOBAL once the layout is known.
  OP_GET_FIELD_NAME_LONG, // 2w:  (A, B)          -> same, wide; ip += w
  OP_STORE_FIELD_NAME,   // w:    (p, a', b')     -> field-or-global k[b'] of `this` = p then k[a'] otherwise r[a'].
                         //                          pre-patch: rewritten to STORE_FIELD (slot), STORE_GLOBAL,
                         //                          or the immutable-field trap.
  OP_STORE_FIELD_NAME_LONG, // 2w: (p, A, B)      -> same, wide; ip += w
  OP_STORE_FIELD_G,      // 2w:   (A, B, C)       -> fields of r[A] at slot C = r[B], hierarchy-guarded
                         //                          (classof(r[A]) must be within the field's defining class).
  OP_STORE_FIELD_G_NAME, // 2w:   (A, B, C)       -> field k[C] of r[A] = r[B], guarded. pre-patch form;
                         //                          the freeze pass bakes the slot (never a global: member
                         //                          stores only ever write fields).
  OP_FIELD_IMMUTABLE,    // w:    (b')            -> always errors: the field named k[b'] was declared
                         //                          without 'mut'. written by the freeze pass in place of a
                         //                          this-relative store it proved illegal.
  OP_FIELD_IMMUTABLE_G,  // 2w:   (A, B, C)       -> the guarded-store flavor of the same trap (C names
                         //                          the field); the widths must match what they replace.
  OP_GET_MEMBER,         // 3w:   (A, B, C) + mic -> r[A] = member k[C] of r[B]: a field (when visible from
                         //                          the calling hierarchy) or a bound method. the mic word
                         //                          caches (class -> slot or entry) for the site.
  OP_INVOKE,             // 3w:   (p, A, B, C) + mic -> r[A] = r[A].k[C](r[A + 1] ... r[A + B]); p == 1
                         //                          takes exactly one result, p == 0 discards. the receiver
                         //                          doubles as the call window base; the mic word caches
                         //                          (receiver class -> entry).
  OP_SUPER_INVOKE,       // 2w:   (p, A, B, C)    -> r[A] = r[0].k[C](r[A + 1] ... r[A + B]) with lookup
                         //                          starting at the parent of the defining class of the
                         //                          running method. p == 1 takes one result, else discard.
  OP_CLASS_END_LONG,      // 2w:   (A, B, C)       -> freeze class r[A]. the wide spelling of OP_CLASS_END:
                         //                          one op, one width, so every code walker stays decodable.
  OP_DEFINE_STATIC_METHOD, // 2w:  (p, A, B, C)    -> class r[A] gains static method k[C] from closure
                         //                          r[B]; p == 1 marks it pub. statics live in their own
                         //                          table and the receiver is the class value itself.
  OP_DEFINE_STATIC_FIELD, // 2w:   (A, B, C, Q)    -> class r[A] gains static field k[C] with the value r[B]
                         //                          (already evaluated inline, at definition time). Q == 1
                         //                          marks it mutable. storage is per defining class and
                         //                          shared down the chain by lookup, never copied.
  OP_GET_STATIC_NAME,     // 2w:   (A, B, C)       -> r[A] = static field k[C] of the class value r[B]
                         //                          (walked down its parent chain). the name form: the
                         //                          layout is a runtime truth, so no slot baking in 0.5.0.
  OP_STORE_STATIC_NAME,   // 2w:   (p, A, B, C)    -> static field k[C] of class value r[B] = p then
                         //                          k[A] otherwise r[A]. mutability checked at the walk.
  OP_GET_METHOD,          // w:    (a', b')        -> r[a'] = bound method k[b'] of `this`. the baked
                         //                          form of a field-maybe read the freeze pass proved to
                         //                          be a method (own or inherited): the receiver is
                         //                          register 0, the entry is looked up by name at
                         //                          runtime (the slow lane next to INVOKE's fast one).
  OP_GET_METHOD_LONG,     // 2w:   (A, B)          -> same, wide; ip += w
  OP_METHOD_NOT_ASSIGNABLE, // w:  (b')            -> always errors: the name k[b'] is a method of the
                         //                          enclosing chain, and methods are not assignable.
                         //                          written by the freeze pass in place of a this-relative
                         //                          store it proved to target a method.
  OP_METHOD_NOT_ASSIGNABLE_LONG, // 2w: (A)         -> same, wide; ip += w
  OP_STRICT_EQUAL,       // w:    (p, a, b, c)    -> r[a] = r[b] === (p then k[c] otherwise r[c]) - raw word
                         //                          identity, total over every value, never dispatchable
  OP_STRICT_EQUAL_LONG,  // 2w:   (p, A, B, C, q) -> r[A] = (q then k[B] otherwise r[B]) === (p then k[C] otherwise r[C]); ip += w
  OP_IS,                 // w:    (p, a, b, c)    -> r[a] = classof(r[b]) === (p then k[c] otherwise r[c]) - the
                         //                          exact-class probe, classof+identity fused, no global involved.
                         //                          the operand must be a class value
  OP_IS_LONG,            // 2w:   (p, A, B, C, q) -> r[A] = classof(q then k[B] otherwise r[B]) === (p then k[C] otherwise r[C]); ip += w
  OP_JUMP_IF_CMP,        // 2w:   (p, A, B, C, Q, cmp) -> jump += A when (p == 1) == truthy(r[B] CMP (Q then k[C] otherwise r[C])),
                         //                          CMP selected by the 3-bit cmp lane. the compare+jump fusion's
                         //                          condition half: the compiler emits it for a DIRECT comparison in
                         //                          an if/while condition, the bool never materializes in a named
                         //                          register (the perform lands it in the lhs temp, which the jump
                         //                          consumes). polarity p matches OP_JUMP_IF.
  OP_YIELD,               // w:    (p, a', b')     -> suspend the running coroutine: the value p then k[b']
                         //                          otherwise r[b'] goes OUT to the resumer, and the NEXT
                         //                          resume's argument lands in r[a'] (the yield expression's
                         //                          own result register). the frame parks in place - no pops, no
                         //                          upvalue closes, no defers - and the run_until driving the
                         //                          coroutine returns RUNTIME_SUSPENDED with the value.
  OP_YIELD_LONG,          // 2w:   (p, A, B)       -> same, wide; ip += w
  OP__COUNT,             // sentinel: the number of opcodes; must remain LAST (the threaded
                         // dispatch table is sized by it).
} opcode;

// the fused compare+jump's comparator lane. ids are stable bytecode surface:
// they ride the 3-bit cmp field of OP_JUMP_IF_CMP.
typedef enum {
  CMP_EQUALS = 0,
  CMP_NOT_EQUAL,
  CMP_LESS,
  CMP_LESS_EQUAL,
  CMP_GREATER,
  CMP_GREATER_EQUAL,
  CMP_STRICT_EQUAL,
  CMP_IS,
} comparison;

bool comparison_is_valid(uint8_t p_cmp);

// clang-format on

ARRAY_DECLARE_DEFAULT(code, word)
ARRAY_DECLARE_DEFAULT(source_info, ok_line_info_repeated)
ARRAY_DECLARE_DEFAULT(identifiers, string)

// word

// opp
#define op_MASK UINT32_C(0x0000007f) // 7 bit width
#define p_MASK UINT32_C(0x00000080)  // 1 bit width

// abc
#define a_MASK UINT32_C(0x0000ff00) // 8 bit width
#define b_MASK UINT32_C(0x00ff0000) // 8 bit width
#define c_MASK UINT32_C(0xff000000) // 8 bit width

#define op_LOFFSET 0u
#define p_LOFFSET (op_LOFFSET + 7)
#define a_LOFFSET (p_LOFFSET + 1)
#define b_LOFFSET (a_LOFFSET + 8)
#define c_LOFFSET (b_LOFFSET + 8)

// a'b'
// wide layout is op(7) p(1) a'(12) b'(12): a' at bits 8..19, b' at bits 20..31
#define aprime_MASK UINT32_C(0x000fff00) // 12 bit width
#define bprime_MASK UINT32_C(0xfff00000) // 12 bit width

#define aprime_LOFFSET (p_LOFFSET + 1u)
#define bprime_LOFFSET (aprime_LOFFSET + 12u)

#define A_LARGE_MASK UINT32_C(0x00ffff00)
#define e_LARGE_MASK UINT32_C(0x07000000) // 3 bit width (returned enum is 0..6)
#define A_LARGE_LOFFSET a_LOFFSET
#define e_LARGE_LOFFSET c_LOFFSET

// 2 words

// ABC
#define OP_MASK UINT64_C(0x000000000000007f) // 7 bit width
#define P_MASK UINT64_C(0x0000000000000080)  // 1 bit width
#define A_MASK UINT64_C(0x0000000000ffff00)  // 16 bit width
#define B_MASK UINT64_C(0x000000ffff000000)  // 16 bit width
#define C_MASK UINT64_C(0x00ffff0000000000)  // 16 bit width
#define Q_MASK UINT64_C(0x0100000000000000)  // 1 bit width
#define CMP_MASK UINT64_C(0x0e00000000000000) // 3 bit width (OP_JUMP_IF_CMP's comparator)

#define OP_LOFFSET 0u
#define P_LOFFSET (OP_LOFFSET + 7u)
#define A_LOFFSET (P_LOFFSET + 1u)
#define B_LOFFSET (A_LOFFSET + 16u)
#define C_LOFFSET (B_LOFFSET + 16u)
#define Q_LOFFSET (C_LOFFSET + 16u)
#define CMP_LOFFSET (Q_LOFFSET + 1u)

#define a_MAX UINT8_MAX
#define b_MAX a_MAX
#define c_MAX b_MAX

#define UINT12_MAX ((UINT16_C(1) << 12) - 1)
#define aprime_MAX UINT12_MAX
#define bprime_MAX aprime_MAX

#define A_MAX UINT16_MAX
#define B_MAX A_MAX
#define C_MAX B_MAX

typedef enum {
  RETURNED_NULL = 0,
  RETURNED_CONSTANT = 1,
  RETURNED_GLOBAL = 2,
  RETURNED_UPVALUE = 3,
  RETURNED_REGISTER = 4,
  RETURNED_KTRUE = 5,
  RETURNED_KFALSE = 6,
} returned;

static inline word2 join_words(word p_w1, word p_w2) {
  return ((word2)p_w1 << 32u) | p_w2;
}

static inline word
encode_oppabc(const opcode p_op, const bool p_p, const uint8_t p_a, const uint8_t p_b, const uint8_t p_c) {
  return ((uint32_t)p_op << op_LOFFSET) & op_MASK | (((uint32_t)p_p) << p_LOFFSET) & p_MASK |
         (((uint32_t)p_a) << a_LOFFSET) & a_MASK | (((uint32_t)p_b) << b_LOFFSET) & b_MASK |
         (((uint32_t)p_c) << c_LOFFSET) & c_MASK;
}

static inline word
encode_oppabprime(const opcode p_op, const bool p_p, const uint16_t p_aprime, const uint16_t p_bprime) {
  return ((uint32_t)p_op << op_LOFFSET) & op_MASK | (((uint32_t)p_p) << p_LOFFSET) & p_MASK |
         (((uint32_t)p_aprime << aprime_LOFFSET) & aprime_MASK) |
         (((uint32_t)p_bprime) << bprime_LOFFSET) & bprime_MASK;
}

static inline word encode_oppA(const opcode p_op, const bool p_p, const uint16_t p_A) {
  return ((uint32_t)p_op << op_LOFFSET) & op_MASK | (((uint32_t)p_p) << p_LOFFSET) & p_MASK |
         (((uint32_t)p_A) << A_LARGE_LOFFSET) & A_LARGE_MASK;
}

static inline word encode_oppAe(const opcode p_op, const bool p_p, const uint16_t p_A, const uint8_t p_e) {
  return ((uint32_t)p_op << op_LOFFSET) & op_MASK | (((uint32_t)p_p) << p_LOFFSET) & p_MASK |
         (((uint32_t)p_A) << A_LARGE_LOFFSET) & A_LARGE_MASK | (((uint32_t)p_e) << e_LARGE_LOFFSET) & e_LARGE_MASK;
}

static inline word2 encode_OPPABCQ(
    const opcode p_OP, const bool p_P, const uint16_t p_A, const uint16_t p_B, const uint16_t p_C, const bool p_Q) {
  return ((uint64_t)p_OP << OP_LOFFSET) & OP_MASK | (((uint64_t)p_P) << P_LOFFSET) & P_MASK |
         (((uint64_t)p_A) << A_LOFFSET) & A_MASK | (((uint64_t)p_B) << B_LOFFSET) & B_MASK |
         (((uint64_t)p_C) << C_LOFFSET) & C_MASK | (((uint64_t)p_Q) << Q_LOFFSET) & Q_MASK;
}

static inline opcode op_of(const word p_word) {
  return (opcode)((p_word & op_MASK) >> op_LOFFSET);
}

static inline bool p_of(const word p_word) {
  return (p_word & p_MASK) >> p_LOFFSET;
}

static inline uint16_t a_of(const word p_word) {
  return (p_word & a_MASK) >> a_LOFFSET;
}

static inline uint16_t b_of(const word p_word) {
  return (p_word & b_MASK) >> b_LOFFSET;
}

static inline uint16_t c_of(const word p_word) {
  return (p_word & c_MASK) >> c_LOFFSET;
}

static inline uint16_t aprime_of(const word p_word) {
  return (p_word & aprime_MASK) >> aprime_LOFFSET;
}

static inline uint16_t bprime_of(const word p_word) {
  return (p_word & bprime_MASK) >> bprime_LOFFSET;
}

static inline uint16_t Alarge_of(const word p_word) {
  return (p_word & A_LARGE_MASK) >> A_LARGE_LOFFSET;
}

static inline uint8_t elarge_of(const word p_word) {
  return (p_word & e_LARGE_MASK) >> e_LARGE_LOFFSET;
}

static inline uint16_t A_of(const word2 p_word) {
  return (p_word & A_MASK) >> A_LOFFSET;
}

static inline uint16_t B_of(const word2 p_word) {
  return (p_word & B_MASK) >> B_LOFFSET;
}

static inline uint16_t C_of(const word2 p_word) {
  return (p_word & C_MASK) >> C_LOFFSET;
}

static inline uint16_t Q_of(const word2 p_word) {
  return (p_word & Q_MASK) >> Q_LOFFSET;
}

static inline uint8_t cmp_of(const word2 p_word) {
  return (uint8_t)((p_word & CMP_MASK) >> CMP_LOFFSET);
}

static inline word2 encode_OPPABCQC(
    const opcode p_OP, const bool p_P, const uint16_t p_A, const uint16_t p_B, const uint16_t p_C, const bool p_Q,
    const uint8_t p_cmp) {
  return encode_OPPABCQ(p_OP, p_P, p_A, p_B, p_C, p_Q) | (((word2)p_cmp << CMP_LOFFSET) & CMP_MASK);
}

// chunks reference their source by index into the vm's source table.
// UINT32_MAX = unstamped: a chunk compiled outside ok_run.
// none today, but the guard keeps reports honest instead of accidentally reading entry 0.
#define VM_SOURCE_UNSTAMPED UINT32_MAX

struct chunk {
  code code;
  identifiers identifiers;
  value_array constants;
  source_info source_info;
  // per-site polymorphic inline caches (the probe chain), addressed by the
  // mic word of OP_INVOKE / OP_GET_MEMBER. the klass values are gc-traced
  // with the chunk (the function tracer walks every way); the raw method
  // pointers beneath them are sound because class tables freeze at
  // definition end.
  mic_caches mic;
  // index into the vm's source table (VM_SOURCE_UNSTAMPED until ok_run stamps the freshly compiled tree).
  // runtime reports read the frame's chunk's entry
  uint32_t source_id;
  allocators* alloc;
};

void chunk_init(chunk* p_chunk, allocators* p_alloc);
void chunk_deinit(chunk* p_chunk);

bool chunk_write_word(chunk* p_chunk, const word p_word, const ok_line_info p_line_info);
bool chunk_write_2words(chunk* p_chunk, const word p_word1, const word p_word2, const ok_line_info p_line_info);
bool chunk_write_word2(chunk* p_chunk, const word2 p_words, const ok_line_info p_line_info);

identifier chunk_add_identifier(chunk* p_chunk, const ok_string_view p_identifier, const ok_line_info p_line_info);
constant chunk_add_constant(chunk* p_chunk, const value p_constant, const ok_line_info p_line_info);

// allocate the next inline-cache slot (compile time). the index rides the
// mic word of the site it belongs to.
mic_slot chunk_add_mic(chunk* p_chunk, const ok_line_info p_line_info);

ok_line_info_repeated* chunk_find_line_info(const chunk* p_chunk, const uint32_t p_instruction_index);

// instruction width in words: the one authority (the disassembler and the
// class freeze pass both walk code with it).
static inline uint32_t opcode_word_count(opcode p_op) {
  switch (p_op) {
  case OP_MOVE_LONG:
  case OP_LOAD_CONSTANT_LONG:
  case OP_JUMP_IF:
  case OP_JUMP_IF_SET:
  case OP_LOAD_NULL:
  case OP_LOAD_GLOBAL_LONG:
  case OP_STORE_GLOBAL_LONG:
  case OP_LOAD_UPVALUE_LONG:
  case OP_STORE_UPVALUE_LONG:
  case OP_CALL_LONG:
  case OP_RETURN_LONG:
  case OP_CLOSURE_LONG:
  case OP_NOT_LONG:
  case OP_NEGATE_LONG:
  case OP_ADD_LONG:
  case OP_SUBTRACT_LONG:
  case OP_MULTIPLY_LONG:
  case OP_DIVIDE_LONG:
  case OP_AS_LONG:
  case OP_MODULUS_LONG:
  case OP_EQUALS_LONG:
  case OP_NOT_EQUAL_LONG:
  case OP_GREATER_LONG:
  case OP_LESS_LONG:
  case OP_GREATER_EQUAL_LONG:
  case OP_LESS_EQUAL_LONG:
  case OP_ARRAY_NEW_LONG:
  case OP_MAP_NEW_LONG:
  case OP_INDEX_GET_LONG:
  case OP_INDEX_SET_LONG:
  case OP_JUMP_IF_ARGC:
  case OP_FOR_PREP:
  case OP_FOR_LOOP:
  case OP_ITER_PREP:
  case OP_ITER_GET:
  case OP_ITER_LOOP:
  case OP_CALL_SPREAD_LONG:
  case OP_TRY_ENTER:
  case OP_THROW:
  case OP_DEFINE_FIELD:
  case OP_DEFINE_METHOD:
  case OP_GET_FIELD_LONG:
  case OP_STORE_FIELD_LONG:
  case OP_GET_FIELD_NAME_LONG:
  case OP_STORE_FIELD_NAME_LONG:
  case OP_STORE_FIELD_G:
  case OP_STORE_FIELD_G_NAME:
  case OP_FIELD_IMMUTABLE_G:
  case OP_SUPER_INVOKE:
  case OP_CLASS_END_LONG:
  case OP_DEFINE_STATIC_METHOD:
  case OP_DEFINE_STATIC_FIELD:
  case OP_GET_STATIC_NAME:
  case OP_STORE_STATIC_NAME:
  case OP_GET_METHOD_LONG:
  case OP_METHOD_NOT_ASSIGNABLE_LONG:
  case OP_STRICT_EQUAL_LONG:
  case OP_IS_LONG:
  case OP_JUMP_IF_CMP:
  case OP_YIELD_LONG:
    return 2;
  case OP_GET_MEMBER:
  case OP_INVOKE:
    return 3;
  default:
    return 1;
  }
}
#endif // OK_CHUNK_HPP
