#ifndef OK_CHUNK_HPP
#define OK_CHUNK_HPP

#include <stddef.h>
#include <stdint.h>

#include "value.h"

#define OP_CODE_WIDTH 1
#define OP_INVALID_OPERANDS_WIDTH 0
#define OP_RETURN_OPERANDS_WIDTH 0
#define OP_CONSTANT_OPERANDS_WIDTH  1
#define OP_NEGATE_OPERANDS_WIDTH 0
#define OP_ADD_OPERANDS_WIDTH 0
#define OP_SUBTRCT_OPERANDS_WIDTH 0
#define OP_MULTIPLY_OPERANDS_WIDTH 0
#define OP_DIVIDE_OPERANDS_WIDTH 0

typedef uint8_t byte;

typedef enum {
  OP_INVALID = 0,
  OP_RETURN,
  OP_CONSTANT,
  OP_NEGATE,
  OP_ADD,
  OP_SUBTRCT,
  OP_MULTIPLY,
  OP_DIVIDE
} OP_CODE;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  byte* code_array; 
} code;

void code_init(code* p_code);
void code_write(code* p_code, const byte p_byte);
void code_free(code* p_code);

typedef struct {
  uint32_t offset;
  uint32_t line;
  uint32_t reps;
} line_info;

void line_info_init(line_info* p_line_info);

typedef struct {
  uint32_t count;
  uint32_t capacity;
  line_info* line_info_array;
} source_info;

void source_info_init(source_info* p_source_info);
void source_info_write(source_info* p_source_info, line_info p_line_info);
line_info* source_info_find(source_info* p_source_info, const uint32_t p_instruction_index);
void source_info_free(source_info* p_source_info);

typedef struct {
  code code;
  values constants;
  source_info source_info;
} chunk;

void chunk_init(chunk* p_chunk);
void chunk_write_code_with_source_info(chunk* p_chunk, byte p_byte, line_info p_line_info);
void chunk_free(chunk* p_chunk);

#endif // OK_CHUNK_HPP
