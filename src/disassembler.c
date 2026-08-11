#include "disassembler.h"
#include <stdio.h>

// this should ultimately use a custom logger so we can output to any stream we wish, but for now it is ok to just use printf.

#define UNKNOWN_ASSUMED_WIDTH 1

const char* disassemble_chunk(chunk* p_chunk, const char* p_name) {
  printf("-- %s --\n", p_name);

  for (uint32_t offset = 0; offset < p_chunk->code.count;) {
    offset = disassemble_instruction(p_chunk, offset);
  }
  return NULL;
}

static uint32_t op_code_instruction(const char* p_opname, uint32_t p_offset);
static uint32_t constant_instruction(const char* p_opname, uint32_t p_offset, const chunk* p_chunk);

uint32_t disassemble_instruction(chunk* p_chunk, uint32_t p_offset) {
  printf("%04d ", p_offset);

  line_info* info = source_info_find(&p_chunk->source_info, p_offset);
  if (info && p_offset == 0) {
    printf("%4d:%d ", info->line, info->offset);
  }
  if (p_offset > 0) {
    line_info* prev_info = source_info_find(&p_chunk->source_info, p_offset - 1);
    line_info* info = source_info_find(&p_chunk->source_info, p_offset);
    if (info && prev_info) {
      if (info->line ==  prev_info->line) {
	printf("   |");
      } else {
	printf("%4d:", info->line);
      }
      if (info->offset ==  prev_info->offset) {
	printf(" | ");
      }
      if (info->offset !=  prev_info->offset)  {
	printf("%d ", info->offset);
      }

    }
  }

  uint8_t instruction = p_chunk->code.code_array[p_offset];
  switch (instruction) {
    case OP_RETURN: 
      return op_code_instruction("OP_RETURN", p_offset);
    case OP_CONSTANT:
      return constant_instruction("OP_CONSTANT", p_offset, p_chunk);
    case OP_NEGATE: 
      return op_code_instruction("OP_NEGATE", p_offset);
    case OP_ADD:
      return op_code_instruction("OP_ADD", p_offset);
    case OP_SUBTRCT: 
      return op_code_instruction("OP_SUBTRACT", p_offset);
    case OP_MULTIPLY:
      return op_code_instruction("OP_MULTIPLY", p_offset);
    case OP_DIVIDE:
      return op_code_instruction("OP_DIVIDE", p_offset);
    default:
      printf("unknown opcode %d (assumed width: %u)\n", instruction, UNKNOWN_ASSUMED_WIDTH);
      return p_offset + UNKNOWN_ASSUMED_WIDTH; // should we even advance here? since it could also be multibyte instruction which will break anyway!
  }
}

uint32_t op_code_instruction(const char* p_opname, uint32_t p_offset) {
  printf("%s\n", p_opname);
  return p_offset + 1;
}

uint32_t constant_instruction(const char* p_opname, uint32_t p_offset, const chunk* p_chunk) {
  uint8_t constant = p_chunk->code.code_array[p_offset + OP_CONSTANT_OPERANDS_WIDTH];
  printf("%-16s %4d '", p_opname, constant);
  value_debug_print(p_chunk->constants.value_array[constant]);
  printf("'\n");
  return p_offset + OP_CONSTANT_OPERANDS_WIDTH + OP_CODE_WIDTH;
}
