#ifndef OK_DEBUG_H
#define OK_DEBUG_H

#include <stdint.h>

#include "okast.h"
#include "okchunk.h"
#include "okglobals_store.h"
#include "okobject.h"

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

bool debug_disassemble_chunk(disassembler* p_disassembler, const char* p_name);
uint32_t debug_disassemble_instruction(disassembler* p_disassembler, uint32_t p_offset);

// dumps a function's chunk, then recurses into the protos in its constants
// table (nested functions after their parent, luac style).
bool debug_disassemble_function(disassembler* p_disassembler, const object_function* p_function, const char* p_name);

#endif // OK_DEBUG_H
