#ifndef OK_OBJECT_HPP
#define OK_OBJECT_HPP

#include "call_frame.hpp"
#include "chunk.hpp"
#include "macros.hpp"
#include "operator.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <flat_map>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>

// TODO(Qais): each object type in separate header file and they all share same implementation file
// why bro?

// clang-format off
#define OK_IS_VALUE_STRING_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_string)
#define OK_IS_VALUE_FUNCTION_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_function)
//#define OK_IS_VALUE_NATIVE_FUNCTION_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && v.as.obj->get_type() == object_type::obj_native_function)
//#define OK_IS_VALUE_NATIVE_METHOD_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && v.as.obj->get_type() == object_type::obj_native_method)
#define OK_IS_VALUE_CLOSURE_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_closure)
#define OK_IS_VALUE_UPVALUE_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_upvalue)
#define OK_IS_VALUE_CLASS_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_class)
#define OK_IS_VALUE_INSTANCE_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_instance)
#define OK_IS_VALUE_BOUND_METHOD_OBJECT(v) (OK_IS_VALUE_OBJECT(v) && OK_VALUE_AS_OBJECT(v)->get_type() == object_type::obj_bound_method)



#define OK_VALUE_AS_STRING_OBJECT(v) ((string_object*)OK_VALUE_AS_OBJECT(v))
#define OK_VALUE_AS_FUNCTION_OBJECT(v) ((function_object*)OK_VALUE_AS_OBJECT(v))
//#define OK_VALUE_AS_NATIVE_FUNCTION_OBJECT(v) ((native_function_object*)v.as.obj)
//#define OK_VALUE_AS_NATIVE_METHOD_OBJECT(v) ((native_method_object*)v.as.obj)
#define OK_VALUE_AS_CLOSURE_OBJECT(v) ((closure_object*)OK_VALUE_AS_OBJECT(v))
#define OK_VALUE_AS_UPVALUE_OBJECT(v) ((upvalue_object*)OK_VALUE_AS_OBJECT(v))
#define OK_VALUE_AS_CLASS_OBJECT(v) ((class_object*)OK_VALUE_AS_OBJECT(v))
#define OK_VALUE_AS_INSTANCE_OBJECT(v) ((instance_object*)OK_VALUE_AS_OBJECT(v))
#define OK_VALUE_AS_BOUND_METHOD_OBJECT(v) ((bound_method_object*)OK_VALUE_AS_OBJECT(v))
// clang-format on

namespace ok
{
  namespace object_type
  {
    typedef enum
    {
      none = 0,
      obj_object,
      obj_meta_class,
      obj_class,
      obj_instance,
      obj_string,
      obj_callable,
      obj_function,
      obj_closure,
      obj_upvalue,
      obj_bound_method,
      obj_last,
    } object_type;
  }

  struct class_object;
  struct object
  {
    object* next = nullptr;
    class_object* class_ = nullptr;

    object(uint32_t p_type,
           class_object* p_class,
           object*& p_objects_list,
           bool is_instance = false,
           bool is_class = false);
    // 24bit integer
    inline uint32_t get_type() const
    {
      // #if defined(PARANOID)
      // return type;
      // #endif
      return type & 0x00ffffff;
    }

    inline void set_type(uint32_t p_type)
    {
#if defined(PARANOID)
      ASSERT(p_type <= uint24_max);
#endif
      type = (type & 0xff000000) | (p_type & 0x00ffffff);
    }

    // keep this flag first so we pack it into the keys
    inline bool is_instance() const
    {
#if defined(PARANOID)
      return _is_instance;
#endif
      return (type & (1u << 24)) != 0;
    }

    inline void set_instance(bool p_instance)
    {
#if defined(PARANOID)
      _is_instance = p_instance;
#endif
      p_instance ? type |= (1u << 24) : type &= ~(1u << 24);
    }

    inline bool is_class() const
    {
#if defined(PARANOID)
      return _is_class;
#endif
      return (type & (1u << 25)) != 0;
    }

    inline void set_class(bool p_is_class)
    {
#if defined(PARANOID)
      _is_class = p_is_class;
#endif
      p_is_class ? type |= (1u << 25) : type &= ~(1u << 25);
    }

    inline bool is_marked() const
    {
#if defined(PARANOID)
      return _is_marked;
#endif
      return (type & (1u << 26)) != 0;
    }

