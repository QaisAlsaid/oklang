#include "value.hpp"
#include "object.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <print>
#include <string_view>

namespace ok
{
  value_t::value_t(bool p_bool) : type(value_type::bool_val), as({.boolean = p_bool})
  {
  }

  value_t::value_t(double p_number) : type(value_type::number_val), as({.number = p_number})
  {
  }

  value_t::value_t() : type(value_type::null_val), as({.number = 0})
  {
  }

  value_t::value_t(const char* p_str, size_t p_length)
      : type(value_type::object_val) //, as({.obj = new_object<string_object>(std::string_view{p_str, p_length})})
  {
    auto* vm_ = get_g_vm();
    auto str_cls = vm_->get_builtin_class(object_type::obj_string);
    auto str = new_object<string_object>(std::string_view{p_str, p_length}, str_cls, vm_->get_objects_list());
    as.pointer = str;
  }

  value_t::value_t(std::string_view p_str)
      : type(value_type::object_val) //, as({.obj = new_object<string_object>(p_str)})

  {
    auto* vm_ = get_g_vm();
    as.pointer =
        new_object<string_object>(p_str, vm_->get_builtin_class(object_type::obj_string), vm_->get_objects_list());
  }

  value_t::value_t(uint8_t p_arity, string_object* p_name)
      : type(value_type::object_val) //, as({.obj = new_object<function_object>(p_arity, p_name)})
  {
    auto* vm_ = get_g_vm();
    as.pointer = new_object<function_object>(
        p_arity, p_name, vm_->get_builtin_class(object_type::obj_function), vm_->get_objects_list());
  }

  value_t::value_t(native_function p_native_function, bool is_free_function) : type(value_type::native_function_val)
  {
    as.pointer = (void*)p_native_function;
  }

  // value_t::value_t(native_function p_native_function,)
  //     : type(value_type::object_val) //, as({.obj = new_object<native_function_object>(p_native_function)})
  // {
  //   auto* vm_ = get_g_vm();
  //   as.obj = new_object<native_function_object>(
  //       p_native_function, vm_->get_builtin_class(object_type::obj_native_function), vm_->get_objects_list());
  // }

  // value_t::value_t(native_method p_native_method)
  //     : type(value_type::object_val) //, as({.obj = new_object<native_method_object>(p_native_method)})
  //{
  //   auto* vm_ = get_g_vm();
  //   as.obj = new_object<native_method_object>(
  //       p_native_method, vm_->get_builtin_class(object_type::obj_native_method), vm_->get_objects_list());
  // }

  //   value_t::value_t(copy<function_object*> p_function) : type(value_type::object_val), as({.obj =
  //   (object*)p_function.t})
  //   {
  //   }

  //   value_t::value_t(copy<closure_object*> p_closure) : type(value_type::object_val), as({.obj =
  //   (object*)p_closure.t})
  //   {
  //   }
  //
} // namespace ok
