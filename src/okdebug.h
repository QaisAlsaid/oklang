#ifndef OK_DEBUG_H
#define OK_DEBUG_H

#include <stdint.h>

#include "okast.h"
#include "okchunk.h"
#include "okglobals_store.h"

typedef struct {
  const chunk* chunk;
  const globals_store* globals_store;
} disassembler;

typedef struct {
  const chunk* chunk;
  const globals_store* globals_store;
} disassembler_specs;

void disassembler_init(disassembler* p_disassembler, disassembler_specs p_specs);
void disassembler_deinit(disassembler* p_disassembler);

const char* debug_disassemble_chunk(disassembler* p_disassembler, const char* p_name);
uint32_t debug_disassemble_instruction(disassembler* p_disassembler, uint32_t p_offset);

#endif // OK_DEBUG_H
