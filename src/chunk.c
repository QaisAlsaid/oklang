#include "chunk.h"
#include "array.h"
#include "mm.h"
#include "utils.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void code_init(code* p_code) {
  OK_ARRAY_INIT(p_code->count, p_code->capacity, p_code->code_array);
}

void code_deinit(code* p_code) {
  OK_ARRAY_FREE(byte, p_code->capacity, p_code->code_array);
  code_init(p_code); // reset to known empty state.
}

bool code_write_1byte(code* p_code, byte p_byte) {
  OK_ARRAY_APPEND(byte, uint32_t, p_code->count, p_code->capacity, p_code->code_array, p_byte);
  return p_code->code_array != NULL;
}

bool code_write_2bytes(code* p_code, const byte p_1st_byte, const byte p_2nd_byte) {
  byte bytes[2] = {p_1st_byte, p_2nd_byte};
  OK_ARRAY_APPEND_N(byte, uint32_t, p_code->count, p_code->capacity, p_code->code_array, bytes, 2);
  return p_code->code_array != NULL;
}

bool code_write(code* p_code, const byte* p_bytes, const size_t p_bytes_count) {
  OK_ARRAY_APPEND_N(byte, uint32_t, p_code->count, p_code->capacity, p_code->code_array, p_bytes, p_bytes_count);
  return p_code->code_array != NULL;
}

void source_info_init(source_info* p_source_info) {
  OK_ARRAY_INIT(p_source_info->count, p_source_info->capacity, p_source_info->line_info_array);
}

void source_info_deinit(source_info* p_source_info) {
  OK_ARRAY_FREE(line_info, p_source_info->capacity, p_source_info->line_info_array);
}

bool source_info_write(source_info* p_source_info, const line_info_repeated p_line_info) {
  if (p_source_info->count == 0) {
    goto append;
  }
  line_info_repeated* back = &p_source_info->line_info_array[p_source_info->count - 1];
  if (back->line_info.line == p_line_info.line_info.line && back->line_info.offset == p_line_info.line_info.offset) {
    back->reps += p_line_info.reps;
  } else {
  append:
    OK_ARRAY_APPEND(line_info_repeated,
                    uint32_t,
                    p_source_info->count,
                    p_source_info->capacity,
                    p_source_info->line_info_array,
                    p_line_info);
    return p_source_info->line_info_array != NULL;
  }
  return true;
}

line_info_repeated* source_info_find(const source_info* p_source_info, const uint32_t p_instruction_index) {
  for (uint32_t num_instructions = 0, i = 0; i < p_source_info->count; ++i) {
    line_info_repeated* info = &p_source_info->line_info_array[i];
    num_instructions += info->reps;
    if (num_instructions > p_instruction_index) {
      return info;
    }
  }
  return NULL;
}

void chunk_init(chunk* p_chunk) {
  code_init(&p_chunk->code);
  value_array_init(&p_chunk->constants);
  source_info_init(&p_chunk->source_info);
}

void chunk_deinit(chunk* p_chunk) {
  code_deinit(&p_chunk->code);
  value_array_deinit(&p_chunk->constants);
  source_info_deinit(&p_chunk->source_info);
}

bool chunk_write_1byte_code_with_line_info(chunk* p_chunk, const byte p_byte, const line_info p_line_info) {
  if (!code_write_1byte(&p_chunk->code, p_byte)) {
    return false;
  }
  line_info_repeated info;
  info.reps = 1;
  info.line_info = p_line_info;
  return source_info_write(&p_chunk->source_info, info);
}

bool chunk_write_2bytes_code_with_line_info(chunk* p_chunk,
                                            const byte p_1st_byte,
                                            const byte p_2nd_byte,
                                            const line_info p_line_info) {
  if (!code_write_2bytes(&p_chunk->code, p_1st_byte, p_2nd_byte)) {
    return false;
  }
  line_info_repeated info;
  info.reps = 2;
  info.line_info = p_line_info;
  return source_info_write(&p_chunk->source_info, info);
}

bool chunk_write_code_with_line_info(chunk* p_chunk,
                                     const byte* p_bytes,
                                     const size_t p_bytes_count,
                                     const line_info p_line_info) {
  if (!code_write(&p_chunk->code, p_bytes, p_bytes_count)) {
    return false;
  }
  line_info_repeated info;
  info.reps = p_bytes_count;
  info.line_info = p_line_info;
  return source_info_write(&p_chunk->source_info, info);
}

uint32_t chunk_write_constant_with_line_info(chunk* p_chunk, const value p_constant, const line_info p_line_info) {
  if (p_chunk->constants.count >= CONSTANT_MAX) {
    return CONSTANT_OVERFLOW;
  }
  if (!value_array_append(&p_chunk->constants, p_constant)) {
    return CONSTANT_ALLOCATION_FAILED;
  }
  uint32_t index = p_chunk->constants.count - 1;
  if (index <= OP_CONSTANT_MAX) {
    if (!chunk_write_2bytes_code_with_line_info(p_chunk, OP_CONSTANT, (byte)index, p_line_info)) {
      return CONSTANT_ALLOCATION_FAILED;
    }
  } else {
    const uint8_t size = OP_CODE_WIDTH + OP_CONSTANT_LONG_OPERANDS_WIDTH;
    byte bytes[size];
    bytes[0] = OP_CONSTANT_LONG;
    encode_int(bytes + OP_CODE_WIDTH, OP_CONSTANT_LONG_OPERANDS_WIDTH, index);
    if (!chunk_write_code_with_line_info(p_chunk, bytes, size, p_line_info)) {
      return CONSTANT_ALLOCATION_FAILED;
    }
  }
  return index;
}
