#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "log.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <print>
#include <string_view>
#include <vector>

// TODO(Qais): access to stack should be like c style arrays but is a dynamic vector
//  so overload the [] operator and make a vector wrapper cuz this isnt working out fine

namespace ok
{
  // temp
  static value_t clock_native(uint8_t argc, value_t* argv);
  static value_t time_native(uint8_t argc, value_t* argv);
  static value_t srand_native(uint8_t argc, value_t* argv);
  static value_t rand_native(uint8_t argc, value_t* argv);

  vm::vm() : m_logger(log_level::paranoid), m_stack(stack_base_size)
  {
    // keep id will need that for logger (yes logger will be globally accessible and requires indirection but its mostly
    // fine because its either debug only on on error where you dont even need fast code)
    static uint32_t id = 0;
    // m_stack.reserve(stack_base_size);
    m_call_frames.reserve(call_frame_max_size);

    // m_chunk = new chunk;
    m_id = ++id;
    // those will be replaced with local ones inside vm and will pass them through calls
    // object_store::set_head(m_id, nullptr);
    // interned_strings_store::create_vm_interned(m_id);

    m_interned_strings = {};
    m_objects_list = nullptr;
  }

  vm::~vm()
  {
    // if(m_chunk)
    //   delete m_chunk;

    destroy_objects_list();
  }

  auto vm::interpret(const std::string_view p_source) -> interpret_result
  {
    m_compiler = compiler{}; // reinitialize and clear previous state
    auto compile_result = m_compiler.compile(p_source, string_object::create<string_object>("__main"), m_id);
    if(!compile_result)
    {
      if(m_compiler.get_parse_errors().errs.empty())
        return interpret_result::compile_error;
      return interpret_result::parse_error;
    }
    // m_ip = m_chunk->code.data();
    // compile_result->associated_chunk.write(opcode::op_return, 123); // TODO(Qais): remove!
    define_native_function("clock", clock_native);
    define_native_function("time", time_native);
    define_native_function("srand", srand_native);
    define_native_function("rand", rand_native);
    m_stack.push(value_t{compile_result});
    call_frame frame;
    frame.function = compile_result;
    frame.ip = compile_result->associated_chunk.code.data();
    frame.slots = 0;
    auto cfr = push_call_frame(frame);
    if(!cfr.has_value())
      return cfr.error();
    auto res = run();
    return res;
  }

