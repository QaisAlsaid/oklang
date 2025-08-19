#ifndef OK_CHUNK_HPP
#define OK_CHUNK_HPP

#include <cstdint>
#include <memory>
#include <print>
#include <span>
#include <vector>

#include "utility.hpp"
#include "value.hpp"

namespace ok
{
  using byte = uint8_t;
  // TODO(Qais): make constant index size spans 4 bytes not only 1, thus the type uint32_t
  enum class opcode : byte
  {
    op_return,
    op_constant,
    op_constant_long,
    op_negate,
    op_add,
    op_subtract,
    op_multiply,
    op_divide,
  };
  constexpr auto op_constant_max_count = UINT8_MAX;
  constexpr auto uint24_max = (1 << 24) - 1;
  constexpr auto op_constant_long_max_count = uint24_max; // UINT24_MAX

  // TODO(Qais): add sized writes, i.e. the constant write should be dependant on constant_index_type. and so on
  // Update: done needs testing and maybe templating
  struct chunk
  {
    inline void write(const byte p_byte, const size_t p_offset)
    {
      code.push_back(p_byte);
      write_offset(p_offset);
      // offsets.push_back(p_offset);
    }

    inline void write(const opcode p_opcode, const size_t p_offset)
    {
      code.push_back(to_utype(p_opcode));
      // offsets.push_back(p_offset);
      write_offset(p_offset);
    }

    inline void write(const std::span<const byte> p_bytes, const size_t p_offset)
    {
      code.append_range(p_bytes);
      write_offset(p_offset, p_bytes.size());
    }

    inline void write_constant(const value_type p_value, const size_t p_offset)
    {
      constants.push_back(p_value);
      const auto index = constants.size() - 1;
      if(index < op_constant_max_count + 1)
      {
        write(opcode::op_constant, p_offset);
        write(index, p_offset);
      }
      else if(index < op_constant_long_max_count + 1)
      {
        write(opcode::op_constant_long, p_offset);
        const auto span = encode_int<size_t, 3>(index);
        // const auto span = std::span<const byte>{(const byte*)std::addressof(index), 3};
        for(auto s : span)
          std::println("byte: {}", (uint8_t)s);
        write(span, p_offset); // 3 bytes since it is uint24
      }
      // TODO(Qais): else error: out of constants
    }

    // only valid when constants.size() < UINT8_MAX, otherwise use write_constant.
    // just use write_constant this is deprecated!
    inline uint8_t add_constant(const value_type p_value)
    {
      constants.push_back(p_value);
      return constants.size() - 1;
    }

    inline void write_offset(const size_t p_offset, const size_t range_count = 1)
    {
      if(offsets.empty())
      {
        offsets.push_back({p_offset, range_count});
        return;
      }
      auto& back = offsets.back();
      if(back.offset == p_offset)
        back.reps += range_count;
      else
        offsets.push_back({p_offset, range_count});
    }

    size_t get_offset(const size_t instruction_index) const
    {
      // each rep maps to an instruction
      for(size_t num_inst = 0; const auto& instruction_offset : offsets)
      {
        num_inst += instruction_offset.reps;
        if(num_inst > instruction_index)
          return instruction_offset.offset;
      }
      return 0; // couldnt find it!
    }

    std::vector<byte> code;
    value_array constants;
    // TODO(Qais): find a better way of storing this WITHOUT cluttering the bytecode array
    // [legacy]: std::vector<size_t> offsets; // stores offset to the first character mapping to the bytecode
    // instruction Update: here is a new version that is not as wasteful as the last one, but still dont think its that
    // great either
    struct offset_with_rep
    {
      size_t offset;
      size_t reps;
    };
    std::vector<offset_with_rep> offsets;
  };
} // namespace ok
#endif // OK_CHUNK_HPP