    inline void set_marked(bool p_mark)
    {
#if defined(PARANOID)
      _is_marked = p_mark;
#endif
      p_mark ? type |= (1u << 26) : type &= ~(1u << 26);
    }

    inline bool same_type_as(const object& p_other) const
    {
      return get_type() == p_other.get_type() && is_instance() == p_other.is_instance();
    }

  private:
    // 24bit for the type => 16 bit for the object type, and 8 bits for the version (related to inheritance),
    // and 1 bit for the _is_registered boolean, and one bit for the _is_marked boolean, and 6 free bits
    uint32_t type;

#if defined(PARANOID) // faster to inspect in debug builds
    bool _is_class = false;
    bool _is_marked = false;
    bool _is_instance = false;
#endif
  };

  // struct native_function_return_type
  // {
  //   bool is_error;
  //   union
  //   {
  //     value_t normal_return;
  //     value_error error_return;
  //   };
  // };

  // typedef union
  // {
  //   value_t value;
  //   std::optional<call_frame> frame;
  //   value_error_code error;
  //   void* other;
  // } native_method_return_value;

  // struct native_method_return_type
  // {
  //   enum
  //   {
  //     rt_operations,
  //     rt_print,
  //     rt_function_call,
  //     rt_error,
  //   } return_type;

  //   native_method_return_value return_value;
  // };

  // typedef native_function_return_type (*native_function)(vm* p_vm, uint8_t p_argc, value_t* p_argv);
  // typedef native_method_return_type (*native_method)(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);

  struct string_object
  {
    // create a string object (allocates char buffer on heap)
    string_object(const std::string_view p_src, class_object* p_string_class, object*& p_objects_list);
    string_object(std::span<std::string_view> p_srcs, class_object* p_string_class, object*& p_objects_list);

    ~string_object();

    template <typename Obj = object>
    static Obj* create(const std::string_view p_src, class_object* p_string_class, object*& p_objects_list);
    template <typename Obj = object>
    static Obj*
    create(const std::span<std::string_view> p_srcs, class_object* p_string_class, object*& p_objects_lists);

    object up;
    hashed_string hash_code;
    size_t length;
    char* chars;

    static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc);

    static native_method_return_type plus(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);

    // private:
    //   string_object();
  };
} // namespace ok

namespace std
{
  template <>
  struct hash<ok::string_object>
  {
    size_t operator()(const ok::string_object& str) const
    {
      return hash<std::string_view>{}({str.chars, str.length});
    }
  };
  template <>
  struct hash<ok::string_object*>
  {
    size_t operator()(const ok::string_object* str) const
    {
      return hash<std::string_view>{}({str->chars, str->length});
    }
  };
} // namespace std

namespace ok
{
  template <typename T, size_t ArrayLength, typename MapKey>
    requires std::is_integral_v<MapKey>            // both array and map indexed using same way
             && std::is_default_constructible_v<T> // default constructor shall yield a "null" instance of that type
  struct arraymap
  {
    std::array<T, ArrayLength> array;
    std::unordered_map<MapKey, T> map;

    void set(size_t p_index, value_t p_value)
    {
      if(p_index < ArrayLength)
      {
        array[p_index] = p_value;
      }
      else
      {
        map[p_index] = p_value;
      }
    }

    const T operator[](size_t p_index) const
    {
      if(p_index < ArrayLength) [[likely]] // optimize for common case
      {
        return array[p_index];
      }
      else
      {
        const auto it = map.find(p_index);
        if(map.end() == it)
        {
          return T{};
        }
        else [[likely]]
        {
          return it->second;
        }
      }
    }
  };

  struct special_methods
  {
    std::array<value_t, method_type::mt_count> operations;
    std::unordered_map<uint32_t, value_t> conversions;
    // arraymap<value_t, 16, uint8_t> call;     // based on arity (in most cases won't exceed 16, but it may
    //  grow up to 255 (based on the highest arity overload)
    // arraymap<value_t, 8, uint8_t> subscript; // just like call (but this most of the time it only has 1 operand)
    // std::unordered_map<uint64_t, value_t>
    //     operations; // generic binops: packs other's (object_type(24bit id + 1bit is_instance) + value_type(3bits
    //  bool|number|null|native function|object)) + operator_type(8bits) + 28 free bit
    // std::unordered_map<uint32_t, value_t> conversions; // generic conversions: packs other's object_type + value_type
  };

