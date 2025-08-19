#include "lexer.hpp"

namespace ok
{
  token_array lexer::lex(const std::string_view p_input)
  {
    // TODO(Qais): estimate tokens count so you resize the vector
    m_start_position = &p_input[0];
    m_current_position = m_start_position;
    m_current_line = 1;
    m_current_offset = 0;
  }
} // namespace ok