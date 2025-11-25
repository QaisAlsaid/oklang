#include "vm.hpp"
#include "call_frame.hpp"
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
  static native_return_type clock_native(vm* p_vm, value_t p_this, uint8_t argc);
  static native_return_type time_native(vm* p_vm, value_t p_this, uint8_t argc);
  static native_return_type srand_native(vm* p_vm, value_t p_this, uint8_t argc);
  static native_return_type rand_native(vm* p_vm, value_t p_this, uint8_t argc);

  // auto vm::call_value_op(value_t p_native, value_t p_receiver, uint8_t p_argc) -> operations_return_type
  // {
  //   update_call_frame_top_index();
  //   if(OK_IS_VALUE_NATIVE_FUNCTION(p_native))
  //   {
  //     auto ret = call_native_method(p_native, p_receiver, p_argc);
  //     if(ret.code != value_error_code::ok)
  //     {
  //       return std::unexpected{ret.code};
  //     }
  //     return {};
  //   }
  //   else if(OK_IS_VALUE_OBJECT(p_native))
  //   {
  //     auto obj = OK_VALUE_AS_OBJECT(p_native);
  //     const auto& call_ops = obj->class_->specials.operations[method_type::mt_unary_postfix_call];
  //     const auto callfcn = call_ops;
  //     if(OK_IS_VALUE_NULL(callfcn))
  //     {
  //       return std::unexpected{value_error_code::undefined_operation};
  //     }
  //     auto ret = call_native_method(callfcn, p_native, p_argc);
  //     if(ret.code != value_error_code::ok)
  //     {
  //       return std::unexpected{ret.code};
  //     }
  //     return {};
  //   }
  //   else
  //   {
  //     return std::unexpected{value_error_code::undefined_operation};
  //   }
  // }

  void vm::setup_stack_for_binary_infix_others()
  {
    auto b = m_stack.pop();
    auto a = m_stack.pop();
    m_stack.push(value_t{}); // maintain same calling convention for all call operations, TODO(Qais): no need
    m_stack.push(a);
    m_stack.push(b);
  }

  template <operator_type>
  bool vm::perform_binary_infix_others(value_t p_this, value_t p_other)
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_plus>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_plus];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_minus>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_minus];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_asterisk>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_asterisk];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_slash>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_slash];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_caret>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_caret];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_modulo>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_modulo];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_ampersand>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_bitwise_and];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_bar>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_bitwise_or];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_bang_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_bang_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_greater>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_greater];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_greater_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_greater_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_less>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_less];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_less_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = this_class->specials.operations[method_type::mt_binary_infix_less_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_plus_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_plus_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_minus_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_minus_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_asterisk_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_asterisk_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_slash_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_slash_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_modulo_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_modulo_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_bitwise_and_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_bitwise_and_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_caret_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_caret_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_bitwise_or_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_bitwise_or_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_shift_left_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_shift_left_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_shift_right_equal>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_shift_right_equal];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_shift_left>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_shift_left];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_shift_right>(value_t p_this, value_t p_other)
  {
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.operations[method_type::mt_binary_infix_shift_right];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_binary_infix_others<operator_type::op_as>(value_t p_this, value_t p_other)
  {
    const auto other_tp = OK_VALUE_AS_OBJECT(p_other)->get_type();
    const auto this_class = OK_VALUE_AS_OBJECT(p_this)->class_;

    auto nm = this_class->specials.conversions.find(other_tp);
    if(this_class->specials.conversions.end() == nm)
    {
      runtime_error("error");
      return false;
    }
    m_stack.pop();
    return call_value(nm->second, p_this, 0);
  }

  template <operator_type>
  bool vm::perform_binary_infix()
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_plus>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) + OK_VALUE_AS_NUMBER(rhs)}});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_plus>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    runtime_error("error");
    return false;
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_minus>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) - OK_VALUE_AS_NUMBER(rhs)}});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_minus>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_asterisk>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) * OK_VALUE_AS_NUMBER(rhs)}});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_asterisk>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_slash>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      if(OK_VALUE_AS_NUMBER(rhs) == 0)
      {
        return false;
        runtime_error("division by 0");
      }
      m_stack.push({value_t{OK_VALUE_AS_NUMBER(lhs) / OK_VALUE_AS_NUMBER(rhs)}});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_slash>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
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
  bool vm::perform_binary_infix<operator_type::op_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_equal>(lhs, rhs);
    }

    auto equality = perform_equality_builtins(lhs, rhs, true);
    m_stack.pop();
    m_stack.pop();
    m_stack.push(value_t{equality});
    return true;
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_bang_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_bang_equal>(lhs, rhs);
    }

    auto equality = perform_equality_builtins(lhs, rhs, false);
    m_stack.pop();
    m_stack.pop();
    m_stack.push(value_t{equality});
    return true;
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_greater>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) > OK_VALUE_AS_NUMBER(rhs)});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_greater>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_greater_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) >= OK_VALUE_AS_NUMBER(rhs)});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_greater_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_less>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) < OK_VALUE_AS_NUMBER(rhs)});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_less>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_less_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      m_stack.pop();
      m_stack.push(value_t{OK_VALUE_AS_NUMBER(lhs) <= OK_VALUE_AS_NUMBER(rhs)});
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_less_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_caret>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
    }
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_caret>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_modulo>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_modulo>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_ampersand>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_ampersand>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_bar>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_bar>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_plus_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      OK_VALUE_AS_NUMBER(m_stack.top()) += OK_VALUE_AS_NUMBER(rhs);
      return true;
    }
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_plus_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_minus_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      OK_VALUE_AS_NUMBER(m_stack.top()) -= OK_VALUE_AS_NUMBER(rhs);
      return true;
    }
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_minus_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_asterisk_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      OK_VALUE_AS_NUMBER(m_stack.top()) *= OK_VALUE_AS_NUMBER(rhs);
      return true;
    }
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_asterisk_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_slash_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_NUMBER(lhs) && OK_IS_VALUE_NUMBER(rhs))
    {
      m_stack.pop();
      OK_VALUE_AS_NUMBER(m_stack.top()) /= OK_VALUE_AS_NUMBER(rhs);
      return true;
    }
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_slash_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_modulo_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_modulo_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_bitwise_and_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_bitwise_and_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_caret_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_caret_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_bitwise_or_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_bitwise_or_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_shift_left_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_shift_left_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_shift_right_equal>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_shift_right_equal>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_shift_left>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_shift_left>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_shift_right>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs))
    {
      return perform_binary_infix_others<operator_type::op_shift_right>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_binary_infix<operator_type::op_as>()
  {
    auto lhs = m_stack.top(1);
    auto rhs = m_stack.top();
    if(OK_IS_VALUE_OBJECT(lhs) && OK_IS_VALUE_OBJECT(rhs))
    {
      return perform_binary_infix_others<operator_type::op_as>(lhs, rhs);
    }
    m_stack.pop();
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <operator_type>
  bool vm::perform_unary_prefix_others(value_t p_this)
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  bool vm::perform_unary_prefix_others<operator_type::op_minus>(value_t p_this)
  {
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_prefix_minus];
    return call_value(nm, p_this, 0);
  }

  template <>
  bool vm::perform_unary_prefix_others<operator_type::op_plus>(value_t p_this)
  {
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_prefix_plus];
    return call_value(nm, p_this, 0);
  }

  template <>
  bool vm::perform_unary_prefix_others<operator_type::op_bang>(value_t p_this)
  {
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_prefix_bang];
    return call_value(nm, p_this, 0);
  }

  template <>
  bool vm::perform_unary_prefix_others<operator_type::op_tiled>(value_t p_this)
  {
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_prefix_tiled];
    return call_value(nm, p_this, 0);
  }

  template <>
  bool vm::perform_unary_prefix_others<operator_type::op_plus_plus>(value_t p_this)
  {
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_prefix_plus_plus];
    return call_value(nm, p_this, 0);
  }

  template <>
  bool vm::perform_unary_prefix_others<operator_type::op_minus_minus>(value_t p_this)
  {
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_prefix_minus_minus];
    return call_value(nm, p_this, 0);
  }

  template <operator_type>
  bool vm::perform_unary_prefix()
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  bool vm::perform_unary_prefix<operator_type::op_minus>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_NUMBER(_this))
    {
      m_stack.top(0) = value_t{-OK_VALUE_AS_NUMBER(_this)};
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_prefix_others<operator_type::op_minus>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_unary_prefix<operator_type::op_plus>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_NUMBER(_this))
    {
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_prefix_others<operator_type::op_plus>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_unary_prefix<operator_type::op_bang>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_BOOL(_this))
    {
      m_stack.top(0) = value_t{!OK_VALUE_AS_BOOL(_this)};
      return true;
    }
    else if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_prefix_others<operator_type::op_bang>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_unary_prefix<operator_type::op_tiled>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_prefix_others<operator_type::op_tiled>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_unary_prefix<operator_type::op_plus_plus>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_NUMBER(_this))
    {
      ++OK_VALUE_AS_NUMBER(m_stack.top());
      return true;
    }
    if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_prefix_others<operator_type::op_plus_plus>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_unary_prefix<operator_type::op_minus_minus>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_NUMBER(_this))
    {
      --OK_VALUE_AS_NUMBER(m_stack.top());
      return true;
    }
    if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_prefix_others<operator_type::op_minus_minus>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <operator_type>
  bool vm::perform_unary_postfix_others(value_t p_this)
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  bool vm::perform_unary_postfix_others<operator_type::op_plus_plus>(value_t p_this)
  {
    m_stack.push(value_t{}); // fake argument
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_postfix_plus_plus];
    return call_value(nm, p_this, 1);
  }

  template <>
  bool vm::perform_unary_postfix_others<operator_type::op_minus_minus>(value_t p_this)
  {
    m_stack.push(value_t{}); // fake argument
    const auto lhs_class = OK_VALUE_AS_OBJECT(p_this)->class_;
    auto nm = lhs_class->specials.operations[method_type::mt_unary_postfix_minus_minus];
    return call_value(nm, p_this, 1);
  }

  template <operator_type>
  bool vm::perform_unary_postfix()
  {
    static_assert(false, "unsupported operation");
  }

  template <>
  bool vm::perform_unary_postfix<operator_type::op_plus_plus>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_NUMBER(_this))
    {
      ++OK_VALUE_AS_NUMBER(m_stack.top());
      return {};
    }
    if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_postfix_others<operator_type::op_plus_plus>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  template <>
  bool vm::perform_unary_postfix<operator_type::op_minus_minus>()
  {
    auto _this = m_stack.top();
    if(OK_IS_VALUE_NUMBER(_this))
    {
      --OK_VALUE_AS_NUMBER(m_stack.top());
      return true;
    }
    if(OK_IS_VALUE_OBJECT(_this))
    {
      return perform_unary_postfix_others<operator_type::op_minus_minus>(_this);
    }
    m_stack.pop();
    return false;
    runtime_error("error");
  }

  vm::vm() : m_stack(s_stack_base_size), m_logger(LOG_LEVEL)
  {
    // keep id will need that for logger (yes logger will be globally accessible and requires indirection but its
    // mostly fine because its either debug only on on error where you dont even need fast code)
    static uint32_t id = 0;
    m_call_frames.reserve(s_call_frame_max_size);

    m_id = ++id;

    m_interned_strings = {};
    m_objects_list = nullptr;
  }

  vm::~vm()
  {
    destroy_objects_list();
  }

  void vm::init()
  {
    m_compiler = {};
    m_globals = {};

    register_builtin_objects();
    m_statics.init(this);

    define_native_function("clock", clock_native);
    define_native_function("time", time_native);
    define_native_function("srand", srand_native);
    define_native_function("rand", rand_native);
  }

  auto vm::interpret(const std::string_view p_filename, const std::string_view p_source) -> interpret_result
  {
    m_compiler = compiler{}; // reinitialize and clear previous state
    push_call_frame(call_frame{.ip = nullptr, .slots = 0, .top = 0, .closure = nullptr});
    auto compile_result = m_compiler.compile(
        this,
        p_filename,
        p_source,
        new_tobject<string_object>("main", get_builtin_class(object_type::obj_string), get_objects_list()));
    if(!compile_result)
    {
      if(m_compiler.get_parse_errors().errs.empty())
        return interpret_result::compile_error;
      return interpret_result::parse_error;
    }
    m_call_frames = {};
    m_call_frames.reserve(s_call_frame_max_size);
    stack_resize(0);
    m_stack.push(value_t{copy{(object*)compile_result}});
    auto closure =
        new_tobject<closure_object>(compile_result, get_builtin_class(object_type::obj_closure), get_objects_list());
    m_stack.top() = value_t{copy{(object*)closure}};
    if(!push_call_frame(call_frame{closure, closure->function->associated_chunk.code.data(), 0, 0}))
    {
      return interpret_result::runtime_error;
    }
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
      case to_utype(opcode::op_additive):
      {
        auto ret = perform_unary_prefix<operator_type::op_plus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_negate):
      {
        auto ret = perform_unary_prefix<operator_type::op_minus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_not):
      {
        auto ret = perform_unary_prefix<operator_type::op_bang>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_tiled):
      {
        auto ret = perform_unary_prefix<operator_type::op_tiled>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_preincrement):
      {
        auto ret = perform_unary_prefix<operator_type::op_plus_plus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_predecrement):
      {
        auto ret = perform_unary_prefix<operator_type::op_minus_minus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_postincrement):
      {
        auto ret = perform_unary_postfix<operator_type::op_plus_plus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_postdecrement):
      {
        auto ret = perform_unary_postfix<operator_type::op_minus_minus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_add):
      {
        auto ret = perform_binary_infix<operator_type::op_plus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_subtract):
      {
        auto ret = perform_binary_infix<operator_type::op_minus>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_multiply):
      {
        auto ret = perform_binary_infix<operator_type::op_asterisk>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_divide):
      {
        auto ret = perform_binary_infix<operator_type::op_slash>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_modulo):
      {
        auto ret = perform_binary_infix<operator_type::op_modulo>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_and):
      {
        auto ret = perform_binary_infix<operator_type::op_ampersand>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_xor):
      {
        auto ret = perform_binary_infix<operator_type::op_caret>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_or):
      {
        auto ret = perform_binary_infix<operator_type::op_bar>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_shift_left):
      {
        auto ret = perform_binary_infix<operator_type::op_shift_left>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_shift_right):
      {
        auto ret = perform_binary_infix<operator_type::op_shift_right>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_as):
      {
        auto ret = perform_binary_infix<operator_type::op_as>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
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
      case to_utype(opcode::op_equal):
      {
        auto ret = perform_binary_infix<operator_type::op_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_not_equal):
      {
        auto ret = perform_binary_infix<operator_type::op_bang_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_greater):
      {
        auto ret = perform_binary_infix<operator_type::op_greater>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_greater_equal):
      {
        auto ret = perform_binary_infix<operator_type::op_greater_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_less):
      {
        auto ret = perform_binary_infix<operator_type::op_less>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_less_equal):
      {
        auto ret = perform_binary_infix<operator_type::op_less_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_add_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_plus_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_subtract_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_minus_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_multiply_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_asterisk_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_divide_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_slash_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_modulo_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_modulo_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_and_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_bitwise_and_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_xor_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_caret_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_or_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_bitwise_or_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_shift_left_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_shift_left_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_shift_right_assign):
      {
        auto ret = perform_binary_infix<operator_type::op_shift_right_equal>();
        if(!ret)
        {
          return interpret_result::runtime_error;
        }
        frame = &m_call_frames.back();
        break;
      }
      case to_utype(opcode::op_print):
      {
        auto ret = perform_print(m_stack.top());
        if(!ret)
        {
          runtime_error("bad print");
          return interpret_result::runtime_error;
        }
        std::println(); // hack!
        frame = &m_call_frames.back();
        m_stack.pop();
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
          runtime_error("redefining global: " + std::string{name_str->chars}); // tf is this?
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
          runtime_error("undefined global: " + std::string{name_str->chars}); // tf is this?
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
      case to_utype(opcode::op_set_if_global):
      case to_utype(opcode::op_set_if_global_long):
      {
        auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_set_global_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);

        auto ret = set_if();
        if(!ret.has_value())
        {
          return interpret_result::runtime_error;
        }
        if(ret.value())
        {
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
        }
        m_stack.pop();
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
      case to_utype(opcode::op_set_if_local):
      case to_utype(opcode::op_set_if_local_long):
      {
        auto& val = read_local(static_cast<opcode>(instruction) == opcode::op_set_local_long);
        auto ret = set_if();
        if(!ret.has_value())
        {
          return interpret_result::runtime_error;
        }
        if(ret.value())
        {
          val = m_stack.top();
        }
        m_stack.pop();
        break;
      }
      case to_utype(opcode::op_conditional_jump):
      case to_utype(opcode::op_conditional_truthy_jump):
      {
        const auto jump = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        auto val = m_stack.pop();
        if(!OK_IS_VALUE_BOOL(val))
        {
          runtime_error("value not bool");
          return interpret_result::runtime_error;
        }
        const auto cond = static_cast<opcode>(instruction) == opcode::op_conditional_jump ? !OK_VALUE_AS_BOOL(val)
                                                                                          : OK_VALUE_AS_BOOL(val);
        frame->ip += jump * cond;
        break;
      }
      case to_utype(opcode::op_conditional_jump_leave):
      case to_utype(opcode::op_conditional_truthy_jump_leave):
      {
        const auto jump = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
        auto val = m_stack.pop();
        if(!OK_IS_VALUE_BOOL(val))
        {
          runtime_error("value not bool");
          return interpret_result::runtime_error;
        }
        const auto cond = static_cast<opcode>(instruction) == opcode::op_conditional_jump ? !OK_VALUE_AS_BOOL(val)
                                                                                          : OK_VALUE_AS_BOOL(val);
        if(cond)
        {
          frame->ip += jump;
        }
        else
        {
          m_stack.pop();
        }
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
        auto res = call_value(peek, peek, argc);
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
      case to_utype(opcode::op_set_upvalue):
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

        using namespace std::string_view_literals;
        std::array<std::string_view, 2> srcs = {std::string_view{name_str->chars, name_str->length}, "_meta"sv};
        // TODO(Qais): vm level helper for class creation is cleaner than this
        auto meta_name =
            new_tobject<string_object>(srcs, get_builtin_class(object_type::obj_string), get_objects_list());
        auto meta = new_tobject<class_object>(meta_name,
                                              object_type::obj_meta_class,
                                              get_builtin_class(object_type::obj_object),
                                              get_builtin_class(object_type::obj_class),
                                              get_objects_list());
        auto cls = new_object<class_object>(
            name_str, id, meta, get_builtin_class(object_type::obj_instance), get_objects_list());
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
      case to_utype(opcode::op_set_if_property):
      case to_utype(opcode::op_set_if_property_long):
      {
        auto obj = OK_VALUE_AS_OBJECT(m_stack.top(1));
        if(OK_IS_VALUE_OBJECT(m_stack.top(1)) && !OK_VALUE_AS_OBJECT(m_stack.top(1))->is_instance())
        {
          runtime_error("only instances can have properties");
          return interpret_result::runtime_error;
        }
        const auto instance = OK_VALUE_AS_INSTANCE_OBJECT(m_stack.top(1));
        const auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_get_property_long);
        const auto name_str = OK_VALUE_AS_STRING_OBJECT(name);

        auto ret = set_if();
        if(!ret.has_value())
        {
          return interpret_result::runtime_error;
        }

        if(ret.value())
        {
          instance->fields[name_str] = m_stack.top();
        }
        m_stack.pop();
        m_stack.pop();
        break;
      }

      case to_utype(opcode::op_method):
      case to_utype(opcode::op_method_long):
      {
        const auto name = read_identifier(static_cast<opcode>(instruction) == opcode::op_method_long);
        auto name_str = OK_VALUE_AS_STRING_OBJECT(name);
        const auto argc = read_byte();
        define_method(name_str, argc);
        break;
      }
      case to_utype(opcode::op_special_method):
      {
        const auto type = read_byte();
        auto argc = read_byte();
        define_special_method(type, argc);
        break;
      }
      case to_utype(opcode::op_convert_method):
      {
        auto argc = read_byte();
        if(!define_convert_method())
        {
          return interpret_result::runtime_error;
        }
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

        if(OK_IS_VALUE_OBJECT(super) && !OK_VALUE_AS_OBJECT(super)->is_class())
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
      case to_utype(opcode::op_save_slot):
      {
        frame->saved_slot = m_stack.top();
        break;
      }
      case to_utype(opcode::op_push_saved_slot):
      {
        m_stack.push(frame->saved_slot);
        frame->saved_slot = value_t{};
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
      return m_stack[frame_stack_slots() + index];
      // return m_call_frames.back().slots[index];
    }
    const uint32_t index = decode_int<uint32_t, 3>(read_bytes<3>(), 0);
    return m_stack[frame_stack_slots() + index];
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

  bool vm::call_value(value_t p_callee, value_t p_this, uint8_t p_argc)
  {
    TRACELN("call value: argc: {}, callee: {}", p_argc, (uint32_t)p_callee.type);
    if(!OK_IS_VALUE_NATIVE_FUNCTION(p_callee) && !OK_IS_VALUE_OBJECT(p_callee))
    {
      runtime_error("bad call: callee isn't of type native function or of type object");
      return false;
    }

    prepare_call_frame_for_call(p_argc);
    return perform_call(p_callee, p_this, p_argc);
  }

  bool vm::perform_call(value_t p_callee, value_t p_this, uint8_t p_argc)
  {
    // this function shall not be called before checking the value_type
    if(OK_IS_VALUE_NATIVE_FUNCTION(p_callee))
    {
      return call_native(OK_VALUE_AS_NATIVE_FUNCTION(p_callee), p_callee, p_argc);
    }
    else if(OK_IS_VALUE_OBJECT(p_callee))
    {
      auto obj = OK_VALUE_AS_OBJECT(p_callee);
      const auto& call_ops = obj->class_->specials.operations[method_type::mt_unary_postfix_call];
      const auto callfcn = call_ops;
      if(OK_IS_VALUE_NULL(callfcn))
      {
        runtime_error("undefined operation");
        return false;
      }
      return call_native(OK_VALUE_AS_NATIVE_FUNCTION(callfcn), p_callee, p_argc);
    }
    runtime_error("undefined operation");
    return false;
  }

  bool vm::call_native(native_function p_native, value_t p_this, uint8_t p_argc, native_return_code p_allowed_codes)
  {
    auto nrt = p_native(this, p_this, p_argc);

    if(is_in_group(nrt.code, native_return_code::nrc_error))
    {
      runtime_error("native call error");
      return false;
    }
    if(!is_in_group(nrt.code, p_allowed_codes))
    {
      runtime_error("not allowed");
      return false;
    }
    return true;
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
        return call_value(it->second, it->second, p_argc);
      }
      return invoke_from_class(obj->class_, p_method_name, p_argc);
    }

    runtime_error("cant perform invoke on non-instance types");
    return false;
  }

  bool vm::invoke_from_class(class_object* p_class, string_object* p_method_name, uint8_t p_argc)
  {
    auto it = p_class->methods.find(p_method_name);
    if(p_class->methods.end() == it)
    {
      runtime_error("undefined method: "); // TODO(Qais): runtime error is shit
      return false;
    }
    return call_value(it->second, it->second, p_argc);
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
    m_stack.pop();
  }

  void vm::define_special_method(uint8_t p_mt, uint8_t p_arity)
  {
    auto method = m_stack.top();
    auto class_ = OK_VALUE_AS_CLASS_OBJECT(m_stack.top(1));
    class_->specials.operations[p_mt] = method;
    m_stack.pop();
  }

  bool vm::define_convert_method()
  {
    auto convertee = m_stack.top();
    auto method = m_stack.top(1);
    auto class_ = OK_VALUE_AS_CLASS_OBJECT(m_stack.top(2));

    if(!(OK_IS_VALUE_OBJECT(convertee) && OK_VALUE_AS_OBJECT(convertee)->is_class()))
    {
      runtime_error("bad conversion");
      m_stack.pop();
      m_stack.pop();
      return false;
    }

    auto convertee_cls = OK_VALUE_AS_CLASS_OBJECT(convertee);
    const auto tp = convertee_cls->up.get_type();
    class_->specials.conversions[tp] = method;
    m_stack.pop();
    m_stack.pop();
    return true;
  }

  bool vm::bind_a_method(class_object* p_class, string_object* p_name)
  {
    auto it = p_class->methods.find(p_name);
    if(p_class->methods.end() == it)
    {
      runtime_error("undefined property: " + std::string{std::string_view{p_name->chars, p_name->length}});
      return false;
    }

    auto bound = new_object<bound_method_object>(
        m_stack.top(), it->second, get_builtin_class(object_type::obj_bound_method), get_objects_list());
    m_stack.top() = value_t{copy{bound}};
    return true;
  }

  void vm::print_value(value_t p_value)
  {
    if(!perform_print(p_value))
      ASSERT(false); // TODO(Qais): find a workaround for this situation
  }

  // always runs in sync (no context switching is allowed)
  bool vm::perform_print(value_t p_printable)
  {
    if(OK_IS_VALUE_NUMBER(p_printable))
    {
      std::print("{}", OK_VALUE_AS_NUMBER(p_printable));
      return true;
    }
    if(OK_IS_VALUE_BOOL(p_printable))
    {
      std::print("{}", OK_VALUE_AS_BOOL(p_printable));
      return true;
    }
    if(OK_IS_VALUE_NULL(p_printable))
    {
      std::print("null");
      return true;
    }
    if(OK_IS_VALUE_NATIVE_FUNCTION(p_printable))
    {
      std::print("{:p}", (void*)OK_VALUE_AS_NATIVE_FUNCTION(p_printable));
      return true;
    }
    if(OK_IS_VALUE_OBJECT(p_printable))
    {
      return perform_print_others(p_printable);
    }
    runtime_error("err");
    return false;
  }

  bool vm::perform_print_others(value_t p_printable)
  {
    auto cls = OK_VALUE_AS_OBJECT(p_printable)->class_;
    auto nm = cls->specials.operations[method_type::mt_print];
    if(OK_IS_VALUE_NULL(nm))
    {
      runtime_error("error");
      return false;
    }
    if(!OK_IS_VALUE_NATIVE_FUNCTION(nm))
    {
      runtime_error("print not allowed");
      return false;
    }
    m_stack.push(p_printable);
    prepare_call_frame_for_call(0);
    auto ret = call_native(OK_VALUE_AS_NATIVE_FUNCTION(nm), p_printable, 0, native_return_code::nrc_print_exit);
    m_stack.pop();
    return ret;
  }

  // this is slow, why the fuck would you encode the function like this, just call it from the vm. omg this is so stupid
  // i cant believe i did this
  std::expected<bool, bool> vm::set_if()
  {
    auto fcn = (compiler::compare_function)decode_int<uint64_t, 8>(read_bytes<8>(), 0);
    if(fcn == nullptr)
    {
      return std::unexpected(false);
    }
    if(fcn(m_stack.top()))
    {
      return true;
    }
    return false;
  }

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

  native_return_type clock_native(vm* p_vm, value_t, uint8_t p_argc)
  {
    if(p_argc != 0)
    {
      return {.code = native_return_code::nrc_error,
              .error.code = value_error_code::arguments_mismatch,
              .error.payload = value_t{std::format("expected 0 arguments, got: {}", p_argc)}};
    }

    p_vm->return_value(value_t{(double)clock() / CLOCKS_PER_SEC});
    return {.code = native_return_code::nrc_return};
  }

  native_return_type time_native(vm* p_vm, value_t, uint8_t p_argc)
  {
    if(p_argc != 0)
    {
      return {.code = native_return_code::nrc_error,
              .error.code = value_error_code::arguments_mismatch,
              .error.payload = value_t{std::format("expected 0 arguments, got: {}", p_argc)}};
    }

    p_vm->return_value(value_t{(double)time(nullptr)});
    return {.code = native_return_code::nrc_return};
  }

  native_return_type srand_native(vm* p_vm, value_t, uint8_t p_argc)
  {
    uint32_t cseed = time(nullptr);
    if(p_argc > 1)
    {
      return {.code = native_return_code::nrc_error,
              .error.code = value_error_code::arguments_mismatch,
              .error.payload = value_t{std::format("expected 0 or 1 arguments, got: {}", p_argc)}};
    }
    if(p_argc == 1)
    {
      auto seed = p_vm->get_arg(0);
      if(!OK_IS_VALUE_NUMBER(seed))
      {
        return {.code = native_return_code::nrc_error,
                .error.code = value_error_code::unknown_type,
                .error.payload = value_t{std::format("expected argument of type number, got: {}",
                                                     (uint8_t)seed.type)}}; // TODO(Qais): proper type string
      }
      cseed = OK_VALUE_AS_NUMBER(seed);
    }
    srand(cseed);
    p_vm->return_value(value_t{});
    return {.code = native_return_code::nrc_return};
  }

  native_return_type rand_native(vm* p_vm, value_t, uint8_t p_argc)
  {
    if(p_argc != 0)
    {
      return {.code = native_return_code::nrc_error,
              .error.code = value_error_code::arguments_mismatch,
              .error.payload = value_t{std::format("expected 0 arguments, got: {}", p_argc)}};
    }

    p_vm->return_value(value_t{(double)rand()});
    return {.code = native_return_code::nrc_return};
  }

} // namespace ok