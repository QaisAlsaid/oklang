#include <assert.h>
#include <stdio.h>

#include "okdebug.h"
#include "okgc.h"
#include "okglobals_store.h"
#include "okobject.h"
#include "okutils.h"

// opcode names, indexed by the opcode enum. keep in sync with okchunk.h: the
// negative-array-width assert below fails compilation when the two drift.
static const char* const OPCODE_NAMES[] = {
    "NOP",
    "MOVE",
    "MOVE_LONG",
    "LOAD_CONSTANT",
    "LOAD_CONSTANT_LONG",
    "JUMP",
    "LOOP",
    "JUMP_IF",
    "JUMP_IF_SET",
    "LOAD_NULL",
    "LOAD_FALSE",
    "LOAD_TRUE",
    "LOAD_GLOBAL",
    "LOAD_GLOBAL_LONG",
    "STORE_GLOBAL",
    "STORE_GLOBAL_LONG",
    "LOAD_UPVALUE",
    "LOAD_UPVALUE_LONG",
    "STORE_UPVALUE",
    "STORE_UPVALUE_LONG",
    "CLOSE_UPVALUES",
    "CALL",
    "CALL_LONG",
    "RETURN",
    "RETURN_LONG",
    "RETURN_1",
    "CLOSURE",
    "CLOSURE_LONG",
    "NOT",
    "NOT_LONG",
    "NEGATE",
    "NEGATE_LONG",
    "ADD",
    "ADD_LONG",
    "SUBTRACT",
    "SUBTRACT_LONG",
    "MULTIPLY",
    "MULTIPLY_LONG",
    "DIVIDE",
    "DIVIDE_LONG",
    "EQUALS",
    "EQUALS_LONG",
    "NOT_EQUAL",
    "NOT_EQUAL_LONG",
    "GREATER",
    "GREATER_LONG",
    "LESS",
    "LESS_LONG",
    "GREATER_EQUAL",
    "GREATER_EQUAL_LONG",
    "LESS_EQUAL",
    "LESS_EQUAL_LONG",
    "PRINT",
    "ARRAY_NEW",
    "ARRAY_NEW_LONG",
    "ARRAY_GET",
    "ARRAY_GET_LONG",
    "ARRAY_SET",
    "ARRAY_SET_LONG",
    "JUMP_IF_ARGC",
    "FOR_PREP",
    "FOR_LOOP",
};
typedef char opcode_names_in_sync[(sizeof(OPCODE_NAMES) / sizeof(OPCODE_NAMES[0]) == (size_t)OP_FOR_LOOP + 1)
                                      ? 1
                                      : -1];

static const char* binop_symbol(const opcode p_op) {
  switch (p_op) {
  case OP_ADD:
  case OP_ADD_LONG:
    return "+";
  case OP_SUBTRACT:
  case OP_SUBTRACT_LONG:
    return "-";
  case OP_MULTIPLY:
  case OP_MULTIPLY_LONG:
    return "*";
  case OP_DIVIDE:
  case OP_DIVIDE_LONG:
    return "/";
  case OP_EQUALS:
  case OP_EQUALS_LONG:
    return "==";
  case OP_NOT_EQUAL:
  case OP_NOT_EQUAL_LONG:
    return "!=";
  case OP_GREATER:
  case OP_GREATER_LONG:
    return ">";
  case OP_LESS:
  case OP_LESS_LONG:
    return "<";
  case OP_GREATER_EQUAL:
  case OP_GREATER_EQUAL_LONG:
    return ">=";
  case OP_LESS_EQUAL:
  case OP_LESS_EQUAL_LONG:
    return "<=";
  default:
    return NULL;
  }
}

static void print_constant_inline(const chunk* p_chunk, const uint16_t p_index) {
  if (p_index >= p_chunk->constants.count) {
    printf("k%u <out of range>", p_index);
    return;
  }
  printf("k%u '", p_index);
  value_debug_print(p_chunk->constants.data[p_index]);
  printf("'");
}

