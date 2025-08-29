#ifndef OK_VM_HPP
#define OK_VM_HPP

#include "chunk.hpp"
#include "interned_string.hpp"
#include "operator.hpp"
#include "value.hpp"
#include <array>
#include <expected>
#include <string_view>

namespace ok
{
  using vm_id = uint32_t;
  class vm
  {
  public:
    enum class interpret_result
    {
      ok,
      compile_error,
      runtime_error
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

  private:
    interpret_result run();
    value_t read_constant(opcode p_op);
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

    void destroy_objects_list();

  private:
    chunk* m_chunk;
    byte* m_ip; // TODO(Qais): move this to local storage
    uint32_t m_id;
    std::vector<value_t> m_stack; // is a vector with stack protocol better than std::stack? Update: yes i think so
    interned_string m_interned_strings;
    object* m_objects_list; // intrusive linked list
    constexpr static size_t stack_base_size = 256;
  };
} // namespace ok

#endif // OK_VM_HPP