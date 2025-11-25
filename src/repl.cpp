#include "repl.hpp"
#include "chunk.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace ok
{

  struct raw_mode
  {
    termios original;

    raw_mode()
    {
      tcgetattr(STDIN_FILENO, &original);
      termios raw = original;
      raw.c_lflag &= ~(ICANON | ECHO);
      raw.c_cc[VMIN] = 1;
      raw.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }

    ~raw_mode()
    {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    }
  };

  enum class key
  {
    key_unknown,
    key_eof,
    key_char,
    key_left,
    key_right,
    key_up,
    key_down,
    key_enter,
    key_backspace,
    key_delete,
  };

  key read_key(char& out)
  {
    char c;
    auto res = read(STDIN_FILENO, &c, 1);

    if(res != 1)
    {
      return key::key_unknown;
    }

    if(c == '\n')
    {
      return key::key_enter;
    }
    if(c == 127)
    {
      return key::key_backspace;
    }
    if(c == '\x04')
    {
      return key::key_eof;
    }
    if(c != '\x1b')
    {
      out = c;
      return key::key_char;
    }

    char seq[2];
    if(read(STDIN_FILENO, &seq[0], 1) != 1)
      return key::key_unknown;
    if(read(STDIN_FILENO, &seq[1], 1) != 1)
      return key::key_unknown;

    if(seq[0] == '[')
    {
      switch(seq[1])
      {
      case 'A':
        return key::key_up;
      case 'B':
        return key::key_down;
      case 'C':
        return key::key_right;
      case 'D':
        return key::key_left;
      case '3':
      {
        char ext;
        if(read(STDIN_FILENO, &ext, 1) != 1)
          return key::key_unknown;
        if(ext == '~')
          return key::key_delete;
      }
      }
    }
    return key::key_unknown;
  }

  void redraw_line(std::string_view p_prompt, std::string_view p_buffer, size_t p_cursor)
  {
    std::print("\r");
    std::print("{} {}", p_prompt, p_buffer);
    std::print("\x1b[K");
    size_t target = p_cursor + p_prompt.size();
    std::print("\r\x1b[{}C", target);
    std::fflush(stdout);
  }

  std::optional<std::string> read_line(std::string_view p_prompt, std::vector<std::string>& p_history)
  {
    std::string buff;
    size_t cursor = 0;
    size_t hist_idx = p_history.size();
    redraw_line(p_prompt, buff, cursor + 1);
    while(true)
    {
      char c;
      auto k = read_key(c);
      switch(k)
      {
      case key::key_char:
      {
        buff.insert(buff.begin() + cursor, c);
        redraw_line(p_prompt, buff, ++cursor + 1);
        break;
      }
      case key::key_left:
      {
        if(cursor > 0)
        {
          redraw_line(p_prompt, buff, --cursor + 1);
        }
        break;
      }
      case key::key_right:
      {
        if(cursor < buff.size())
        {
          redraw_line(p_prompt, buff, ++cursor + 1);
        }
        break;
      }
      case key::key_up:
      {
        if(hist_idx > 0)
        {
          buff = p_history[--hist_idx];
          cursor = buff.size();
          redraw_line(p_prompt, buff, cursor + 1);
        }
        break;
      }
      case key::key_down:
      {
        if(hist_idx > 0 && hist_idx < p_history.size() - 1)
        {
          buff = p_history[hist_idx++];
        }
        else
        {
          hist_idx = p_history.size() - 1;
          buff.clear();
        }
        cursor = buff.size();
        redraw_line(p_prompt, buff, cursor + 1);
        break;
      }
      case key::key_backspace:
      {
        if(cursor > 0)
        {
          buff.erase(buff.begin() + --cursor);
          redraw_line(p_prompt, buff, cursor + 1);
        }
        break;
      }
      case key::key_delete:
      {
        if(buff.size() > cursor)
        {
          buff.erase(buff.begin() + cursor);
          redraw_line(p_prompt, buff, cursor + 1);
        }
        break;
      }
      case key::key_enter:
      {
        std::println();
        return buff;
      }
      case key::key_eof:
      {
        return {};
      }
      case key::key_unknown:
      {
        break; // ignore
      }
      }
    }
  }

  constexpr auto prompt = ">>";
  void repl::start()
  {
    raw_mode term;

    ok::vm vm;
    ok::vm_guard guard{&vm};
    vm.init();
    std::vector<std::string> history;
    while(true)
    {
      auto ret = read_line(prompt, history);
      if(!ret.has_value())
      {
        break;
      }
      history.push_back(ret.value());
      // running = static_cast<bool>(std::getline(std::cin, line));
      // if(!running)
      //   break;

      const auto res = vm.interpret("", ret.value());
      switch(res)
      {
      case ok::vm::interpret_result::parse_error:
        vm.get_parse_errors().show();
        break;
      case ok::vm::interpret_result::compile_error:
        vm.get_compile_errors().show();
        break;
      case ok::vm::interpret_result::runtime_error:
      case ok::vm::interpret_result::ok:
        break;
      }
    }
  }
} // namespace ok