static void print_global_inline(const disassembler* p_disassembler, const uint16_t p_index) {
  if (p_disassembler->globals_store == NULL || p_index >= p_disassembler->globals_store->global_values.count) {
    printf("g%u", p_index);
    return;
  }
  printf("g%u '", p_index);
  value_debug_print(p_disassembler->globals_store->global_values.data[p_index]);
  printf("'");
}

static bool op_is_two_words(opcode p_op) {
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
  case OP_EQUALS_LONG:
  case OP_NOT_EQUAL_LONG:
  case OP_GREATER_LONG:
  case OP_LESS_LONG:
  case OP_GREATER_EQUAL_LONG:
  case OP_LESS_EQUAL_LONG:
  case OP_ARRAY_NEW_LONG:
  case OP_ARRAY_GET_LONG:
  case OP_ARRAY_SET_LONG:
  case OP_JUMP_IF_ARGC:
  case OP_FOR_PREP:
  case OP_FOR_LOOP:
    return true;
  default:
    return false;
  }
}

void disassembler_init(disassembler* p_disassembler, disassembler_specs p_specs) {
  p_disassembler->chunk = p_specs.chunk;
  p_disassembler->globals_store = p_specs.globals_store;
}

void disassembler_deinit(disassembler* p_disassembler) {
  p_disassembler->chunk = NULL;
  p_disassembler->globals_store = NULL;
}

bool debug_disassemble_chunk(disassembler* p_disassembler, const char* p_name) {
  const chunk* chunk = p_disassembler->chunk;
  gc_pause(chunk->alloc->gc);
  printf("--- %s ---\n", p_name);

  int last_line = -1;
  for (uint32_t offset = 0; offset < chunk->code.count;) {
    const ok_line_info_repeated* info = chunk_find_line_info(chunk, offset);
    if (info != NULL && info->line_info.line != last_line) {
      last_line = (int)info->line_info.line;
      printf("          ; line %d\n", last_line);
    }
    offset = debug_disassemble_instruction(p_disassembler, offset);
  }
  printf("--- %s ---\n", p_name);
  gc_resume(chunk->alloc->gc);
  return true;
}

