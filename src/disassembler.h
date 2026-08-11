#ifndef OK_DISASSEMBLER_H
#define OK_DISASSEMBLER_H

#include <stdint.h>

#include "chunk.h"

const char* disassemble_chunk(chunk* p_chunk, const char* p_name);
uint32_t disassemble_instruction(chunk* p_chunk, uint32_t p_offset);

#endif // OK_DISASSEMBLER_H
