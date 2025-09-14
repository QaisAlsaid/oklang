#ifndef OK_CALL_FRAME_HPP
#define OK_CALL_FRAME_HPP

#include "chunk.hpp"
#include <cstddef>

namespace ok
{
  class function_object;

  struct call_frame
  {
    function_object* function;
    byte* ip;
    size_t slots = 0;
    size_t top = 0;
  };

} // namespace ok

#endif // OK_CALL_FRAME_HPP