uint32_t debug_disassemble_instruction(disassembler* p_disassembler, uint32_t p_offset) {
  const chunk* chunk = p_disassembler->chunk;
  if (p_offset >= chunk->code.count) {
    return chunk->code.count;
  }
  const word instruction = chunk->code.data[p_offset];
  const opcode op = op_of(instruction);
  const uint32_t words = op_is_two_words(op) ? 2 : 1;
  const word2 joined =
      words == 2 ? (((word2)chunk->code.data[p_offset + 1] << 32u) | (word2)instruction) : (word2)instruction;

  printf("%04u ", p_offset);
  for (uint32_t i = 0; i < words; ++i) {
    printf("%08x ", chunk->code.data[p_offset + i]);
  }
  if (words == 1) {
    printf("         "); // keep operand columns aligned across widths
  }
  printf("%-16s ", OPCODE_NAMES[op]);

  const bool p = p_of(instruction);
  const uint16_t ap = aprime_of(instruction);
  const uint16_t bp = bprime_of(instruction);
  const uint16_t a = a_of(instruction);
  const uint16_t b = b_of(instruction);
  const uint16_t c = c_of(instruction);
  const uint16_t Al = Alarge_of(instruction);
  const uint16_t A = A_of(joined);
  const uint16_t B = B_of(joined);
  const uint16_t C = C_of(joined);
  const bool q = Q_of(joined);

  switch (op) {
  case OP_NOP:
    break;
  case OP_MOVE:
    printf("r%u = r%u", ap, bp);
    break;
  case OP_MOVE_LONG:
    printf("r%u = r%u", A, B);
    break;
  case OP_LOAD_CONSTANT:
    printf("r%u = ", ap);
    print_constant_inline(chunk, bp);
    break;
  case OP_LOAD_CONSTANT_LONG:
    printf("r%u = ", A);
    print_constant_inline(chunk, B);
    break;
  case OP_JUMP:
    printf("-> %04u", p_offset + 1 + Al);
    break;
  case OP_LOOP:
    printf("-> %04u (back)", p_offset + 1 - Al);
    break;
  case OP_JUMP_IF:
    printf("-> %04u when %s(r%u)", p_offset + 2 + A, p ? "truthy" : "falsy", B);
    break;
  case OP_JUMP_IF_SET:
    printf("-> %04u when %s(r%u); r%u = r%u", p_offset + 2 + A, p ? "truthy" : "falsy", B, C, B);
    break;
  case OP_LOAD_NULL:
    printf("null -> r%u x%u", A, B);
    break;
  case OP_LOAD_FALSE:
    printf("r%u = false", Al);
    break;
  case OP_LOAD_TRUE:
    printf("r%u = true", Al);
    break;
  case OP_LOAD_GLOBAL:
    printf("r%u = ", ap);
    print_global_inline(p_disassembler, bp);
    break;
  case OP_LOAD_GLOBAL_LONG:
    printf("r%u = ", A);
    print_global_inline(p_disassembler, B);
    break;
  case OP_STORE_GLOBAL:
    if (p) {
      printf("g%u = ", ap);
      print_constant_inline(chunk, bp);
    } else {
      printf("g%u = r%u", ap, bp);
    }
    break;
  case OP_STORE_GLOBAL_LONG:
    if (p) {
      printf("g%u = ", A);
      print_constant_inline(chunk, B);
    } else {
      printf("g%u = r%u", A, B);
    }
    break;
  case OP_LOAD_UPVALUE:
    printf("r%u = u%u", ap, bp);
    break;
  case OP_LOAD_UPVALUE_LONG:
    printf("r%u = u%u", A, B);
    break;
  case OP_STORE_UPVALUE:
    if (p) {
      printf("u%u = ", ap);
      print_constant_inline(chunk, bp);
    } else {
      printf("u%u = r%u", ap, bp);
    }
    break;
  case OP_STORE_UPVALUE_LONG:
    if (p) {
      printf("u%u = ", A);
      print_constant_inline(chunk, B);
    } else {
      printf("u%u = r%u", A, B);
    }
    break;
  case OP_CLOSE_UPVALUES:
    printf("close upvalues >= r%u", Al);
    break;
  case OP_CALL:
    printf("r%u = call r%u, argc %u%s, ret ", a, a, b, p ? " (varargs)" : "");
    if (c == 0) {
      printf("all");
    } else {
      printf("%u", c - 1);
    }
    break;
  case OP_CALL_LONG:
    printf("r%u = call r%u, argc %u%s, ret ", A, A, B, p ? " (varargs)" : "");
    if (C == 0) {
      printf("all");
    } else {
      printf("%u", C - 1);
    }
    break;
  case OP_RETURN:
    if (bp == 0) {
      printf("return r%u..top (passthrough)", ap);
    } else {
      printf("return r%u..r%u", ap, ap + bp - 1);
    }
    break;
  case OP_RETURN_LONG:
    if (B == 0) {
      printf("return r%u..top (passthrough)", A);
    } else {
      printf("return r%u..r%u", A, A + B - 1);
    }
    break;
  case OP_RETURN_1:
    if (p) {
      printf("return null");
    } else {
      printf("return r%u", Al);
    }
    break;
  case OP_CLOSURE:
    printf("r%u = closure(", ap);
    print_constant_inline(chunk, bp);
    printf(")");
    break;
  case OP_CLOSURE_LONG:
    printf("r%u = closure(", A);
    print_constant_inline(chunk, B);
    printf(")");
    break;
  case OP_NOT:
    printf("r%u = !%s", ap, p ? "k" : "r");
    if (p) {
      print_constant_inline(chunk, bp);
    } else {
      printf("%u", bp);
    }
    break;
  case OP_NOT_LONG:
    printf("r%u = !%s", A, p ? "k" : "r");
    if (p) {
      print_constant_inline(chunk, B);
    } else {
      printf("%u", B);
    }
    break;
  case OP_NEGATE:
    printf("r%u = -%s", ap, p ? "k" : "r");
    if (p) {
      print_constant_inline(chunk, bp);
    } else {
      printf("%u", bp);
    }
    break;
  case OP_NEGATE_LONG:
    printf("r%u = -%s", A, p ? "k" : "r");
    if (p) {
      print_constant_inline(chunk, B);
    } else {
      printf("%u", B);
    }
    break;
  case OP_ARRAY_NEW:
    if (c == 0) {
      printf("r%u = []", a);
    } else {
      printf("r%u = [r%u..r%u]", a, b, b + c - 1);
    }
    break;
  case OP_ARRAY_NEW_LONG:
    if (C == 0) {
      printf("r%u = []", A);
    } else {
      printf("r%u = [r%u..r%u]", A, B, B + C - 1);
    }
    break;
  case OP_ARRAY_GET:
    printf("r%u = r%u[", a, b);
    if (p) {
      print_constant_inline(chunk, c);
    } else {
      printf("r%u", c);
    }
    printf("]");
    break;
  case OP_ARRAY_GET_LONG:
    printf("r%u = r%u[", A, B);
    if (p) {
      print_constant_inline(chunk, C);
    } else {
      printf("r%u", C);
    }
    printf("]");
    break;
  case OP_ARRAY_SET:
    printf("r%u[", a);
    if (p) {
      print_constant_inline(chunk, b);
    } else {
      printf("r%u", b);
    }
    printf("] = r%u", c);
    break;
  case OP_ARRAY_SET_LONG:
    printf("r%u[", A);
    if (p) {
      print_constant_inline(chunk, B);
    } else {
      printf("r%u", B);
    }
    printf("] = ");
    if (q) {
      print_constant_inline(chunk, C);
    } else {
      printf("r%u", C);
    }
    break;
  case OP_JUMP_IF_ARGC:
    printf("-> %04u when argc %s %u", p_offset + 2 + A, p ? ">" : "<=", B);
    break;
  case OP_FOR_PREP:
    printf("i=r%u limit=r%u step=r%u, empty -> %04u", B, B + 1, B + 2, p_offset + 2 + A);
    break;
  case OP_FOR_LOOP:
    printf("i=r%u += r%u, -> %04u (back)", B, B + 2, p_offset + 2 - A);
    break;
  case OP_PRINT:
    printf("print r%u", Al);
    break;
  default: {
    // binary value operators, narrow and long
    const char* symbol = binop_symbol(op);
    if (symbol == NULL) {
      printf("?");
      break;
    }
    if (op_is_two_words(op)) {
      printf("r%u = ", A);
      if (q) {
        print_constant_inline(chunk, B);
      } else {
        printf("r%u", B);
      }
      printf(" %s ", symbol);
      if (p) {
        print_constant_inline(chunk, C);
      } else {
        printf("r%u", C);
      }
    } else {
      printf("r%u = r%u %s ", a, b, symbol);
      if (p) {
        print_constant_inline(chunk, c);
      } else {
        printf("r%u", c);
      }
    }
    break;
  }
  }
  printf("\n");
  return p_offset + words;
}

