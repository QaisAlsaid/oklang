#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include "value.hpp"
#include <cassert>
#include <expected>
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
    m_chunk->write(opcode::op_return, 123); // TODO(Qais): remove!
    m_ip = m_chunk->code.data();
    auto res = run();
    return res;
    // return interpret_result::ok;
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
        auto ret = perform_unary_prefix(operator_type::minus);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_add):
      {
        auto ret = perform_binary_infix(operator_type::plus);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_subtract):
      {
        auto ret = perform_binary_infix(operator_type::minus);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_multiply):
      {
        auto ret = perform_binary_infix(operator_type::asterisk);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_divide):
      {
        auto ret = perform_binary_infix(operator_type::slash);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_true):
      {
        m_stack.push_back(value_t{true});
        break;
      }
      case to_utype(opcode::op_false):
      {
        m_stack.push_back(value_t{false});
        break;
      }
      case to_utype(opcode::op_null):
      {
        m_stack.push_back(value_t{});
        break;
      }
      case to_utype(opcode::op_not):
      {
        m_stack.back() = value_t{m_stack.back().is_falsy()};
        break;
      }
      case to_utype(opcode::op_equal):
      {
        auto ret = perform_binary_infix(operator_type::equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_not_equal):
      {
        auto ret = perform_binary_infix(operator_type::bang_equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_less):
      {
        auto ret = perform_binary_infix(operator_type::less);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_greater):
      {
        auto ret = perform_binary_infix(operator_type::greater);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_less_equal):
      {
        auto ret = perform_binary_infix(operator_type::less_equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_greater_equal):
      {
        auto ret = perform_binary_infix(operator_type::greater_equal);
        if(!ret.has_value())
          return ret.error();
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

  value_t vm::read_constant(opcode p_op)
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

  auto vm::perform_unary_prefix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
    auto ret = m_stack.back().operator_prefix_unary(p_operator);
    if(!ret.has_value())
    {
      runtime_error("invalid operation");
      return std::unexpected(interpret_result::runtime_error);
    }
    m_stack.back() = ret.value();
    return {};
  }

  auto vm::perform_binary_infix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
    auto b = m_stack.back();
    m_stack.pop_back();
    auto a = m_stack.back();
    auto ret = a.operator_infix_binary(p_operator, b);
    if(!ret.has_value())
    {
      // TODO(Qais): check error type at least
      runtime_error("invalid operation");
      return std::unexpected(interpret_result::runtime_error);
    }
    m_stack.back() = ret.value();
    return {};
  }

  auto vm::perform_unary_infix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
  }

  void vm::runtime_error(const std::string& err)
  {
    std::println("runtime error: {}", err);
  }
} // namespace ok