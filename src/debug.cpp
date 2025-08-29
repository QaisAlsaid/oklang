#include "debug.hpp"
#include "chunk.hpp"
#include "utility.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <cstdint>
#include <print>
#include <string_view>

namespace ok::debug
{
  void disassembler::disassemble_chunk(const chunk& p_chunk, const std::string_view p_name)
  {
    std::println("== {} ==", p_name);
    for(auto offset = 0; offset < p_chunk.code.size();)
    {
      offset = disassemble_instruction(p_chunk, offset);
    }
  }

  int disassembler::disassemble_instruction(const chunk& p_chunk, int p_offset)
  {
    std::print("{:04d}", p_offset);

    // this was broken at first because it was comparing the previous offset regardless of that instruction being even
    // printed or it is used implicitly and its even more broken after introducing the small memory optimization for the
    // offset storage, so its not worth the hassel

    // [legacy]:
    // p_offset > 0 && p_chunk.offsets[p_offset - 1].offset ==
    // p_chunk.offsets[p_offset].offset
    //     ? std::print("   | ")
    //     : std::print("{:4d} ", p_chunk.get_offset(p_offset));

    std::print("{:4d} ", p_chunk.get_offset(p_offset));
    auto instruction = p_chunk.code[p_offset];
    switch(instruction)
    {
    case to_utype(opcode::op_return):
      return simple_instruction("op_return", p_offset);
    case to_utype(opcode::op_constant):
      return constant_instruction("op_constant", p_chunk, p_offset);
    case to_utype(opcode::op_constant_long):
      return constant_long_instruction("op_constant_long", p_chunk, p_offset);
    case to_utype(opcode::op_add):
      return simple_instruction("op_add", p_offset);
    case to_utype(opcode::op_subtract):
      return simple_instruction("op_subtract", p_offset);
    case to_utype(opcode::op_multiply):
      return simple_instruction("op_multiply", p_offset);
    case to_utype(opcode::op_divide):
      return simple_instruction("op_divide", p_offset);
    case to_utype(opcode::op_negate):
      return simple_instruction("op_negate", p_offset);
    case to_utype(opcode::op_true):
      return simple_instruction("op_true", p_offset);
    case to_utype(opcode::op_false):
      return simple_instruction("op_false", p_offset);
    case to_utype(opcode::op_null):
      return simple_instruction("op_null", p_offset);
    case to_utype(opcode::op_not):
      return simple_instruction("op_not", p_offset);
    case to_utype(opcode::op_equal):
      return simple_instruction("op_equal", p_offset);
    case to_utype(opcode::op_not_equal):
      return simple_instruction("op_not_equal", p_offset);
    case to_utype(opcode::op_less):
      return simple_instruction("op_less", p_offset);
    case to_utype(opcode::op_greater):
      return simple_instruction("op_greater", p_offset);
    case to_utype(opcode::op_less_equal):
      return simple_instruction("op_less_equal", p_offset);
    case to_utype(opcode::op_greater_equal):
      return simple_instruction("op_greater_equal", p_offset);
    default:
    {
      std::println("unknown opcode: {}", instruction);
      return p_offset + 1;
    }
    }
  }

  int disassembler::simple_instruction(const std::string_view p_name, int p_offset)
  {
    std::println("{}", p_name);
    return p_offset + 1;
  }

  int disassembler::constant_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                  // from start offset
    constexpr auto INSTRUCTION_SIZE = CONSTANT_INDEX + sizeof(uint8_t); // from start offset
    uint8_t constant = p_chunk.code[p_offset + CONSTANT_INDEX];
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.constants[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::constant_long_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_LONG_INDEX = 1;
    constexpr auto INSTRUCTION_SIZE = CONSTANT_LONG_INDEX + sizeof(uint8_t) * 3;
    // const uint8_t first = p_chunk.code[p_offset + CONSTANT_LONG_INDEX];
    // const uint8_t second = p_chunk.code[p_offset + CONSTANT_LONG_INDEX + 1];
    // const uint8_t third = p_chunk.code[p_offset + CONSTANT_LONG_INDEX + 2];
    // const uint32_t constant = (first << 16) | (second << 8) | third;
    const uint32_t constant = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX);
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.constants[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }
} // namespace ok::debug