bool debug_disassemble_function(disassembler* p_disassembler, const object_function* p_function, const char* p_name) {
  const chunk* saved = p_disassembler->chunk;
  p_disassembler->chunk = &p_function->chunk;
  gc_pause(p_function->chunk.alloc->gc);
  printf("=== %s: %u instructions, %u constants, %u registers, %u upvalues ===\n", p_name,
         (unsigned)p_function->chunk.code.count, (unsigned)p_function->chunk.constants.count,
         (unsigned)p_function->registers, (unsigned)p_function->upvalues.count);
  for (uint32_t i = 0; i < p_function->upvalues.count; ++i) {
    const upvalue_descriptor ud = p_function->upvalues.data[i];
    printf("  up[%u] = %s r%u\n", i, ud.is_local ? "local" : "up", ud.index);
  }
  debug_disassemble_chunk(p_disassembler, p_name);
  gc_resume(p_function->chunk.alloc->gc);

  // nested protos live in the constants table: dump them after the parent
  for (uint32_t i = 0; i < p_function->chunk.constants.count; ++i) {
    const value v = p_function->chunk.constants.data[i];
    if (IS_VALUE_FUNCTION(v)) {
      const object_function* proto = VALUE_AS_FUNCTION(v);
      const char* name = proto->name != NULL ? proto->name->string.chars : "<anonymous>";
      debug_disassemble_function(p_disassembler, proto, name);
    }
  }
  p_disassembler->chunk = saved;
  return true;
}