  struct method_key
  {
    method_key(uint64_t ptr, uint8_t arity) : m_pointer(ptr), m_arity(arity)
    {
    }
    void* pointer() const
    {
      return (void*)m_pointer;
    }

    uint8_t arity() const
    {
      return m_arity;
    }

    bool operator==(const method_key& p_other) const
    {
      return m_pointer == p_other.m_pointer && m_arity == p_other.m_arity;
    }

  private:
    uint64_t m_pointer;
    uint8_t m_arity;
    template <typename>
    friend struct std::hash;
    // #if defined(NO_NAN_BOX)
    //     uint8_t arity;
    // #endif
  };
} // namespace ok

namespace std
{
  template <>
  struct hash<ok::method_key>
  {
    size_t operator()(const ok::method_key& key) const
    {
      auto res = hash<std::uint64_t>{}(key.m_pointer);
#if defined(NO_NAN_BOX)
      res |= key.arity; // TODO(Qais): proper lightweight hash combine
#endif
      return res;
    }
  };
} // namespace std

namespace ok
{
  constexpr uint64_t make_binary_operations_key(uint32_t p_object_type /* extracted pure 24bit type */,
                                                bool p_is_instance,
                                                value_type p_value_type,
                                                operator_type p_operator_type)
  {
    p_object_type = p_object_type & 0x00ffffff; // clear from any excess waste
    return static_cast<uint64_t>(p_object_type) << 8 | static_cast<uint64_t>(p_is_instance) << 9 |
           static_cast<uint64_t>(p_value_type) << 12 | static_cast<uint64_t>(p_operator_type) << 20;
  }

  constexpr uint32_t make_conversions_key(uint32_t p_object_type /* extracted pure 24bit type */,
                                          bool p_is_instance,
                                          value_type p_value_type)
  {
    p_object_type = p_object_type & 0x00ffffff; // clear from any excess waste

    return static_cast<uint64_t>(p_object_type) << 8 | static_cast<uint64_t>(p_is_instance) << 9 |
           static_cast<uint64_t>(p_value_type) << 12;
  }

  constexpr method_key make_methods_key(void* p_ptr, uint8_t p_arity)
  {
    // TODO(Qais): do it
    return {(uint64_t)p_ptr, p_arity};
  }

  struct class_object
  {
    class_object(string_object* p_name,
                 uint32_t p_class_type,
                 class_object* p_meta,
                 class_object* p_super,
                 object*& p_objects_list);
    ~class_object();

    template <typename Obj = object>
    static Obj* create(string_object* p_name,
                       uint32_t p_class_type,
                       class_object* p_meta,
                       class_object* p_super,
                       object*& p_objects_list);
    object up;
    string_object* name;
    special_methods specials;
    std::unordered_map<string_object*, value_t> methods;
    // std::unordered_map<method_key, value_t> methods; // TODO(Qais): override by arity, steal bits from pointer,
    //  and dont hash the string (it is interned), and hide this behind a compile time flag, and use a custom struct for
    //  the non optimized version

    static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);

