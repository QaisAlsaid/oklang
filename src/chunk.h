#ifndef OK_CHUNK_HPP
#define OK_CHUNK_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "array.h"
#include "source.h"
#include "value.h"

#define UINT24_MAX ((1 << 24) - 1)

#define CONSTANT_ALLOCATION_FAILED UINT32_MAX
#define CONSTANT_OVERFLOW (UINT32_MAX - 1)
#define CONSTANT_MAX UINT24_MAX
#define CONSTANT_INVALID (CONSTANT_MAX + 1)
#define IS_CONSTANT_VALID(c) ((c) < CONSTANT_INVALID)

#define OP_CONSTANT_MAX UINT8_MAX
#define OP_CONSTANT_LONG_MAX UINT24_MAX

#define OP_CODE_WIDTH 1
#define OP_INVALID_OPERANDS_WIDTH 0
#define OP_RETURN_OPERANDS_WIDTH 0
#define OP_CONSTANT_OPERANDS_WIDTH 1
#define OP_CONSTANT_LONG_OPERANDS_WIDTH 3
#define OP_POP_OPERANDS_WIDTH 0
#define OP_NULL_OPERANDS_WIDTH 0
#define OP_FALSE_OPERANDS_WIDTH 0
#define OP_TRUE_OPERANDS_WIDTH 0
#define OP_NOT_OPERANDS_WIDTH 0
#define OP_NEGATE_OPERANDS_WIDTH 0
#define OP_ADD_OPERANDS_WIDTH 0
#define OP_SUBTRCT_OPERANDS_WIDTH 0
#define OP_MULTIPLY_OPERANDS_WIDTH 0
#define OP_DIVIDE_OPERANDS_WIDTH 0
#define OP_EQUAL_OPERANDS_WIDTH 0
#define OP_NOT_EQUAL_OPERANDS_WIDTH 0
#define OP_LESS_OPERANDS_WIDTH 0
#define OP_GREATER_OPERANDS_WIDTH 0
#define OP_LESS_EQUAL_OPERANDS_WIDTH 0
#define OP_GREATER_EQUAL_OPERANDS_WIDTH 0
#define OP_PRINT_OPERANDS_WIDTH 0

typedef uint8_t byte;

typedef enum {
  OP_INVALID = 0,
  OP_RETURN,
  OP_CONSTANT,
  OP_CONSTANT_LONG,
  OP_POP,
  OP_NULL,
  OP_FALSE,
  OP_TRUE,
  OP_NOT,
  OP_NEGATE,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_EQUAL,
  OP_NOT_EQUAL,
  OP_LESS,
  OP_GREATER,
  OP_LESS_EQUAL,
  OP_GREATER_EQUAL,
  OP_PRINT,
} OP_CODE;

ARRAY_DECLARE(code, byte, uint32_t)
bool code_write_1byte(code* p_code, const byte p_byte);
bool code_write_2bytes(code* p_code, const byte p_1st_byte, const byte p_2nd_byte);
bool code_write(code* p_code, const byte* p_bytes, const size_t p_bytes_count);

ARRAY_DECLARE(line_info_array, line_info_repeated, uint32_t)
typedef struct {
  line_info_array line_info_array;
} source_info;

void source_info_init(source_info* p_source_info);
void source_info_deinit(source_info* p_source_info);
bool source_info_write(source_info* p_source_info, const line_info_repeated p_line_info);
line_info_repeated* source_info_find(const source_info* p_source_info, const uint32_t p_instruction_index);

typedef struct {
  code code;
  value_array constants;
  source_info source_info;
} chunk;

void chunk_init(chunk* p_chunk);
void chunk_deinit(chunk* p_chunk);
bool chunk_write_1byte_code_with_line_info(chunk* p_chunk, const byte p_byte, const line_info p_line_info);
bool chunk_write_2bytes_code_with_line_info(chunk* p_chunk,
                                            const byte p_1st_byte,
                                            const byte p_2nd_byte,
                                            const line_info p_line_info);
bool chunk_write_code_with_line_info(chunk* p_chunk,
                                     const byte* p_bytes,
                                     const size_t p_bytes_count,
                                     const line_info p_line_info);
uint32_t chunk_write_constant_with_line_info(chunk* p_chunk, const value p_constant, const line_info p_line_info);

#endif // OK_CHUNK_HPP
