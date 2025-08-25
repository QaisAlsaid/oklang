#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "utility.hpp"
#include "value.hpp"
#include <cassert>
#include <print>
#include <string_view>
#include <vector>

#define DEBUG_PRINT

namespace ok
{
  vm::vm()
  {
    m_stack.reserve(stack_base_size);
    m_chunk = new chunk;
  }

  vm::~vm()
  {
    if(m_chunk)
      delete m_chunk;
  }

  auto vm::interpret(const std::string_view p_source) -> interpret_result
  {
    compiler com;
    auto compile_result = com.compile(p_source, m_chunk);
    if(!compile_result)
      return interpret_result::compile_error;
    return interpret_result::ok;
    // m_ip = m_chunk->code.data();
    // auto res = run();
    // return res;
  }

  auto vm::run() -> interpret_result
  {
    auto end = m_chunk->code.data() + m_chunk->code.size();
    while(m_ip < end)
    {
#if defined(DEBUG_PRINT) // TODO(Qais): proper build targets amd definations
      std::print("    ");
      for(auto e : m_stack)
      {
        std::print("[");
        e.print();
        std::print("]");
      }
      std::println();
      // FIXME(Qais): offset isnt being calculated properly, Update: i think its fixed, keeping this if it broke
      debug::disassembler::disassemble_instruction(*m_chunk, static_cast<size_t>(m_ip - m_chunk->code.data()));
#endif
      // TODO(Qais): computed goto
      switch(uint8_t instruction = read_byte())
      {
      case to_utype(opcode::op_return):
      {
        // TODO(Qais): abstract
        auto back = m_stack.back();
        m_stack.pop_back();
        back.print();
        std::println();
        return interpret_result::ok;
      }
      case to_utype(opcode::op_constant):
      case to_utype(opcode::op_constant_long):
      {
        auto val = read_constant(static_cast<opcode>(instruction));
        m_stack.push_back(val);
        break;
      }
      case to_utype(opcode::op_negate):
      {
        m_stack.back() *= -1;
        break;
      }
      // TODO(Qais): DRY stupid
      case to_utype(opcode::op_add):
      {
        auto b = m_stack.back();
        m_stack.pop_back();
        auto a = m_stack.back();
        m_stack.pop_back();
        m_stack.push_back(a + b);
        break;
      }
      case to_utype(opcode::op_subtract):
      {
        auto b = m_stack.back();
        m_stack.pop_back();
        auto a = m_stack.back();
        m_stack.pop_back();
        m_stack.push_back(a - b);
        break;
      }
      case to_utype(opcode::op_multiply):
      {
        auto b = m_stack.back();
        m_stack.pop_back();
        auto a = m_stack.back();
        m_stack.pop_back();
        m_stack.push_back(a * b);
        break;
      }
      case to_utype(opcode::op_divide):
      {
        auto b = m_stack.back();
        m_stack.pop_back();
        auto a = m_stack.back();
        m_stack.pop_back();
        m_stack.push_back(a / b);
        break;
      }
      }
    }
  }

  byte vm::read_byte()
  {
    const auto base = m_chunk->code.data();
    const auto end = base + m_chunk->code.size();
    assert(m_ip < end && "ip out of bounds");
    return *m_ip++;
  }

  value_type vm::read_constant(opcode p_op)
  {
    // here m_ip is the first in the series of bytes
    if(p_op == opcode::op_constant)
    {
      const uint32_t index = read_byte();
      assert(index < m_chunk->constants.size() && "constant index out of range");
      return m_chunk->constants[index]; // here we get it then m_ip is next
    }

    // here we get 1st then m_ip is next so on.... till we get 3rd and m_ip is 4th
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    assert(index < m_chunk->constants.size() && "constant index out of range");
    return m_chunk->constants[index];
  }

} // namespace ok