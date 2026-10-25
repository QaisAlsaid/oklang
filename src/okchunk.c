#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "okchunk.h"
#include "okutils.h"

ARRAY_DEFINE_DEFAULT(code, word, ARRAY_DEFAULT_TYPE_DEINIT)
ARRAY_DEFINE_DEFAULT(source_info, ok_line_info_repeated, ARRAY_DEFAULT_TYPE_DEINIT)
#define IDENTIFIERS_DEINIT(x, a) string_deinit(x, a)
ARRAY_DEFINE(identifiers, string, uint32_t, UINT32_MAX, IDENTIFIERS_DEINIT)

bool source_info_write(source_info* p_source_info, const ok_line_info_repeated p_line_info) {
  if (p_source_info->count == 0) {
    goto append;
  }
  ok_line_info_repeated* back = &p_source_info->data[p_source_info->count - 1];
  if (back->line_info.line == p_line_info.line_info.line && back->line_info.offset == p_line_info.line_info.offset) {
    back->reps += p_line_info.reps;
  } else {
  append:
    return source_info_append(p_source_info, p_line_info);
  }
  return true;
}

void chunk_init(chunk* p_chunk, allocators* p_alloc) {
  p_chunk->alloc = p_alloc;
  code_init(&p_chunk->code, p_alloc);
  identifiers_init(&p_chunk->identifiers, p_alloc);
  value_array_init(&p_chunk->constants, p_alloc);
  source_info_init(&p_chunk->source_info, p_alloc);
}

void chunk_deinit(chunk* p_chunk) {
  code_deinit(&p_chunk->code, p_chunk->alloc);
  identifiers_deinit(&p_chunk->identifiers, p_chunk->alloc);
  value_array_deinit(&p_chunk->constants, p_chunk->alloc);
  source_info_deinit(&p_chunk->source_info, p_chunk->alloc);
}

bool chunk_write_word(chunk* p_chunk, const word p_word, const ok_line_info p_line_info) {
  if (!code_append(&p_chunk->code, p_word)) {
    return false;
  }
  ok_line_info_repeated info;
  info.reps = 1;
  info.line_info = p_line_info;
  return source_info_write(&p_chunk->source_info, info);
}

bool chunk_write_2words(chunk* p_chunk, const word p_word1, const word p_word2, const ok_line_info p_line_info) {
  if (!(code_append(&p_chunk->code, p_word1) && code_append(&p_chunk->code, p_word2))) {
    return false;
  }
  ok_line_info_repeated info;
  info.reps = 2;
  info.line_info = p_line_info;
  return source_info_write(&p_chunk->source_info, info);
}

bool chunk_write_word2(chunk* p_chunk, const word2 p_words, const ok_line_info p_line_info) {
  // low 32 bits first, high 32 bits second (matches VM JOIN_WORD + patch helpers)
  return chunk_write_2words(p_chunk, (word)(p_words & UINT64_C(0xffffffff)), (word)(p_words >> 32u), p_line_info);
}

index_wrapper chunk_add_constant(chunk* p_chunk, const value p_constant, const ok_line_info p_line_info) {
  if (p_chunk->constants.count >= CONSTANTS_MAX) {
    return (index_wrapper){
        0, 0, CONSTANT_OVERFLOW
    };
  }
  if (!value_array_append(&p_chunk->constants, p_constant)) {
    return (index_wrapper){0, 0, CONSTANT_ALLOCATION_FAILED};
  }
  return (index_wrapper){p_chunk->constants.count - 1, 0, CONSTANT_OK};
}

index_wrapper chunk_add_identifier(chunk* p_chunk, const ok_string_view p_identifier, const ok_line_info p_line_info) {
  if (p_chunk->identifiers.count >= IDENTIFIERS_MAX) {
    return (index_wrapper){
        0, 0, IDENTIFIER_OVERFLOW
    };
  }
  string owned = create_string_from_string_view(p_identifier, p_chunk->alloc);
  if (owned.chars == NULL) {
    return (index_wrapper){0, 0, IDENTIFIER_ALLOCATION_FAILED};
  }
  if (!identifiers_append(&p_chunk->identifiers, owned)) {
    string_deinit(&owned, p_chunk->alloc);
    return (index_wrapper){0, 0, IDENTIFIER_ALLOCATION_FAILED};
  }
  return (index_wrapper){p_chunk->identifiers.count - 1, 0, IDENTIFIER_OK};
}

ok_line_info_repeated* chunk_find_line_info(const chunk* p_chunk, const uint32_t p_instruction_index) {
  for (uint32_t num_instructions = 0, i = 0; i < p_chunk->source_info.count; ++i) {
    ok_line_info_repeated* info = &p_chunk->source_info.data[i];
    num_instructions += info->reps;
    if (num_instructions > p_instruction_index) {
      return info;
    }
  }
  return NULL;
}
