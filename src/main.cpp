#include "chunk.hpp"
#include "debug.hpp"
#include "lexer.hpp"
#include "token.hpp"
#include "vm.hpp"
#include <cstdint>
#include <print>

int main(int argc, char** argv)
{
  ok::chunk chunk;
  // auto constant_idx = chunk.add_constant(69);
  // chunk.write(ok::opcode::op_constant, 22);
  // chunk.write(constant_idx, 123);
  // chunk.write(ok::opcode::op_return, 123);
  // for(auto i = 0; i < UINT8_MAX + UINT8_MAX; ++i)
  //  chunk.write_constant(i, i);
  // chunk.write(ok::opcode::op_return, 22);

  // chunk.write_constant(3.14, 123);
  // chunk.write_constant(3.76, 123);
  // chunk.write(ok::opcode::op_add, 123);

  // chunk.write_constant(10, 123);
  // chunk.write(ok::opcode::op_divide, 123);
  // chunk.write(ok::opcode::op_negate, 123);
  // chunk.write(ok::opcode::op_negate, 123);
  // chunk.write(ok::opcode::op_return, 123);
  // ok::vm vm;
  // vm.interpret(&chunk);
  //  ok::debug::disassembler::disassemble_chunk(chunk, "test");

  ok::lexer lx;
  auto arr = lx.lex("//hello let letdown + 1;");
  for(auto idx = 0; auto elem : arr)
    std::println("found elem: type: {}, raw: {}, line: {}, at in array location: {}",
                 ok::token_type_to_string(elem.type),
                 elem.raw_literal,
                 elem.line,
                 idx++);
}