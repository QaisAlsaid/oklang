#include <assert.h>
#include <stdio.h>

#include "okdebug.h"
#include "okglobals_store.h"
#include "okobject.h"
#include "okutils.h"

#define UNKNOWN_ASSUMED_WIDTH 1

static uint32_t op_code_instruction(const char* p_opname, const uint32_t p_offset);
static uint32_t constant_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t constant_long_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t global_instruction(const char* p_opname, const uint32_t p_offset, const disassembler* p_disassembler);
static uint32_t
global_long_instruction(const char* p_opname, const uint32_t p_offset, const disassembler* p_disassembler);
static uint32_t local_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t local_long_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t jump_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t loop_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t call_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t upvalue_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t upvalue_long_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk);
static uint32_t closure_instruction(const char* p_opname, uint32_t p_offset, const chunk* p_chunk);

void disassembler_init(disassembler* p_disassembler, disassembler_specs p_specs) {
  p_disassembler->chunk = p_specs.chunk;
  p_disassembler->globals_store = p_specs.globals_store;
}

void disassembler_deinit(disassembler* p_disassembler) {
  p_disassembler->chunk = NULL;
  p_disassembler->globals_store = NULL;
}

const char* debug_disassemble_chunk(disassembler* p_disassembler, const char* p_name) {
  printf("--- %s ---\n", p_name);

  for (uint32_t offset = 0; offset < p_disassembler->chunk->code.count;) {
    offset = debug_disassemble_instruction(p_disassembler, offset);
  }
  printf("--- %s ---\n", p_name);
  return NULL;
}

