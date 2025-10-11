#include "object.hpp"
#include "chunk.hpp"
#include "constants.hpp"
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
  object::object(uint32_t p_type) : type(p_type) //& 0x00ffffff)
  {
    ASSERT(p_type <= uint24_max);
    next = get_g_vm()->get_objects_list();
    get_g_vm()->get_objects_list() = this;
    set_marked(false);
    set_registered(false);
  }

  string_object::string_object(const std::string_view p_src) : string_object()
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
  string_object* string_object::create(const std::string_view p_src)
  {
    auto& interned_strings = get_g_vm()->get_interned_strings();
    auto interned = interned_strings.get(p_src);
    if(interned != nullptr)
      return interned;

    return interned_strings.set(p_src);
  }

  template <>
  object* string_object::create(const std::string_view p_src)
  {
    return (object*)create<string_object>(p_src);
  }

  template <>
  string_object* string_object::create(const std::span<std::string_view> p_srcs)
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
  object* string_object::create(const std::span<std::string_view> p_srcs)
  {
    return (object*)create<string_object>(p_srcs);
  }

  string_object::string_object() : up(object_type::obj_string)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
          std::pair<uint32_t, vm::operation_function_infix_binary>{
              _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_string),
              string_object::equal},
          {_make_object_key(operator_type::op_plus, value_type::object_val, object_type::obj_string),
           string_object::plus}};
      ops.binary_infix.register_operations(op_fcn);

      ops.print_function = string_object::print;
      return true;
    }();
  }

  std::expected<value_t, value_error> string_object::equal(object* p_this, value_t p_sure_is_string)
  {
    auto other = (string_object*)p_sure_is_string.as.obj;
    if(p_this->get_type() == other->up.get_type())
    {
      return value_t{(string_object*)p_this == other}; // pointer compare, interning makes it possible
    }
    return std::unexpected(value_error::undefined_operation);
  }

  std::expected<value_t, value_error> string_object::plus(object* p_this, value_t p_sure_is_string)
  {
    auto other = (string_object*)p_sure_is_string.as.obj;
    if(p_this->get_type() == other->up.get_type()) // redundant check!
    {
      auto this_string = (string_object*)p_this;
      std::array<std::string_view, 2> arr = {std::string_view{this_string->chars, this_string->length},
                                             {other->chars, other->length}};
      return value_t{copy{create(arr)}};
    }
    return std::unexpected(value_error::undefined_operation);
  }

  void string_object::print(object* p_this)
  {
    auto this_string = (string_object*)p_this;
    std::print("{}", std::string_view{this_string->chars, this_string->length});
  }

  function_object::function_object(uint8_t p_arity, string_object* p_name) : function_object()
  {
    name = p_name;
  }

  function_object::function_object() : up(object_type::obj_function)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
          std::pair<uint32_t, vm::operation_function_infix_binary>{
              _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_function),
              function_object::equal}};
      ops.binary_infix.register_operations(op_fcn);
      ops.call_function = function_object::call;
      ops.print_function = function_object::print;
      return true;
    }();
  }

  function_object::~function_object()
  {
  }

  std::expected<value_t, value_error> function_object::equal(object* p_this, value_t p_sure_is_function)
  {
    return equal_impl((function_object*)p_this, (function_object*)p_sure_is_function.as.obj);
  }

  std::expected<value_t, value_error> function_object::equal_impl(function_object* p_this, function_object* p_other)
  {
    return value_t{p_this->arity == p_other->arity &&
                   p_this->name == p_other->name /*&& this_function->associated_chunk == other->associated_chunk*/};
  }

  void function_object::print(object* p_this)
  {
    auto this_function = (function_object*)p_this;
    std::print("<fu {}>", std::string_view{this_function->name->chars, this_function->name->length});
  }

  std::expected<std::optional<call_frame>, value_error> function_object::call(object* p_this, uint8_t p_argc)
  {
    // auto this_function = (function_object*)p_this;
    // if(p_argc != this_function->arity)
    // {
    //   return std::unexpected(value_error::arguments_mismatch);
    // }
    // auto vm = get_g_vm();
    // auto frame = call_frame{this_function,
    //                         this_function->associated_chunk.code.data(),
    //                         vm->frame_stack_top() - p_argc - 1,
    //                         vm->frame_stack_top() - p_argc - 1};
    // return frame;
  }

  template <typename T>
  T* function_object::create(uint8_t p_arity, string_object* p_name)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* function_object::create<object>(uint8_t p_arity, string_object* p_name)
  {
    auto fo = new function_object(p_arity, p_name);
    return (object*)fo;
  }

  template <>
  function_object* function_object::create<function_object>(uint8_t p_arity, string_object* p_name)
  {
    auto fo = new function_object(p_arity, p_name);
    return fo;
  }

  native_function_object::native_function_object(native_function p_function) : native_function_object()
  {
    function = p_function;
  }

  native_function_object::~native_function_object()
  {
  }

  std::expected<value_t, value_error> native_function_object::equal(object* p_this, value_t p_sure_is_function)
  {
    auto other = (native_function_object*)p_sure_is_function.as.obj;
    auto this_native_function = (native_function_object*)p_this;
    return value_t{this_native_function->function == other->function};
  }

  void native_function_object::print(object* p_this)
  {
    auto this_native_function = (native_function_object*)p_this;
    std::print("<native fu {:p}>", (void*)this_native_function->function);
  }

  std::expected<std::optional<call_frame>, value_error> native_function_object::call(object* p_this, uint8_t p_argc)
  {
    auto this_native_function = (native_function_object*)p_this;
    auto vm = get_g_vm();
    auto ret = this_native_function->function(p_argc, vm->stack_at(vm->frame_stack_top() - p_argc));

    vm->get_current_call_frame().top = vm->frame_stack_top() - p_argc - 1;
    vm->stack_resize(vm->get_current_call_frame().top);
    vm->stack_push(ret);
    return {};
  }

  native_function_object::native_function_object() : up(object_type::obj_native_function)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
          std::pair<uint32_t, vm::operation_function_infix_binary>{
              _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_native_function),
              native_function_object::equal}};
      ops.binary_infix.register_operations(op_fcn);
      ops.call_function = native_function_object::call;
      ops.print_function = native_function_object::print;
      return true;
    }();
  }

  template <typename T>
  T* native_function_object::create(native_function p_function)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* native_function_object::create<object>(native_function p_function)
  {
    auto fo = new native_function_object(p_function);
    return (object*)fo;
  }

  template <>
  native_function_object* native_function_object::create<native_function_object>(native_function p_function)
  {
    auto fo = new native_function_object(p_function);
    return fo;
  }

  closure_object::closure_object(function_object* p_function) : closure_object()
  {
    function = p_function;
    for(uint32_t i = 0; i < p_function->upvalues; ++i)
      upvalues.emplace_back(nullptr);
  }

  closure_object::closure_object() : up(object_type::obj_closure)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
          std::pair<uint32_t, vm::operation_function_infix_binary>{
              _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_native_function),
              closure_object::equal}};
      ops.binary_infix.register_operations(op_fcn);
      ops.call_function = closure_object::call;
      ops.print_function = closure_object::print;
      return true;
    }();
  }

  closure_object::~closure_object()
  {
  }

  std::expected<value_t, value_error> closure_object::equal(object* p_this, value_t p_sure_is_closure)
  {
    auto other = (closure_object*)p_sure_is_closure.as.obj;
    auto this_closure = (closure_object*)p_this;
    return function_object::equal_impl(this_closure->function, other->function);
  }

  void closure_object::print(object* p_this)
  {
    std::print("<closure {:p}>", (void*)p_this);
  }

  std::expected<std::optional<call_frame>, value_error> closure_object::call(object* p_this, uint8_t p_argc)
  {
    auto this_closure = (closure_object*)p_this;
    if(p_argc != this_closure->function->arity)
    {
      return std::unexpected(value_error::arguments_mismatch);
    }
    auto vm = get_g_vm();
    auto frame = call_frame{this_closure,
                            this_closure->function->associated_chunk.code.data(),
                            vm->frame_stack_top() - p_argc - 1,
                            vm->frame_stack_top() - p_argc - 1};
    return frame;
  }

  template <typename T>
  T* closure_object::create(function_object* p_function)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* closure_object::create<object>(function_object* p_function)
  {
    auto fo = new closure_object(p_function);
    return (object*)fo;
  }

  template <>
  closure_object* closure_object::create<closure_object>(function_object* p_function)
  {
    auto fo = new closure_object(p_function);
    return fo;
  }

  upvalue_object::upvalue_object(value_t* slot) : upvalue_object()
  {
    location = slot;
  }

  upvalue_object::~upvalue_object()
  {
  }

  template <typename T>
  T* upvalue_object::create(value_t* p_slot)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* upvalue_object::create(value_t* p_slot)
  {
    return (object*)new upvalue_object(p_slot);
  }

  template <>
  upvalue_object* upvalue_object::create(value_t* p_slot)
  {
    return new upvalue_object(p_slot);
  }

  upvalue_object::upvalue_object() : up(object_type::obj_upvalue)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      ops.print_function = upvalue_object::print;
      return true;
    }();
  }

  void upvalue_object::print(object* p_this)
  {
    std::print("upvalue: {:p}", (void*)p_this);
  }

  class_object::class_object(string_object* p_name) : class_object()
  {
    name = p_name;
  }

  class_object::~class_object()
  {
  }

  class_object::class_object() : up(object_type::obj_class)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
          std::pair<uint32_t, vm::operation_function_infix_binary>{
              _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_class),
              class_object::equal}};
      ops.binary_infix.register_operations(op_fcn);
      ops.call_function = class_object::call;
      ops.print_function = class_object::print;
      return true;
    }();
  }

  std::expected<value_t, value_error> class_object::equal(object* p_this, value_t p_sure_is_class)
  {
    auto other = (class_object*)p_sure_is_class.as.obj;
    auto this_class = (class_object*)p_this;
    return value_t{this_class->name == other->name}; // TODO(Qais): operator is and a isinstance equivalent
    // return function_object::equal_impl(this_closure->function, other->function);
  }

  void class_object::print(object* p_this)
  {
    auto this_class = (class_object*)p_this;
    std::print("{}", std::string_view{this_class->name->chars, this_class->name->length});
  }

  std::expected<std::optional<call_frame>, value_error> class_object::call(object* p_this, uint8_t p_argc)
  {
    auto this_class = (class_object*)p_this;
    auto _vm = get_g_vm();
    auto instance = new_tobject<instance_object>(this_class);
    _vm->stack_top(p_argc) = value_t{copy{(object*)instance}};

    const auto it = instance->class_->methods.find(_vm->get_statics().init_string);
    if(instance->class_->methods.end() != it)
    {
      auto res = _vm->call_value(p_argc, it->second);
      if(!res)
      {
        return std::unexpected{value_error::internal_propagated_error};
      }
    }

    return {{}};
  }

  template <typename T>
  T* class_object::create(string_object* p_name)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* class_object::create<object>(string_object* p_name)
  {
    auto co = new class_object(p_name);
    return (object*)co;
  }

  template <>
  class_object* class_object::create<class_object>(string_object* p_name)
  {
    auto co = new class_object(p_name);
    return co;
  }

  instance_object::instance_object(class_object* p_class) : instance_object()
  {
    class_ = p_class;
  }

  instance_object::~instance_object()
  {
  }

  instance_object::instance_object() : up(object_type::obj_instance)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      // std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
      //     std::pair<uint32_t, vm::operation_function_infix_binary>{
      //         _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_class),
      //         class_object::equal}};
      // ops.binary_infix.register_operations(op_fcn);
      // ops.call_function = class_object::call;
      ops.print_function = instance_object::print;
      return true;
    }();
  }

  void instance_object::print(object* p_this)
  {
    auto this_instance = (instance_object*)p_this;
    // TODO(Qais): find an elegant way to do to_string conversion
    std::print("instance of: {}",
               std::string_view{this_instance->class_->name->chars, this_instance->class_->name->length});
  }

  template <typename T>
  T* instance_object::create(class_object* p_class)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* instance_object::create<object>(class_object* p_class)
  {
    auto io = new instance_object(p_class);
    return (object*)io;
  }

  template <>
  instance_object* instance_object::create<instance_object>(class_object* p_class)
  {
    auto io = new instance_object(p_class);
    return io;
  }

  bound_method_object::bound_method_object(value_t p_receiver, closure_object* p_method) : bound_method_object()
  {
    receiver = p_receiver;
    method = p_method;
  }

  bound_method_object::~bound_method_object()
  {
  }

  bound_method_object::bound_method_object() : up(object_type::obj_bound_method)
  {
    [[maybe_unused]] static const bool _ = [this] -> bool
    {
      auto _vm = get_g_vm();
      auto& ops = _vm->register_object_operations(up.get_type());
      std::array<std::pair<uint32_t, vm::operation_function_infix_binary>, 2> op_fcn = {
          std::pair<uint32_t, vm::operation_function_infix_binary>{
              _make_object_key(operator_type::op_equal, value_type::object_val, object_type::obj_class),
              bound_method_object::equal}};
      ops.binary_infix.register_operations(op_fcn);
      ops.call_function = bound_method_object::call;
      ops.print_function = bound_method_object::print;
      return true;
    }();
  }

  void bound_method_object::print(object* p_this)
  {
    auto this_bd = (bound_method_object*)p_this;
    closure_object::print((object*)this_bd->method);
  }

  std::expected<value_t, value_error> bound_method_object::equal(object* p_this, value_t p_sure_is_bound_method)
  {
    auto this_bm = (bound_method_object*)p_this;
    auto other = (bound_method_object*)p_sure_is_bound_method.as.obj;
    return value_t{closure_object::equal((object*)this_bm->method, value_t{copy{(object*)other->method}})
                       .value_or(value_t{false})
                       .as.boolean &&
                   this_bm->receiver.type == other->receiver.type &&
                   this_bm->receiver.as.obj == other->receiver.as.obj};
  }

  std::expected<std::optional<call_frame>, value_error> bound_method_object::call(object* p_this, uint8_t p_argc)
  {
    auto this_bm = (bound_method_object*)p_this;
    get_g_vm()->stack_top(p_argc) = this_bm->receiver;
    return closure_object::call((object*)this_bm->method, p_argc);
  }

  template <typename T>
  T* bound_method_object::create(value_t p_receiver, closure_object* p_method)
  {
    static_assert(false, "type mismatch");
  }

  template <>
  object* bound_method_object::create<object>(value_t p_receiver, closure_object* p_method)
  {
    auto bmo = new bound_method_object(p_receiver, p_method);
    return (object*)bmo;
  }

  template <>
  bound_method_object* bound_method_object::create<bound_method_object>(value_t p_receiver, closure_object* p_method)
  {
    auto bmo = new bound_method_object(p_receiver, p_method);
    return bmo;
  }

  void delete_object(object* p_object)
  {
    switch(p_object->get_type())
    {
    case object_type::obj_string:
      delete(string_object*)p_object;
      break;
    case object_type::obj_function:
      delete(function_object*)p_object;
      break;
    case object_type::obj_native_function:
      delete(native_function_object*)p_object;
      break;
    case object_type::obj_closure:
      delete(closure_object*)p_object;
      break;
    case object_type::obj_upvalue:
      delete(upvalue_object*)p_object;
      break;
    case object_type::obj_class:
      delete(class_object*)p_object;
      break;
    case object_type::obj_instance:
      delete(instance_object*)p_object;
      break;
    case object_type::obj_bound_method:
      delete(bound_method_object*)p_object;
      break;
    default:
      ASSERT(false);
      break; // TODO(Qais): error
    }
    p_object = nullptr;
  }
} // namespace ok