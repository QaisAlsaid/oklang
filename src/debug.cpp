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
    case to_utype(opcode::op_modulo):
      return simple_instruction("op_modulo", p_offset);
    case to_utype(opcode::op_and):
      return simple_instruction("op_and", p_offset);
    case to_utype(opcode::op_xor):
      return simple_instruction("op_xor", p_offset);
    case to_utype(opcode::op_or):
      return simple_instruction("op_or", p_offset);
    case to_utype(opcode::op_shift_left):
      return simple_instruction("op_shift_left", p_offset);
    case to_utype(opcode::op_shift_right):
      return simple_instruction("op_shift_right", p_offset);
    case to_utype(opcode::op_add_assign):
      return simple_instruction("op_plus_equal", p_offset);
    case to_utype(opcode::op_subtract_assign):
      return simple_instruction("op_minus_equal", p_offset);
    case to_utype(opcode::op_multiply_assign):
      return simple_instruction("op_multiply_equal", p_offset);
    case to_utype(opcode::op_divide_assign):
      return simple_instruction("op_divide_equal", p_offset);
    case to_utype(opcode::op_modulo_assign):
      return simple_instruction("op_modulo_equal", p_offset);
    case to_utype(opcode::op_and_assign):
      return simple_instruction("op_and_equal", p_offset);
    case to_utype(opcode::op_xor_assign):
      return simple_instruction("op_xor_equal", p_offset);
    case to_utype(opcode::op_or_assign):
      return simple_instruction("op_or_equal", p_offset);
    case to_utype(opcode::op_shift_left_assign):
      return simple_instruction("op_shift_left_equal", p_offset);
    case to_utype(opcode::op_shift_right_assign):
      return simple_instruction("op_shift_right_equal", p_offset);
    case to_utype(opcode::op_as):
      return simple_instruction("op_as", p_offset);
    case to_utype(opcode::op_additive):
      return simple_instruction("op_additive", p_offset);
    case to_utype(opcode::op_negate):
      return simple_instruction("op_negate", p_offset);
    case to_utype(opcode::op_tiled):
      return simple_instruction("op_tiled", p_offset);
    case to_utype(opcode::op_preincrement):
      return simple_instruction("op_preincrement", p_offset);
    case to_utype(opcode::op_predecrement):
      return simple_instruction("op_predecrement", p_offset);
    case to_utype(opcode::op_postdecrement):
      return simple_instruction("op_postdecrement", p_offset);
    case to_utype(opcode::op_postincrement):
      return simple_instruction("op_postincrement", p_offset);
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
      return define_global_instruction("op_define_global", p_chunk, p_offset);
    case to_utype(opcode::op_define_global_long):
      return define_global_long_instruction("op_define_global_long", p_chunk, p_offset);
    case to_utype(opcode::op_get_global):
      return identifier_instruction("op_get_global", p_chunk, p_offset);
    case to_utype(opcode::op_get_global_long):
      return identifier_long_instruction("op_get_global_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_global):
      return identifier_instruction("op_set_global", p_chunk, p_offset);
    case to_utype(opcode::op_set_global_long):
      return identifier_long_instruction("op_set_global_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_if_global):
    {
      p_offset = identifier_instruction("op_set_if_global", p_chunk, p_offset);
      return set_if_instruction(p_chunk, p_offset);
    }
    case to_utype(opcode::op_set_if_global_long):
    {
      p_offset = identifier_long_instruction("op_set_if_global", p_chunk, p_offset);
      return set_if_instruction(p_chunk, p_offset);
    }
    case to_utype(opcode::op_get_local):
      return single_operand_instruction("op_get_local", p_chunk, p_offset);
    case to_utype(opcode::op_get_local_long):
      return multi_operand_instruction<3>("op_get_local_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_local):
      return single_operand_instruction("op_set_local", p_chunk, p_offset);
    case to_utype(opcode::op_set_local_long):
      return multi_operand_instruction<3>("op_set_local_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_if_local):
    {
      p_offset = single_operand_instruction("set_if_local", p_chunk, p_offset);
      return set_if_instruction(p_chunk, p_offset);
    }
    case to_utype(opcode::op_set_if_local_long):
    {
      p_offset = multi_operand_instruction<3>("set_if_local_long", p_chunk, p_offset);
      return set_if_instruction(p_chunk, p_offset);
    }
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
    case to_utype(opcode::op_set_upvalue):
      return single_operand_instruction("op_set_upvalue", p_chunk, p_offset);
    case to_utype(opcode::op_set_upvalue_long):
      return multi_operand_instruction<3>("op_set_upvalue_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_if_upvalue):
    {
      p_offset = single_operand_instruction("set_if_upvalue", p_chunk, p_offset);
      return set_if_instruction(p_chunk, p_offset);
    }
    case to_utype(opcode::op_set_if_upvalue_long):
    {
      p_offset = multi_operand_instruction<3>("set_if_upvalue_long", p_chunk, p_offset);
      return set_if_instruction(p_chunk, p_offset);
    }
    case to_utype(opcode::op_close_upvalue):
      return simple_instruction("op_close_upvalue", p_offset);
    case to_utype(opcode::op_class):
      return class_instruction("op_class", p_chunk, p_offset);
    case to_utype(opcode::op_class_long):
      return class_long_instruction("op_class_long", p_chunk, p_offset);
    case to_utype(opcode::op_get_property):
      return identifier_instruction("op_get_property", p_chunk, p_offset);
    case to_utype(opcode::op_get_property_long):
      return identifier_long_instruction("op_get_property_long", p_chunk, p_offset);
    case to_utype(opcode::op_set_property):
      return identifier_instruction("op_set_property", p_chunk, p_offset);
    case to_utype(opcode::op_set_property_long):
      return identifier_long_instruction("op_set_property_long", p_chunk, p_offset);
    case to_utype(opcode::op_method):
      return method_instruction("op_method", p_chunk, p_offset);
    case to_utype(opcode::op_method_long):
      return method_long_instruction("op_method_long", p_chunk, p_offset);
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
    case to_utype(opcode::op_special_method):
      return special_method_instruction("op_special_method", p_chunk, p_offset);
    case to_utype(opcode::op_convert_method):
      return convert_method_instruction("op_convert_method", p_chunk, p_offset);
    case to_utype(opcode::op_save_slot):
      return simple_instruction("op_save_slot", p_offset);
    case to_utype(opcode::op_push_saved_slot):
      return simple_instruction("op_push_saved_slot", p_offset);
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

  int disassembler::define_global_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                      // from start offset
    constexpr auto INSTRUCTION_SIZE = CONSTANT_INDEX + sizeof(uint8_t) * 2; // from start offset
    uint32_t constant = p_chunk.code[p_offset + CONSTANT_INDEX];
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::print("' ");
    std::println("{:4d}", p_chunk.code[p_offset + INSTRUCTION_SIZE - 1]);
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::define_global_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_LONG_INDEX = 1;
    constexpr auto INSTRUCTION_SIZE = CONSTANT_LONG_INDEX + sizeof(uint8_t) * 4;
    const uint32_t constant = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX);
    std::print("{} {:4d} '", p_name, constant);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::print("' ");
    std::println("{:4d}", p_chunk.code[p_offset + INSTRUCTION_SIZE - 1]);
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

    auto fun = OK_VALUE_AS_FUNCTION_OBJECT(p_chunk.constants[constant]);
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

  int disassembler::method_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                      // from start offset
    constexpr auto INSTRUCTION_SIZE = 1 + CONSTANT_INDEX + sizeof(uint8_t); // from start offset
    uint32_t constant = p_chunk.code[p_offset + CONSTANT_INDEX];
    uint32_t arity = p_chunk.code[p_offset + CONSTANT_INDEX + 1];
    std::print("{} {:4d} {} '", p_name, constant, arity);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::method_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_LONG_INDEX = 1;
    constexpr auto INSTRUCTION_SIZE = 1 + CONSTANT_LONG_INDEX + sizeof(uint8_t) * 3;
    const uint32_t constant = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX);
    uint32_t arity = p_chunk.code[p_offset + CONSTANT_LONG_INDEX + 3 + 1];
    std::print("{} {:4d} {} '", p_name, constant, arity);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::class_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                      // from start offset
    constexpr auto INSTRUCTION_SIZE = CONSTANT_INDEX + sizeof(uint8_t) + 3; // from start offset
    uint32_t constant = p_chunk.code[p_offset + CONSTANT_INDEX];
    uint32_t id = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_INDEX + 1);
    std::print("{} {:4d} {} '", p_name, constant, id);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::class_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_LONG_INDEX = 1;
    constexpr auto INSTRUCTION_SIZE = CONSTANT_LONG_INDEX + sizeof(uint8_t) * 3 + 3;
    const uint32_t constant = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX);
    uint32_t id = decode_int<uint32_t, 3>(p_chunk.code, p_offset + CONSTANT_LONG_INDEX + 1);
    std::print("{} {:4d} {} '", p_name, constant, id);
    get_g_vm()->print_value(p_chunk.identifiers[constant]);
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::special_method_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                                      // from start offset
    constexpr auto INSTRUCTION_SIZE = 1 + CONSTANT_INDEX + sizeof(uint8_t); // from start offset
    uint8_t type = p_chunk.code[p_offset + CONSTANT_INDEX];
    uint8_t arity = p_chunk.code[p_offset + CONSTANT_INDEX + 1];
    std::print("{} {:4d} {} '", p_name, type, arity);
    std::print("{}", method_type_to_string(type));
    std::println("'");
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::convert_method_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset)
  {
    constexpr auto CONSTANT_INDEX = 1;                    // from start offset
    constexpr auto INSTRUCTION_SIZE = 1 + CONSTANT_INDEX; // from start offset
    uint8_t arity = p_chunk.code[p_offset + CONSTANT_INDEX];
    std::println("{} {}", p_name, arity);
    return p_offset + INSTRUCTION_SIZE;
  }

  int disassembler::set_if_instruction(const chunk& p_chunk, int p_offset)
  {
    auto fcn = decode_int<uint64_t, 8>(p_chunk.code, p_offset);
    std::println("function: {}", (void*)fcn);
    return p_offset + sizeof(uint64_t);
  }
} // namespace ok::debug