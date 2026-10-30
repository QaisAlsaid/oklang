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
  OP_ARRAY_GET,          // w:    (p, a, b, c)    -> r[a] = r[b][p then k[c] otherwise r[c]]
  OP_ARRAY_GET_LONG,     // 2w:   (p, A, B, C)    -> r[A] = r[B][p then k[C] otherwise r[C]]; ip += w
  OP_ARRAY_SET,          // w:    (p, a, b, c)    -> r[a][p then k[b] otherwise r[b]] = r[c] (value is a register in narrow)
  OP_ARRAY_SET_LONG,     // 2w:   (p, q, A, B, C) -> r[A][p then k[B] : r[B]] = (q otherwise k[C] : r[C]); ip += w
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
} opcode;

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
#define e_LARGE_MASK UINT32_C(0x00000010)
#define A_LARGE_LOFFSET a_LOFFSET
#define e_LARGE_LOFFSET b_LOFFSET

// 2 words

// ABC
#define OP_MASK UINT64_C(0x000000000000007f) // 7 bit width
#define P_MASK UINT64_C(0x0000000000000080)  // 1 bit width
#define A_MASK UINT64_C(0x0000000000ffff00)  // 16 bit width
#define B_MASK UINT64_C(0x000000ffff000000)  // 16 bit width
#define C_MASK UINT64_C(0x00ffff0000000000)  // 16 bit width
#define Q_MASK UINT64_C(0x0100000000000000)  // 1 bit width

#define OP_LOFFSET 0u
#define P_LOFFSET (OP_LOFFSET + 7u)
#define A_LOFFSET (P_LOFFSET + 1u)
#define B_LOFFSET (A_LOFFSET + 16u)
#define C_LOFFSET (B_LOFFSET + 16u)
#define Q_LOFFSET (C_LOFFSET + 16u)

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

static inline word encode_oppAe(const opcode p_op, const bool p_p, const uint16_t p_A, const bool p_e) {
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

struct chunk {
  code code;
  identifiers identifiers;
  value_array constants;
  source_info source_info;
  allocators* alloc;
};

void chunk_init(chunk* p_chunk, allocators* p_alloc);
void chunk_deinit(chunk* p_chunk);

bool chunk_write_word(chunk* p_chunk, const word p_word, const ok_line_info p_line_info);
bool chunk_write_2words(chunk* p_chunk, const word p_word1, const word p_word2, const ok_line_info p_line_info);
bool chunk_write_word2(chunk* p_chunk, const word2 p_words, const ok_line_info p_line_info);

identifier chunk_add_identifier(chunk* p_chunk, const ok_string_view p_identifier, const ok_line_info p_line_info);
constant chunk_add_constant(chunk* p_chunk, const value p_constant, const ok_line_info p_line_info);

ok_line_info_repeated* chunk_find_line_info(const chunk* p_chunk, const uint32_t p_instruction_index);
#endif // OK_CHUNK_HPP