uint32_t debug_disassemble_instruction(disassembler* p_disassembler, uint32_t p_offset) {
  const chunk* chunk = p_disassembler->chunk;
  printf("%04d ", p_offset);

  line_info_repeated* info = source_info_find(&chunk->source_info, p_offset);
  if (info && p_offset == 0) {
    printf("%4d:%-4d ", info->line_info.line, info->line_info.offset);
  }
  if (p_offset > 0) {
    line_info_repeated* prev_info = source_info_find(&chunk->source_info, p_offset - 1);
    line_info_repeated* info = source_info_find(&chunk->source_info, p_offset);
    if (info && prev_info) {
      if (info->line_info.line == prev_info->line_info.line) {
        if (info->line_info.offset == prev_info->line_info.offset) {
          printf("   |");
          printf(" |   ");
        } else {
          printf("   |");
          printf(" %-4d ", info->line_info.offset);
        }
      } else {
        if (info->line_info.offset == prev_info->line_info.offset) {
          printf("%4d: |", info->line_info.line);
        } else {
          printf("%4d:%-4d ", info->line_info.line, info->line_info.offset);
        }
      }
    }
  }

  uint8_t instruction = chunk->code.data[p_offset];
  switch (instruction) {
  case OP_RETURN:
    return op_code_instruction("OP_RETURN", p_offset);
  case OP_CONSTANT:
    return constant_instruction("OP_CONSTANT", p_offset, chunk);
  case OP_CONSTANT_LONG:
    return constant_long_instruction("OP_CONSTANT_LONG", p_offset, chunk);
  case OP_POP:
    return op_code_instruction("OP_POP", p_offset);
  case OP_JUMP:
    return jump_instruction("OP_JUMP", p_offset, chunk);
  case OP_TRUTHY_JUMP:
    return jump_instruction("OP_TRUTHY_JUMP", p_offset, chunk);
  case OP_FALSY_JUMP:
    return jump_instruction("OP_FALSY_JUMP", p_offset, chunk);
  case OP_LOOP:
    return loop_instruction("OP_LOOP", p_offset, chunk);
  case OP_NULL:
    return op_code_instruction("OP_NULL", p_offset);
  case OP_FALSE:
    return op_code_instruction("OP_FALSE", p_offset);
  case OP_TRUE:
    return op_code_instruction("OP_TRUE", p_offset);
  case OP_GET_GLOBAL:
    return global_instruction("OP_GET_GLOBAL", p_offset, p_disassembler);
  case OP_GET_GLOBAL_LONG:
    return global_long_instruction("OP_GET_GLOBAL_LONG", p_offset, p_disassembler);
  case OP_SET_GLOBAL:
    return global_instruction("OP_SET_GLOBAL", p_offset, p_disassembler);
  case OP_SET_GLOBAL_LONG:
    return global_long_instruction("OP_SET_GLOBAL_LONG", p_offset, p_disassembler);
  case OP_GET_LOCAL:
    return local_instruction("OP_GET_LOCAL", p_offset, p_disassembler->chunk);
  case OP_GET_LOCAL_LONG:
    return local_long_instruction("OP_GET_LOCAL_LONG", p_offset, p_disassembler->chunk);
  case OP_SET_LOCAL:
    return local_instruction("OP_SET_LOCAL", p_offset, p_disassembler->chunk);
  case OP_SET_LOCAL_LONG:
    return local_long_instruction("OP_SET_LOCAL_LONG", p_offset, p_disassembler->chunk);
  case OP_GET_UPVALUE:
    return upvalue_instruction("OP_GET_UPVALUE", p_offset, chunk);
  case OP_GET_UPVALUE_LONG:
    return upvalue_long_instruction("OP_GET_UPVALUE_LONG", p_offset, chunk);
  case OP_SET_UPVALUE:
    return upvalue_instruction("OP_SET_UPVALUE", p_offset, chunk);
  case OP_SET_UPVALUE_LONG:
    return upvalue_long_instruction("OP_SET_UPVALUE_LONG", p_offset, chunk);
  case OP_CLOSE_UPVALUE:
    return op_code_instruction("OP_CLOSE_UPVALUE", p_offset);
  case OP_CALL:
    return call_instruction("OP_CALL", p_offset, chunk);
  case OP_CLOSURE:
    return closure_instruction("OP_CLOSURE", p_offset, chunk);
  case OP_NOT:
    return op_code_instruction("OP_NOT", p_offset);
  case OP_NEGATE:
    return op_code_instruction("OP_NEGATE", p_offset);
  case OP_ADD:
    return op_code_instruction("OP_ADD", p_offset);
  case OP_SUBTRACT:
    return op_code_instruction("OP_SUBTRACT", p_offset);
  case OP_MULTIPLY:
    return op_code_instruction("OP_MULTIPLY", p_offset);
  case OP_DIVIDE:
    return op_code_instruction("OP_DIVIDE", p_offset);
  case OP_EQUAL:
    return op_code_instruction("OP_EQUAL", p_offset);
  case OP_NOT_EQUAL:
    return op_code_instruction("OP_NOT_EQUAL", p_offset);
  case OP_LESS:
    return op_code_instruction("OP_LESS", p_offset);
  case OP_GREATER:
    return op_code_instruction("OP_GREATER", p_offset);
  case OP_LESS_EQUAL:
    return op_code_instruction("OP_LESS_EQUAL", p_offset);
  case OP_GREATER_EQUAL:
    return op_code_instruction("OP_GREATER_EQUAL", p_offset);
  case OP_PRINT:
    return op_code_instruction("OP_PRINT", p_offset);
  default:
    printf("unknown opcode %d (assumed width: %u)\n", instruction, UNKNOWN_ASSUMED_WIDTH);
    return p_offset + UNKNOWN_ASSUMED_WIDTH; // should we even advance here? since it could also be multibyte
                                             // instruction which will break anyway!
  }
}

uint32_t op_code_instruction(const char* p_opname, const uint32_t p_offset) {
  printf("%-16s\n", p_opname);
  return p_offset + OP_CODE_WIDTH;
}

uint32_t constant_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  uint8_t constant = p_chunk->code.data[p_offset + OP_CODE_WIDTH];
  printf("%-16s %4d '", p_opname, constant);
  value_debug_print(p_chunk->constants.data[constant]);
  printf("'\n");
  return p_offset + OP_CODE_WIDTH + OP_CONSTANT_OPERANDS_WIDTH;
}

uint32_t constant_long_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  const uint32_t constant = decode_int(p_offset + p_chunk->code.data + OP_CODE_WIDTH, OP_CONSTANT_LONG_OPERANDS_WIDTH);
  printf("%-16s %4d '", p_opname, constant);
  value_debug_print(p_chunk->constants.data[constant]);
  printf("'\n");
  return p_offset + OP_CODE_WIDTH + OP_CONSTANT_LONG_OPERANDS_WIDTH;
}

uint32_t global_instruction(const char* p_opname, const uint32_t p_offset, const disassembler* p_disassembler) {
  // TODO get the identifier from debug info
  const uint8_t index = p_disassembler->chunk->code.data[p_offset + OP_CODE_WIDTH];
  printf("%-16s %4d\n", p_opname, index);
  return p_offset + OP_CODE_WIDTH + OP_XX_GLOBAL_OPERANDS_WIDTH;
}

