#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "copy.hpp"
#include "debug.hpp"
#include "log.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// TODO(Qais): access to stack should be like c style arrays but is a dynamic vector
//  so overload the [] operator and make a vector wrapper cuz this isnt working out fine
// Update: its kind of done, but i dislike how the api and its useage turned out so i think it should be rewritten

#if defined(PARANOID)
#define LOG_LEVEL log_level::paranoid
#else
#define LOG_LEVEL log_level::error
#endif

namespace ok
{
  // temp
  static native_function_return_type clock_native(vm* p_vm, value_t p_this, uint8_t argc);
  static native_function_return_type time_native(vm* p_vm, value_t p_this, uint8_t argc);
  static native_function_return_type srand_native(vm* p_vm, value_t p_this, uint8_t argc);
  static native_function_return_type rand_native(vm* p_vm, value_t p_this, uint8_t argc);

  auto vm::call_native_binary_infix(native_function native, vm* p_vm, value_t p_this, uint8_t p_argc)
      -> operations_return_type
  {
    auto ret = native(this, p_this, p_argc);

    if(ret.code != value_error_code::ok)
    {
      return std::unexpected{ret.code};
    }
    return {};
    // [[likely]] if(ret.return_type == native_method_return_type::rt_operations)
    // {
    //   m_stack.pop();
    //   m_stack.pop();
    //   m_stack.push(ret.return_value.value);
    //   return {};
    // }

    // if(ret.return_type == native_method_return_type::rt_error)
    // {
    //   return std::unexpected{ret.return_value.error};
    // }
    // else [[unlikely]]
    // {
    //   return std::unexpected{value_error_code::invalid_return_type};
    // }
  }

  void vm::setup_stack_for_binary_infix_others()
  {
    auto b = m_stack.pop();
    auto a = m_stack.pop();
    m_stack.push(value_t{}); // maintain same calling convention for all call operations, TODO(Qais): no need
    m_stack.push(a);
    m_stack.push(b);
  }

