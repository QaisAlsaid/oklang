#include "token.hpp"
#include <string_view>

namespace ok
{
  class lexer
  {
  public:
    // lex all input ahead of time, for multi pass compiler(if i end up doing that)
    token_array lex(const std::string_view p_input);

  private:
    const char* m_start_position = nullptr;
    const char* m_current_position = nullptr;
    size_t m_current_line = 0;
    size_t m_current_offset = 0;
  };
} // namespace ok