  auto vm::run() -> interpret_result
  {
    auto* frame = &m_call_frames.back();
    // auto end = m_chunk->code.data() + m_chunk->code.size();
    void* endptr = frame->function->associated_chunk.code.data() + frame->function->associated_chunk.code.size();
    auto end = (byte*)endptr;
    auto& ip = frame->ip;
    while(true)
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
      debug::disassembler::disassemble_instruction(
          frame->function->associated_chunk,
          static_cast<size_t>(frame->ip - frame->function->associated_chunk.code.data()));
#endif
      // TODO(Qais): computed goto
      switch(uint8_t instruction = read_byte())
      {
      case to_utype(opcode::op_return):
      {
        auto res = m_stack.pop();
        m_stack.resize(m_call_frames.back().slots);
        pop_call_frame();
        if(m_call_frames.size() == 0)
        {
          return interpret_result::ok;
        }
        frame = &m_call_frames.back();
        m_stack.push(res);
        break;
      }
      case to_utype(opcode::op_pop):
      {
        m_stack.pop();
        break;
      }
      case to_utype(opcode::op_pop_n):
      {
        uint32_t count = read_byte();
        ASSERT(m_stack.size() > count - 1);
        // TODO(Qais): batch remove
        for(auto i = 0; i < count; ++i)
          m_stack.pop();
        break;
      }
      case to_utype(opcode::op_constant):
      case to_utype(opcode::op_constant_long):
      {
        auto val = read_constant(static_cast<opcode>(instruction));
        m_stack.push(val);
        break;
      }
      case to_utype(opcode::op_negate):
      {
        auto ret = perform_unary_prefix(operator_type::op_minus);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_add):
      {
        auto ret = perform_binary_infix(operator_type::op_plus);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_subtract):
      {
        auto ret = perform_binary_infix(operator_type::op_minus);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_multiply):
      {
        auto ret = perform_binary_infix(operator_type::op_asterisk);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_divide):
      {
        auto ret = perform_binary_infix(operator_type::op_slash);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_true):
      {
        m_stack.push(value_t{true});
        break;
      }
      case to_utype(opcode::op_false):
      {
        m_stack.push(value_t{false});
        break;
      }
      case to_utype(opcode::op_null):
      {
        m_stack.push(value_t{});
        break;
      }
      case to_utype(opcode::op_not):
      {
        m_stack.top() = value_t{is_value_falsy(m_stack.top())};
        break;
      }
      case to_utype(opcode::op_equal):
      {
        auto ret = perform_binary_infix(operator_type::op_equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_not_equal):
      {
        auto ret = perform_binary_infix(operator_type::op_bang_equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_less):
      {
        auto ret = perform_binary_infix(operator_type::op_less);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_greater):
      {
        auto ret = perform_binary_infix(operator_type::op_greater);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_less_equal):
      {
        auto ret = perform_binary_infix(operator_type::op_less_equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_greater_equal):
      {
        auto ret = perform_binary_infix(operator_type::op_greater_equal);
        if(!ret.has_value())
          return ret.error();
        break;
      }
      case to_utype(opcode::op_print):
      {
        auto back = m_stack.top();
        m_stack.pop();
        print_value(back);
        break;
      }
      case to_utype(opcode::op_define_global):
      case to_utype(opcode::op_define_global_long):
      {
        auto name = read_global_definition(static_cast<opcode>(instruction) == opcode::op_define_global_long);
        ASSERT(name.type == value_type::object_val);
        ASSERT(name.as.obj->type == object_type::obj_string);
        auto name_str = (string_object*)name.as.obj;
        auto it = m_globals.find(name_str);
        if(m_globals.end() != it)
        {
          runtime_error("redefining global"); // tf is this?
          return vm::interpret_result::runtime_error;
        }
        m_globals[name_str] = m_stack.top();
        m_stack.pop();
        break;
      }
      case to_utype(opcode::op_get_global):
      case to_utype(opcode::op_get_global_long):
      {
        auto name = read_global_definition(static_cast<opcode>(instruction) == opcode::op_get_global_long);
        ASSERT(name.type == value_type::object_val);
        ASSERT(name.as.obj->type == object_type::obj_string);
        auto name_str = (string_object*)name.as.obj;
        auto it = m_globals.find(name_str);
        if(m_globals.end() == it)
        {
          runtime_error("undefined global"); // tf is this?
          return vm::interpret_result::runtime_error;
        }
        m_stack.push(it->second);
        break;
      }
      case to_utype(opcode::op_set_global):
      case to_utype(opcode::op_set_global_long):
      {
        auto name = read_global_definition(static_cast<opcode>(instruction) == opcode::op_set_global_long);
        ASSERT(name.type == value_type::object_val);
        ASSERT(name.as.obj->type == object_type::obj_string);
        auto name_str = (string_object*)name.as.obj;
        auto it = m_globals.find(name_str);
        if(m_globals.end() == it)
        {
          runtime_error("undefined global");
          return interpret_result::runtime_error;
        }
        it->second = m_stack.top();
        break;
      }
      case to_utype(opcode::op_get_local):
      case to_utype(opcode::op_get_local_long):
      {
        const auto val = read_local(static_cast<opcode>(instruction) == opcode::op_get_local_long);
        m_stack.push(val);
        break;
      }
      case to_utype(opcode::op_set_local):
      case to_utype(opcode::op_set_local_long):
      {
        auto& val = read_local(static_cast<opcode>(instruction) == opcode::op_set_local_long);
        val = m_stack.top();
        break;
      }
      case to_utype(opcode::op_conditional_jump):
      case to_utype(opcode::op_conditional_truthy_jump):
      {
        auto jump = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        auto cond = static_cast<opcode>(instruction) == opcode::op_conditional_jump ? is_value_falsy(m_stack.top())
                                                                                    : is_value_falsy(m_stack.top());
        if(cond)
          frame->ip += jump;
        int x = jump;
        break;
      }
      case to_utype(opcode::op_jump):
      {
        auto jump = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        frame->ip += jump;
        break;
      }
      case to_utype(opcode::op_loop):
      {
        auto loop = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        frame->ip -= loop;
        break;
      }
      case to_utype(opcode::op_call):
      {
        auto argc = read_byte();
        auto peek = m_stack[m_stack.size() - argc - 1];
        auto res = call_value(argc, peek);
        if(!res.has_value())
          return res.error();
        frame = &m_call_frames.back();
      }
      }
    }
  }

  value_t& vm::read_local(bool p_is_long)
  {
    if(!p_is_long)
    {
      const uint32_t index = read_byte();
      ASSERT(index < m_stack.size());
      return m_stack[frame_stack_slots() + index];
      // return m_call_frames.back().slots[index];
    }
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    ASSERT(index < m_stack.size());
    return m_stack[frame_stack_top() + index];
    // return m_call_frames.back().slots[index];
  }

  byte vm::read_byte()
  {
    auto* frame = &m_call_frames.back();
    const auto base = frame->function->associated_chunk.code.data();
    const auto end = base + frame->function->associated_chunk.code.size();

    ASSERT(frame->ip < end); // address pointer out of pounds
    return *frame->ip++;
  }

  value_t vm::read_constant(opcode p_op)
  {
    auto* frame = &m_call_frames.back();

    // here m_ip is the first in the series of bytes
    if(p_op == opcode::op_constant)
    {
      const uint32_t index = read_byte();
      ASSERT(index < frame->function->associated_chunk.constants.size()); // constant index out of range

      return frame->function->associated_chunk.constants[index]; // here we get it then m_ip is next
    }

    // here we get 1st then m_ip is next so on.... till we get 3rd and m_ip is 4th
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    ASSERT(index < frame->function->associated_chunk.constants.size()); // constant index out of range

    return frame->function->associated_chunk.constants[index];
  }

  // i know its just a copy from the previous function, but this method is temporary so its ok-ish
  value_t vm::read_global_definition(bool p_is_long)
  {
    auto* frame = &m_call_frames.back();

    // here m_ip is the first in the series of bytes
    if(!p_is_long)
    {
      const uint32_t index = read_byte();
      ASSERT(index < frame->function->associated_chunk.identifiers.size()); // constant index out of range

      return frame->function->associated_chunk.identifiers[index]; // here we get it then m_ip is next
    }

    // here we get 1st then m_ip is next so on.... till we get 3rd and m_ip is 4th
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    ASSERT(index < frame->function->associated_chunk.identifiers.size()); // constant index out of range

    return frame->function->associated_chunk.identifiers[index];
  }

  auto vm::perform_unary_prefix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
    auto ret = perform_unary_prefix_real(p_operator, m_stack.top());
    if(!ret.has_value())
    {
      runtime_error("invalid operation"); // TODO(Qais): atleast check error type
      return std::unexpected(interpret_result::runtime_error);
    }
    m_stack.top() = ret.value();
    return {};
  }

  auto vm::perform_binary_infix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
    auto b = m_stack.top();
    m_stack.pop();
    auto a = m_stack.top();

    auto ret = perform_binary_infix_real(a, p_operator, b); // a.operator_infix_binary(p_operator, b);
    if(!ret.has_value())
    {
      // TODO(Qais): check error type at least
      runtime_error("invalid operation");
      return std::unexpected(interpret_result::runtime_error);
    }
    m_stack.top() = ret.value();
    return {};
  }

