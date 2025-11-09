#include "object.hpp"
#include "chunk.hpp"
#include "constants.hpp"
#include "macros.hpp"
#include "operator.hpp"
#include "value.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <array>
#include <expected>
#include <numeric>
#include <print>
#include <string_view>

namespace ok
{
  object::object(uint32_t p_type, class_object* p_class, object*& p_objects_list, bool is_instance, bool is_class)
      :
#if defined(PARANOID)
        type(p_type),
#else
        type(p_type & 0x00ffffff),
#endif
        class_(p_class)
  {
    // next = get_g_vm()->get_objects_list();
    // get_g_vm()->get_objects_list() = this;
    next = p_objects_list;
    p_objects_list = this;
    set_instance(is_instance);
    set_class(is_class);

#if defined(PARANOID)
    set_marked(false);
#endif
  }

  string_object::string_object(const std::string_view p_src, class_object* p_string_class, object*& p_objects_list)
      : up(object_type::obj_string, p_string_class, p_objects_list)
  {
    length = p_src.size();
    chars = new char[p_src.size() + 1];
    strncpy(chars, p_src.data(), length);
    chars[length] = '\0';
    hash_code = hash(chars);
  }

  // string_object::string_object(std::span<std::string_view> p_srcs) : string_object()
  //{
  //   hash_code = hash(chars);
  // }

  string_object::~string_object()
  {
    delete[] chars;
    chars = nullptr;
  }

  template <>
  string_object*
  string_object::create(const std::string_view p_src, class_object* p_string_class, object*& p_objects_list)
  {
    auto& interned_strings = get_g_vm()->get_interned_strings();
    auto interned = interned_strings.get(p_src);
    if(interned != nullptr)
      return interned;

    return interned_strings.set(p_src);
  }

  template <>
  object* string_object::create(const std::string_view p_src, class_object* p_string_class, object*& p_objects_list)
  {
    return (object*)create<string_object>(p_src, p_string_class, p_objects_list);
  }

  template <>
  string_object*
  string_object::create(const std::span<std::string_view> p_srcs, class_object* p_string_class, object*& p_objects_list)
  {
    // TODO(Qais): double copy new ctor and method in interned store will mitigate that
    auto length = std::accumulate(
        p_srcs.begin(), p_srcs.end(), size_t{0}, [](const auto acc, const auto& entry) { return acc + entry.size(); });

    auto chars = new char[length + 1];
    auto curr = chars;
    for(auto src : p_srcs)
    {
      strncpy(curr, src.data(), src.size());
      curr += src.size();
    }
    chars[length] = '\0';
    auto& interned_strings = get_g_vm()->get_interned_strings();
    auto interned = interned_strings.get({chars, length});
    if(interned != nullptr)
      return interned;
    return interned_strings.set({chars, length});
  }

  template <>
  object*
  string_object::create(const std::span<std::string_view> p_srcs, class_object* p_string_class, object*& p_objects_list)
  {
    return (object*)create<string_object>(p_srcs, p_string_class, p_objects_list);
  }

  // string_object::string_object() : up(object_type::obj_string)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_string),
  //             string_object::equal},
  //         {_make_object_key(operator_type::op_plus, value_type::object_val, object_type::obj_string),
  //          string_object::plus}};
  //     ops.binary_infix.register_operations(op_fcn);

  //     ops.print_function = string_object::print;
  //     return true;
  //   }();
  // }

