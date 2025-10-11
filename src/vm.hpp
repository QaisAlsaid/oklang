#ifndef OK_VM_HPP
#define OK_VM_HPP

#include "call_frame.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "constants.hpp"
#include "gc.hpp"
#include "interned_string.hpp"
#include "log.hpp"
#include "macros.hpp"
#include "object.hpp"
#include "operator.hpp"
#include "value.hpp"
#include "value_operations.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ok
{
  using vm_id = uint32_t;

  template <typename T>
  class stack
  {
  public:
    stack(size_t p_initial_size) : m_storeage(p_initial_size)
    {
    }

    void push(const T& p_value)
    {
      if(m_top == m_storeage.size())
      {
        m_storeage.push_back(p_value);
      }
      else
      {
        m_storeage[m_top++] = p_value;
      }
    }

    const T& top(size_t p_index = 0) const
    {
      ASSERT(!m_storeage.empty());
      return m_storeage[m_top - 1 - p_index];
    }

    T& top(size_t p_index = 0)
    {
      ASSERT(!m_storeage.empty());
      return m_storeage[m_top - 1 - p_index];
    }

    T pop()
    {
      ASSERT(!m_storeage.empty());
      ASSERT(m_top > 0);
      T temp = top();
      if(m_top != 0)
        m_top--;
      return temp;
    }

    T& operator[](const size_t p_index)
    {
      ASSERT(p_index < m_storeage.size());
      return m_storeage[p_index];
    }

    const T* value_ptr(size_t start = 0) const
    {
      return m_storeage.data() + start;
    }

    T* value_ptr(size_t start = 0)
    {
      return m_storeage.data() + start;
    }

    T* value_ptr_top(size_t start = 0)
    {
      return m_storeage.data() + m_top - start - 1;
    }

    const T* value_ptr_top(size_t start = 0) const
    {
      return m_storeage.data() + m_top - start - 1;
    }

    std::vector<T>::iterator begin()
    {
      return m_storeage.begin();
    }

    std::vector<T>::iterator end()
    {
      return m_storeage.begin() + m_top;
    }

    void resize(size_t p_new_top)
    {
      ASSERT(p_new_top < m_storeage.size());
      m_top = p_new_top;
      +1;
    }

    size_t size() const
    {
      return m_top;
    }

    bool empty() const
    {
      return m_top == 0 || m_storeage.empty();
    }

  private:
    std::vector<T> m_storeage;
    size_t m_top = 0;
  };

  class vm
  {
  public:
    enum class interpret_result
    {
      ok,
      parse_error,
      compile_error,
      runtime_error,
    };

    using operations_return_type = std::expected<value_t, value_error>;
    using operation_function_infix_binary = std::function<operations_return_type(object* lhs, value_t rhs)>;
    using operation_function_prefix_unary = std::function<operations_return_type(object* _this)>;
    using operation_function_print = std::function<void(object* _this)>;
    using operation_function_call =
        std::function<std::expected<std::optional<call_frame>, value_error>(object* _this, uint8_t argc)>;
    using object_value_operations_infix_binary =
        value_operations_base<uint32_t, operation_function_infix_binary, operations_return_type, object*, value_t>;
    using object_value_operations_prefix_unary =
        value_operations_base<uint16_t, operation_function_prefix_unary, operations_return_type, object*>;

    struct object_value_operations
    {
      object_value_operations_infix_binary binary_infix;
      object_value_operations_prefix_unary unray_prefix;
      operation_function_print print_function;
      operation_function_call call_function;
    };

    struct statics
    {
      void init()
      {
        init_string = new_tobject<string_object>(constants::init_string_literal);
        deinit_string = new_tobject<string_object>(constants::deinit_string_literal);
      }

      string_object* init_string = nullptr;
      string_object* deinit_string = nullptr;
    };

  public:
    vm();
    ~vm();
    interpret_result interpret(const std::string_view p_source);

    inline object*& get_objects_list()
    {
      return m_objects_list;
    }

    inline interned_string& get_interned_strings()
    {
      return m_interned_strings;
    }

    inline object_value_operations* get_object_operations(uint32_t type)
    {
      const auto& it = m_objects_operations.find(type);
      if(m_objects_operations.end() == it)
        return nullptr;
      return &it->second;
    }

    // overwrites current if exists
    inline object_value_operations& register_object_operations(uint32_t type)
    {
      m_objects_operations[type] = {};
      return m_objects_operations[type];
    }

    inline logger& get_logger()
    {
      return m_logger;
    }

    inline const compiler::errors& get_compile_errors() const
    {
      return m_compiler.get_compile_errors();
    }

    inline const parser::errors& get_parse_errors() const
    {
      return m_compiler.get_parse_errors();
    }

    void destroy_objects_list();

    inline std::expected<void, interpret_result> push_call_frame(call_frame p_call_frame)
    {
      if(m_call_frames.size() >= call_frame_max_size)
      {
        runtime_error("stackoverflow");
        return std::unexpected(interpret_result::runtime_error);
      }
      // if(!m_call_frames.empty())
      //   m_call_frames.back().top = m_stack.size();
      m_call_frames.push_back(p_call_frame);
      return {};
    }

    inline void update_call_frame_top_index()
    {
      if(!m_call_frames.empty())
        m_call_frames.back().top = m_stack.size();
    }

    inline size_t frame_stack_top()
    {
      ASSERT(!m_stack.empty());
      return m_call_frames.back().top;
    }

    inline size_t frame_stack_slots()
    {
      ASSERT(!m_call_frames.empty());
      return m_call_frames.back().slots;
    }

    inline const call_frame& get_current_call_frame() const
    {
      return m_call_frames.back();
    }

    inline call_frame& get_current_call_frame()
    {
      return m_call_frames.back();
    }

    inline value_t* stack_at(size_t p_pos)
    {
      return m_stack.value_ptr(p_pos);
    }

    inline const value_t* stack_at(size_t p_pos) const
    {
      return m_stack.value_ptr(p_pos);
    }

    inline const value_t& stack_top(size_t p_pos) const
    {
      return m_stack.top(p_pos);
    }

    inline value_t& stack_top(size_t p_pos)
    {
      return m_stack.top(p_pos);
    }

    inline void stack_push(value_t p_val)
    {
      m_stack.push(p_val);
    }

    inline void stack_resize(size_t p_new_top)
    {
      m_stack.resize(p_new_top);
    }

    inline gc& get_gc()
    {
      return m_gc;
    }

    inline const statics& get_statics() const
    {
      return m_statics;
    }

    bool define_native_function(std::string_view p_name, native_function p_fu);

    void print_value(value_t p_value);
    bool call_value(uint8_t argc, value_t callee);
    void runtime_error(const std::string& err); // TODO(Qais): error types and format strings

  private:
    interpret_result run();
    // TODO(Qais): refactor all functions that take either one byte operand or 24bit int operand into sourceing one
    // place for getting the values
    value_t read_constant(opcode p_op);
    value_t read_variable_definition(bool p_is_long);
    value_t& read_local(bool is_long);
    byte read_byte();

    template <size_t N>
    std::array<byte, N> read_bytes()
    {
      std::array<byte, N> bytearray;
      for(size_t i = 0; i < N; ++i)
        bytearray[i] = read_byte();
      return bytearray;
    }

    void reset_stack();
    std::expected<void, interpret_result> perform_unary_prefix(const operator_type p_operator);
    std::expected<void, interpret_result> perform_binary_infix(const operator_type p_operator);
    std::expected<void, interpret_result> perform_unary_infix(const operator_type p_operator);

    std::expected<value_t, value_error> perform_unary_prefix_real(const operator_type p_operator, const value_t p_this);
    std::expected<value_t, value_error> perform_unary_prefix_real_object(operator_type p_operator, value_t p_this);

    std::expected<value_t, value_error>
    perform_binary_infix_real(const value_t& p_this, const operator_type p_operator, const value_t& p_other);

    std::expected<value_t, value_error>
    perform_binary_infix_real_object(object* p_this, operator_type p_operator, value_t p_other);
    void print_object(object* p_object);
    bool is_value_falsy(value_t p_value) const;
    upvalue_object* capture_value(size_t p_slot);
    void close_upvalue(value_t* p_value);

    void define_method(string_object* p_name);
    bool bind_a_method(class_object* p_class, string_object* p_name);
    bool invoke(string_object* p_method_name, uint8_t p_argc);
    bool invoke_from_class(class_object* p_class, string_object* p_method_name, uint8_t p_argc);
    bool call(uint8_t p_argc, closure_object* p_callee);

    inline void pop_call_frame()
    {
      ASSERT(!m_call_frames.empty());
      m_call_frames.pop_back();
    }

  private:
    // chunk* m_chunk;
    // byte* m_ip; // TODO(Qais): move this to local storage
    gc m_gc;
    stack<value_t> m_stack;
    std::vector<call_frame> m_call_frames;
    uint32_t m_id;
    // std::vector<value_t> m_stack; // is a vector with stack protocol better than std::stack? Update: yes i think so
    interned_string m_interned_strings;
    object* m_objects_list; // intrusive linked list
    upvalue_object* m_open_upvalues = nullptr;
    // TODO(Qais): integer based globals, with the compiler defining those ints, is much faster than hash map lookup.
    // maybe do it when adding optimization pass
    std::unordered_map<string_object*, value_t> m_globals;
    // yes another indirection, shutup you cant eliminate all indirections
    std::unordered_map<uint32_t, object_value_operations> m_objects_operations;
    logger m_logger;
    compiler m_compiler; // temporary
    statics m_statics;
    constexpr static size_t call_frame_max_size = 64;
    constexpr static size_t stack_base_size = (UINT8_MAX + 1) * call_frame_max_size;

  private:
    friend class gc;
  };
} // namespace ok

#endif // OK_VM_HPP