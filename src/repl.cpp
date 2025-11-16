#include "repl.hpp"
#include "chunk.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <cstdio>
#include <iostream>
#include <print>
#include <string>
#include <termios.h>
#include <unistd.h>

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

  constexpr auto prompt = ">> ";
  void repl::start()
  {
    bool running = true;
    ok::vm vm;
    ok::vm_guard guard{&vm};
    vm.init();
    while(running)
    {
      std::print("{}", prompt);
      std::string line;
      running = static_cast<bool>(std::getline(std::cin, line));
      if(!running)
        break;

      const auto res = vm.interpret(line);
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