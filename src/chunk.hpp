#ifndef OK_CHUNK_HPP
#define OK_CHUNK_HPP

#include "macros.hpp"
#include "utility.hpp"
#include "value.hpp"
#include "vm_stack.hpp"
#include <cstdint>
#include <print>
#include <span>
#include <utility>
#include <vector>

namespace ok
{
  struct local
  {
    std::string name;
    int depth;
    bool is_captured = false;
    bool is_imported_module = false;
  };
  using byte = uint8_t;
  enum class opcode : byte
  {
    // internal idk
    op_invalid,
    op_pop,
    op_pop_n, // no long version, max is 255 pops. other than that emit more than one of these
    op_constant,
    op_constant_long,
    op_conditional_jump, // jumps are long by default (24bit) integer operator
    op_conditional_truthy_jump,
    op_jump,
    op_loop,
    op_call, // kinda operator!
    op_closure,
    op_class,
    op_class_long,
    op_method,
    op_method_long,
    op_invoke,
    op_invoke_long,
    op_inherit,
    op_get_super,
    op_get_super_long,
    op_invoke_super,
    op_invoke_super_long,
    // operators
    op_negate,
    op_add,
    op_subtract,
    op_multiply,
    op_divide,
    op_not,
    op_equal,
    op_not_equal,
    op_greater,
    op_less,
    op_greater_equal,
    op_less_equal,
    // idk name
    op_null,
    op_true,
    op_false,
    // statements
    op_print,
    op_return,

    // globals
    op_define_global,
    op_define_global_long,
    op_get_global,
    op_get_global_long,
    op_set_global,
    op_set_global_long,

    // locals
    // no define cuz there is nothing to do since its stack based operation
    op_get_local,
    op_get_local_long,
    op_set_local,
    op_set_local_long,

    // upvalues
    op_get_upvalue,
    op_get_upvalue_long,
    op_set_up_value,
    op_set_upvalue_long,
    op_close_upvalue,

    // properties
    op_get_property,
    op_get_property_long,
    op_set_property,
    op_set_property_long,
  };
  constexpr uint32_t op_constant_max_count = UINT8_MAX;
  constexpr uint32_t uint24_max = (1 << 24) - 1;
  constexpr uint32_t op_constant_long_max_count = uint24_max;
  constexpr uint32_t op__global_max_count = UINT8_MAX;
  constexpr uint32_t op__global_long_max_count = uint24_max;

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

    inline void patch(const std::span<const byte> p_bytes, const size_t p_position)
    {
      ASSERT(p_position < code.size());
      for(size_t i = 0; auto b : p_bytes)
        code[p_position + i++] = b;
    }

    inline void write_constant(const value_t p_value, const size_t p_offset)
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
#if defined(PARANOID)
        for(auto s : span)
          TRACELN("byte: {}", (uint8_t)s);
#endif
        write(span, p_offset); // 3 bytes since it is uint24
      }
      // TODO(Qais): else error: out of constants
    }

    // returns pair of <is_long, index>, and it doesnt write anything to the table
    // lmao wont the index tell you that its long or not, why the boolean lol
    uint32_t add_identifier(const value_t p_global, const size_t p_offset)
    {
      ASSERT(p_global.type == value_type::object_val);
      identifiers.push_back(p_global);
      const auto index = identifiers.size() - 1;
      if(index < op__global_max_count + 1)
      {
        // write(is_define ? opcode::op_define_global : opcode::op_get_global, p_offset);
        // write(index, p_offset);
        return index;
      }
      else if(index < op__global_long_max_count + 1)
      {
        // write(is_define ? opcode::op_define_global_long : opcode::op_get_global_long, p_offset);
        // const auto span = encode_int<size_t, 3>(index);
#if defined(PARANOID)
        // for(auto s : span)
        //   TRACELN("byte: {}", (uint8_t)s);
#endif
        // write(span, p_offset);
        return index;
      }
      return UINT32_MAX; // error cant add more
    }

    inline uint32_t add_constant(const value_t p_value, const size_t p_offset)
    {
      constants.push_back(p_value);
      const auto index = constants.size() - 1;
      if(index < op_constant_max_count + 1)
      {
        write(index, p_offset);
        return index;
      }
      else if(index < op_constant_long_max_count + 1)
      {
        const auto span = encode_int<size_t, 3>(index);
#if defined(PARANOID)
        for(auto s : span)
          TRACELN("byte: {}", (uint8_t)s);
#endif
        write(span, p_offset); // 3 bytes since it is uint24
        return index;
      }
      return UINT32_MAX;
      // ASSERT(false);
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
    value_array identifiers;
    // std::vector<local> m_locals;
    struct offset_with_rep
    {
      size_t offset;
      size_t reps;
    };
    std::vector<offset_with_rep> offsets;
  };
} // namespace ok
#endif // OK_CHUNK_HPP