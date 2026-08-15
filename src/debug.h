#ifndef OK_DEBUG_H
#define OK_DEBUG_H

#include <stdint.h>

#include "ast.h"
#include "chunk.h"

const char* debug_disassemble_chunk(const chunk* p_chunk, const char* p_name);
uint32_t debug_disassemble_instruction(const chunk* p_chunk, uint32_t p_offset);

#endif // OK_DEBUG_H
