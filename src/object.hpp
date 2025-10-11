#ifndef OK_OBJECT_HPP
#define OK_OBJECT_HPP

#include "call_frame.hpp"
#include "chunk.hpp"
#include "macros.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <unordered_map>

// TODO(Qais): each object type in separate header file and they all share same implementation file

namespace ok
{
  // enum class object_type : uint8_t // TODO(Qais): for now its uint8_t and enum class, you should do this refactor
  // asap
  //  before its too hard
  //{
  //  none, // compatiblity with _make_object_key
  //  obj_string,
  // };

  namespace object_type
  {
    enum
    {
      none = 0,
      obj_string,
      obj_function,
      obj_native_function,
      obj_closure,
      obj_upvalue,
      obj_class,
      obj_instance,
      obj_bound_method,
    };
  }

  struct object
  {
    object* next = nullptr;
    object(uint32_t p_type);
    // 24bit integer
    inline uint32_t get_type() const
    {
      // return type & 0x00ffffff;
      return type;
    }

    inline void set_type(uint32_t p_type)
    {
      ASSERT(p_type <= uint24_max);
      // type = (type & 0xff000000) | (p_type & 0x00ffffff);
      type = p_type;
    }

    inline bool is_registered() const
    {
      // return (type & (1u << 24)) != 0;
      return _is_registered;
    }

    inline void set_registered(bool p_is_registered)
    {
      p_is_registered ? type |= (1u << 24) : type &= ~(1u << 24);
      //_is_registered = p_is_registered;
    }

    inline bool is_marked() const
    {
      // return (type & (1u << 25)) != 0;
      return _is_marked;
    }

    inline void set_marked(bool p_mark)
    {
      // p_mark ? type |= (1u << 25) : type &= ~(1u << 25);
      _is_marked = p_mark;
    }

  private:
    uint32_t
        type; // 24bit for the type => 16 bit for the object type, and 8 bits for the version (related to inheritance),
              // and 1 bit for the _is_registered boolean, and one bit for the _is_marked boolean, and 6 free bits

    bool _is_registered = false;
    bool _is_marked = false;
  };

  struct string_object
  {
    // create a string object (allocates char buffer on heap)
    string_object(const std::string_view p_src);
    // not implemented!
    [[deprecated("not implemented yet!")]] string_object(std::span<std::string_view> p_srcs);

    ~string_object();

    template <typename Obj = object>
    static Obj* create(const std::string_view p_src);
    template <typename Obj = object>
    static Obj* create(const std::span<std::string_view> p_srcs);

    object up;
    hashed_string hash_code;
    size_t length;
    char* chars;

  private:
    string_object();

  private:
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_string);
    static std::expected<value_t, value_error> plus(object* p_this, value_t p_sure_is_string);
    static void print(object* p_this);
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
  struct function_object
  {
    function_object(uint8_t p_arity, string_object* p_name);
    ~function_object();

    template <typename Obj = object>
    static Obj* create(uint8_t p_arity, string_object* p_name);

    object up;
    chunk associated_chunk;
    string_object* name = nullptr;
    uint32_t upvalues = 0;
    uint8_t arity = 0;

  private:
    function_object();
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_function);
    static std::expected<value_t, value_error> equal_impl(function_object* p_this, function_object* p_other);
    static void print(object* p_this);
    static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
    friend struct closure_object;
  };

  typedef value_t (*native_function)(uint8_t argc, value_t* argv);

  struct native_function_object
  {
    native_function_object(native_function p_function);
    ~native_function_object();

    template <typename Obj = object>
    static Obj* create(native_function p_function);

    object up;
    native_function function;

  private:
    native_function_object();
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_native_function);
    static void print(object* p_this);
    static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
  };

  struct upvalue_object
  {
    upvalue_object(value_t* slot);
    ~upvalue_object();

    template <typename Obj = object>
    static Obj* create(value_t* slot);

    object up;
    value_t* location = nullptr;
    upvalue_object* next = nullptr;
    value_t closed{};

  private:
    upvalue_object();
    static void print(object* p_this);
  };

  struct closure_object
  {
    closure_object(function_object* p_function);
    ~closure_object();

    template <typename Obj = object>
    static Obj* create(function_object* p_function);

    object up;
    function_object* function;
    std::vector<upvalue_object*> upvalues;

  private:
    closure_object();
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_closure);
    static void print(object* p_this);
    static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
    friend struct bound_method_object;
  };

  struct class_object
  {
    class_object(string_object* p_name);
    ~class_object();

    template <typename Obj = object>
    static Obj* create(string_object* p_name);

    object up;
    string_object* name;
    std::unordered_map<string_object*, value_t> methods;

  private:
    class_object();
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_class);
    static void print(object* p_this);
    static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
  };

  struct instance_object
  {
    instance_object(class_object* p_class);
    ~instance_object();

    template <typename Obj = object>
    static Obj* create(class_object* p_class);

    object up;
    class_object* class_;
    std::unordered_map<string_object*, value_t> fields;

  private:
    instance_object();
    static void print(object* p_this);
    // static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_instance);
    // static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
  };

  struct bound_method_object
  {
    bound_method_object(value_t p_receiver, closure_object* p_method);
    ~bound_method_object();

    template <typename Obj = object>
    static Obj* create(value_t p_receiver, closure_object* p_method);

    object up;
    value_t receiver;
    closure_object* method;

  private:
    bound_method_object();
    static void print(object* p_this);
    static std::expected<value_t, value_error> equal(object* p_this, value_t p_sure_is_bound_method);
    static std::expected<std::optional<call_frame>, value_error> call(object* p_this, uint8_t p_argc);
  };

  template <typename T, typename... Args>
  object* new_object(Args&&... args)
  {
    auto& gc = get_vm_gc();
    gc.increment_used_memory(sizeof(T));
    return T::create(std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  T* new_tobject(Args&&... args)
  {
    auto& gc = get_vm_gc();
    gc.increment_used_memory(sizeof(T));
    return T::template create<T>(std::forward<Args>(args)...);
  }

  void delete_object(object* p_object);

  // leaves a 24bit integer room for objects which is more than we ever will need
  constexpr uint32_t _make_object_key(const operator_type p_operator,
                                      const value_type p_rhs,
                                      const uint32_t p_other_object_type = object_type::none)
  {
    if(p_other_object_type == object_type::none)
    {
      return static_cast<uint32_t>(to_utype(p_operator)) << 24 | static_cast<uint32_t>(to_utype(p_rhs)) | 16;
    }
    else
    {
      return static_cast<uint32_t>(to_utype(p_operator)) << 24 | (p_other_object_type);
    }
  }
} // namespace ok
#endif // OK_OBJECT_HPP