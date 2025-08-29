#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
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
    // keep id will need that for logger (yes logger will be globally accessible and requires indirection but its mostly
    // fine because its either debug only on on error where you dont even need fast code)
    static uint32_t id = 0;
    m_stack.reserve(stack_base_size);
    m_chunk = new chunk;
    m_id = ++id;
    // those will be replaced with local ones inside vm and will pass them through calls
    // object_store::set_head(m_id, nullptr);
    // interned_strings_store::create_vm_interned(m_id);

    m_interned_strings = {};
    m_objects_list = nullptr;
  }

  vm::~vm()
  {
    if(m_chunk)
      delete m_chunk;

    destroy_objects_list();
  }

  auto vm::interpret(const std::string_view p_source) -> interpret_result
  {
    vm_guard guard{this};
    compiler com;
    auto compile_result = com.compile(p_source, m_chunk, m_id);
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
#ifdef PARANOID
      TRACE("    ");
      for(auto e : m_stack)
      {
        TRACE("[");
        print_value(e);
        TRACE("]");
      }
      TRACELN("");
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
        print_value(back);
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
        m_stack.back() = value_t{is_value_falsy(m_stack.back())};
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

    ASSERT(m_ip < end); // address pointer out of pounds
    return *m_ip++;
  }

  value_t vm::read_constant(opcode p_op)
  {
    // here m_ip is the first in the series of bytes
    if(p_op == opcode::op_constant)
    {
      const uint32_t index = read_byte();
      ASSERT(index < m_chunk->constants.size()); // constant index out of range

      return m_chunk->constants[index]; // here we get it then m_ip is next
    }

    // here we get 1st then m_ip is next so on.... till we get 3rd and m_ip is 4th
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    ASSERT(index < m_chunk->constants.size()); // constant index out of range

    return m_chunk->constants[index];
  }

  auto vm::perform_unary_prefix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
    auto ret = perform_unary_prefix_real(p_operator, m_stack.back());
    if(!ret.has_value())
    {
      runtime_error("invalid operation"); // TODO(Qais): atleast check error type
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

    auto ret = perform_binary_infix_real(a, p_operator, b); // a.operator_infix_binary(p_operator, b);
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

  std::expected<value_t, value_error>
  vm::perform_binary_infix_real(const value_t& p_this, const operator_type p_operator, const value_t& p_other)
  {
    auto key = _make_value_key(p_this.type, p_operator, p_other.type);
    switch(key)
    {
    case _make_value_key(value_type::number_val, operator_type::plus, value_type::number_val):
      return value_t{p_this.as.number + p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::minus, value_type::number_val):
      return value_t{p_this.as.number - p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::asterisk, value_type::number_val):
      return value_t{p_this.as.number * p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::slash, value_type::number_val):
    {
      if(p_other.as.number == 0)
        return std::unexpected(value_error::division_by_zero);
      return value_t{p_this.as.number / p_other.as.number};
    }
    case _make_value_key(value_type::number_val, operator_type::equal, value_type::number_val):
      return value_t{p_this.as.number == p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::bang_equal, value_type::number_val):
      return value_t{p_this.as.number != p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::less, value_type::number_val):
      return value_t{p_this.as.number < p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::greater, value_type::number_val):
      return value_t{p_this.as.number > p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::less_equal, value_type::number_val):
      return value_t{p_this.as.number <= p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::greater_equal, value_type::number_val):
      return value_t{p_this.as.number >= p_other.as.number};
    case _make_value_key(value_type::bool_val, operator_type::equal, value_type::bool_val):
      return value_t{p_this.as.boolean == p_other.as.boolean};
    case _make_value_key(value_type::bool_val, operator_type::bang_equal, value_type::bool_val):
      return value_t{p_this.as.boolean != p_other.as.boolean};
    case _make_value_key(value_type::null_val, operator_type::equal, value_type::null_val):
      return value_t{true};
    case _make_value_key(value_type::null_val, operator_type::bang_equal, value_type::null_val):
      return value_t{false};
    default:
    {
      if(p_this.type == value_type::object_val)
      {
        return perform_binary_infix_real_object(p_this.as.obj, p_operator, p_other);
        // try lookup the operation in the object table
      }
      if(p_operator == operator_type::equal)
      {
        return value_t{is_value_falsy(p_this) == is_value_falsy(p_other)};
      }
      else if(p_operator == operator_type::bang_equal)
      {
        return value_t{is_value_falsy(p_this) != is_value_falsy(p_other)};
      }
      return std::unexpected(value_error::undefined_operation);
    }
    }
  }

  std::expected<value_t, value_error>
  vm::perform_binary_infix_real_object(object* p_this, operator_type p_operator, value_t p_other)
  {
    auto op_it = m_objects_operations.find(p_this->type);
    if(m_objects_operations.end() == op_it)
      return std::unexpected(value_error::undefined_operation);
    auto ret = op_it->second.binary_infix.call_operation(
        _make_object_key(p_operator,
                         p_other.type,
                         p_other.type == value_type::object_val ? p_other.as.obj->type : object_type::none),
        std::move(p_this),
        std::move(p_other));
    if(!ret.has_value())
      return std::unexpected(value_error::undefined_operation);
    return ret.value();
  }

  std::expected<value_t, value_error> vm::perform_unary_prefix_real(const operator_type p_operator,
                                                                    const value_t p_this)
  {
    auto key = _make_value_key(p_operator, p_this.type);
    switch(key)
    {
      // is this ok??
    case _make_value_key(operator_type::plus, value_type::number_val):
      return p_this;
    case _make_value_key(operator_type::minus, value_type::number_val):
      return value_t{-p_this.as.number};
    case _make_value_key(operator_type::bang, value_type::bool_val):
      return value_t{!p_this.as.boolean};
    default:
      if(p_operator == operator_type::bang)
        return value_t{is_value_falsy(p_this)};
      return std::unexpected(value_error::undefined_operation);
    }
  }

  std::expected<value_t, value_error> vm::perform_unary_prefix_real_object(operator_type p_operator, value_t p_this)
  {
  }

  bool vm::is_value_falsy(value_t p_value) const
  {
    return p_value.type == value_type::null_val || (p_value.type == value_type::bool_val && !p_value.as.boolean) ||
           (p_value.type == value_type::number_val && p_value.as.number == 0);
  }

  void vm::print_value(value_t p_value)
  {
    switch(p_value.type)
    {
    case value_type::number_val:
      std::print("{}", p_value.as.number);
      break;
    case value_type::bool_val:
      std::print("{}", p_value.as.boolean ? "true" : "false");
      break;
    case value_type::null_val:
      std::print("null");
      break;
    case value_type::object_val:
      print_object(p_value.as.obj);
      break;
    default:
      std::print("{}", p_value.as.number); // TODO(Qais): print as pointer
      break;
    }
  }

  void vm::print_object(object* p_object)
  {
    auto op_it = m_objects_operations.find(p_object->type);
    if(m_objects_operations.end() == op_it)
      runtime_error("invalid print");
    op_it->second.print_function(p_object);
  }

  void vm::runtime_error(const std::string& err)
  {
    ERRORLN("runtime error: {}", err);
  }

  void vm::destroy_objects_list()
  {
    while(m_objects_list != nullptr)
    {
      auto next = m_objects_list->next;
      delete m_objects_list;
      m_objects_list = next;
    }
  }
} // namespace ok