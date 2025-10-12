#include "debug.hpp"
#include "chunk.hpp"
#include "object.hpp"
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
    case to_utype(opcode::op_pop):
      return simple_instruction("op_pop", p_offset);
    case to_utype(opcode::op_pop_n):
      return single_operand_instruction("op_pop_n", p_chunk, p_offset);
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
    case to_utype(opcode::op_print):
      return simple_instruction("op_print", p_offset);
    case to_utype(opcode::op_define_global):
      return identifier_instruction("op_define_global", p_chunk, p_offset);
    case to_utype(opcode::op_define_global_long):
      return identifier_long_instruction("op_define_global_long", p_chunk, p_offset);
    case to_utype(opcode::op_get_global):
      return identifier_instruction("op_get_global", p_chunk, p_offset);
    case to_utype(opcode::op_get_global_long):
      return identifier_long_instruction("op_get_global_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_global):
      return identifier_instruction("op_set_global", p_chunk, p_offset);
    case to_utype(opcode::op_set_global_long):
      return identifier_long_instruction("op_set_global_long", p_chunk, p_offset);
    case to_utype(opcode::op_get_local):
      return single_operand_instruction("op_get_local", p_chunk, p_offset);
    case to_utype(opcode::op_get_local_long):
      return multi_operand_instruction<3>("op_get_local_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_local):
      return single_operand_instruction("op_set_local", p_chunk, p_offset);
    case to_utype(opcode::op_set_local_long):
      return multi_operand_instruction<3>("op_set_local_long", p_chunk, p_offset);
    case to_utype(opcode::op_conditional_jump):
      return multi_operand_instruction<3>("op_conditional_jump", p_chunk, p_offset);
    case to_utype(opcode::op_conditional_truthy_jump):
      return multi_operand_instruction<3>("op_conditional_truthy_jump", p_chunk, p_offset);
    case to_utype(opcode::op_conditional_jump_leave):
      return multi_operand_instruction<3>("op_conditional_jump_leave", p_chunk, p_offset);
    case to_utype(opcode::op_conditional_truthy_jump_leave):
      return multi_operand_instruction<3>("op_conditional_truthy_jump_leave", p_chunk, p_offset);
    case to_utype(opcode::op_jump):
      return multi_operand_instruction<3>("op_jump", p_chunk, p_offset);
    case to_utype(opcode::op_loop):
      return multi_operand_instruction<3>("op_loop", p_chunk, p_offset);
    case to_utype(opcode::op_call):
      return single_operand_instruction("op_call", p_chunk, p_offset);
    case to_utype(opcode::op_closure):
      return closure_instruction("op_closure", p_chunk, p_offset);
    case to_utype(opcode::op_get_upvalue):
      return single_operand_instruction("op_get_upvalue", p_chunk, p_offset);
    case to_utype(opcode::op_get_upvalue_long):
      return multi_operand_instruction<3>("op_get_upvalue_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_up_value):
      return single_operand_instruction("op_set_upvalue", p_chunk, p_offset);
    case to_utype(opcode::op_set_upvalue_long):
      return multi_operand_instruction<3>("op_set_upvalue_long", p_chunk, p_offset);
    case to_utype(opcode::op_close_upvalue):
      return simple_instruction("op_close_upvalue", p_offset);
    case to_utype(opcode::op_class):
      return identifier_instruction("op_class", p_chunk, p_offset);
    case to_utype(opcode::op_class_long):
      return identifier_long_instruction("op_class", p_chunk, p_offset);
    case to_utype(opcode::op_get_property):
      return identifier_instruction("op_get_property", p_chunk, p_offset);
    case to_utype(opcode::op_get_property_long):
      return identifier_long_instruction("op_get_property_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_property):
      return identifier_instruction("op_set_property", p_chunk, p_offset);
    case to_utype(opcode::op_set_property_long):
      return identifier_long_instruction("op_set_property_long", p_chunk, p_offset);
    case to_utype(opcode::op_method):
      return identifier_instruction("op_method", p_chunk, p_offset);
    case to_utype(opcode::op_method_long):
      return identifier_long_instruction("op_method_long", p_chunk, p_offset);
    case to_utype(opcode::op_invoke):
      return invoke_instruction("op_invoke", p_chunk, p_offset);
    case to_utype(opcode::op_invoke_long):
      return invoke_long_instruction("op_invoke_long", p_chunk, p_offset);
    case to_utype(opcode::op_inherit):
      return simple_instruction("op_inherit", p_offset);
    case to_utype(opcode::op_get_super):
      return identifier_instruction("op_get_super", p_chunk, p_offset);
    case to_utype(opcode::op_get_super_long):
      return identifier_long_instruction("op_get_super_long", p_chunk, p_offset);
    case to_utype(opcode::op_invoke_super):
      return invoke_instruction("op_invoke_super", p_chunk, p_offset);
    case to_utype(opcode::op_invoke_super_long):
      return invoke_long_instruction("op_invoke_super_long", p_chunk, p_offset);
    default:
    {
      std::println("unknown opcode: '{}'", instruction);
      return p_offset + 1;
    }
    }
  }

  int disassembler::simple_instruction(const std::string_view p_name, int p_offset)
  {
    std::println("{}", p_name);
    return p_offset + 1;
  }

  int disassembler::single_operand_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    std::println("{}: {:4d}", p_name, p_chunk.code[p_offset + 1]);
    return p_offset + 2;
  }

  int disassembler::constant_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                  // from start offset
    constexpr auto INSTRUCTION_SIZE = CONSTANT_INDEX + sizeof(uint8_t); // from start offset
    uint32_t constant = p_chunk.code[p_offset + CONSTANT_INDEX];
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.constants[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::constant_long_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_LONG_INDEX = 1;
    constexpr auto INSTRUCTION_SIZE = CONSTANT_LONG_INDEX + sizeof(uint8_t) * 3;
    const uint32_t constant = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX);
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.constants[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::identifier_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                  // from start offset
    constexpr auto INSTRUCTION_SIZE = CONSTANT_INDEX + sizeof(uint8_t); // from start offset
    uint32_t constant = p_chunk.code[p_offset + CONSTANT_INDEX];
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::identifier_long_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_LONG_INDEX = 1;
    constexpr auto INSTRUCTION_SIZE = CONSTANT_LONG_INDEX + sizeof(uint8_t) * 3;
    const uint32_t constant = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX);
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::closure_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    auto const_inst = p_chunk.code[++p_offset];
    uint32_t constant;
    if(const_inst == to_utype(opcode::op_constant))
      constant = p_chunk.code[++p_offset];
    else
    {
      constant = decode_int<uint32_t, 3>(p_chunk.code, ++p_offset);
      p_offset = p_offset + 3;
    }
    std::print("{} {:4d} ", p_name, constant);
    get_g_vm()->print_value(p_chunk.constants[constant]);
    std::println();

    auto fun = (function_object*)p_chunk.constants[constant].as.obj;
    for(uint32_t i = 0; i < fun->upvalues; ++i)
    {
      auto is_local = p_chunk.code[p_offset++];
      auto index = decode_int<uint32_t, 3>(p_chunk.code, ++p_offset);
      std::println("    {:4d} {} {}", p_offset - 2, is_local ? "local" : "upvalue", index);
      p_offset += 3;
    }
    return ++p_offset;
  }

  int disassembler::invoke_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    auto ident = p_chunk.code[p_offset + 1];
    auto argc = p_chunk.code[p_offset + 2];
    std::print("{} ({} args) {:4d} ", p_name, argc, ident);
    get_g_vm()->print_value(p_chunk.identifiers[ident]);
    std::println("");
    return p_offset + 3;
  }

  int disassembler::invoke_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    auto ident = decode_int<uint32_t, 3>(p_chunk.code, p_offset + 1);
    auto argc = p_chunk.code[p_offset + 5];
    std::print("{} ({} args) {:4d} ", p_name, argc, ident);
    get_g_vm()->print_value(p_chunk.identifiers[ident]);
    std::println("");
    return p_offset + 6;
  }
} // namespace ok::debug