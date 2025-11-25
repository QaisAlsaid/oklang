#include "lexer.hpp"
#include "token.hpp"
#include "utf8.hpp"
#include <cctype>
#include <cstdint>

namespace ok
{
  token_array lexer::lex(const std::string_view p_input)
  {
    // TODO(Qais): estimate tokens count so you resize the vector
    if(p_input.size() == 0)
      return {};
    m_start_position = &p_input[0];
    m_current_position = m_start_position;
    m_current_line = 0;
    m_current_offset = 0;
    auto verr = utf8::validate(p_input);
    token_array arr;
    if(!verr.has_value())
    {
      // TODO(Qais): expressive error?!
      error(arr, "invalid utf-8 input");
      return arr;
    }

    while(true)
    {
      skip_whitespace();
      m_start_position = m_current_position;
      if(is_end())
      {
        emplace_token(arr, token_type::tok_eof);
        return arr;
      }

      auto prev = m_current_position; // for is_not_allowed
      auto c = advance();
      switch(c)
      {
      case '(':
        emplace_token(arr, token_type::tok_left_paren);
        break;
      case ')':
        emplace_token(arr, token_type::tok_right_paren);
        break;
      case '{':
        emplace_token(arr, token_type::tok_left_brace);
        break;
      case '}':
        emplace_token(arr, token_type::tok_right_brace);
        break;
      case '[':
        emplace_token(arr, token_type::tok_left_bracket);
        break;
      case ']':
        emplace_token(arr, token_type::tok_right_bracket);
        break;
      case ';':
        emplace_token(arr, token_type::tok_semicolon);
        break;
      case ':':
        emplace_token(arr, token_type::tok_colon);
        break;
      case ',':
        emplace_token(arr, token_type::tok_comma);
        break;
      case '.': // TODO(Qais): handle floats that start with . here like .69 => 0.69
        emplace_token(arr, token_type::tok_dot);
        break;
      case '+':
        if(match('+'))
        {
          emplace_token(arr, token_type::tok_plus_plus);
        }
        else if(match('='))
        {
          emplace_token(arr, token_type::tok_plus_equal);
        }
        else
        {
          emplace_token(arr, token_type::tok_plus);
        }
        break;
      case '-':
        if(match('-'))
        {
          emplace_token(arr, token_type::tok_minus_minus);
        }
        else if(match('='))
        {
          emplace_token(arr, token_type::tok_minus_equal);
        }
        else
        {
          emplace_token(arr, match('>') ? token_type::tok_arrow : token_type::tok_minus);
        }
        break;
      case '*':
        if(match('='))
        {
          emplace_token(arr, token_type::tok_asterisk_equal);
        }
        else
        {
          emplace_token(arr, token_type::tok_asterisk);
        }
        break;
      case '/':
        // TODO(Qais): validate comment logic
        if(*m_current_position == '/')
        {
          while(*m_current_position != '\n' && !is_end())
            advance();
        }
        else if(*m_current_position == '*')
        {
          while(*m_current_position != '*' && *utf8::peek(m_current_position, 1) != '/' && !is_end())
            advance();
        }
        else if(match('='))
        {
          emplace_token(arr, token_type::tok_slash_equal);
        }
        else
        {
          emplace_token(arr, token_type::tok_slash);
        }
        break;
      case '%':
        emplace_token(arr, match('=') ? token_type::tok_modulo_equal : token_type::tok_modulo);
        break;
      case '&':
        emplace_token(arr, match('=') ? token_type::tok_ampersand_equal : token_type::tok_ampersand);
        break;
      case '^':
        emplace_token(arr, match('=') ? token_type::tok_caret_equal : token_type::tok_caret);
        break;
      case '|':
        emplace_token(arr, match('=') ? token_type::tok_bar_equal : token_type::tok_bar);
        break;
      case '<<':
        emplace_token(arr, match('=') ? token_type::tok_shift_left_equal : token_type::tok_shift_left);
        break;
      case '>>':
        emplace_token(arr, match('=') ? token_type::tok_shift_right_equal : token_type::tok_shift_right);
        break;
      case '!':
        emplace_token(arr, match('=') ? token_type::tok_bang_equal : token_type::tok_bang);
        break;
      case '?':
        emplace_token(arr, token_type::tok_question);
        break;
      case '=':
        emplace_token(arr, match('=') ? token_type::tok_equal : token_type::tok_assign);
        break;
      case '<':
        emplace_token(arr, match('=') ? token_type::tok_less_equal : token_type::tok_less);
        break;
      case '>':
        emplace_token(arr, match('=') ? token_type::tok_greater_equal : token_type::tok_greater);
        break;
      case '\'':
      case '"':
        emplace_string(arr, c);
        break;
      default:
        if(isdigit(c))
        {
          emplace_number(arr);
          break;
        }
        if(!is_not_allowed(prev))
        {
          emplace_identifier(arr);
          break;
        }
        error(arr, "unexpected character");
        advance();
      }
    }
  }

  void lexer::emplace_string(token_array& p_array, char expect_end)
  {
    while(*m_current_position != expect_end && !is_end())
    {
      if(*m_current_position == '\n')
        m_current_line++;
      advance();
    }
    if(is_end())
    {
      error(p_array, "unterminated string");
      return;
    }
    // close
    advance();
    p_array.emplace_back(token_type::tok_string,
                         std::string(m_start_position + 1, m_current_position - m_start_position - 2),
                         m_current_line,
                         m_current_offset);
  }

  void lexer::emplace_number(token_array& p_array)
  {
    while(isdigit(*m_current_position))
      advance();
    if(*m_current_position == '.' && isdigit(*utf8::peek(m_current_position, 1)))
      advance();
    while(isdigit(*m_current_position))
      advance();
    return emplace_token(p_array, token_type::tok_number);
  }

  void lexer::emplace_identifier(token_array& p_array)
  {
    while(!is_not_allowed(m_current_position))
      advance();
    const auto tok = lookup_identifier(std::string(m_start_position, m_current_position - m_start_position));
    emplace_token(p_array, tok);
  }

  bool lexer::is_not_allowed(const char* c)
  {
    constexpr auto ascii = 1;
    auto uc = (const uint8_t*)c;
    auto next = (const uint8_t*)utf8::advance(c);
    long length = ascii;
    if(next != nullptr) // reached end => current is "\0" therfore length is ascii set it to 1
      length = next - uc;

    auto e = utf8::validate_codepoint(uc, next);
    bool ed = true;
    if(length == ascii)
    {
      // TODO(Qais): check if there should be more invalids
      switch(*uc)
      {
      case '(':
      case ')':
      case '{':
      case '}':
      case '[':
      case ']':
      case ';':
      case ':':
      case ',':
      case '.':
      case '+':
      case '-':
      case '*':
      case '/':
      case '!':
      case '=':
      case '<':
      case '>':
      case '\'':
      case '"':
      case '\\':
      case '#':
      case '@':
      case '$':
      case '%':
      case '^':
      case '&':
      case '`':
      case ' ':
      case '\t':
      case '\n':
      case '\r':
      case '\0':
        ed = false;
        break;
      }
    }
    auto v = e.value_or(0); // 0 invalid utf
    ed;                     // false illegal identifier
    if(v == false || ed == false)
      return true;
    return false;
  }
} // namespace ok