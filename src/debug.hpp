#ifndef OK_DEBUG_HPP
#define OK_DEBUG_HPP

#include "chunk.hpp"
#include <string_view>

namespace ok::debug
{
  // TODO(Qais): offset size_t
  class disassembler
  {
  public:
    static void disassemble_chunk(const chunk& p_chunk, const std::string_view p_name);
    static int disassemble_instruction(const chunk& p_chunk, int p_offset);

  private:
    static int simple_instruction(const std::string_view p_name, int p_offset);
    static int constant_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int constant_long_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int identifier_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int identifier_long_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int single_operand_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset);
    template <size_t Count>
      requires(sizeof(uint8_t) * Count <= sizeof(size_t))
    static int multi_operand_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset)
    {
      constexpr auto INSTRUCTION_OP_INDEX = 1;
      const auto INSTRUCTION_WIDTH = INSTRUCTION_OP_INDEX + sizeof(uint8_t) * Count;
      auto operand = decode_int<size_t, Count>(p_chunk.code, p_offset + INSTRUCTION_OP_INDEX);
      std::println("{}: {}", p_name, operand);
      return p_offset + INSTRUCTION_WIDTH;
    }
    static int closure_instruction(const std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int invoke_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int invoke_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int method_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int method_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int class_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int class_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int define_global_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
    static int define_global_long_instruction(std::string_view p_name, const chunk& p_chunk, int p_offset);
  };
} // namespace ok::debug

#endif // OK_DEBUG_HPP