uint32_t global_long_instruction(const char* p_opname, const uint32_t p_offset, const disassembler* p_disassembler) {
  const uint32_t index =
      decode_int(p_offset + p_disassembler->chunk->code.data + OP_CODE_WIDTH, OP_XX_GLOBAL_LONG_OPERANDS_WIDTH);
  printf("%-16s %4d\n", p_opname, index);
  return p_offset + OP_CODE_WIDTH + OP_XX_GLOBAL_LONG_OPERANDS_WIDTH;
}

uint32_t local_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  const byte slot = p_chunk->code.data[p_offset + OP_CODE_WIDTH];
  printf("%-16s %4d\n", p_opname, slot);
  return p_offset + OP_CODE_WIDTH + OP_XX_LOCAL_OPERANDS_WIDTH;
}

uint32_t local_long_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  const uint32_t slot = decode_int(p_offset + p_chunk->code.data + OP_CODE_WIDTH, OP_XX_LOCAL_LONG_OPERANDS_WIDTH);
  printf("%-16s %4d\n", p_opname, slot);
  return p_offset + OP_CODE_WIDTH + OP_XX_LOCAL_LONG_OPERANDS_WIDTH;
}

uint32_t jump_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  uint32_t jmp = decode_int(p_chunk->code.data + p_offset + OP_CODE_WIDTH, OP_XX_JUMP_OPERANDS_WIDTH);
  printf("%-16s %4d -> %d\n", p_opname, p_offset, p_offset + OP_CODE_WIDTH + jmp);
  return p_offset + OP_CODE_WIDTH + OP_XX_JUMP_OPERANDS_WIDTH;
}

uint32_t loop_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  uint32_t loop = decode_int(p_chunk->code.data + p_offset + OP_CODE_WIDTH, OP_LOOP_OPERANDS_WIDTH);
  printf("%-16s %4d -> %d\n", p_opname, p_offset, p_offset + OP_CODE_WIDTH + OP_LOOP_OPERANDS_WIDTH - loop);
  return p_offset + OP_CODE_WIDTH + OP_LOOP_OPERANDS_WIDTH;
}

uint32_t call_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  const byte argc = p_chunk->code.data[p_offset + OP_CODE_WIDTH];
  printf("%-16s %4d\n", p_opname, argc);
  return p_offset + OP_CODE_WIDTH + OP_CALL_OPERANDS_WIDTH;
}

uint32_t upvalue_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  const byte up = p_chunk->code.data[p_offset + OP_XX_UPVALUE_OPERANDS_WIDTH];
  printf("%-16s %4d\n", p_opname, up);
  return p_offset + OP_CODE_WIDTH + OP_XX_UPVALUE_OPERANDS_WIDTH;
}

uint32_t upvalue_long_instruction(const char* p_opname, const uint32_t p_offset, const chunk* p_chunk) {
  const uint32_t up = decode_int(p_chunk->code.data + p_offset + OP_CODE_WIDTH, OP_XX_UPVALUE_LONG_OPERANDS_WIDTH);
  printf("%-16s %4d\n", p_opname, up);
  return p_offset + OP_CODE_WIDTH + OP_XX_UPVALUE_LONG_OPERANDS_WIDTH;
}

uint32_t closure_instruction(const char* p_opname, uint32_t p_offset, const chunk* p_chunk) {
  p_offset += OP_CODE_WIDTH;
  uint32_t constant = decode_int(p_chunk->code.data + p_offset, OP_CONSTANT_LONG_OPERANDS_WIDTH);
  p_offset += OP_CONSTANT_LONG_OPERANDS_WIDTH;
  printf("%-16s %4d ", p_opname, constant);
  value val = p_chunk->constants.data[constant];
  value_debug_print(val);
  puts("");
  object_function* fu = VALUE_AS_FUNCTION(val);
  for (uint32_t i = 0; i < fu->upvalues; ++i) {
    p_offset++;
    bool is_local = (bool)p_chunk->code.data[p_offset];                            // one byte is_local;
    uint32_t index = decode_int(p_chunk->code.data + p_offset, UINT24_BYTE_COUNT); // 3 bytes index
    printf("%04d           |  %s %d\n", p_offset - 2, is_local ? "local" : "upvalue", index);
    p_offset += 3;
  }
  return p_offset;
}
