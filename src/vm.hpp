#ifndef OK_VM_HPP
#define OK_VM_HPP

#include "chunk.hpp"
#include "compiler.hpp"
#include "interned_string.hpp"
#include "log.hpp"
#include "object.hpp"
#include "operator.hpp"
#include "value.hpp"
#include "value_operations.hpp"
#include <array>
#include <expected>
#include <string_view>
#include <unordered_map>

namespace ok
{
  using vm_id = uint32_t;
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
    using object_value_operations_infix_binary =
        value_operations_base<uint32_t, operation_function_infix_binary, operations_return_type, object*, value_t>;
    using object_value_operations_prefix_unary =
        value_operations_base<uint16_t, operation_function_prefix_unary, operations_return_type, object*>;

    struct object_value_operations
    {
      object_value_operations_infix_binary binary_infix;
      object_value_operations_prefix_unary unray_prefix;
      operation_function_print print_function;
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

    inline object_value_operations* get_object_operations(object_type type)
    {
      const auto& it = m_objects_operations.find(type);
      if(m_objects_operations.end() == it)
        return nullptr;
      return &it->second;
    }

    // overwrites current if exists
    inline object_value_operations& register_object_operations(object_type type)
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

    void print_value(value_t p_value);

  private:
    interpret_result run();
    // TODO(Qais): refactor all functions that take either one byte operand or 24bit int operand into sourceing one
    // place for getting the values
    value_t read_constant(opcode p_op);
    value_t read_global_definition(bool p_is_long);
    value_t& read_local(bool is_long);
    byte read_byte();
    void runtime_error(const std::string& err); // TODO(Qais): error types and format strings

    template <size_t N>
    std::array<byte, N> read_bytes()
    {
      std::array<byte, N> bytearray;
      for(size_t i = 0; i < N; ++i)
        bytearray[i] = read_byte();
      return bytearray;
    }

  private:
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

    void destroy_objects_list();

  private:
    chunk* m_chunk;
    byte* m_ip; // TODO(Qais): move this to local storage
    uint32_t m_id;
    std::vector<value_t> m_stack; // is a vector with stack protocol better than std::stack? Update: yes i think so
    interned_string m_interned_strings;
    object* m_objects_list; // intrusive linked list
    // TODO(Qais): integer based globals, with the compiler defining those ints, is much faster than hash map lookup.
    // maybe do it when adding optimization pass
    std::unordered_map<string_object*, value_t> m_globals;
    // yes another indirection, shutup you cant eliminate all indirections
    std::unordered_map<object_type, object_value_operations> m_objects_operations;
    logger m_logger;
    compiler m_compiler; // temporary
    constexpr static size_t stack_base_size = 256;
  };
} // namespace ok

#endif // OK_VM_HPP