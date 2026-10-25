#include <assert.h>
#include <stdio.h>

#include "okdebug.h"
#include "okgc.h"
#include "okglobals_store.h"
#include "okobject.h"
#include "okutils.h"

typedef enum {
  FIELD_HIDDEN,
  FIELD_PRESERVED,
  FIELD_CONSTANT,
  FIELD_GLOBAL,
  FIELD_BOOLEAN,
  FIELD_NULL,
  FIELD_KPROTO,
} field_type;

typedef struct {
  uint16_t value;
  uint8_t type;
  uint8_t extra;
} field;

static bool disassemble_field(disassembler* p_disassembler, field p_field) {
  switch (p_field.type) {
  case FIELD_HIDDEN: {
    return false;
  }
  case FIELD_PRESERVED: {
    printf("%4d", p_field.value);
    return true;
  }
  case FIELD_CONSTANT: {
    printf("%4d '", p_field.value);
    value_debug_print(p_disassembler->chunk->constants.data[p_field.value]);
    printf("'\n");
    return true;
  }
  case FIELD_GLOBAL: {
    printf("%4d '", p_field.value);
    value_debug_print(p_disassembler->globals_store->global_values.data[p_field.value]);
    printf("'\n");
    return true;
  }
  case FIELD_BOOLEAN: {
    printf(p_field.value ? "    true" : "   false");
    return true;
  }
  case FIELD_NULL: {
    printf("    null");
    return true;
  }
  case FIELD_KPROTO: {
    const value vproto = p_disassembler->chunk->constants.data[p_field.value];
    printf("%4d ...", p_field.value);
    const object_function* proto = VALUE_AS_FUNCTION(vproto);
    value_debug_print(vproto);
    printf("\n");
    for (uint32_t i = 0; i < proto->upvalues.count; ++i) {
      upvalue_descriptor ud = proto->upvalues.data[i];
      printf("    UP %4d, %s", ud.index, ud.is_local ? "local" : "up");
    }
    return true;
  }
  }
  return false;
}

static void
disassemble(disassembler* p_dissasembler, ok_cstring_view p_opname, field* p_fields, uint32_t p_fields_count) {
  printf("%-16s\n", p_opname.chars);
  for (uint32_t i = 0; i < p_fields_count; ++i) {
    disassemble_field(p_dissasembler, p_fields[i]);
  }
  printf("\n");
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
  gc_pause(p_disassembler->chunk->alloc->gc);
  printf("--- %s ---\n", p_name);

  for (uint32_t offset = 0; offset < p_disassembler->chunk->code.count;) {
    offset = debug_disassemble_instruction(p_disassembler, offset);
  }
  printf("--- %s ---\n", p_name);
  gc_resume(p_disassembler->chunk->alloc->gc);
  return true;
}

// placeholder instruction printer: decodes instruction width and raw operand slots.
// the real peropcode disassembler is scheduled for v0.0.6
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
    return true;
  default:
    return false;
  }
}

uint32_t debug_disassemble_instruction(disassembler* p_disassembler, uint32_t p_offset) {
  const chunk* chunk = p_disassembler->chunk;
  if (p_offset >= chunk->code.count) {
    return chunk->code.count;
  }
  const word instruction = chunk->code.data[p_offset];
  const opcode op = op_of(instruction);
  const uint32_t words = op_is_two_words(op) ? 2 : 1;

  printf("%04d ", p_offset);
  for (uint32_t i = 0; i < words; ++i) {
    printf("%08x ", chunk->code.data[p_offset + i]);
  }
  const uint16_t p = p_of(instruction);
  const uint16_t ap = aprime_of(instruction);
  const uint16_t bp = bprime_of(instruction);
  printf("op=%03u p=%u a'=%-4u b'=%-4u\n", (unsigned)op, p, ap, bp);

  const ok_line_info_repeated* info = chunk_find_line_info(chunk, p_offset);
  if (info != NULL) {
    printf("     line %d\n", info->line_info.line);
  }
  return p_offset + words;
}
