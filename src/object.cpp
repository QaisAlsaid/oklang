#include "object.hpp"
#include "chunk.hpp"
#include "constants.hpp"
#include "macros.hpp"
#include "operator.hpp"
#include "value.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <array>
#include <cstring>
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

  string_object::string_object(std::span<std::string_view> p_srcs,
                               class_object* p_string_class,
                               object*& p_objects_list)
      : up(object_type::obj_string, p_string_class, p_objects_list)
  {
    length = 0;
    for(auto src : p_srcs)
    {
      length += src.length();
    }
    chars = new char[length + 1];
    for(auto i = 0; auto src : p_srcs)
    {
      strncpy(chars + src.length() * i++, src.data(), src.length());
    }
    chars[length] = '\0';
    hash_code = hash(chars);
  }

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

  native_return_type string_object::equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_STRING_OBJECT(other)) [[likely]]
    {
      p_vm->return_value(value_t{OK_VALUE_AS_STRING_OBJECT(this_) == OK_VALUE_AS_STRING_OBJECT(other)});
      return {.code = native_return_code::nrc_return};
    }

    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid string '==', rhs is not of type string"}}}}};
  }

  native_return_type string_object::bang_equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_STRING_OBJECT(other)) [[likely]]
    {
      p_vm->return_value(value_t{OK_VALUE_AS_STRING_OBJECT(this_) != OK_VALUE_AS_STRING_OBJECT(other)});
      return {.code = native_return_code::nrc_return};
    }

    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid string '!=', rhs is not of type string"}}}}};
  }

  native_return_type string_object::plus(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_STRING_OBJECT(other)) [[likely]]
    {
      const auto this_string = OK_VALUE_AS_STRING_OBJECT(this_);
      const auto other_string = OK_VALUE_AS_STRING_OBJECT(other);
      std::array<std::string_view, 2> arr = {std::string_view{this_string->chars, this_string->length},
                                             {other_string->chars, other_string->length}};
      p_vm->return_value(value_t{copy{create(arr, this_string->up.class_, p_vm->get_objects_list())}});
      return {.code = native_return_code::nrc_return};
    }

    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid string '+', rhs is not of type string"}}}}};
  }

  native_return_type string_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    const auto this_string = OK_VALUE_AS_STRING_OBJECT(this_);
    std::print("{}", std::string_view{this_string->chars, this_string->length});
    return {.code = native_return_code::nrc_print_exit};
  }

  struct string_meta_class
  {
    static native_return_type call(vm* p_vm, value_t, uint8_t p_argc)
    {
      const auto this_ = p_vm->get_receiver();
      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(this_);
      const auto arg = p_vm->get_arg(0);

      if(p_argc != 1)
      {
        return {
            .code = native_return_code::nrc_error,
            .error{.code = value_error_code::arguments_mismatch,
                   .payload{value_t{std::format("invalid string construct, expected 1 argument got: {}", p_argc)}}}};
      }
      else if(!OK_IS_VALUE_OBJECT(arg) || !OK_IS_VALUE_STRING_OBJECT(arg))
      {
        return {
            .code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid string construct, argument is not of type string"}}}}};
      }

      auto arg_str = OK_VALUE_AS_STRING_OBJECT(arg);
      const auto str = new_tobject<string_object>(
          std::string_view{arg_str->chars, arg_str->length}, OK_VALUE_AS_CLASS_OBJECT(this_), p_vm->get_objects_list());

      p_vm->return_value(value_t{copy{(object*)str}});
      return {.code = native_return_code::nrc_return};
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

  function_object::~function_object()
  {
  }

  native_return_type function_object::equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_FUNCTION_OBJECT(other)) [[likely]]
    {
      p_vm->return_value(equal_impl(OK_VALUE_AS_FUNCTION_OBJECT(this_), OK_VALUE_AS_FUNCTION_OBJECT(other)));
      return {.code = native_return_code::nrc_return};
    }

    // TODO(Qais): i think those should be defined on callable
    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid '==' on function, rhs is not of type function"}}}}};
  }

  native_return_type function_object::bang_equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_FUNCTION_OBJECT(other)) [[likely]]
    {
      p_vm->return_value(equal_impl(OK_VALUE_AS_FUNCTION_OBJECT(this_), OK_VALUE_AS_FUNCTION_OBJECT(other), false));
      return {.code = native_return_code::nrc_return};
    }

    // TODO(Qais): i think those should be defined on callable
    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid '!=' on function, rhs is not of type function"}}}}};
  }

  native_return_type function_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    const auto this_function = OK_VALUE_AS_FUNCTION_OBJECT(this_);
    std::print("<fu {}>", std::string_view{this_function->name->chars, this_function->name->length});
    return {.code = native_return_code::nrc_print_exit};
  }

  value_t function_object::equal_impl(function_object* p_this, function_object* p_other, bool p_equal)
  {
    return value_t{p_this->name == p_other->name && p_equal};
    /*p_this->arity == p_other->arity &&*/ // no function overloading yet!
  }

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

  closure_object::closure_object(function_object* p_function, class_object* p_upvalue_class, object*& p_objects_list)
      : up(object_type::obj_closure, p_upvalue_class, p_objects_list)
  {
    function = p_function;
    for(uint32_t i = 0; i < p_function->upvalues; ++i)
      upvalues.emplace_back(nullptr);
  }

  closure_object::~closure_object()
  {
  }

  native_return_type closure_object::equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    return equality_impl(p_vm, this_, other, true);
  }

  native_return_type closure_object::bang_equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    return equality_impl(p_vm, this_, other, false);
  }

  native_return_type closure_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    return print_impl(p_vm, this_);
  }

  native_return_type closure_object::call(vm* p_vm, value_t p_this, uint8_t p_argc)
  {
    // only here use p_this, since closure may be called with a receiver other than closure_object instance
    return call_impl(p_vm, p_argc, p_this);
  }

  native_return_type closure_object::equality_impl(vm* p_vm, value_t p_this, value_t p_other, bool p_equals)
  {
    if(OK_IS_VALUE_CLOSURE_OBJECT(p_other)) [[likely]]
    {
      const auto other_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_other);
      const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
      p_vm->return_value(function_object::equal_impl(this_closure->function, other_closure->function, p_equals));
      return {.code = native_return_code::nrc_return};
    }

    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid '==' on closure, rhs is not of type closure"}}}}};
  }

  native_return_type closure_object::call_impl(vm* p_vm, uint8_t p_argc, value_t p_this)
  {
    const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
    if(p_argc != this_closure->function->arity)
    {
      return {.code = native_return_code::nrc_error,
              .error{.code = value_error_code::arguments_mismatch,
                     .payload{value_t{std::format("invalid call on closure, expected {} arguments, got: {}",
                                                  this_closure->function->arity,
                                                  p_argc)}}}};
    }

    const auto frame = call_frame{this_closure,
                                  this_closure->function->associated_chunk.code.data(),
                                  p_vm->frame_stack_top(),
                                  p_vm->frame_stack_top()};
    const auto res = p_vm->start_subcall(frame);
    if(!res)
    {
      return {.code = native_return_code::nrc_error, .error.code = value_error_code::internal_propagated_error};
    }

    return {.code = native_return_code::nrc_subcall};
  }

  native_return_type closure_object::print_impl(vm* p_vm, value_t p_this)
  {
    const auto this_closure = OK_VALUE_AS_CLOSURE_OBJECT(p_this);
    std::print("<closure {:p}>", (void*)this_closure);
    return {.code = native_return_code::nrc_print_exit};
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
    static native_return_type call(vm* p_vm, value_t, uint8_t p_argc)
    {
      const auto this_ = p_vm->get_receiver();
      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(this_);
      if(p_argc != 1)
      {
        return {
            .code = native_return_code::nrc_error,
            .error{.code = value_error_code::arguments_mismatch,
                   .payload{value_t{std::format("invalid closure construct, expected 1 arguments, got: {}", p_argc)}}}};
      }

      const auto arg = p_vm->get_arg(0);
      if(!OK_IS_VALUE_OBJECT(arg) || !OK_IS_VALUE_CLOSURE_OBJECT(arg))
      {
        return {
            .code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid closure construct, argument is not of type closure"}}}}};
      }

      p_vm->return_value(arg);
      return {.code = native_return_code::nrc_return};
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

  native_return_type upvalue_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    std::print("upvalue: {:p}", (void*)OK_VALUE_AS_UPVALUE_OBJECT(this_));
    return {.code = native_return_code::nrc_print_exit};
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

  class_object::class_object(string_object* p_name,
                             uint32_t p_class_type,
                             class_object* p_meta,
                             class_object* p_super,
                             object*& p_objects_list)
      : up(p_class_type, p_meta, p_objects_list, false, true)
  {
    name = p_name;
    if(p_super != nullptr)
    {
      object_inherit(p_super, this);
    }
  }

  class_object::~class_object()
  {
  }

  native_return_type class_object::equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_CLASS_OBJECT(other)) [[likely]]
    {
      const auto other_class = OK_VALUE_AS_CLASS_OBJECT(other);
      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(this_);
      p_vm->return_value(value_t{this_class == other_class && this_class->name == other_class->name});
      return {.code = native_return_code::nrc_return};
    }

    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid '==' on class, rhs is not of class type"}}}}};
  }

  native_return_type class_object::bang_equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_CLASS_OBJECT(other)) [[likely]]
    {
      const auto other_class = OK_VALUE_AS_CLASS_OBJECT(other);
      const auto this_class = OK_VALUE_AS_CLASS_OBJECT(this_);
      p_vm->return_value(value_t{this_class != other_class || this_class->name != other_class->name});
      return {.code = native_return_code::nrc_return};
    }

    return {.code = native_return_code::nrc_error,
            .error{.code = value_error_code::undefined_operation,
                   .payload{value_t{std::string_view{"invalid '!=' on class, rhs is not of class type"}}}}};
  }

  native_return_type class_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    const auto this_class = OK_VALUE_AS_CLASS_OBJECT(this_);
    std::print("{}", std::string_view{this_class->name->chars, this_class->name->length});
    return {.code = native_return_code::nrc_print_exit};
  }

  native_return_type class_object::call(vm* p_vm, value_t, uint8_t p_argc)
  {
    const auto this_ = p_vm->get_receiver();
    const auto this_class = OK_VALUE_AS_CLASS_OBJECT(this_);
    const auto instance = new_tobject<instance_object>(this_class->up.get_type(), this_class, p_vm->get_objects_list());

    const auto ctor = instance->up.class_->specials.operations[method_type::mt_ctor];
    if(!OK_IS_VALUE_NULL(ctor))
    {
      p_vm->stack_top(p_argc) = value_t{copy{(object*)instance}}; // forwarding, not a return
      if(!p_vm->call_value(ctor, ctor, p_argc))
      {
        return {.code = native_return_code::nrc_error, .error.code = value_error_code::internal_propagated_error};
      }
      return {.code = native_return_code::nrc_subcall};
    }
    else if(p_argc != 0)
    {
      return {
          .code = native_return_code::nrc_error,
          .error{.code = value_error_code::undefined_operation,
                 .payload{value_t{std::format("invalid default constructor on class '{}', expected 0 arguments got: {}",
                                              this_class->name->chars,
                                              p_argc)}}}};
    }

    p_vm->return_value(value_t{copy{(object*)instance}});
    return {.code = native_return_code::nrc_return};
  }

  template <typename T>
  T* class_object::create(string_object* p_name,
                          uint32_t p_class_type,
                          class_object* p_meta,
                          class_object* p_super,
                          object*& p_objects_list)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* class_object::create<object>(string_object* p_name,
                                       uint32_t p_class_type,
                                       class_object* p_meta,
                                       class_object* p_super,
                                       object*& p_objects_list)
  {
    auto co = new class_object(p_name, p_class_type, p_meta, p_super, p_objects_list);
    return (object*)co;
  }

  template <>
  class_object* class_object::create<class_object>(string_object* p_name,
                                                   uint32_t p_class_type,
                                                   class_object* p_meta,
                                                   class_object* p_super,
                                                   object*& p_objects_list)
  {
    auto co = new class_object(p_name, p_class_type, p_meta, p_super, p_objects_list);
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

  native_return_type instance_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    const auto this_instance = OK_VALUE_AS_INSTANCE_OBJECT(this_);
    std::print("instance of: {}",
               std::string_view{this_instance->up.class_->name->chars, this_instance->up.class_->name->length});
    return {.code = native_return_code::nrc_print_exit};
  }

  native_return_type instance_object::clone(vm* p_vm, value_t, uint8_t p_argc)
  {
    if(p_argc != 0)
    {
      return {.code = native_return_code::nrc_error,
              .error{.code = value_error_code::arguments_mismatch,
                     .payload{value_t{std::format("invalid clone, expected 0 arguments, got: {}", p_argc)}}}};
    }
    const auto this_ = p_vm->get_receiver();
    const auto this_instance = OK_VALUE_AS_INSTANCE_OBJECT(this_);
    auto clone =
        new_tobject<instance_object>(this_instance->up.get_type(), this_instance->up.class_, p_vm->get_objects_list());
    clone->fields = this_instance->fields; // shallow copy except for primitives
    p_vm->return_value(value_t{copy{clone}});
    return {.code = native_return_code::nrc_return};
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

  native_return_type bound_method_object::equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_BOUND_METHOD_OBJECT(other)) [[likely]]
    {
      const auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(this_);
      const auto other_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(other);
      // when very ugly meets slow
      const auto method = other_bound_method->method;
      // TODO(Qais): ts pmo
      p_vm->return_value(
          value_t{this_bound_method->receiver.type == other_bound_method->receiver.type &&
                  OK_VALUE_AS_OBJECT(this_bound_method->receiver) == OK_VALUE_AS_OBJECT(other_bound_method->receiver)});
      return {.code = native_return_code::nrc_return};
    }
    p_vm->stack_push(value_t{});
    return {
        .code = native_return_code::nrc_error,
        .error{.code = value_error_code::undefined_operation,
               .payload{value_t{std::format("invalid '==' on bound method, expected 0 arguments got: {}", p_argc)}}}};
  }

  native_return_type bound_method_object::bang_equal(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 1);
    const auto this_ = p_vm->get_receiver();
    const auto other = p_vm->get_arg(0);
    if(OK_IS_VALUE_BOUND_METHOD_OBJECT(other)) [[likely]]
    {
      const auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(this_);
      const auto other_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(other);
      // when very ugly meets slow
      const auto method = other_bound_method->method;
      // TODO(Qais): ts pmo
      p_vm->return_value(
          value_t{this_bound_method->receiver.type != other_bound_method->receiver.type ||
                  OK_VALUE_AS_OBJECT(this_bound_method->receiver) != OK_VALUE_AS_OBJECT(other_bound_method->receiver)});
      return {.code = native_return_code::nrc_return};
    }
    p_vm->stack_push(value_t{});
    return {
        .code = native_return_code::nrc_error,
        .error{.code = value_error_code::undefined_operation,
               .payload{value_t{std::format("invalid '==' on bound method, expected 0 arguments got: {}", p_argc)}}}};
  }

  native_return_type bound_method_object::call(vm* p_vm, value_t, uint8_t p_argc)
  {
    const auto this_ = p_vm->get_receiver();
    const auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(this_);
    const auto method = this_bound_method->method;
    if(OK_IS_VALUE_NULL(method))
    {
      return {.code = native_return_code::nrc_error,
              .error{.code = value_error_code::undefined_operation,
                     .payload{value_t{std::string_view{"invalid call on bound method"}}}}};
    }
    p_vm->stack_top(p_argc) = this_bound_method->receiver; // TODO(Qais): check this pls pls
    if(!p_vm->call_value(method, method, p_argc))
    {
      return {.code = native_return_code::nrc_error, .error.code = value_error_code::internal_propagated_error};
    }
    return {.code = native_return_code::nrc_subcall};
  }

  native_return_type bound_method_object::print(vm* p_vm, value_t, uint8_t p_argc)
  {
    ASSERT(p_argc == 0);
    const auto this_ = p_vm->get_receiver();
    auto this_bound_method = OK_VALUE_AS_BOUND_METHOD_OBJECT(this_);
    std::print("<bound method: {:p}>", (void*)this_bound_method);
    return {.code = native_return_code::nrc_print_exit};
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

  static class_object* register_object_class(object*& p_objects_list);
  static class_object* register_meta_class_class(object*& p_objects_list, class_object* p_object_class);
  static class_object* register_class_class(object*& p_objects_list, class_object* p_meta_class);
  static class_object*
  register_instance_class(object*& p_objects_list, class_object* p_string, class_object* p_meta_class);
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

    auto* object_class = register_object_class(objects);
    auto* meta_class_class = register_meta_class_class(objects, object_class);
    auto* class_class = register_class_class(objects, meta_class_class);

    auto* string_class = register_string_class(objects, class_class);
    p_vm->register_api_builtin((object*)string_class, object_type::obj_string, string_class->name);

    // update all strings in broken state
    for(auto interned : p_vm->get_interned_strings().underlying())
    {
      interned.second->up.class_ = string_class;
    }

    object_class->name = new_tobject<string_object>("object", string_class, objects);
    meta_class_class->name = new_tobject<string_object>("meta_class", string_class, objects);
    class_class->name = new_tobject<string_object>("class", string_class, objects);

    p_vm->register_api_builtin((object*)object_class, object_type::obj_object, object_class->name);
    p_vm->register_builtin((object*)meta_class_class, object_type::obj_meta_class);
    p_vm->register_builtin((object*)class_class, object_type::obj_class);

    auto* instance_class = register_instance_class(objects, string_class, meta_class_class);
    p_vm->register_builtin((object*)instance_class, object_type::obj_instance);

    auto* callable_class = register_callable_class(objects, string_class, meta_class_class);
    p_vm->register_api_builtin((object*)callable_class, object_type::obj_callable, callable_class->name);

    auto* function_class = register_function_class(objects, string_class, callable_class);
    p_vm->register_api_builtin((object*)function_class, object_type::obj_function, function_class->name);

    auto* closure_class = register_closure_class(objects, string_class, callable_class);
    p_vm->register_api_builtin((object*)closure_class, object_type::obj_closure, closure_class->name);

    p_vm->get_gc().resume();
    return true;
  }

  void register_base_methods(object* p_object)
  {
    auto& ops = p_object->class_->specials.operations;
    ops[method_type::mt_print] = value_t{instance_object::print, false};
  }

  static class_object* register_object_class(object*& p_objects_list)
  {
    auto* object_class = new_tobject<class_object>(nullptr, object_type::obj_object, nullptr, nullptr, p_objects_list);
    object_class->up.class_ = object_class;
    auto& ops = object_class->specials.operations;
    ops[method_type::mt_binary_infix_equal] = value_t{class_object::equal, false};
    ops[method_type::mt_binary_infix_bang_equal] = value_t{class_object::bang_equal, false};
    ops[method_type::mt_print] = value_t{class_object::print, false};
    return object_class;
  }

  static class_object* register_meta_class_class(object*& p_objects_list, class_object* p_object_class)
  {
    auto* meta_class =
        new_tobject<class_object>(nullptr, object_type::obj_meta_class, p_object_class, p_object_class, p_objects_list);
    auto& ops = meta_class->specials.operations;
    ops[method_type::mt_binary_infix_equal] = value_t{class_object::equal, false};
    ops[method_type::mt_binary_infix_bang_equal] = value_t{class_object::bang_equal, false};
    ops[method_type::mt_print] = value_t{class_object::print, false};
    return meta_class;
  }

  static class_object* register_class_class(object*& p_objects_list, class_object* p_meta_class)
  {
    auto* class_class =
        new_tobject<class_object>(nullptr, object_type::obj_class, p_meta_class, p_meta_class, p_objects_list);
    auto& ops = class_class->specials.operations;
    ops[method_type::mt_unary_postfix_call] = value_t{class_object::call, false};
    return class_class;
  }

  static class_object*
  register_instance_class(object*& p_objects_list, class_object* p_string, class_object* p_meta_class)
  {
    auto* instance_class_name = new_tobject<string_object>("instance", p_string, p_objects_list);
    auto* instance_class = new_tobject<class_object>(
        instance_class_name, object_type::obj_function, p_meta_class, nullptr, p_objects_list);

    auto* clone_method_name = new_tobject<string_object>("clone", p_string, p_objects_list);
    instance_class->methods[clone_method_name] = value_t{instance_object::clone, false};
    auto& ops = instance_class->specials.operations;
    ops[method_type::mt_print] = value_t{instance_object::print, false};

    return instance_class;
  }

  class_object* register_string_class(object*& p_objects_list, class_object* p_class)
  {
    auto* string_meta_class = new_tobject<class_object>(nullptr,
                                                        object_type::obj_meta_class,
                                                        p_class, // TODO(Qais): this should be object not class!
                                                        p_class,
                                                        p_objects_list);
    auto* string_class =
        new_tobject<class_object>(nullptr, object_type::obj_string, string_meta_class, nullptr, p_objects_list);

    auto* string_meta_name = new_tobject<string_object>("string_meta", string_class, p_objects_list);
    auto* string_class_name = new_tobject<string_object>("string", string_class, p_objects_list);
    string_meta_class->name = string_meta_name;
    string_class->name = string_class_name;

    auto& meta_ops = string_meta_class->specials.operations;
    meta_ops[method_type::mt_unary_postfix_call] = value_t{string_meta_class::call, false};

    auto& ops = string_class->specials.operations;
    ops[method_type::mt_binary_infix_plus] = value_t{string_object::plus, false};
    ops[method_type::mt_binary_infix_equal] = value_t{string_object::equal, false};
    ops[method_type::mt_binary_infix_bang_equal] = value_t{string_object::bang_equal, false};
    ops[method_type::mt_print] = value_t{string_object::print, false};

    return string_class;
  }

  static class_object*
  register_callable_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class)
  {
    auto* callable_class_name = new_tobject<string_object>("callable", p_string, p_objects_list);
    auto* callable_class = new_tobject<class_object>(
        callable_class_name, object_type::obj_callable, p_class_class, p_class_class, p_objects_list);
    auto& ops = callable_class->specials.operations;
    ops[method_type::mt_unary_postfix_call] = value_t{};
    return callable_class;
  }

  static class_object*
  register_function_class(object*& p_objects_list, class_object* p_string, class_object* p_callable_class)
  {
    auto* function_class_name = new_tobject<string_object>("function", p_string, p_objects_list);
    auto* function_class = new_tobject<class_object>(
        function_class_name, object_type::obj_function, p_callable_class, p_callable_class, p_objects_list);

    auto& ops = function_class->specials.operations;
    ops[method_type::mt_binary_infix_equal] = value_t{function_object::equal, false};
    ops[method_type::mt_binary_infix_bang_equal] = value_t{function_object::bang_equal, false};
    ops[method_type::mt_print] = value_t{function_object::print, false};

    return function_class;
  }

  static class_object*
  register_closure_class(object*& p_objects_list, class_object* p_string, class_object* p_callable_class)
  {
    auto* closure_class_name = new_tobject<string_object>("closure", p_string, p_objects_list);
    auto* closure_meta_class_name = new_tobject<string_object>("closure_meta", p_string, p_objects_list);

    auto* closure_meta_class = new_tobject<class_object>(
        closure_meta_class_name, object_type::obj_meta_class, p_callable_class, p_callable_class, p_objects_list);
    auto* closure_class = new_tobject<class_object>(
        closure_class_name, object_type::obj_closure, closure_meta_class, nullptr, p_objects_list);

    auto& meta_ops = closure_meta_class->specials.operations;
    meta_ops[method_type::mt_unary_postfix_call] = value_t{closure_meta_class::call, false};

    auto& ops = closure_class->specials.operations;
    ops[method_type::mt_binary_infix_equal] = value_t{closure_object::equal, false};
    ops[method_type::mt_binary_infix_bang_equal] = value_t{closure_object::bang_equal, false};
    ops[method_type::mt_unary_postfix_call] = value_t{closure_object::call, false};
    ops[method_type::mt_print] = value_t{closure_object::print, false};
    ops[method_type::mt_unary_postfix_call] = value_t{closure_object::call, false};

    return closure_class;
  }

  static class_object*
  register_bound_method_class(object*& p_objects_list, class_object* p_string, class_object* p_class_class)
  {
    auto* bound_method_class_name = new_tobject<string_object>("bound_method", p_string, p_objects_list);
    auto* bound_method_class = new_tobject<class_object>(
        bound_method_class_name, object_type::obj_bound_method, p_class_class, p_class_class, p_objects_list);

    auto& ops = bound_method_class->specials.operations;
    ops[method_type::mt_binary_infix_equal] = value_t{bound_method_object::equal, false};
    ops[method_type::mt_binary_infix_bang_equal] = value_t{bound_method_object::bang_equal, false};
    ops[method_type::mt_unary_postfix_call] = value_t{bound_method_object::call, false};
    ops[method_type::mt_print] = value_t{bound_method_object::print, false};

    return bound_method_class;
  }
} // namespace ok