  template <operator_type>
  auto vm::perform_binary_infix_others() -> operations_return_type
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_plus>() -> operations_return_type
  {
    // setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_plus];
    if(!OK_IS_VALUE_NATIVE_FUNCTION(nm))
    {
      return std::unexpected{value_error_code::undefined_operation};
    }
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_minus>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_minus];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_asterisk>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_asterisk];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_slash>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_slash];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_equal>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_equal];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_bang_equal>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_bang_equal];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_greater>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_greater];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_greater_equal>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_greater_equal];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_less>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_less];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <>
  auto vm::perform_binary_infix_others<operator_type::op_less_equal>() -> operations_return_type
  {
    setup_stack_for_binary_infix_others();
    auto& lhs = m_stack.top(1);
    auto& rhs = m_stack.top();
    const auto lhs_class = OK_VALUE_AS_OBJECT(lhs)->class_;

    auto nm = lhs_class->specials.operations[overridable_operator_type::oot_binary_infix_less_equal];
    return call_native_binary_infix(OK_VALUE_AS_NATIVE_FUNCTION(nm), this, lhs, 1);
  }

  template <operator_type>
  auto vm::perform_binary_infix() -> operations_return_type
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_plus>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) + OK_VALUE_AS_NUMBER(rhs)}});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_plus>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_minus>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) - OK_VALUE_AS_NUMBER(rhs)}});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_minus>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_asterisk>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) * OK_VALUE_AS_NUMBER(rhs)}});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_asterisk>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_slash>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      if(OK_VALUE_AS_NUMBER(rhs) == 0)
      {
        return std::unexpected{value_error_code::division_by_zero};
      }
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) / OK_VALUE_AS_NUMBER(rhs)}});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_slash>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  bool vm::perform_equality_builtins(value_t p_lhs, value_t p_rhs, bool p_equals)
  {
    if(OK_VALUE_TYPE(p_lhs) != OK_VALUE_TYPE(p_rhs))
    {
      return !p_equals;
    }
    if(OK_IS_VALUE_NUMBER(p_lhs))
    {
      return (OK_VALUE_AS_NUMBER(p_lhs) == OK_VALUE_AS_NUMBER(p_rhs)) && p_equals;
    }
    if(OK_IS_VALUE_BOOL(p_lhs))
    {
      return (OK_VALUE_AS_BOOL(p_lhs) == OK_VALUE_AS_BOOL(p_rhs)) && p_equals;
    }
    if(OK_IS_VALUE_NULL(p_lhs))
    {
      return p_equals;
    }
    if(OK_IS_VALUE_NATIVE_FUNCTION(p_lhs))
    {
      return (OK_VALUE_AS_NATIVE_FUNCTION(p_lhs) == OK_VALUE_AS_NATIVE_FUNCTION(p_rhs)) && p_equals;
    }
    return !p_equals;
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_equal>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_equal>();
    }

    auto equality = perform_equality_builtins(lhs, rhs, true);
    m_stack.pop();
    m_stack.pop();
    m_stack.push(value_t{equality});
    return {};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_bang_equal>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_bang_equal>();
    }

    auto equality = perform_equality_builtins(lhs, rhs, false);
    m_stack.pop();
    m_stack.pop();
    m_stack.push(value_t{equality});
    return {};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_greater>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) > OK_VALUE_AS_NUMBER(rhs)});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_greater>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_greater_equal>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) >= OK_VALUE_AS_NUMBER(rhs)});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_greater_equal>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_less>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) < OK_VALUE_AS_NUMBER(rhs)});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_less>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <>
  auto vm::perform_binary_infix<operator_type::op_less_equal>() -> operations_return_type
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) >= OK_VALUE_AS_NUMBER(rhs)});
      return {};
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_less_equal>();
    }
    m_stack.pop();
    m_stack.pop();
    return std::unexpected{value_error_code::undefined_operation};
  }

  template <operator_type>
  auto vm::perform_unary_prefix_others() -> operations_return_type
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  auto vm::perform_unary_prefix_others<operator_type::op_minus>() -> operations_return_type
  {
    // const auto res =
    //     m_value_operations.negate_operations.call_operation(OK_VALUE_AS_OBJECT(p_this)->get_type(),
    //     std::move(p_this));
    // if(!res.has_value())
    // {
    //   return std::unexpected{value_error::undefined_operation};
    // }
    // return res.value();
    return {};
  }

  template <>
  auto vm::perform_unary_prefix_others<operator_type::op_plus>() -> operations_return_type
  {
    // const auto res = m_value_operations.unary_plus_operations.call_operation(OK_VALUE_AS_OBJECT(p_this)->get_type(),
    //                                                                          std::move(p_this));
    // if(!res.has_value())
    // {
    //   return std::unexpected{value_error::undefined_operation};
    // }
    // return res.value();
    return {};
  }

  template <>
  auto vm::perform_unary_prefix_others<operator_type::op_bool>() -> operations_return_type
  {
    // const auto res =
    //     m_value_operations.bang_operations.call_operation(OK_VALUE_AS_OBJECT(p_this)->get_type(), std::move(p_this));
    // if(!res.has_value())
    // {
    //   return value_t{!is_value_falsy(p_this)};
    // }

    // const auto val = res.value();
    // if(val.has_value() && !OK_IS_VALUE_BOOL(val.value()))
    // {
    //   return std::unexpected{value_error::invalid_return_type};
    // }

    // return val;
    return {};
  }

  template <>
  auto vm::perform_unary_prefix<operator_type::op_bool>() -> operations_return_type
  {
    // const auto res = is_builtin_falsy(p_this);
    // if(res.has_value())
    // {
    //   return value_t{!res.value()};
    // }
    // else if(OK_IS_VALUE_OBJECT(p_this))
    // {
    //   return perform_unary_prefix_others<operator_type::op_bool>(p_this);
    // }
    // return std::unexpected{value_error::undefined_operation};
  }

  template <>
  auto vm::perform_unary_prefix_others<operator_type::op_bang>() -> operations_return_type
  {
    // const auto res =
    //     m_value_operations.bang_operations.call_operation(OK_VALUE_AS_OBJECT(p_this)->get_type(), std::move(p_this));
    // if(!res.has_value())
    // {
    //   const auto boolform = perform_unary_prefix<operator_type::op_bool>(p_this);
    //   if(boolform.has_value())
    //   {
    //     return value_t{OK_VALUE_AS_BOOL(boolform.value())};
    //   }
    //   return value_t{is_value_falsy(p_this)}; // fallback!
    // }
    // return res.value();
    return {};
  }

  template <operator_type>
  auto vm::perform_unary_prefix() -> operations_return_type
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  auto vm::perform_unary_prefix<operator_type::op_minus>() -> operations_return_type
  {
    // if(OK_IS_VALUE_NUMBER(p_this))
    // {
    //   return value_t{-OK_VALUE_AS_NUMBER(p_this)};
    // }
    // else if(OK_IS_VALUE_OBJECT(p_this))
    // {
    //   return perform_unary_prefix_others<operator_type::op_minus>(p_this);
    // }
    // return std::unexpected{value_error::undefined_operation};
  }

  template <>
  auto vm::perform_unary_prefix<operator_type::op_plus>() -> operations_return_type
  {
    // if(OK_IS_VALUE_NUMBER(p_this))
    // {
    //   return value_t{+OK_VALUE_AS_NUMBER(p_this)};
    // }
    // else if(OK_IS_VALUE_OBJECT(p_this))
    // {
    //   return perform_unary_prefix_others<operator_type::op_plus>(p_this);
    // }
    // return std::unexpected{value_error::undefined_operation};
  }

  template <>
  auto vm::perform_unary_prefix<operator_type::op_bang>() -> operations_return_type
  {
    // const auto ret = is_builtin_falsy(p_this);
    // if(ret.has_value())
    // {
    //   return value_t{ret.value()};
    // }
    // else if(OK_IS_VALUE_OBJECT(p_this))
    // {
    //   return perform_unary_prefix_others<operator_type::op_bang>(p_this);
    // }
    // return std::unexpected{value_error::undefined_operation};
  }

  vm::vm() : m_logger(LOG_LEVEL), m_stack(stack_base_size)
  {
    // keep id will need that for logger (yes logger will be globally accessible and requires indirection but its
    // mostly fine because its either debug only on on error where you dont even need fast code)
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

  // auto vm::interpret(const std::string_view p_source) -> interpret_result
  // {
  //   // m_compiler = compiler{}; // reinitialize and clear previous state
  //   // m_globals = {};

  //   // register_builtin_objects();
  //   // m_statics.init(this);

  //   // auto compile_result = m_compiler.compile(
  //   //     this,
  //   //     p_source,
  //   //     new_tobject<string_object>("main", get_builtin_class(object_type::obj_string), get_objects_list()));
  //   // if(!compile_result)
  //   // {
  //   //   if(m_compiler.get_parse_errors().errs.empty())
  //   //     return interpret_result::compile_error;
  //   //   return interpret_result::parse_error;
  //   // }

  //   // define_native_function("clock", clock_native);
  //   // define_native_function("time", time_native);
  //   // define_native_function("srand", srand_native);
  //   // define_native_function("rand", rand_native);

  //   // m_stack.push(value_t{copy{(object*)compile_result}});

  //   return reset(p_source);

  //   // auto closure =
  //   //     new_tobject<closure_object>(compile_result, get_builtin_class(object_type::obj_closure),
  //   get_objects_list());
  //   // m_stack.top() = value_t{copy{(object*)closure}};
  //   // // auto call_result = call_value(0, m_stack.top());
  //   // auto call_result = push_call_frame(call_frame{closure, closure->function->associated_chunk.code.data(), 0,
  //   0});
  //   // if(!call_result.has_value())
  //   //   return call_result.error();
  //   // auto res = run();
  //   // return res;
  // }

  void vm::init()
  {
    m_compiler = compiler{}; // reinitialize and clear previous state
    m_globals = {};

    register_builtin_objects();
    m_statics.init(this);

    define_native_function("clock", clock_native);
    define_native_function("time", time_native);
    define_native_function("srand", srand_native);
    define_native_function("rand", rand_native);
  }

  auto vm::interpret(const std::string_view p_source) -> interpret_result
  {
    m_compiler = compiler{}; // reinitialize and clear previous state
    auto compile_result = m_compiler.compile(
        this,
        p_source,
        new_tobject<string_object>("main", get_builtin_class(object_type::obj_string), get_objects_list()));
    if(!compile_result)
    {
      if(m_compiler.get_parse_errors().errs.empty())
        return interpret_result::compile_error;
      return interpret_result::parse_error;
    }
    stack_resize(0);
    m_stack.push(value_t{copy{(object*)compile_result}});
    auto closure =
        new_tobject<closure_object>(compile_result, get_builtin_class(object_type::obj_closure), get_objects_list());
    m_stack.top() = value_t{copy{(object*)closure}};
    // auto call_result = call_value(0, m_stack.top());
    auto call_result = push_call_frame(call_frame{closure, closure->function->associated_chunk.code.data(), 0, 0});
    if(!call_result.has_value())
      return call_result.error();
    auto res = run();
#if !defined(OK_NOT_GARBAGE_COLLECTED)
    m_gc.collect();
#endif
    return res;
  }

  bool vm::register_builtin_objects()
  {
    object_register_builtins(this);
    return true;
  }

  // bool vm::register_builtin_objects()
  // {
  //   // string
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_string, object_type::obj_string),
  //       string_object::equal);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_string, object_type::obj_string),
  //       string_object::bang_equal);
  //   register_value_operation_binary_infix<operator_type::op_plus,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_string, object_type::obj_string),
  //       string_object::plus);
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_string,
  //                                                                                      string_object::print);

  //   // functions
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_function, object_type::obj_function),
  //       function_object::equal);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_function, object_type::obj_function),
  //       function_object::bang_equal);
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_function,
  //                                                                                      function_object::print);

  //   // native functions
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_native_function,
  //                                             object_type::obj_native_function),
  //       native_function_object::equal);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_native_function,
  //                                             object_type::obj_native_function),
  //       native_function_object::bang_equal);
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_native_function,
  //           object_type::obj_closure),
  //       native_function_object::equal_closure);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_native_function,
  //           object_type::obj_closure),
  //       native_function_object::bang_equal_closure);
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_native_function,
  //                                             object_type::obj_bound_method),
  //       native_function_object::equal_bound_method);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_native_function,
  //                                             object_type::obj_bound_method),
  //       native_function_object::bang_equal_bound_method);
  //   register_value_operation_call(object_type::obj_native_function, native_function_object::call);
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_native_function,
  //                                                                                      native_function_object::print);

  //   // closure
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_closure, object_type::obj_closure),
  //       closure_object::equal);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_closure, object_type::obj_closure),
  //       closure_object::bang_equal);
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_closure,
  //           object_type::obj_native_function),
  //       closure_object::equal_native_function);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_closure,
  //           object_type::obj_native_function),
  //       closure_object::bang_equal_native_function);
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_closure,
  //           object_type::obj_bound_method),
  //       closure_object::equal_bound_method);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_closure,
  //           object_type::obj_bound_method),
  //       closure_object::bang_equal_bound_method);
  //   register_value_operation_call(object_type::obj_closure, closure_object::call);
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_closure,
  //                                                                                      closure_object::print);

  //   // upvalue
  //   register_value_operation_print(object_type::obj_upvalue, upvalue_object::print);

  //   // class
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_class, object_type::obj_class),
  //       class_object::equal);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_class, object_type::obj_class),
  //       class_object::bang_equal);
  //   register_value_operation_call(object_type::obj_class, class_object::call);
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_class,
  //                                                                                      class_object::print);

  //   // instance
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_instance,
  //                                                                                      instance_object::print);

  //   // bound method
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_bound_method,
  //                                             object_type::obj_bound_method),
  //       bound_method_object::equal);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_bound_method,
  //                                             object_type::obj_bound_method),
  //       bound_method_object::bang_equal);
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_bound_method,
  //                                             object_type::obj_native_function),
  //       bound_method_object::equal_native_function);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(value_type::object_val,
  //                                             value_type::object_val,
  //                                             object_type::obj_bound_method,
  //                                             object_type::obj_native_function),
  //       bound_method_object::bang_equal_native_function);
  //   register_value_operation_binary_infix<operator_type::op_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_bound_method,
  //           object_type::obj_closure),
  //       bound_method_object::equal_closure);
  //   register_value_operation_binary_infix<operator_type::op_bang_equal,
  //                                         value_operations_infix_binary::collision_policy_overwrite>(
  //       combine_value_types_with_object_types(
  //           value_type::object_val, value_type::object_val, object_type::obj_bound_method,
  //           object_type::obj_closure),
  //       bound_method_object::bang_equal_closure);
  //   register_value_operation_call(object_type::obj_bound_method, bound_method_object::call);
  //   register_value_operation_print<value_operations_print::collision_policy_overwrite>(object_type::obj_bound_method,
  //                                                                                      bound_method_object::print);
  //   return true;
  // }

  auto vm::run() -> interpret_result
  {
    auto* frame = &m_call_frames.back();
    // auto end = m_chunk->code.data() + m_chunk->code.size();
    void* endptr =
        frame->closure->function->associated_chunk.code.data() + frame->closure->function->associated_chunk.code.size();
    auto end = (byte*)endptr;
    auto& ip = frame->ip;
    while(true)
    {
#ifdef PARANOID
      TRACE("  [stack view]:  [");
      for(auto e : m_stack)
      {
        TRACE("[");
        print_value(e);
        TRACE("]");
      }
      TRACELN("]");
      // FIXME(Qais): offset isnt being calculated properly, Update: i think its fixed, keeping this if it broke
      debug::disassembler::disassemble_instruction(
          frame->closure->function->associated_chunk,
          static_cast<size_t>(frame->ip - frame->closure->function->associated_chunk.code.data()));
#endif
      // TODO(Qais): computed goto
      switch(uint8_t instruction = read_byte())
      {
      case to_utype(opcode::op_return):
      {
        auto res = m_stack.pop();
        close_upvalue(m_stack.value_ptr(m_call_frames.back().slots));
        m_stack.resize(m_call_frames.back().slots);
        pop_call_frame();
        if(m_call_frames.size() == 0)
        {
#ifdef PARANOID // one last dance
          TRACE("  [stack view]:  [");
          for(auto e : m_stack)
          {
            TRACE("[");
            print_value(e);
            TRACE("]");
          }
          TRACELN("]");
#endif
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
        auto ret = perform_unary_prefix<operator_type::op_minus>();
        if(!ret.has_value())
        {
          runtime_error("bad -");
          return interpret_result::runtime_error;
        }
        // m_stack.pop();
        //  m_stack.push(ret.value());
        break;
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_add):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        //  m_stack.pop();
        //  m_stack.pop();
        auto ret = perform_binary_infix<operator_type::op_plus>();
        if(!ret.has_value())
        {
          runtime_error("bad +"); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back(); // always update the cached frame because any operation might change it
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_plus);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_subtract):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_minus>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad -"); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_minus);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_multiply):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_asterisk>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad *"); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_asterisk);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_divide):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_slash>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad /"); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_slash);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
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
        // auto back = m_stack.top();
        auto ret = perform_unary_prefix<operator_type::op_bang>();
        if(!ret.has_value())
        {
          runtime_error("bad !");
          return interpret_result::runtime_error;
        }
        // m_stack.pop();
        // m_stack.push(ret.value());
        break;
        // m_stack.top() = value_t{is_value_falsy(m_stack.top())};
        // break;
      }
      case to_utype(opcode::op_equal):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_equal>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad =="); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix<operator_type::op_equal>();
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_not_equal):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_bang_equal>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad !="); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_bang_equal);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_greater):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_greater>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad >"); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_greater);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_greater_equal):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_greater_equal>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad >="); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_greater_equal);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_less):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_less>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad <"); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_less);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_less_equal):
      {
        // auto b = m_stack.top();
        // auto a = m_stack.top(1);
        auto ret = perform_binary_infix<operator_type::op_less_equal>();
        // m_stack.pop();
        // m_stack.pop();
        if(!ret.has_value())
        {
          runtime_error("bad <="); // TODO(Qais): fix stupid runtime errors
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        // m_stack.push(ret.value());
        break;
        // auto ret = perform_binary_infix(operator_type::op_less_equal);
        // if(!ret.has_value())
        //   return ret.error();
        // break;
      }
      case to_utype(opcode::op_print):
      {
        // auto back = m_stack.top();
        auto ret = perform_print(m_stack.top());
        if(!ret.has_value())
        {
          runtime_error("bad print");
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        m_stack.pop();
        // print_value(back);
        break;
      }
      case to_utype(opcode::op_define_global):
      case to_utype(opcode::op_define_global_long):
      {
        auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_define_global_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);
        auto flags = static_cast<variable_declaration_flags>(read_byte());
        auto it = m_globals.find(name_str);
        if(m_globals.end() != it)
        {
          runtime_error("redefining global"); // tf is this?
          return vm::interpret_result::runtime_error;
        }
        m_globals[name_str] = {m_stack.top(), flags};
        m_stack.pop();
        break;
      }
      case to_utype(opcode::op_get_global):
      case to_utype(opcode::op_get_global_long):
      {
        auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_get_global_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);

        auto it = m_globals.find(name_str);
        if(m_globals.end() == it)
        {
          runtime_error("undefined global"); // tf is this?
          return vm::interpret_result::runtime_error;
        }
        m_stack.push(it->second.global);
        break;
      }
      case to_utype(opcode::op_set_global):
      case to_utype(opcode::op_set_global_long):
      {
        auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_set_global_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);

        auto it = m_globals.find(name_str);
        if(m_globals.end() == it)
        {
          runtime_error("undefined global");
          return interpret_result::runtime_error;
        }
        if((it->second.flags & variable_declaration_flags::vdf_mutable) == variable_declaration_flags::vdf_none)
        {
          runtime_error("attempting to mutate an immutable global variable. did you forget to declare it 'mut'?");
          return interpret_result::runtime_error;
        }
        it->second.global = m_stack.top();
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
        ASSERT(false); // TODO(Qais): fix once conversions are in place
        // const auto jump = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        // const auto res = perform_unary_prefix<operator_type::op_bool>(
        //     m_stack.pop()); // always take bool and bang it if necessary, because ! operation does not guarantee
        //                     // boolean return type, while bool operation always guarantee boolean return type
        // if(!res.has_value())
        // {
        //   runtime_error("invalid boolean conversion");
        //   return interpret_result::runtime_error;
        // }
        // const auto cond = static_cast<opcode>(instruction) == opcode::op_conditional_jump
        //                       ? !OK_VALUE_AS_BOOL(res.value())
        //                       : OK_VALUE_AS_BOOL(res.value());
        // frame->ip += jump * cond;
        break;
      }
      case to_utype(opcode::op_conditional_jump_leave):
      case to_utype(opcode::op_conditional_truthy_jump_leave):
      {
        // TODO(Qais): same as above
        //  const auto jump = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        //  const auto res = perform_unary_prefix<operator_type::op_bool>(
        //      m_stack.top()); // always take bool and bang it if necessary, because ! operation does not guarantee
        //                      // boolean return type, while bool operation always guarantee boolean return type
        //  if(!res.has_value())
        //  {
        //    runtime_error("invalid boolean conversion");
        //    return interpret_result::runtime_error;
        //  }
        //  const auto cond = static_cast<opcode>(instruction) == opcode::op_conditional_jump_leave
        //                        ? !OK_VALUE_AS_BOOL(res.value())
        //                        : OK_VALUE_AS_BOOL(res.value());
        //  if(cond)
        //  {
        //    frame->ip += jump; //* cond;
        //  }
        //  else
        //  {
        //    m_stack.pop();
        //  }
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
        auto peek = m_stack.top(argc);
        auto res = call_value(argc, peek);
        if(!res)
          return interpret_result::runtime_error;
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_closure):
      {
        auto const_inst = *frame->ip++;
        auto function = read_constant((opcode)const_inst);
        auto closure = closure_object::create<closure_object>(
            OK_VALUE_AS_FUNCTION_OBJECT(function), get_builtin_class(object_type::obj_closure), get_objects_list());
        auto fu = OK_VALUE_AS_FUNCTION_OBJECT(function);
        auto val = value_t{copy{(object*)closure}};
        m_stack.push(val);
        for(uint32_t i = 0; i < closure->function->upvalues; ++i)
        {
          uint8_t is_local = read_byte();
          uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
          if(is_local)
            closure->upvalues[i] = capture_value(frame->slots + index);
          else
            closure->upvalues[i] = frame->closure->upvalues[index];
        }
        break;
      }
      case to_utype(opcode::op_get_upvalue):
      {
        auto slot = read_byte();
        m_stack.push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case to_utype(opcode::op_get_upvalue_long):
      {
        auto slot = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        m_stack.push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case to_utype(opcode::op_set_up_value):
      {
        auto slot = read_byte();
        *frame->closure->upvalues[slot]->location = m_stack.top();
        break;
      }
      case to_utype(opcode::op_set_upvalue_long):
      {
        auto slot = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        *frame->closure->upvalues[slot]->location = m_stack.top();
        break;
      }
      case to_utype(opcode::op_close_upvalue):
      {
        close_upvalue(m_stack.value_ptr_top());
        // dont pop because it will be handled later by op_pop_n
        break;
      }
      case to_utype(opcode::op_class):
      case to_utype(opcode::op_class_long):
      {
        auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_class_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);
        auto id = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        auto cls =
            new_object<class_object>(name_str, id, get_builtin_class(object_type::obj_class), get_objects_list());
        m_stack.push(value_t{copy{cls}});
        break;
      }
      case to_utype(opcode::op_get_property):
      case to_utype(opcode::op_get_property_long):
      {
        // TODO(Qais): now it assumes instance objects only, but you should expand it to support dot access on built
        // in types via a table in the vm
        if(OK_IS_VALUE_OBJECT(m_stack.top()) && !OK_VALUE_AS_OBJECT(m_stack.top())->is_instance())
        {
          runtime_error("only instances can have properties");
          return interpret_result::runtime_error;
        }
        const auto instance = OK_VALUE_AS_INSTANCE_OBJECT(m_stack.top());
        const auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_get_property_long);
        const auto name_str = OK_VALUE_AS_STRING_OBJECT(name);
        const auto it = instance->fields.find(name_str);
        if(instance->fields.end() != it)
        {
          m_stack.top() = it->second;
          break;
        }
        if(!bind_a_method(instance->up.class_, name_str))
        {
          return interpret_result::runtime_error;
        }
        break;
      }
      case to_utype(opcode::op_set_property):
      case to_utype(opcode::op_set_property_long):
      {
        // TODO(Qais): now it assumes instance objects only, but you should expand it to support dot access on built
        // in types via a table in the vm
        auto obj = OK_VALUE_AS_OBJECT(m_stack.top(1));
        if(OK_IS_VALUE_OBJECT(m_stack.top(1)) && !OK_VALUE_AS_OBJECT(m_stack.top(1))->is_instance())
        {
          runtime_error("only instances can have properties");
          return interpret_result::runtime_error;
        }
        const auto instance = OK_VALUE_AS_INSTANCE_OBJECT(m_stack.top(1));
        const auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_get_property_long);
        const auto name_str = OK_VALUE_AS_STRING_OBJECT(name);
        instance->fields[name_str] = m_stack.top();
        const auto v = m_stack.pop();
        m_stack.pop();
        m_stack.push(v);
        break;
      }
      case to_utype(opcode::op_method):
      case to_utype(opcode::op_method_long):
      {
        const auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_method_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);

        const auto argc = read_byte();
        // TODO(Qais): emit that in compiler
        define_method(name_str, argc);
        break;
      }
      case to_utype(opcode::op_invoke):
      case to_utype(opcode::op_invoke_long):
      {
        const auto method = read_identifier(static_cast<opcode>(instruction) == opcode::op_invoke_long);
        const auto argc = read_byte();
        if(!invoke(OK_VALUE_AS_STRING_OBJECT(method), argc))
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_inherit):
      {
        auto super = m_stack.top(1);
        auto sub = m_stack.top();

        if(super.type != value_type::object_val || OK_VALUE_AS_OBJECT(super)->get_type() != object_type::obj_class)
        {
          runtime_error("superclass must be a class");
          return interpret_result::runtime_error;
        }

        auto super_class = OK_VALUE_AS_CLASS_OBJECT(super);
        auto sub_class = OK_VALUE_AS_CLASS_OBJECT(sub);

        object_inherit(super_class, sub_class);
        m_stack.pop();

        break;
      }
      case to_utype(opcode::op_get_super):
      case to_utype(opcode::op_get_super_long):
      {
        auto method_name =
            OK_VALUE_AS_STRING_OBJECT(read_identifier(static_cast<opcode>(instruction) == opcode::op_get_super_long));
        auto superclass = OK_VALUE_AS_CLASS_OBJECT(m_stack.pop());
        m_gc.guard_value(value_t{copy{(object*)superclass}});
        if(!bind_a_method(superclass, method_name))
        {
          m_gc.letgo_value();
          return interpret_result::runtime_error;
        }
        m_gc.letgo_value();
        break;
      }
      case to_utype(opcode::op_invoke_super):
      case to_utype(opcode::op_invoke_super_long):
      {
        auto method_name = OK_VALUE_AS_STRING_OBJECT(
            read_identifier(static_cast<opcode>(instruction) == opcode::op_invoke_super_long));
        auto argc = read_byte();
        auto superclass = OK_VALUE_AS_CLASS_OBJECT(m_stack.pop());
        if(!invoke_from_class(superclass, method_name, argc))
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
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
    const auto base = frame->closure->function->associated_chunk.code.data();
    const auto end = base + frame->closure->function->associated_chunk.code.size();

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
      ASSERT(index < frame->closure->function->associated_chunk.constants.size()); // constant index out of range

      return frame->closure->function->associated_chunk.constants[index]; // here we get it then m_ip is next
    }

    // here we get 1st then m_ip is next so on.... till we get 3rd and m_ip is 4th
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    ASSERT(index < frame->closure->function->associated_chunk.constants.size()); // constant index out of range

    return frame->closure->function->associated_chunk.constants[index];
  }

  value_t vm::read_identifier(bool p_is_long)
  {
    auto* frame = &m_call_frames.back();

    // here m_ip is the first in the series of bytes
    if(!p_is_long)
    {
      const uint32_t index = read_byte();
      ASSERT(index < frame->closure->function->associated_chunk.identifiers.size()); // constant index out of range

      return frame->closure->function->associated_chunk.identifiers[index]; // here we get it then m_ip is next
    }

    // here we get 1st then m_ip is next so on.... till we get 3rd and m_ip is 4th
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    ASSERT(index < frame->closure->function->associated_chunk.identifiers.size()); // constant index out of range

    return frame->closure->function->associated_chunk.identifiers[index];
  }

  // auto vm::perform_unary_prefix(const operator_type p_operator) -> std::expected<void, interpret_result>
  // {
  //   auto ret = perform_unary_prefix_real(p_operator, m_stack.top());
  //   if(!ret.has_value())
  //   {
  //     runtime_error("invalid operation"); // TODO(Qais): atleast check error type

  //     m_stack.pop(); // remove the orphan value, since we wont be overwriting it
  //     return std::unexpected(interpret_result::runtime_error);
  //   }
  //   m_stack.top() = ret.value();
  //   return {};
  // }

  // auto vm::perform_binary_infix(const operator_type p_operator) -> std::expected<void, interpret_result>
  // {
  //   auto b = m_stack.top();
  //   auto a = m_stack.top(1);

  //   auto ret = perform_binary_infix_real(a, p_operator, b); // a.operator_infix_binary(p_operator, b);
  //   m_stack.pop();
  //   if(!ret.has_value())
  //   {
  //     // TODO(Qais): check error type at least
  //     runtime_error("invalid operation");
  //     m_stack.pop(); // remove the orphan value, since we wont be overwriting it
  //     return std::unexpected(interpret_result::runtime_error);
  //   }
  //   m_stack.top() = ret.value();
  //   return {};
  // }

  // auto vm::perform_unary_infix(const operator_type p_operator) -> std::expected<void, interpret_result>
  // {
  // }

  // auto vm::call_value(uint8_t argc, value_t callee) -> bool
  // {
  //   TRACELN("call value: argc: {}, callee: {}", argc, (uint32_t)callee.type);
  //   if(callee.type != value_type::object_val)
  //   {
  //     runtime_error("bad call: callee isn't of type object");
  //     return false;
  //   }

  //   // auto it = m_objects_operations.find(callee.as.obj->get_type());
  //   // if(it == m_objects_operations.end())
  //   {
  //     runtime_error("bad call: callee doesn't implement call");
  //     return false;
  //   }

  //   update_call_frame_top_index();
  //   // auto res = it->second.call_function(callee.as.obj, argc);
  //   // if(!res.has_value())
  //   {
  //     // if(res.error() != value_error::internal_propagated_error)
  //     {
  //       // runtime_error("" + std::to_string(to_utype(res.error())));
  //     }
  //     return false;
  //   }
  //   // else
  //   {
  //     // auto opt = res.value();
  //     // if(opt.has_value())
  //     {
  //       // auto f_res = push_call_frame(opt.value());
  //       // return f_res.has_value();
  //     }
  //   }
  //   return true;
  // }

  auto vm::call_value(uint8_t p_argc, value_t p_callee) -> bool
  {
    // auto callee = m_stack[m_stack.size() - p_argc - 1];
    TRACELN("call value: argc: {}, callee: {}", p_argc, (uint32_t)p_callee.type);
    if(!OK_IS_VALUE_NATIVE_FUNCTION(p_callee) && !OK_IS_VALUE_OBJECT(p_callee))
    {
      runtime_error("bad call: callee isn't of type native function or of type object");
      return false;
    }

    update_call_frame_top_index();
    const auto res = perform_call(p_argc, p_callee);

    // if(!res.has_value())
    // {
    //   runtime_error("bad call");
    //   return false;
    // }

    if(res.code != value_error_code::ok) [[unlikely]]
    {
      if(res.code != value_error_code::internal_propagated_error)
      {
        runtime_error("" + std::to_string(to_utype(res.code)));
      }
      return false;
    }
    // auto opt = res.value();
    // if(opt.has_value())
    //{
    //   auto f_res = push_call_frame(opt.value());
    //   return f_res.has_value();
    return true;
  }

  value_error vm::perform_call(uint8_t p_argc, value_t p_callee)
  {
    // this function shall not be called before checking the value_type
    if(OK_IS_VALUE_NATIVE_FUNCTION(p_callee))
    {
      return call_native_function(p_callee, p_argc);
    }
    else if(OK_IS_VALUE_OBJECT(p_callee))
    {

      auto obj = OK_VALUE_AS_OBJECT(p_callee);
      const auto& call_ops = obj->class_->specials.operations[overridable_operator_type::oot_unary_postfix_call];
      const auto callfcn = call_ops;
      if(OK_IS_VALUE_NULL(callfcn))
      {
        return {.code = value_error_code::undefined_operation, .has_payload = false};
      }

      // TODO(Qais): we dont need value_t wrapper, just a function pointer will do!
      return call_native_method(callfcn, p_callee, p_argc);
    }
    else
    {
      return {.code = value_error_code::undefined_operation, .has_payload = false};
    }
  }

  value_error vm::call_native_function(value_t p_native, uint8_t p_argc)
  {
    ASSERT(OK_IS_VALUE_NATIVE_FUNCTION(p_native));

    auto res = OK_VALUE_AS_NATIVE_FUNCTION(p_native)(this, p_native, p_argc);
    return res;

    return {.code = value_error_code::unknown_type, .has_payload = false};
  }

  value_error vm::call_native_method(value_t p_native, value_t p_receiver, uint8_t p_argc)
  {
    ASSERT(OK_IS_VALUE_NATIVE_FUNCTION(p_native));

    auto res = OK_VALUE_AS_NATIVE_FUNCTION(p_native)(this, p_receiver, p_argc);
    return res;

    return {.code = value_error_code::unknown_type, .has_payload = false};
  }

  bool vm::call(uint8_t p_argc, closure_object* p_callee)
  {
  }

  bool vm::invoke(string_object* p_method_name, uint8_t p_argc)
  {
    auto receiver = m_stack.top(p_argc);
    if(!OK_IS_VALUE_OBJECT(receiver))
    {
      runtime_error("can't invoke method on non-class types");
      return false;
    }

    auto obj = OK_VALUE_AS_OBJECT(receiver);
    if(obj->is_instance())
    {
      auto instance = OK_VALUE_AS_INSTANCE_OBJECT(receiver);
      const auto it = instance->fields.find(p_method_name);
      if(instance->fields.end() != it)
      {
        m_stack.top(p_argc) = it->second;
        return call_value(p_argc, it->second);
      }
    }
    return invoke_from_class(obj->class_, p_method_name, p_argc);
  }

  bool vm::invoke_from_class(class_object* p_class, string_object* p_method_name, uint8_t p_argc)
  {
    auto it = p_class->methods.find(p_method_name);
    if(p_class->methods.end() == it)
    {
      runtime_error("undefined method: "); // TODO(Qais): runtime error is shit
      return false;
    }
    // m_stack.top(p_argc) = it->second;
    return call_value(p_argc, it->second);
  }

  upvalue_object* vm::capture_value(size_t p_slot)
  {
    auto slot_ptr = m_stack.value_ptr(p_slot);
    upvalue_object* pre = nullptr;
    upvalue_object* upval = m_open_upvalues;
    while(upval != nullptr && upval->location > slot_ptr)
    {
      pre = upval;
      upval = upval->next;
    }
    if(upval != nullptr && upval->location == slot_ptr)
      return upval;
    auto* new_upval =
        new_tobject<upvalue_object>(slot_ptr, get_builtin_class(object_type::obj_upvalue), get_objects_list());
    new_upval->next = upval;
    if(pre == nullptr)
      m_open_upvalues = new_upval;
    else
      pre->next = new_upval;
    return new_upval;
  }

  void vm::close_upvalue(value_t* p_value)
  {
    while(m_open_upvalues != nullptr && m_open_upvalues->location >= p_value)
    {
      auto* upval = m_open_upvalues;
      upval->closed = *upval->location;
      upval->location = &upval->closed;
      m_open_upvalues = upval->next;
    }
  }

  void vm::define_method(string_object* p_name, uint8_t p_arity)
  {
    auto method = m_stack.top();
    auto class_ = OK_VALUE_AS_CLASS_OBJECT(m_stack.top(1));
    class_->methods[p_name] = method;
    auto meth_m = OK_VALUE_AS_CLOSURE_OBJECT(method);
    m_stack.pop();
  }

  bool vm::bind_a_method(class_object* p_class, string_object* p_name)
  {
    auto it = p_class->methods.find(p_name);
    if(p_class->methods.end() == it)
    {
      runtime_error("undefined property: " + std::string{std::string_view{p_name->chars, p_name->length}});
      return false;
    }

    // bound_method_object::methods_store s;
    // for(const auto m : p_class->methods)
    //{
    //   s.set(m.first.arity(), m.second);
    // }

    auto bound = new_object<bound_method_object>(
        m_stack.top(), it->second, get_builtin_class(object_type::obj_bound_method), get_objects_list());
    m_stack.top() = value_t{copy{bound}};
    return true;
  }

  // std::expected<value_t, value_error>
  // vm::perform_binary_infix_real(const value_t& p_this, const operator_type p_operator, const value_t& p_other)
  // {
  //   auto key = _make_value_key(p_this.type, p_operator, p_other.type);
  //   switch(key)
  //   {
  //   case _make_value_key(value_type::number_val, operator_type::op_plus, value_type::number_val):
  //     return value_t{p_this.as.number + p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_minus, value_type::number_val):
  //     return value_t{p_this.as.number - p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_asterisk, value_type::number_val):
  //     return value_t{p_this.as.number * p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_slash, value_type::number_val):
  //   {
  //     if(p_other.as.number == 0)
  //       return std::unexpected(value_error::division_by_zero);
  //     return value_t{p_this.as.number / p_other.as.number};
  //   }
  //   case _make_value_key(value_type::number_val, operator_type::op_equal, value_type::number_val):
  //     return value_t{p_this.as.number == p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_bang_equal, value_type::number_val):
  //     return value_t{p_this.as.number != p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_less, value_type::number_val):
  //     return value_t{p_this.as.number < p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_greater, value_type::number_val):
  //     return value_t{p_this.as.number > p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_less_equal, value_type::number_val):
  //     return value_t{p_this.as.number <= p_other.as.number};
  //   case _make_value_key(value_type::number_val, operator_type::op_greater_equal, value_type::number_val):
  //     return value_t{p_this.as.number >= p_other.as.number};
  //   case _make_value_key(value_type::bool_val, operator_type::op_equal, value_type::bool_val):
  //     return value_t{p_this.as.boolean == p_other.as.boolean};
  //   case _make_value_key(value_type::bool_val, operator_type::op_bang_equal, value_type::bool_val):
  //     return value_t{p_this.as.boolean != p_other.as.boolean};
  //   case _make_value_key(value_type::null_val, operator_type::op_equal, value_type::null_val):
  //     return value_t{true};
  //   case _make_value_key(value_type::null_val, operator_type::op_bang_equal, value_type::null_val):
  //     return value_t{false};
  //   default:
  //   {
  //     if(p_this.type == value_type::object_val)
  //     {
  //       return perform_binary_infix_real_object(p_this.as.obj, p_operator, p_other);
  //       // try lookup the operation in the object table
  //     }
  //     if(p_operator == operator_type::op_equal)
  //     {
  //       return value_t{is_value_falsy(p_this) == is_value_falsy(p_other)};
  //     }
  //     else if(p_operator == operator_type::op_bang_equal)
  //     {
  //       return value_t{is_value_falsy(p_this) != is_value_falsy(p_other)};
  //     }
  //     return std::unexpected(value_error::undefined_operation);
  //   }
  //   }
  // }

  // std::expected<value_t, value_error>
  // vm::perform_binary_infix_real_object(object* p_this, operator_type p_operator, value_t p_other)
  // {
  //   auto op_it = m_objects_operations.find(p_this->get_type());
  //   if(m_objects_operations.end() == op_it)
  //     return std::unexpected(value_error::undefined_operation);
  //   auto ret = op_it->second.binary_infix.call_operation(
  //       _make_object_key(p_operator,
  //                        p_other.type,
  //                        p_other.type == value_type::object_val ? p_other.as.obj->get_type() : object_type::none),
  //       std::move(p_this),
  //       std::move(p_other));
  //   if(!ret.has_value())
  //     return std::unexpected(value_error::undefined_operation);
  //   return ret.value();
  // }

  // std::expected<value_t, value_error> vm::perform_unary_prefix_real(const operator_type p_operator,
  //                                                                   const value_t p_this)
  // {
  //   auto key = _make_value_key(p_operator, p_this.type);
  //   switch(key)
  //   {
  //     // is this ok??
  //   case _make_value_key(operator_type::op_plus, value_type::number_val):
  //     return p_this;
  //   case _make_value_key(operator_type::op_minus, value_type::number_val):
  //     return value_t{-p_this.as.number};
  //   case _make_value_key(operator_type::op_bang, value_type::bool_val):
  //     return value_t{!p_this.as.boolean};
  //   default:
  //     if(p_operator == operator_type::op_bang)
  //       return value_t{is_value_falsy(p_this)};
  //     return std::unexpected(value_error::undefined_operation);
  //   }
  // }

  // std::expected<value_t, value_error> vm::perform_unary_prefix_real_object(operator_type p_operator, value_t
  // p_this)
  // {
  // }

  // std::expected<value_t, value_error> vm::perform_add(value_t p_lhs, value_t p_rhs)
  // {
  //   if(OK_IS_VALUE_NUMBER(p_lhs) && OK_IS_VALUE_NUMBER(p_rhs))
  //   {
  //     return value_t{p_lhs.as.number + p_lhs.as.number};
  //   }
  //   else if(OK_IS_VALUE_OBJECT(p_lhs) || OK_IS_VALUE_OBJECT(p_rhs))
  //   {
  //     return perform_add_others(p_lhs, p_rhs);
  //   }
  //   return std::unexpected{value_error::undefined_operation};
  // }

  // std::expected<value_t, value_error> vm::perform_add_others(value_t p_lhs, value_t p_rhs)
  // {
  //   if(OK_IS_VALUE_STRING_OBJECT(p_lhs) && OK_IS_VALUE_STRING_OBJECT(p_rhs))
  //   {
  //     return string_object::plus(OK_VALUE_AS_OBJECT(p_lhs), p_rhs);
  //   }

  //   auto ret = m_value_operations.add_operations.call_operation(
  //       combine_value_type_with_object_type(p_lhs, p_rhs), std::move(p_lhs), std::move(p_rhs));
  //   if(!ret->has_value())
  //   {
  //     return std::unexpected{value_error::undefined_operation};
  //   }
  //   return ret->value();
  // }

  // std::expected<value_t, value_error> vm::perform_subtract(value_t p_lhs, value_t p_rhs)
  // {
  //   if(OK_IS_VALUE_NUMBER(p_lhs) && OK_IS_VALUE_NUMBER(p_rhs))
  //   {
  //     return value_t{p_lhs.as.number - p_lhs.as.number};
  //   }
  //   else if(OK_IS_VALUE_OBJECT(p_lhs) || OK_IS_VALUE_OBJECT(p_rhs))
  //   {
  //     return perform_add_others(p_lhs, p_rhs);
  //   }
  //   return std::unexpected{value_error::undefined_operation};
  // }

  // std::expected<value_t, value_error> vm::perform_subtract_others(value_t p_lhs, value_t p_rhs)
  // {
  //   auto ret = m_value_operations.subtract_operations.call_operation(
  //       combine_value_type_with_object_type(p_lhs, p_rhs), std::move(p_lhs), std::move(p_rhs));
  //   if(!ret->has_value())
  //   {
  //     return std::unexpected{value_error::undefined_operation};
  //   }
  //   return ret->value();
  // }

  // std::expected<value_t, value_error> vm::perform_multiply(value_t p_lhs, value_t p_rhs)
  // {
  //   if(OK_IS_VALUE_NUMBER(p_lhs) && OK_IS_VALUE_NUMBER(p_rhs))
  //   {
  //     return value_t{p_lhs.as.number * p_lhs.as.number};
  //   }
  //   else if(OK_IS_VALUE_OBJECT(p_lhs) || OK_IS_VALUE_OBJECT(p_rhs))
  //   {
  //     return perform_add_others(p_lhs, p_rhs);
  //   }
  //   return std::unexpected{value_error::undefined_operation};
  // }

  // std::expected<value_t, value_error> vm::perform_multiply_others(value_t p_lhs, value_t p_rhs)
  // {
  //   auto ret = m_value_operations.multiply_operations.call_operation(
  //       combine_value_type_with_object_type(p_lhs, p_rhs), std::move(p_lhs), std::move(p_rhs));
  //   if(!ret->has_value())
  //   {
  //     return std::unexpected{value_error::undefined_operation};
  //   }
  //   return ret->value();
  // }

  // std::expected<value_t, value_error> vm::perform_divide(value_t p_lhs, value_t p_rhs)
  // {
  //   if(OK_IS_VALUE_NUMBER(p_lhs) && OK_IS_VALUE_NUMBER(p_rhs))
  //   {
  //     return value_t{p_lhs.as.number / p_lhs.as.number};
  //   }
  //   else if(OK_IS_VALUE_OBJECT(p_lhs) || OK_IS_VALUE_OBJECT(p_rhs))
  //   {
  //     return perform_add_others(p_lhs, p_rhs);
  //   }
  //   return std::unexpected{value_error::undefined_operation};
  // }

  // std::expected<value_t, value_error> vm::perform_divide_others(value_t p_lhs, value_t p_rhs)
  // {
  //   auto ret = m_value_operations.divide_operations.call_operation(
  //       combine_value_type_with_object_type(p_lhs, p_rhs), std::move(p_lhs), std::move(p_rhs));
  //   if(!ret->has_value())
  //   {
  //     return std::unexpected{value_error::undefined_operation};
  //   }
  //   return ret->value();
  // }

  bool vm::is_value_falsy(value_t p_value) const
  {
    return OK_IS_VALUE_NULL(p_value) || (OK_IS_VALUE_BOOL(p_value) && !OK_VALUE_AS_BOOL(p_value));
  }

  std::optional<bool> vm::is_builtin_falsy(value_t p_value) const
  {
    if(OK_IS_VALUE_OBJECT(p_value))
      return {};
    return OK_IS_VALUE_NULL(p_value) || (OK_IS_VALUE_BOOL(p_value) && !OK_VALUE_AS_BOOL(p_value));
  }

  void vm::print_value(value_t p_value)
  {
    if(!perform_print(p_value))
      ASSERT(false); // TODO(Qais): find a workaround for this situation
  }

  auto vm::perform_print(value_t p_printable) -> operation_print_return_type
  {
    if(OK_IS_VALUE_NUMBER(p_printable))
    {
      std::print("{}", OK_VALUE_AS_NUMBER(p_printable));
      return {};
    }
    if(OK_IS_VALUE_BOOL(p_printable))
    {
      std::print("{}", OK_VALUE_AS_BOOL(p_printable));
      return {};
    }
    if(OK_IS_VALUE_NULL(p_printable))
    {
      std::print("null");
      return {};
    }
    if(OK_IS_VALUE_NATIVE_FUNCTION(p_printable))
    {
      std::print("{:p}", (void*)OK_VALUE_AS_NATIVE_FUNCTION(p_printable));
      return {};
    }
    if(OK_IS_VALUE_OBJECT(p_printable))
    {
      return perform_print_others(p_printable);
    }
    ASSERT(false);
    return std::unexpected{value_error_code::unknown_type};
  }

  auto vm::perform_print_others(value_t p_printable) -> operation_print_return_type
  {
    ASSERT(OK_IS_VALUE_OBJECT(p_printable));
    // const auto res =
    //     m_value_operations.print_operations.call_operation(OK_VALUE_AS_OBJECT(p_this)->get_type(),
    //     std::move(p_this));
    // if(!res.has_value())
    // {
    //   return std::unexpected{value_error::undefined_operation};
    // }

    // value_t argv[3] = {p_this, value_t{0.0}, value_t{copy{(object*)nullptr}}};
    auto nm = OK_VALUE_AS_OBJECT(p_printable)->class_->specials.operations[overridable_operator_type::oot_print];
    if(OK_IS_VALUE_NULL(nm))
    {
      return std::unexpected(value_error_code::undefined_operation);
    }
    auto ret = OK_VALUE_AS_NATIVE_FUNCTION(nm)(this, p_printable, 0);
    // auto ret = m_value_operations.add_operations.call_operation(
    //     combine_value_types_with_object_types(p_lhs, p_rhs), std::move(p_lhs), std::move(p_rhs));
    return {};
  }

  // void vm::print_value(value_t p_value)
  // {
  //   switch(p_value.type)
  //   {
  //   case value_type::number_val:
  //     std::print("{}", p_value.as.number);
  //     break;
  //   case value_type::bool_val:
  //     std::print("{}", p_value.as.boolean ? "true" : "false");
  //     break;
  //   case value_type::null_val:
  //     std::print("null");
  //     break;
  //   case value_type::object_val:
  //     print_object(p_value.as.obj);
  //     break;
  //   default:
  //     std::print("{}", (void*)p_value.as.obj);
  //     break;
  //   }
  // }

  // void vm::print_object(object* p_object)
  // {
  //   const auto tp = p_object->get_type();
  //   auto op_it = m_objects_operations.find(tp);
  //   if(m_objects_operations.end() == op_it)
  //   {
  //     ASSERT(false);
  //   }
  //   op_it->second.print_function(p_object);
  // }

  void vm::runtime_error(const std::string& err)
  {
    ERRORLN("runtime error: {}", err);
    for(const auto& frame : m_call_frames)
    {
      size_t instruction = frame.ip - frame.closure->function->associated_chunk.code.data() - 1;
      ERRORLN("offset: {}, in: {}",
              frame.closure->function->associated_chunk.get_offset(instruction),
              frame.closure->function->name->chars);
    }
  }

  // TODO(Qais): name spaces
  // it always overrides currently
  bool vm::define_native_function(std::string_view p_name, native_function p_fu)
  {
    m_stack.push(value_t{p_name});
    m_stack.push(value_t{p_fu, true});
    auto topmin1 = OK_VALUE_AS_OBJECT(*m_stack.value_ptr_top(1));
    auto top = m_stack.top();
    m_globals[(string_object*)topmin1] = {top}; // immutable
    m_stack.pop();
    m_stack.pop();
    return true;
  }

  void vm::destroy_objects_list()
  {
    while(m_objects_list != nullptr)
    {
      auto next = m_objects_list->next;
      delete_object(m_objects_list);
      m_objects_list = next;
    }
  }

  native_function_return_type clock_native(vm* p_vm, value_t p_this, uint8_t argc)
  {
    if(argc != 0)
    {
      return {.code = value_error_code::arguments_mismatch};
      // return {.return_type = native_function_return_type::rt_error,
      //         .return_value.error = value_error_code::arguments_mismatch};
      //  return {.return_type=native_function_return_type::rt_operations, .return_value.value=value_t{}};
      //  return {.is_error = false, .normal_return = value_t{}};
    }
    // return {.return_type = native_function_return_type::rt_operations,

    p_vm->stack_push(value_t{(double)clock() / CLOCKS_PER_SEC});
    return {.code = value_error_code::ok};
  }

  native_function_return_type time_native(vm* p_vm, value_t p_this, uint8_t argc)
  {
    if(argc != 0)
    {
      return {.code = value_error_code::arguments_mismatch};
      // return {.is_error = false, .normal_return = value_t{}};
    }
    p_vm->stack_push(value_t{(double)time(nullptr)});
    return {.code = value_error_code::ok};
    // return {.is_error = false, .normal_return = value_t{(double)time(nullptr)}};
  }

  native_function_return_type srand_native(vm* p_vm, value_t p_this, uint8_t argc)
  {
    uint32_t cseed = time(nullptr);
    if(argc > 1)
    {
      return {.code = value_error_code::arguments_mismatch};
      // return {.is_error = false, .normal_return = value_t{}};
    }
    if(argc == 1)
    {
      auto seed = p_vm->stack_top(0);
      if(!OK_IS_VALUE_NUMBER(seed))
      {
        return {.code = value_error_code::unknown_type};
        // return {.is_error = false, .normal_return = value_t{}};
      }
      cseed = OK_VALUE_AS_NUMBER(seed);
    }
    srand(cseed);
    p_vm->stack_push(value_t{});
    return {.code = value_error_code::ok};
    // return {.is_error = false, .normal_return = value_t{}};
  }

  native_function_return_type rand_native(vm* p_vm, value_t p_this, uint8_t argc)
  {
    if(argc != 0)
      return {.code = value_error_code::arguments_mismatch};
    p_vm->stack_push(value_t{(double)rand()});
    return {.code = value_error_code::ok};
    // return {.is_error = false, .normal_return = value_t{(double)rand()}};
  }

  template <operator_type>
  auto& vm::value_operations::get_value_operation()
  {
    static_assert(false, "unsupported operation");
  }
} // namespace ok