  auto vm::perform_unary_infix(const operator_type p_operator) -> std::expected<void, interpret_result>
  {
  }

  auto vm::call_value(uint8_t argc, value_t callee) -> std::expected<void, interpret_result>
  {
    TRACELN("call value: argc: {}, callee: {}", argc, (uint32_t)callee.type);
    if(callee.type != value_type::object_val)
    {
      runtime_error("bad call: callee isnt of type object");
      return std::unexpected(interpret_result::runtime_error);
    }

    auto it = m_objects_operations.find(callee.as.obj->type);
    if(it == m_objects_operations.end())
    {
      runtime_error("bad call: callee doesnt implement call");
      return std::unexpected(interpret_result::runtime_error);
    }

    update_call_frame_top_index();
    auto res = it->second.call_function(callee.as.obj, argc);
    if(!res.has_value())
    {
      runtime_error("argument mismatch");
      return std::unexpected(interpret_result::runtime_error);
    }
    else
    {
      auto opt = res.value();
      if(opt.has_value())
      {
        auto f_res = push_call_frame(opt.value());
        if(!f_res.has_value())
          return std::unexpected(interpret_result::runtime_error);
      }
    }
    return {};
  }

  std::expected<value_t, value_error>
  vm::perform_binary_infix_real(const value_t& p_this, const operator_type p_operator, const value_t& p_other)
  {
    auto key = _make_value_key(p_this.type, p_operator, p_other.type);
    switch(key)
    {
    case _make_value_key(value_type::number_val, operator_type::op_plus, value_type::number_val):
      return value_t{p_this.as.number + p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_minus, value_type::number_val):
      return value_t{p_this.as.number - p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_asterisk, value_type::number_val):
      return value_t{p_this.as.number * p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_slash, value_type::number_val):
    {
      if(p_other.as.number == 0)
        return std::unexpected(value_error::division_by_zero);
      return value_t{p_this.as.number / p_other.as.number};
    }
    case _make_value_key(value_type::number_val, operator_type::op_equal, value_type::number_val):
      return value_t{p_this.as.number == p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_bang_equal, value_type::number_val):
      return value_t{p_this.as.number != p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_less, value_type::number_val):
      return value_t{p_this.as.number < p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_greater, value_type::number_val):
      return value_t{p_this.as.number > p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_less_equal, value_type::number_val):
      return value_t{p_this.as.number <= p_other.as.number};
    case _make_value_key(value_type::number_val, operator_type::op_greater_equal, value_type::number_val):
      return value_t{p_this.as.number >= p_other.as.number};
    case _make_value_key(value_type::bool_val, operator_type::op_equal, value_type::bool_val):
      return value_t{p_this.as.boolean == p_other.as.boolean};
    case _make_value_key(value_type::bool_val, operator_type::op_bang_equal, value_type::bool_val):
      return value_t{p_this.as.boolean != p_other.as.boolean};
    case _make_value_key(value_type::null_val, operator_type::op_equal, value_type::null_val):
      return value_t{true};
    case _make_value_key(value_type::null_val, operator_type::op_bang_equal, value_type::null_val):
      return value_t{false};
    default:
    {
      if(p_this.type == value_type::object_val)
      {
        return perform_binary_infix_real_object(p_this.as.obj, p_operator, p_other);
        // try lookup the operation in the object table
      }
      if(p_operator == operator_type::op_equal)
      {
        return value_t{is_value_falsy(p_this) == is_value_falsy(p_other)};
      }
      else if(p_operator == operator_type::op_bang_equal)
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
    case _make_value_key(operator_type::op_plus, value_type::number_val):
      return p_this;
    case _make_value_key(operator_type::op_minus, value_type::number_val):
      return value_t{-p_this.as.number};
    case _make_value_key(operator_type::op_bang, value_type::bool_val):
      return value_t{!p_this.as.boolean};
    default:
      if(p_operator == operator_type::op_bang)
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
      std::print("{}", (uintptr_t)(uint8_t*)p_value.as.obj); // TODO(Qais): print as pointer
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
    for(const auto& frame : m_call_frames)
    {
      size_t instruction = frame.ip - frame.function->associated_chunk.code.data() - 1;
      ERRORLN(
          "offset: {}, in: {}", frame.function->associated_chunk.get_offset(instruction), frame.function->name->chars);
    }
  }

  // TODO(Qais): name spaces
  // it always overrides currently
  bool vm::define_native_function(std::string_view p_name, native_function p_fu)
  {
    m_stack.push(value_t{p_name});
    m_stack.push(value_t{p_fu});
    auto topmin1 = m_stack.value_ptr_top(1)->as.obj;
    auto top = m_stack.top();
    m_globals[(string_object*)topmin1] = top;
    m_stack.pop();
    m_stack.pop();
    return true;
  }

  void vm::destroy_objects_list()
  {
    while(m_objects_list != nullptr)
    {
      auto next = m_objects_list->next;
      switch(m_objects_list->type)
      {
      case object_type::obj_string:
        delete(string_object*)m_objects_list;
        break;
      case object_type::obj_function:
        delete(function_object*)m_objects_list;
        break;
      case object_type::obj_native_function:
        delete(native_function_object*)m_objects_list;
        break;
      default:
        runtime_error("trying to free an object of an unallocateing type");
        break; // TODO(Qais): error
      }
      m_objects_list = next;
    }
  }

  value_t clock_native(uint8_t argc, value_t* argv)
  {
    if(argc != 0)
    {
      return value_t{};
    }
    return value_t{(double)clock() / CLOCKS_PER_SEC};
  }

  value_t time_native(uint8_t argc, value_t* argv)
  {
    if(argc != 0)
    {
      return value_t{};
    }
    return value_t{(double)time(nullptr)};
  }

  value_t srand_native(uint8_t argc, value_t* argv)
  {
    uint32_t cseed = time(nullptr);
    if(argc > 1)
      return value_t{};
    if(argc == 1)
    {
      auto seed = argv;
      if(argv->type != value_type::number_val)
        return value_t{};
      cseed = seed->as.number;
    }
    srand(cseed);
    return value_t{};
  }

  value_t rand_native(uint8_t argc, value_t* argv)
  {
    if(argc != 0)
      return value_t{};
    return value_t{(double)rand()};
  }
} // namespace ok