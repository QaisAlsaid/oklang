#ifndef OK_LEXER_HPP
#define OK_LEXER_HPP

#include "token.hpp"
#include "utf8.hpp"
#include <string_view>

namespace ok
{
  // TODO(Qais): calculate proper offset!
  class lexer
  {
  public:
    // lex all input ahead of time, for multi pass compiler(if i end up doing that)
    token_array lex(const std::string_view p_input);

  private:
    inline bool is_end()
    {
      return *m_current_position == '\0';
    }
    void emplace_token(token_array& p_array, const token_type p_type)
    {
      p_array.emplace_back(p_type,
                           std::string(m_start_position, m_current_position - m_start_position),
                           m_current_line,
                           m_current_offset);
    }
    void error(token_array& p_array, const std::string_view p_error)
    {
      p_array.emplace_back(token_type::tok_error, std::string(p_error), m_current_line, m_current_offset);
    }
    char advance()
    {
      auto tmp = *m_current_position;
      m_current_position = utf8::advance(m_current_position);
      return tmp;
    }
    bool match(char p_expect)
    {
      if(is_end())
        return false;
      if(*m_current_position != p_expect)
        return false;
      m_current_position = utf8::advance(m_current_position);
      return true;
    }
    void skip_whitespace()
    {
      while(true)
      {
        // auto c = utf8::peek(m_current_position, 1);
        auto c = *m_current_position;
        switch(c)
        {
        case ' ':
        case '\t':
        case '\r':
          advance();
          break;
        case '\n':
          m_current_line++;
          advance();
          break;
        default:
          return;
        }
      }
    }
    bool is_not_allowed(const char* c);
    void emplace_string(token_array& p_array, char expect_end);
    void emplace_number(token_array& p_array);
    void emplace_identifier(token_array& p_array);

  private:
    const char* m_start_position = nullptr;
    const char* m_current_position = nullptr;
    size_t m_current_line = 0;
    size_t m_current_offset = 0;
  };
} // namespace ok

#endif // OK_LEXER_HPP