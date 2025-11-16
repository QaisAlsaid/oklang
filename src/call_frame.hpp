#ifndef OK_CALL_FRAME_HPP
#define OK_CALL_FRAME_HPP

#include "chunk.hpp"
#include <cstddef>

namespace ok
{
  class closure_object;

  struct call_frame
  {
    closure_object* closure;
    byte* ip;
    size_t slots = 0;
    size_t top = 0;
    value_t saved_slot{};
  };

} // namespace ok

#endif // OK_CALL_FRAME_HPP