    // private:
    //   class_object();
  };

  struct function_object
  {
    function_object(uint8_t p_arity, string_object* p_name, class_object* p_function_class, object*& p_objects_list);
    ~function_object();

    template <typename Obj = object>
    static Obj* create(uint8_t p_arity, string_object* p_name, class_object* p_function_class, object*& p_objects_list);

    object up;
    chunk associated_chunk;
    string_object* name = nullptr;
    uint32_t upvalues = 0;
    uint8_t arity = 0;

    static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);

    // static std::expected<std::optional<call_frame>, value_error> call(value_t* p_this, uint8_t p_argc);

  private:
    static value_t equal_impl(function_object* p_this, function_object* p_other, bool p_equal = true);
    friend struct closure_object;
    // private:
    //   function_object();
    //   friend struct closure_object;
  };

  // struct native_function_object
  // {
  //   native_function_object(native_function p_function, class_object* p_native_function_class, object*&
  //   p_objects_list); ~native_function_object();

  //   template <typename Obj = object>
  //   static Obj* create(native_function p_function, class_object* p_native_function_class, object*& p_objects_list);

  //   object up;
  //   native_function function;

  //   static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);
  //   static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);

  //   static std::expected<value_t, value_error> equal_bound_method(value_t p_this, value_t p_bound_method);
  //   static std::expected<value_t, value_error> bang_equal_bound_method(value_t p_this, value_t p_bound_method);

  //   static std::expected<value_t, value_error> equal_closure(value_t p_this, value_t p_closure);
  //   static std::expected<value_t, value_error> bang_equal_closure(value_t p_this, value_t p_closure);

  //   static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);
  //   static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);

  //   // private:
  //   //   native_function_object();
  // };

  // struct native_method_object
  // {
  //   native_method_object(native_method p_native_method, class_object* p_native_method_class, object*&
  //   p_objects_list); ~native_method_object();

  //   template <typename Obj = object>
  //   static Obj* create(native_method p_native_method, class_object* p_native_method_class, object*& p_objects_list);

  //   object up;
  //   native_method method;

  //   static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);
  //   static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);

  //   static std::expected<value_t, value_error> equal_bound_method(value_t p_this, value_t p_bound_method);
  //   static std::expected<value_t, value_error> bang_equal_bound_method(value_t p_this, value_t p_bound_method);

  //   static std::expected<value_t, value_error> equal_closure(value_t p_this, value_t p_closure);
  //   static std::expected<value_t, value_error> bang_equal_closure(value_t p_this, value_t p_closure);

  //   static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);
  //   static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc, value_t* p_argv);

  //   // private:
  //   //   native_function_object();
  // };

  struct upvalue_object
  {
    upvalue_object(value_t* slot, class_object* p_upvalue_class, object*& p_objects_list);
    ~upvalue_object();

    template <typename Obj = object>
    static Obj* create(value_t* slot, class_object* p_function_class, object*& p_objects_list);

    object up;
    value_t* location = nullptr;
    upvalue_object* next = nullptr;
    value_t closed{};

    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);
    // private:
    //   upvalue_object();
  };

  struct closure_object
  {
    closure_object(function_object* p_function, class_object* p_closure_class, object*& p_objects_list);
    ~closure_object();

    template <typename Obj = object>
    static Obj* create(function_object* p_function, class_object* p_closure_class, object*& p_objects_list);

    object up;
    function_object* function;
    std::vector<upvalue_object*> upvalues;

    static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);

    static native_method_return_type equal_impl(vm* p_vm, value_t p_this, value_t p_other);
    static native_method_return_type bang_equal_impl(vm* p_vm, value_t p_this, value_t p_other);
    static native_method_return_type call_impl(vm* p_vm, uint8_t p_argc, value_t p_this);
    static native_method_return_type print_impl(vm* p_vm, value_t p_this);

    static std::expected<value_t, value_error_code> equal_native_function(value_t p_this, value_t p_native_function);
    static std::expected<value_t, value_error_code> bang_equal_native_function(value_t p_this,
                                                                               value_t p_native_function);
    static std::expected<value_t, value_error_code> equal_bound_method(value_t p_this, value_t p_bound_method);
    static std::expected<value_t, value_error_code> bang_equal_bound_method(value_t p_this, value_t p_bound_method);

    // private:
    //   closure_object();
    //   friend struct bound_method_object;
  };

  struct instance_object
  {
    instance_object(uint32_t p_instance_type, class_object* p_class, object*& p_objects_list);
    ~instance_object();

    template <typename Obj = object>
    static Obj* create(uint32_t p_instance_type, class_object* p_class, object*& p_objects_list);

    object up;
    std::unordered_map<string_object*, value_t> fields;

    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);

    // private:
    //   instance_object();
    // static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_instance);
    // static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
  };

  struct bound_method_object
  {
    // using methods_store = arraymap<value_t, 4, uint8_t>;
    bound_method_object(value_t p_receiver, value_t p_methods, class_object* p_class, object*& p_objects_list);
    ~bound_method_object();

    template <typename Obj = object>
    static Obj* create(value_t p_receiver, value_t p_methods, class_object* p_class, object*& p_objects_list);

    object up;
    value_t receiver;
    value_t method;
    // methods_store methods;

    static native_method_return_type equal(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type bang_equal(vm* p_vm, value_t p_this, uint8_t p_argc);

    static std::expected<value_t, value_error_code> equal_closure(value_t p_this, value_t p_closure);
    static std::expected<value_t, value_error_code> bang_equal_closure(value_t p_this, value_t p_closure);

    static std::expected<value_t, value_error_code> equal_native_function(value_t p_this, value_t p_native_function);
    static std::expected<value_t, value_error_code> bang_equal_native_function(value_t p_this,
                                                                               value_t p_native_function);

    static native_method_return_type call(vm* p_vm, value_t p_this, uint8_t p_argc);
    static native_method_return_type print(vm* p_vm, value_t p_this, uint8_t p_argc);
  };

  // template <typename T, typename... Args>
  // concept doesconstruct = requires(Args&&... args) { T(std::forward<Args>(args)...); };

  template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
  object* new_object(Args&&... args)
  {
    auto& gc = get_vm_gc();
    gc.increment_used_memory(sizeof(T));
    return T::create(std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
  T* new_tobject(Args&&... args)
  {
    auto& gc = get_vm_gc();
    gc.increment_used_memory(sizeof(T));
    return T::template create<T>(std::forward<Args>(args)...);
  }

  void delete_object(object* p_object);

  // // leaves a 24bit integer room for objects which is more than we ever will need
  // constexpr uint32_t _make_object_key(const operator_type p_operator,
  //                                     const value_type p_rhs,
  //                                     const uint32_t p_other_object_type = object_type::none)
  // {
  //   if(p_other_object_type == object_type::none)
  //   {
  //     return static_cast<uint32_t>(to_utype(p_operator)) << 24 | static_cast<uint32_t>(to_utype(p_rhs)) | 16;
  //   }
  //   else
  //   {
  //     return static_cast<uint32_t>(to_utype(p_operator)) << 24 | (p_other_object_type);
  //   }
  // }

  constexpr uint8_t get_value_type(const value_t& p_val)
  {
    return to_utype(p_val.type);
  }

  constexpr uint32_t combine_value_type_with_object_type(value_type p_value_type, uint32_t p_object_type)
  {
    // TODO(Qais): this looks awful, object shall never be a c++ struct rather a c struct with free functions, with
    // helpers for the bit testing on raw numbers
    return p_object_type << 8 | (to_utype(p_value_type) << 7 | (p_object_type & (1u << 24)));
  }

  // constexpr uint64_t combine_value_types_with_object_types(value_type lhs_value_type,
  //                                                              value_type rhs_value_type,
  //                                                              uint32_t lhs_object_type,
  //                                                              uint32_t rhs_object_type)
  // {
  //   return (static_cast<uint64_t>(lhs_object_type << 8 | to_utype(lhs_value_type)) << 32) |
  //          static_cast<uint64_t>(rhs_object_type << 8 | to_utype(rhs_value_type));
  // }

  // TODO(Qais): DRY bitch
  constexpr uint64_t combine_value_types_with_object_types(const value_t& p_lhs, const value_t& p_rhs)
  {
    const uint8_t lhs_value_type = get_value_type(p_lhs);
    const uint8_t rhs_value_type = get_value_type(p_rhs);

    const uint32_t lhs_object_type =
        OK_IS_VALUE_OBJECT(p_lhs) ? OK_VALUE_AS_OBJECT(p_lhs)->get_type() : object_type::none;
    const uint32_t rhs_object_type =
        OK_IS_VALUE_OBJECT(p_rhs) ? OK_VALUE_AS_OBJECT(p_rhs)->get_type() : object_type::none;

    const uint8_t lhs_is_instance = OK_IS_VALUE_OBJECT(p_lhs) ? OK_VALUE_AS_OBJECT(p_lhs)->is_instance() : 0;
    const uint8_t rhs_is_instance = OK_IS_VALUE_OBJECT(p_rhs) ? OK_VALUE_AS_OBJECT(p_rhs)->is_instance() : 0;

    return (static_cast<uint64_t>(lhs_object_type << 8 | (lhs_value_type << 7 | lhs_is_instance)) << 32) |
           static_cast<uint64_t>(rhs_object_type << 8 | (rhs_value_type << 7 | rhs_is_instance));
  }

  constexpr uint32_t make_callable_key(const value_t& p_callable, uint8_t p_argc)
  {
    // not sure if we need the instance info here, but sure later we should pack the argc for caller preventing calls
    // with mismatched argc
    const uint32_t callable_type =
        (OK_VALUE_AS_OBJECT(p_callable)->get_type() << 8) | (OK_VALUE_AS_OBJECT(p_callable)->is_instance());
    return callable_type;
  }

  bool object_register_builtins(vm* p_vm);
  void object_inherit(class_object* p_super, class_object* p_sub);
  void register_base_methods(object* p_object);
} // namespace ok
#endif // OK_OBJECT_HPP
