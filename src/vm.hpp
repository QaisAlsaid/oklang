#ifndef OK_VM_HPP
#define OK_VM_HPP

#include "chunk.hpp"
#include "value.hpp"
#include <array>
#include <stack>
namespace ok
{
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
    interpret_result interpret(chunk* p_chunk);

  private:
    interpret_result run();
    value_type read_constant(opcode p_op);
    byte read_byte();

    template <size_t N>
    std::array<byte, N> read_bytes()
    {
      std::array<byte, N> bytearray;
      for(size_t i = 0; i < N; ++i)
        bytearray[i] = read_byte();
      return bytearray;
    }

  private:
    chunk* m_chunk;
    byte* m_ip;                      // TODO(Qais): move this to local storage
    std::vector<value_type> m_stack; // is a vector with stack protocol better than std::stack? Update: yes u think so
    constexpr static size_t stack_base_size = 256;
  };
} // namespace ok

#endif // OK_VM_HPP