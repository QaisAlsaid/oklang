#include <stddef.h>
#include "chunk.h"
#include "mm.h"
#include "array.h"

void code_init(code* p_code) {
  OK_ARRAY_INIT(p_code->count, p_code->capacity, p_code->code_array);
}

void code_write(code* p_code, byte p_byte) {
  OK_ARRAY_APPEND(byte, p_code->count, p_code->capacity, p_code->code_array, p_byte);
}

void code_free(code* p_code) {
  OK_ARRAY_FREE(byte, p_code->capacity, p_code->code_array);
  code_init(p_code); // reset to known empty state.
}

void line_info_init(line_info* p_line_info) {
  p_line_info->offset = 0;
  p_line_info->line = 0;
  p_line_info->reps = 1;
}

void source_info_init(source_info* p_source_info) {
  OK_ARRAY_INIT(p_source_info->count, p_source_info->capacity, p_source_info->line_info_array);
}

void source_info_write(source_info* p_source_info, line_info p_line_info) {
  if (p_source_info->count == 0) {
    OK_ARRAY_APPEND(line_info, p_source_info->count, p_source_info->capacity, p_source_info->line_info_array, p_line_info);
    return;
  }
  line_info* back = &p_source_info->line_info_array[p_source_info->count - 1];
  if (back->line == p_line_info.line && back->offset == p_line_info.offset) {
    back->reps += p_line_info.reps;
  } else {
    OK_ARRAY_APPEND(line_info, p_source_info->count, p_source_info->capacity, p_source_info->line_info_array, p_line_info);
  }
}

line_info* source_info_find(source_info* p_source_info, const uint32_t p_instruction_index) {
  for (uint32_t num_instructions = 0, i = 0; i < p_source_info->count; ++i) {
    line_info* info = &p_source_info->line_info_array[i]; 
    num_instructions += info->reps;
    if (num_instructions > p_instruction_index) {
      return info;
    }
  }
  return NULL;
}

void source_info_free(source_info* p_source_info) {
  OK_ARRAY_FREE(line_info, p_source_info->capacity, p_source_info->line_info_array);
}

void chunk_init(chunk* p_chunk) {
  code_init(&p_chunk->code);
  values_init(&p_chunk->constants);
  source_info_init(&p_chunk->source_info);
}

void chunk_write_code_with_source_info(chunk* p_chunk, byte p_byte, line_info p_line_info) {
  code_write(&p_chunk->code, p_byte);
  source_info_write(&p_chunk->source_info, p_line_info);
}

void chunk_free(chunk* p_chunk) {
  code_free(&p_chunk->code);
  values_free(&p_chunk->constants);
  source_info_free(&p_chunk->source_info);
}

