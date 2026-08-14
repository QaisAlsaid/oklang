#ifndef OK_CHUNK_HPP
#define OK_CHUNK_HPP

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "value.h"
#include "source.h"

#define CONSTANTS_INVALID UINT32_MAX
#define CONSTANTS_MAX (CONSTANTS_INVALID - 1)

#define UINT24_MAX ((1 << 24) - 1)
#define OP_CONSTANT_MAX UINT8_MAX 
#define OP_CONSTANT_LONG_MAX UINT24_MAX

#define OP_CODE_WIDTH 1
#define OP_INVALID_OPERANDS_WIDTH 0
#define OP_RETURN_OPERANDS_WIDTH 0
#define OP_CONSTANT_OPERANDS_WIDTH  1
#define OP_CONSTANT_LONG_OPERANDS_WIDTH 3
#define OP_POP_OPERANDS_WIDTH 0
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
  OP_CONSTANT_LONG,
  OP_POP,
  OP_NEGATE,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE
} OP_CODE;

typedef struct {
  uint32_t count;
  uint32_t capacity;
  byte* code_array; 
} code;

void code_init(code* p_code);
void code_deinit(code* p_code);
void code_write_1byte(code* p_code, const byte p_byte);
void code_write_2bytes(code* p_code, const byte p_1st_byte, const byte p_2nd_byte);
void code_write(code* p_code, const byte* p_bytes, const size_t p_bytes_count);

typedef struct {
  uint32_t count;
  uint32_t capacity;
  line_info_repeated* line_info_array;
} source_info;

void source_info_init(source_info* p_source_info);
void source_info_deinit(source_info* p_source_info);
void source_info_write(source_info* p_source_info, const line_info_repeated p_line_info);
line_info_repeated* source_info_find(const source_info* p_source_info, const uint32_t p_instruction_index);

typedef struct {
  code code;
  value_array constants;
  source_info source_info;
} chunk;

void chunk_init(chunk* p_chunk);
void chunk_deinit(chunk* p_chunk);
void chunk_write_1byte_code_with_line_info(chunk* p_chunk, const byte p_byte, const line_info p_line_info);
void chunk_write_2bytes_code_with_line_info(chunk* p_chunk, const byte p_1st_byte, const byte p_2nd_byte, const line_info p_line_info);
bool chunk_write_code_with_line_info(chunk* p_chunk, const byte* p_bytes, const size_t p_bytes_count, const line_info p_line_info);
uint32_t chunk_write_constant_with_line_info(chunk* p_chunk, const value p_constant, const line_info p_line_info);

#endif // OK_CHUNK_HPP