  native_method_return_type string_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_STRING_OBJECT(other)) [[likely]]
    {
      p_vm->stack_push(value_t{OK_VALUE_AS_STRING_OBJECT(p_this) == OK_VALUE_AS_STRING_OBJECT(other)});
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type string_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_STRING_OBJECT(other)) [[likely]]
    {
      p_vm->stack_push(value_t{OK_VALUE_AS_STRING_OBJECT(p_this) != OK_VALUE_AS_STRING_OBJECT(other)});
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type string_object::plus(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_STRING_OBJECT(other)) [[likely]]
    {
      const auto this_string = OK_VALUE_AS_STRING_OBJECT(p_this);
      const auto other_string = OK_VALUE_AS_STRING_OBJECT(other);
      std::array<std::string_view, 2> arr = {std::string_view{this_string->chars, this_string->length},
                                             {other_string->chars, other_string->length}};
      p_vm->stack_push(value_t{copy{create(arr, this_string->up.class_, p_vm->get_objects_list())}});
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type string_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();

    const auto this_string = OK_VALUE_AS_STRING_OBJECT(p_this);
    std::print("{}", std::string_view{this_string->chars, this_string->length});
    return {.code = value_error_code::ok};
  }

  struct string_meta_class
  {
    static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc)
    {
      // const auto this_ = p_vm->stack_top(p_argc);
      const auto arg = p_vm->stack_top(0);

      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(p_this);
      if(p_argc != 1 || (!OK_IS_VALUE_OBJECT(arg) && !OK_IS_VALUE_STRING_OBJECT(arg)))
      {
        return {.code = value_error_code::arguments_mismatch};
      }
      // const std::string_view p_src, class_object* p_string_class, object*& p_objects_list

      auto arg_str = OK_VALUE_AS_STRING_OBJECT(arg);
      const auto str = new_tobject<string_object>(std::string_view{arg_str->chars, arg_str->length},
                                                  OK_VALUE_AS_CLASS_OBJECT(p_this),
                                                  p_vm->get_objects_list());
      p_vm->stack_top(p_argc) = value_t{copy{(object*)str}};

      return {.code = value_error_code::ok};
    }
  };

  function_object::function_object(uint8_t p_arity,
                                   string_object* p_name,
                                   class_object* p_function_class,
                                   object*& p_objects_list)
      : up(object_type::obj_function, p_function_class, p_objects_list)
  {
    name = p_name;
  }

  // function_object::function_object() : up(object_type::obj_function)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_function),
  //             function_object::equal}};
  //     ops.binary_infix.register_operations(op_fcn);
  //     ops.call_function = function_object::call;
  //     ops.print_function = function_object::print;
  //     return true;
  //   }();
  // }

  function_object::~function_object()
  {
  }

  native_method_return_type function_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_FUNCTION_OBJECT(other)) [[likely]]
    {
      p_vm->stack_push(equal_impl(OK_VALUE_AS_FUNCTION_OBJECT(p_this), OK_VALUE_AS_FUNCTION_OBJECT(other)));
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type function_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_FUNCTION_OBJECT(other)) [[likely]]
    {
      p_vm->stack_push(equal_impl(OK_VALUE_AS_FUNCTION_OBJECT(p_this), OK_VALUE_AS_FUNCTION_OBJECT(other), false));
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  value_t function_object::equal_impl(function_object* p_this, function_object* p_other, bool p_equal)
  {
    return value_t{p_this->name == p_other->name && p_equal};
    /*p_this->arity == p_other->arity &&*/ // no function overloading yet!
  }

  native_method_return_type function_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();
    const auto this_function = OK_VALUE_AS_FUNCTION_OBJECT(p_this);
    std::print("<fu {}>", std::string_view{this_function->name->chars, this_function->name->length});
    return {.code = value_error_code::ok};
    // return {.return_type = native_method_return_type::rt_print, .return_value = {.other = nullptr}};
  }

  // std::expected<std::optional<call_frame>, value_error> function_object::call(value_t p_this, uint8_t p_argc)
  // {
  //   auto this_function = OK_VALUE_AS_FUNCTION_OBJECT(p_this);
  //   if(p_argc != this_function->arity)
  //   {
  //     return std::unexpected(value_error::arguments_mismatch);
  //   }
  //   auto vm = get_g_vm();
  //   auto frame = call_frame{this_function,
  //                           this_function->associated_chunk.code.data(),
  //                           vm->frame_stack_top() - p_argc - 1,
  //                           vm->frame_stack_top() - p_argc - 1};
  //   return frame;
  // }

  template <typename T>
  T* function_object::create(uint8_t p_arity,
                             string_object* p_name,
                             class_object* p_function_class,
                             object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* function_object::create<object>(uint8_t p_arity,
                                          string_object* p_name,
                                          class_object* p_function_class,
                                          object*& p_objects_list)
  {
    auto fo = new function_object(p_arity, p_name, p_function_class, p_objects_list);
    return (object*)fo;
  }

  template <>
  function_object* function_object::create<function_object>(uint8_t p_arity,
                                                            string_object* p_name,
                                                            class_object* p_function_class,
                                                            object*& p_objects_list)
  {
    auto fo = new function_object(p_arity, p_name, p_function_class, p_objects_list);
    return fo;
  }

  // native_function_object::native_function_object(native_function p_function,
  //                                                class_object* p_native_function_class,
  //                                                object*& p_objects_list)
  //     : up(object_type::obj_native_function, p_native_function_class, p_objects_list)
  // {
  //   function = p_function;
  // }

  // native_function_object::~native_function_object()
  // {
  // }

  // native_method_return_type native_function_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 1);
  //   const auto other = p_argv[0];

  //   const auto other_native_function = OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(other);
  //   const auto this_native_function = OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(p_this);
  //   return {.return_type = native_method_return_type::rt_operations,
  //           .return_value = {.value = value_t{this_native_function->function == other_native_function->function}}};
  // }

  // native_method_return_type
  // native_function_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 1);
  //   const auto other = p_argv[0];

  //   const auto other_native_function = OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(other);
  //   const auto this_native_function = OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(p_this);
  //   return {.return_type = native_method_return_type::rt_operations,
  //           .return_value = {.value = value_t{this_native_function->function != other_native_function->function}}};
  // }

  // std::expected<value_t, value_error> native_function_object::equal_bound_method(value_t p_this, value_t
  // p_bound_method)
  // {
  //   return value_t{false};
  // }

  // std::expected<value_t, value_error> native_function_object::bang_equal_bound_method(value_t p_this,
  //                                                                                     value_t p_bound_method)
  // {
  //   return value_t{false};
  // }

  // std::expected<value_t, value_error> native_function_object::equal_closure(value_t p_this, value_t p_closure)
  // {
  //   return value_t{false};
  // }

  // std::expected<value_t, value_error> native_function_object::bang_equal_closure(value_t p_this, value_t p_closure)
  // {
  //   return value_t{false};
  // }

  // native_method_return_type native_function_object::print(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 0);
  //   std::print("<native fu {:p}>", (void*)OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(p_this));
  //   return {.return_type = native_method_return_type::rt_print, .return_value.other = nullptr};
  // }

  // native_method_return_type native_function_object::call(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 1);
  //   const auto argc = (uint8_t)OK_VALUE_AS_NUMBER(p_argv[0]);

  //   auto this_native_function = OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(p_this);
  //   auto ret = this_native_function->function(p_vm, argc, p_vm->stack_at(p_vm->frame_stack_top() - argc));

  //   p_vm->get_current_call_frame().top = p_vm->frame_stack_top() - argc - 1;
  //   p_vm->stack_resize(p_vm->get_current_call_frame().top);
  //   if(ret.is_error)
  //   {
  //     p_vm->stack_push(value_t{});
  //     return {.return_type = native_method_return_type::rt_error, .return_value.error = ret.error_return};
  //   }
  //   p_vm->stack_push(ret.normal_return);
  //   return {.return_type = native_method_return_type::rt_function_call, .return_value.frame = {}};
  // }

  // // native_function_object::native_function_object() : up(object_type::obj_native_function)
  // // {
  // //   [[maybe_unused]] static const bool _ = [this] -> bool
  // //   {
  // //     auto _vm = get_g_vm();
  // //     auto& ops = _vm->register_object_operations(up.get_type());
  // //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  // //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  // //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_native_function),
  // //             native_function_object::equal}};
  // //     ops.binary_infix.register_operations(op_fcn);
  // //     ops.call_function = native_function_object::call;
  // //     ops.print_function = native_function_object::print;
  // //     return true;
  // //   }();
  // // }

  // template <typename T>
  // T* native_function_object::create(native_function p_function,
  //                                   class_object* p_native_function_class,
  //                                   object*& p_objects_list)
  // {
  //   static_assert(false, "type mismatch");
  // }

  // template <>
  // object* native_function_object::create<object>(native_function p_function,
  //                                                class_object* p_native_function_class,
  //                                                object*& p_objects_list)
  // {
  //   auto fo = new native_function_object(p_function, p_native_function_class, p_objects_list);
  //   return (object*)fo;
  // }

  // template <>
  // native_function_object* native_function_object::create<native_function_object>(native_function p_function,
  //                                                                                class_object*
  //                                                                                p_native_function_class, object*&
  //                                                                                p_objects_list)
  // {
  //   auto fo = new native_function_object(p_function, p_native_function_class, p_objects_list);
  //   return fo;
  // }

  // native_method_object::native_method_object(native_method p_native_method,
  //                                            class_object* p_native_method_class,
  //                                            object*& p_objects_list)
  //     : up(object_type::obj_native_method, p_native_method_class, p_objects_list)
  // {
  //   method = p_native_method;
  // }

  // native_method_object::~native_method_object()
  // {
  // }

  // native_method_return_type native_method_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 1);
  //   const auto other = p_argv[0];

  //   const auto other_native_method = OK_VALUE_AS_NATIVE_METHOD_OBJECT(other);
  //   const auto this_native_method = OK_VALUE_AS_NATIVE_METHOD_OBJECT(p_this);
  //   return {.return_type = native_method_return_type::rt_operations,
  //           .return_value = {.value = value_t{this_native_method->method == other_native_method->method}}};
  // }

  // native_method_return_type native_method_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t*
  // p_argv)
  // {
  //   ASSERT(p_argc == 1);
  //   const auto other = p_argv[0];

  //   const auto other_native_method = OK_VALUE_AS_NATIVE_METHOD_OBJECT(other);
  //   const auto this_native_method = OK_VALUE_AS_NATIVE_METHOD_OBJECT(p_this);
  //   return {.return_type = native_method_return_type::rt_operations,
  //           .return_value = {.value = value_t{this_native_method->method != other_native_method->method}}};
  // }

  // std::expected<value_t, value_error> native_method_object::equal_bound_method(value_t p_this, value_t
  // p_bound_method)
  // {
  //   return value_t{false};
  // }

  // std::expected<value_t, value_error> native_method_object::bang_equal_bound_method(value_t p_this,
  //                                                                                   value_t p_bound_method)
  // {
  //   return value_t{false};
  // }

  // std::expected<value_t, value_error> native_method_object::equal_closure(value_t p_this, value_t p_closure)
  // {
  //   return value_t{false};
  // }

  // std::expected<value_t, value_error> native_method_object::bang_equal_closure(value_t p_this, value_t p_closure)
  // {
  //   return value_t{false};
  // }

  // native_method_return_type native_method_object::print(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 0);
  //   std::print("<native method {:p}>", (void*)OK_VALUE_AS_NATIVE_METHOD_OBJECT(p_this));
  //   return {.return_type = native_method_return_type::rt_print, .return_value.other = nullptr};
  // }

  // native_method_return_type native_method_object::call(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv)
  // {
  //   ASSERT(p_argc == 3);
  //   const auto that = p_argv[0];
  //   const auto that_argc = (uint8_t)OK_VALUE_AS_NUMBER(p_argv[1]);
  //   const auto that_argv = (value_t*)(void*)OK_VALUE_AS_OBJECT(p_argv[2]); // man this is so fucking unsafe!

  //   auto this_native_method = OK_VALUE_AS_NATIVE_METHOD_OBJECT(p_this);
  //   auto ret = this_native_method->method(p_vm, that, that_argc, that_argv);

  //   if(ret.return_type == native_method_return_type::rt_function_call)
  //   {
  //     p_vm->get_current_call_frame().top = p_vm->frame_stack_top() - that_argc - 1;
  //     p_vm->stack_resize(p_vm->get_current_call_frame().top);
  //   }
  //   return ret;
  //   // if(ret.is_error)
  //   // {
  //   //   p_vm->stack_push(value_t{});
  //   //   return {.return_type = native_method_return_type::rt_error, .return_value.error = ret.error_return};
  //   // }
  //   // p_vm->stack_push(ret.normal_return);
  //   // return {.return_type = native_method_return_type::rt_function_call, .return_value.frame = {}};
  // }

  // // native_function_object::native_function_object() : up(object_type::obj_native_function)
  // // {
  // //   [[maybe_unused]] static const bool _ = [this] -> bool
  // //   {
  // //     auto _vm = get_g_vm();
  // //     auto& ops = _vm->register_object_operations(up.get_type());
  // //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  // //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  // //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_native_function),
  // //             native_function_object::equal}};
  // //     ops.binary_infix.register_operations(op_fcn);
  // //     ops.call_function = native_function_object::call;
  // //     ops.print_function = native_function_object::print;
  // //     return true;
  // //   }();
  // // }

  // template <typename T>
  // T* native_method_object::create(native_method p_native_method,
  //                                 class_object* p_native_method_class,
  //                                 object*& p_objects_list)
  // {
  //   static_assert(false, "type mismatch");
  // }

  // template <>
  // object* native_method_object::create<object>(native_method p_native_method,
  //                                              class_object* p_native_method_class,
  //                                              object*& p_objects_list)
  // {
  //   auto nmo = new native_method_object(p_native_method, p_native_method_class, p_objects_list);
  //   return (object*)nmo;
  // }

  // template <>
  // native_method_object* native_method_object::create<native_method_object>(native_method p_native_method,
  //                                                                          class_object* p_native_method_class,
  //                                                                          object*& p_objects_list)
  // {
  //   auto nmo = new native_method_object(p_native_method, p_native_method_class, p_objects_list);
  //   return nmo;
  // }

  closure_object::closure_object(function_object* p_function, class_object* p_upvalue_class, object*& p_objects_list)
      : up(object_type::obj_closure, p_upvalue_class, p_objects_list)
  {
    function = p_function;
    for(uint32_t i = 0; i < p_function->upvalues; ++i)
      upvalues.emplace_back(nullptr);
  }

  // closure_object::closure_object() : up(object_type::obj_closure)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_native_function),
  //             closure_object::equal}};
  //     ops.binary_infix.register_operations(op_fcn);
  //     ops.call_function = closure_object::call;
  //     ops.print_function = closure_object::print;
  //     return true;
  //   }();
  // }

  closure_object::~closure_object()
  {
  }

  native_method_return_type closure_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    return equal_impl(p_vm, p_this, other);
  }

  native_method_return_type closure_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    return bang_equal_impl(p_vm, p_this, other);
  }

  std::expected<value_t, value_error_code> closure_object::equal_native_function(value_t p_this,
                                                                                 value_t p_native_function)
  {
    return value_t{false};
  }

  std::expected<value_t, value_error_code> closure_object::bang_equal_native_function(value_t p_this,
                                                                                      value_t p_native_function)
  {
    return value_t{false};
  }

  std::expected<value_t, value_error_code> closure_object::equal_bound_method(value_t p_this, value_t p_bound_method)
  {
    return value_t{false};
  }

  std::expected<value_t, value_error_code> closure_object::bang_equal_bound_method(value_t p_this,
                                                                                   value_t p_bound_method)
  {
    return value_t{false};
  }

  native_method_return_type closure_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();
    return print_impl(p_vm, p_this);
    // return {.return_type = native_method_return_type::rt_print, .return_value.other = nullptr};
  }

  // this is a call operation on closure that means the vm will build the call for it
  // so arguments shall be in the expected order, also this argc is directly for the call of the closure
  // stack: [ [this closure], <[args...p_argc]> ]
  native_method_return_type closure_object::call(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    // const auto this_ = p_vm->stack_top(p_argc);
    return call_impl(p_vm, p_argc, p_this);
  }

  native_method_return_type closure_object::equal_impl(vm* p_vm, value_t p_this, value_t p_other)
  {
    if(OK_IS_VALUE_CLOSURE_OBJECT(p_other)) [[likely]]
    {
      const auto other_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_other);
      const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
      p_vm->stack_push(function_object::equal_impl(this_closure->function, other_closure->function));
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type closure_object::bang_equal_impl(vm* p_vm, value_t p_this, value_t p_other)
  {
    if(OK_IS_VALUE_CLOSURE_OBJECT(p_other)) [[likely]]
    {
      const auto other_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_other);
      const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
      p_vm->stack_push(function_object::equal_impl(this_closure->function, other_closure->function, false));
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type closure_object::call_impl(vm* p_vm, uint8_t p_argc, value_t p_this)
  {
    const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
    if(p_argc != this_closure->function->arity)
    {
      // clean the stack
      p_vm->stack_pop(p_argc + 1); // pop all args + the closure
      p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
      return {.code = value_error_code::arguments_mismatch, .has_payload = true};
    }

    const auto frame = call_frame{this_closure,
                                  this_closure->function->associated_chunk.code.data(),
                                  p_vm->frame_stack_top() - p_argc - 1,
                                  p_vm->frame_stack_top() - p_argc - 1};
    auto res = p_vm->push_call_frame(frame);
    if(!res)
    {
      return {.code = value_error_code::stackoverflow,
              .has_payload = false,
              .recoverable = false}; // irrecoverable no payload (vm already pushes a runtime error)
    }
    return {.code = value_error_code::ok};
  }

  native_method_return_type closure_object::print_impl(vm* p_vm, value_t p_this)
  {
    const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
    std::print("<closure {:p}>", (void*)this_closure);
    return {.code = value_error_code::ok};
  }

  template <typename T>
  T* closure_object::create(function_object* p_function, class_object* p_closure_class, object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object*
  closure_object::create<object>(function_object* p_function, class_object* p_closure_class, object*& p_objects_list)
  {
    auto fo = new closure_object(p_function, p_closure_class, p_objects_list);
    return (object*)fo;
  }

  template <>
  closure_object* closure_object::create<closure_object>(function_object* p_function,
                                                         class_object* p_closure_class,
                                                         object*& p_objects_list)
  {
    auto fo = new closure_object(p_function, p_closure_class, p_objects_list);
    return fo;
  }

  struct closure_meta_class
  {
    static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc)
    {
      // const auto this_ = p_vm->stack_top(p_argc);
      const auto arg = p_vm->stack_top(0);

      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(p_this);
      if(p_argc != 1 || (!OK_IS_VALUE_OBJECT(arg) && !OK_IS_VALUE_CLOSURE_OBJECT(arg)))
      {
        return {.code = value_error_code::arguments_mismatch};
      }
      // const std::string_view p_src, class_object* p_string_class, object*& p_objects_list

      auto arg_clo = OK_VALUE_AS_CLOSURE_OBJECT(arg);
      const auto clo = arg_clo;
      p_vm->stack_top(p_argc) = value_t{copy{(object*)clo}};

      return {.code = value_error_code::ok};
    }
  };

  upvalue_object::upvalue_object(value_t* slot, class_object* p_upvalue_class, object*& p_objects_list)
      : up(object_type::obj_upvalue, p_upvalue_class, p_objects_list)
  {
    location = slot;
  }

  upvalue_object::~upvalue_object()
  {
  }

  template <typename T>
  T* upvalue_object::create(value_t* p_slot, class_object* p_upvalue_class, object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* upvalue_object::create(value_t* p_slot, class_object* p_upvalue_class, object*& p_objects_list)
  {
    return (object*)new upvalue_object(p_slot, p_upvalue_class, p_objects_list);
  }

  template <>
  upvalue_object* upvalue_object::create(value_t* p_slot, class_object* p_upvalue_class, object*& p_objects_list)
  {
    return new upvalue_object(p_slot, p_upvalue_class, p_objects_list);
  }

  // upvalue_object::upvalue_object() : up(object_type::obj_upvalue)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     ops.print_function = upvalue_object::print;
  //     return true;
  //   }();
  // }

  native_method_return_type upvalue_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();
    std::print("upvalue: {:p}", (void*)OK_VALUE_AS_UPVALUE_OBJECT(p_this));
    return {.code = value_error_code::ok};
  }

  class_object::class_object(string_object* p_name,
                             uint32_t p_class_type,
                             class_object* p_class_class,
                             object*& p_objects_list)
      : up(p_class_type, p_class_class, p_objects_list, false, true)
  {
    name = p_name;
    if(p_class_class != nullptr)
    {
      object_inherit(p_class_class, this);
    }
  }

  class_object::~class_object()
  {
  }

  // class_object::class_object() : up(object_type::obj_class)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_class),
  //             class_object::equal}};
  //     ops.binary_infix.register_operations(op_fcn);
  //     ops.call_function = class_object::call;
  //     ops.print_function = class_object::print;
  //     return true;
  //   }();
  // }

  native_method_return_type class_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_CLASS_OBJECT(other)) [[likely]]
    {
      const auto other_class = OK_VALUE_AS_CLASS_OBJECT(other);
      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(p_this);
      p_vm->stack_push(
          value_t{this_class == other_class &&
                  this_class->name == other_class->name}); // mostly redundant extra name check (it will short circuit)
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type class_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_CLASS_OBJECT(other)) [[likely]]
    {
      const auto other_class = OK_VALUE_AS_CLASS_OBJECT(other);
      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(p_this);
      p_vm->stack_push(
          value_t{this_class != other_class ||
                  this_class->name != other_class->name}); // mostly redundant extra name check (it will short circuit)
      return {.code = value_error_code::ok};
    }

    p_vm->stack_push(value_t{}); // TODO(Qais): proper error payload
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type class_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();
    const auto this_class = OK_VALUE_AS_CLASS_OBJECT(p_this);
    std::print("{}", std::string_view{this_class->name->chars, this_class->name->length});
    return {.code = value_error_code::ok};
  }

  native_method_return_type class_object::call(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    // const auto this_ = p_vm->stack_top(p_argc);
    const auto this_class = OK_VALUE_AS_CLASS_OBJECT(p_this);
    // yo
    const auto instance = new_tobject<instance_object>(this_class->up.get_type(), this_class, p_vm->get_objects_list());
    register_base_methods((object*)instance);
    p_vm->stack_top(p_argc) = value_t{copy{(object*)instance}};

    // auto key = make_methods_key(p_vm->get_statics().init_string, p_argc);
    const auto it = instance->up.class_->methods.find(
        p_vm->get_statics().init_string); // TODO(Qais): use the right api (ctors are not in the methods table)
    if(instance->up.class_->methods.end() != it)
    {
      auto res = p_vm->call_value(p_argc, it->second);
      if(!res)
      {
        return {.code = value_error_code::internal_propagated_error};
      }
    }

    return {.code = value_error_code::ok};
  }

  template <typename T>
  T* class_object::create(string_object* p_name,
                          uint32_t p_class_type,
                          class_object* p_class_class,
                          object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* class_object::create<object>(string_object* p_name,
                                       uint32_t p_class_type,
                                       class_object* p_class_class,
                                       object*& p_objects_list)
  {
    auto co = new class_object(p_name, p_class_type, p_class_class, p_objects_list);
    return (object*)co;
  }

  template <>
  class_object* class_object::create<class_object>(string_object* p_name,
                                                   uint32_t p_class_type,
                                                   class_object* p_class_class,
                                                   object*& p_objects_list)
  {
    auto co = new class_object(p_name, p_class_type, p_class_class, p_objects_list);
    return co;
  }

  instance_object::instance_object(uint32_t p_instance_type, class_object* p_class, object*& p_objects_list)
      : up(p_instance_type, p_class, p_objects_list, true)
  {
  }

  instance_object::~instance_object()
  {
    // const auto _vm = get_g_vm();
    // const auto dtor = up.class_->specials.operations[overridable_operator_type::oot_dtor];
    // if(!OK_IS_VALUE_NULL(dtor))
    {
      // TODO(Qais): don't use ctor/dtor
      //[[maybe_unused]] const auto res = _vm->call_value(0, dtor);
      // if(!res)
      //{
      // std::unexpected{value_error::internal_propagated_error};
      //}
    }
  }

  // instance_object::instance_object() : up(object_type::obj_instance)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     // std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  //     //     std::pair<uint32_t, vm::operation_function_infix_binary>{
  //     //         _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_class),
  //     //         class_object::equal}};
  //     // ops.binary_infix.register_operations(op_fcn);
  //     // ops.call_function = class_object::call;
  //     ops.print_function = instance_object::print;
  //     return true;
  //   }();
  // }

  native_method_return_type instance_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();
    const auto this_instance = OK_VALUE_AS_INSTANCE_OBJECT(p_this);
    std::print("instance of: {}",
               std::string_view{this_instance->up.class_->name->chars, this_instance->up.class_->name->length});
    return {.code = value_error_code::ok};
    // return {.return_type = native_method_return_type::rt_print, .return_value.other = nullptr};
  }

  template <typename T>
  T* instance_object::create(uint32_t p_instance_type, class_object* p_class, object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* instance_object::create<object>(uint32_t p_instance_type, class_object* p_class, object*& p_objects_list)
  {
    auto io = new instance_object(p_instance_type, p_class, p_objects_list);
    return (object*)io;
  }

  template <>
  instance_object*
  instance_object::create<instance_object>(uint32_t p_instance_type, class_object* p_class, object*& p_objects_list)
  {
    auto io = new instance_object(p_instance_type, p_class, p_objects_list);
    return io;
  }

  bound_method_object::bound_method_object(value_t p_receiver,
                                           value_t p_method,
                                           class_object* p_class,
                                           object*& p_objects_list)
      : up(object_type::obj_bound_method, p_class, p_objects_list)
  {
    receiver = p_receiver;
    method = p_method;
  }

  bound_method_object::~bound_method_object()
  {
  }

  // bound_method_object::bound_method_object() : up(object_type::obj_bound_method)
  // {
  //   [[maybe_unused]] static const bool _ = [this] -> bool
  //   {
  //     auto _vm = get_g_vm();
  //     auto& ops = _vm->register_object_operations(up.get_type());
  //     std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
  //         std::pair<uint32_t, vm::operation_function_infix_binary>{
  //             _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_class),
  //             bound_method_object::equal}};
  //     ops.binary_infix.register_operations(op_fcn);
  //     ops.call_function = bound_method_object::call;
  //     ops.print_function = bound_method_object::print;
  //     return true;
  //   }();
  // }

  native_method_return_type bound_method_object::print(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    // const auto this_ = p_vm->stack_pop();
    auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(p_this);
    // when ugly meets slow
    // value_t val{copy{this_bound_method->method}};
    // return closure_object::print_impl(p_vm, val);
    std::print("<bound method: {:p}>", (void*)this_bound_method);
    return {.code = value_error_code::ok};
  }

  native_method_return_type bound_method_object::equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_BOUND_METHOD_OBJECT(other)) [[likely]]
    {
      const auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(p_this);
      const auto other_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(other);
      // when very ugly meets slow
      const auto method = other_bound_method->method;
      // value_t argv[2] = {value_t{copy{this_bound_method->method}}, method};

      // auto closure_equal =
      //     closure_object::equal_impl(p_vm, value_t{copy{this_bound_method->method}}, method); // looks bad!
      // if(closure_equal.code != value_error_code::ok)
      //   return closure_equal;

      // TODO(Qais): ts pmo
      p_vm->stack_top(0) =
          value_t{this_bound_method->receiver.type == other_bound_method->receiver.type &&
                  OK_VALUE_AS_OBJECT(this_bound_method->receiver) == OK_VALUE_AS_OBJECT(other_bound_method->receiver) &&
                  this_bound_method &&
                  other_bound_method}; // i mean come on, if that other line looks bad then how does this line look like
      return {.code = value_error_code::ok};
    }
    p_vm->stack_push(value_t{});
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  native_method_return_type bound_method_object::bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto other = p_vm->stack_pop();
    // const auto this_ = p_vm->stack_pop();
    if(OK_IS_VALUE_BOUND_METHOD_OBJECT(other)) [[likely]]
    {
      const auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(p_this);
      const auto other_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(other);
      // when very ugly meets slow
      // const auto method = value_t{copy{other_bound_method->method}};
      // value_t argv[2] = {value_t{copy{this_bound_method->method}}, method};
      // auto closure_equal =
      //    closure_object::equal_impl(p_vm, value_t{copy{this_bound_method->method}}, method); // looks bad!
      // if(closure_equal.code != value_error_code::ok)
      //  return closure_equal;

      // TODO(Qais): ts pmo
      p_vm->stack_top(0) = value_t{
          !(this_bound_method->receiver.type == other_bound_method->receiver.type &&
            OK_VALUE_AS_OBJECT(this_bound_method->receiver) == OK_VALUE_AS_OBJECT(other_bound_method->receiver) &&
            this_bound_method &&
            other_bound_method)}; // i mean come on, if that other line looks bad then what do this line look like
      return {.code = value_error_code::ok};
    }
    p_vm->stack_push(value_t{});
    return {.code = value_error_code::undefined_operation, .has_payload = true};
  }

  std::expected<value_t, value_error_code> bound_method_object::equal_closure(value_t p_this, value_t p_closure)
  {
    return value_t{false};
  }

  std::expected<value_t, value_error_code> bound_method_object::bang_equal_closure(value_t p_this, value_t p_closure)
  {
    return value_t{false};
  }

  std::expected<value_t, value_error_code> bound_method_object::equal_native_function(value_t p_this,
                                                                                      value_t p_native_function)
  {
    return value_t{false};
  }

  std::expected<value_t, value_error_code> bound_method_object::bang_equal_native_function(value_t p_this,
                                                                                           value_t p_native_function)
  {
    return value_t{false};
  }

  native_method_return_type bound_method_object::call(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    // const auto this_ = p_vm->stack_top(p_argc);
    const auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(p_this);
    p_vm->stack_top(p_argc) = this_bound_method->receiver;
    // value_t argv[2] = {value_t{copy{this_bound_method->method}}, p_argv[1]};
    // return closure_object::call_impl(p_vm, p_argc, value_t{copy{this_bound_method->method}});
    auto method = this_bound_method->method;
    if(OK_IS_VALUE_NULL(method))
    {
      p_vm->stack_push(value_t{});
      return {.code = value_error_code::undefined_operation, .has_payload = true};
    }
    p_vm->stack_top(p_argc) = method; // put the wrapped method on the stack and perform normal call
    auto res = p_vm->call_value(p_argc, method);
    if(!res)
    {
      return {.code = value_error_code::internal_propagated_error};
    }
    return {.code = value_error_code::ok};
  }

  template <typename T>
  T* bound_method_object::create(value_t p_receiver, value_t p_method, class_object* p_class, object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* bound_method_object::create<object>(value_t p_receiver,
                                              value_t p_method,
                                              class_object* p_class,
                                              object*& p_objects_list)
  {
    auto bmo = new bound_method_object(p_receiver, p_method, p_class, p_objects_list);
    return (object*)bmo;
  }

  template <>
  bound_method_object* bound_method_object::create<bound_method_object>(value_t p_receiver,
                                                                        value_t p_method,
                                                                        class_object* p_class,
                                                                        object*& p_objects_list)
  {
    auto bmo = new bound_method_object(p_receiver, p_method, p_class, p_objects_list);
    return bmo;
  }

  void delete_object(object* p_object)
  {
    if(p_object->is_instance())
    {
      delete(instance_object*)p_object;
      goto OUT;
    }
    if(p_object->is_class())
    {
      delete(class_object*)p_object;
      goto OUT;
    }
    switch(p_object->get_type())
    {
    case object_type::obj_string:
      delete(string_object*)p_object;
      break;
    case object_type::obj_function:
      delete(function_object*)p_object;
      break;
    // case object_type::obj_native_function:
    //   delete(native_function_object*)p_object;
    //   break;
    case object_type::obj_closure:
      delete(closure_object*)p_object;
      break;
    case object_type::obj_upvalue:
      delete(upvalue_object*)p_object;
      break;
      // case object_type::obj_class:
      //   delete(class_object*)p_object;
      break;
    // case object_type::obj_instance:
    //   delete(instance_object*)p_object;
    //   break;
    case object_type::obj_bound_method:
      delete(bound_method_object*)p_object;
      break;
    // case object_type::obj_native_method:
    //   delete(native_method_object*)p_object;
    //   break;
    default:
      ASSERT(false);
      break; // TODO(Qais): error
    }
  OUT:
    p_object = nullptr;
  }

  void object_inherit(class_object* p_super, class_object* p_sub)
  {
    // TODO(Qais): mro and specials
    p_sub->methods.insert_range(p_super->methods);
    p_sub->specials.operations = p_super->specials.operations;
  }

  static class_object* register_string_class(object*& p_objects_list, class_object* p_class_class);

  static class_object* register_meta_class_class(object*& p_objects_list);
  static class_object* register_class_class(object*& p_objects_list, class_object* p_meta_class);
  static class_object*
  register_callable_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class);
  static class_object*
  register_function_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class);
  static class_object*
  register_native_function_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class);
  static class_object*
  register_native_method_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class);
  static class_object*
  register_closure_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class);
  static class_object*
  register_bound_method_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class);

  bool object_register_builtins(vm* p_vm)
  {
    p_vm->get_gc().pause();
    auto& objects = p_vm->get_objects_list();

    auto* meta_class_class = register_meta_class_class(objects);
    meta_class_class->up.class_ = meta_class_class;
    auto* class_class = register_class_class(objects, meta_class_class);

    auto* string_class = register_string_class(objects, class_class);
    p_vm->register_builtin((object*)string_class, object_type::obj_string, string_class->name);

    // update all strings in broken state
    for(auto interned : p_vm->get_interned_strings().underlying())
    {
      interned.second->up.class_ = string_class;
    }

    class_class->name = new_tobject<string_object>("class", string_class, objects);
    meta_class_class->name = new_tobject<string_object>("meta_class", string_class, objects);

    p_vm->register_builtin((object*)class_class, object_type::obj_class, class_class->name);

    auto* callable_class = register_callable_class(objects, string_class, meta_class_class);
    p_vm->register_builtin((object*)callable_class, object_type::obj_callable, callable_class->name);

    auto* function_class = register_function_class(objects, string_class, callable_class);
    p_vm->register_builtin((object*)function_class, object_type::obj_function, function_class->name);

    auto* closure_class = register_closure_class(objects, string_class, callable_class);
    p_vm->register_builtin((object*)closure_class, object_type::obj_closure, closure_class->name);

    p_vm->get_gc().resume();
    return true;
  }

  void register_base_methods(object* p_object)
  {
    auto& ops = p_object->class_->specials.operations;
    ops[overridable_operator_type::oot_print] = value_t{instance_object::print, false};
  }

  static class_object* register_meta_class_class(object*& p_objects_list)
  {
    auto* meta_class = new_tobject<class_object>(nullptr, object_type::obj_meta_class, nullptr, p_objects_list);
    auto& ops = meta_class->specials.operations;
    ops[overridable_operator_type::oot_binary_infix_equal] = value_t{class_object::equal, false};
    ops[overridable_operator_type::oot_binary_infix_bang_equal] = value_t{class_object::bang_equal, false};
    ops[overridable_operator_type::oot_print] = value_t{class_object::print, false};
    return meta_class;
  }

  static class_object* register_class_class(object*& p_objects_list, class_object* p_meta_class)
  {
    auto* class_class = new_tobject<class_object>(nullptr, object_type::obj_class, p_meta_class, p_objects_list);
    auto& ops = class_class->specials.operations;
    ops[overridable_operator_type::oot_unary_postfix_call] = value_t{class_object::call, false};
    return class_class;
  }

  class_object* register_string_class(object*& p_objects_list, class_object* p_class)
  {
    auto* string_meta_class = new_tobject<class_object>(nullptr, object_type::obj_meta_class, p_class, p_objects_list);
    auto* string_class = new_tobject<class_object>(nullptr, object_type::obj_string, string_meta_class, p_objects_list);

    auto* string_meta_name = new_tobject<string_object>("string_meta", string_class, p_objects_list);
    auto* string_class_name = new_tobject<string_object>("string", string_class, p_objects_list);
    string_meta_class->name = string_meta_name;
    string_class->name = string_class_name;

    auto& meta_ops = string_meta_class->specials.operations;
    meta_ops[overridable_operator_type::oot_unary_postfix_call] = value_t{string_meta_class::call, false};

    auto& ops = string_class->specials.operations;
    ops[overridable_operator_type::oot_binary_infix_plus] = value_t{string_object::plus, false};
    ops[overridable_operator_type::oot_binary_infix_equal] = value_t{string_object::equal, false};
    ops[overridable_operator_type::oot_binary_infix_bang_equal] = value_t{string_object::bang_equal, false};
    ops[overridable_operator_type::oot_print] = value_t{string_object::print, false};

    return string_class;
  }

  static class_object*
  register_callable_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class)
  {
    auto* callable_class_name = new_tobject<string_object>("callable", p_string, p_objects_list);
    auto* callable_class =
        new_tobject<class_object>(callable_class_name, object_type::obj_callable, p_class_class, p_objects_list);
    auto& ops = callable_class->specials.operations;
    ops[overridable_operator_type::oot_unary_postfix_call] = value_t{};
    return callable_class;
  }

  static class_object*
  register_function_class(object*& p_objects_list, class_object* p_string, class_object* p_callable_class)
  {
    auto* function_class_name = new_tobject<string_object>("function", p_string, p_objects_list);
    auto* function_class =
        new_tobject<class_object>(function_class_name, object_type::obj_function, p_callable_class, p_objects_list);

    auto& ops = function_class->specials.operations;
    ops[overridable_operator_type::oot_binary_infix_equal] = value_t{function_object::equal, false};
    ops[overridable_operator_type::oot_binary_infix_bang_equal] = value_t{function_object::bang_equal, false};
    ops[overridable_operator_type::oot_print] = value_t{function_object::print, false};

    return function_class;
  }

  static class_object*
  register_closure_class(object*& p_objects_list, class_object* p_string, class_object* p_callable_class)
  {
    auto* closure_class_name = new_tobject<string_object>("closure", p_string, p_objects_list);
    auto* closure_meta_class_name = new_tobject<string_object>("closure_meta", p_string, p_objects_list);

    auto* closure_meta_class = new_tobject<class_object>(
        closure_meta_class_name, object_type::obj_meta_class, p_callable_class, p_objects_list);
    auto* closure_class =
        new_tobject<class_object>(closure_class_name, object_type::obj_closure, closure_meta_class, p_objects_list);

    auto& meta_ops = closure_meta_class->specials.operations;
    meta_ops[overridable_operator_type::oot_unary_postfix_call] = value_t{closure_meta_class::call, false};

    auto& ops = closure_class->specials.operations;
    ops[overridable_operator_type::oot_binary_infix_equal] = value_t{closure_object::equal, false};
    ops[overridable_operator_type::oot_binary_infix_bang_equal] = value_t{closure_object::bang_equal, false};
    ops[overridable_operator_type::oot_unary_postfix_call] = value_t{closure_object::call, false};
    ops[overridable_operator_type::oot_print] = value_t{closure_object::print, false};
    ops[overridable_operator_type::oot_unary_postfix_call] = value_t{closure_object::call, false};

    return closure_class;
  }

  static class_object*
  register_bound_method_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class)
  {
    auto* bound_method_class_name = new_tobject<string_object>("bound_method", p_string, p_objects_list);
    auto* bound_method_class = new_tobject<class_object>(
        bound_method_class_name, object_type::obj_bound_method, p_class_class, p_objects_list);

    auto& ops = bound_method_class->specials.operations;
    ops[overridable_operator_type::oot_binary_infix_equal] = value_t{bound_method_object::equal, false};
    ops[overridable_operator_type::oot_binary_infix_bang_equal] = value_t{bound_method_object::bang_equal, false};
    ops[overridable_operator_type::oot_unary_postfix_call] = value_t{bound_method_object::call, false};
    ops[overridable_operator_type::oot_print] = value_t{bound_method_object::print, false};

    return bound_method_class;
  }
} // namespace ok