#ifndef OK_DEBUG_HPP
#define OK_DEBUG_HPP

#include "chunk.hpp"
#include <string_view>

namespace ok::debug
{
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
  };
} // namespace ok::debug

#endif // OK_DEBUG_HPP