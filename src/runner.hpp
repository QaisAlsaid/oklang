#ifndef OK_RUNNER_HPP
#define OK_RUNNER_HPP

#include "vm.hpp"
#include <expected>
#include <filesystem>

namespace ok
{
  struct runner
  {
    enum class error
    {
      file_not_found,
      no_permission,
      not_a_file,
    };

    static std::expected<vm::interpret_result, error> start(const std::filesystem::path& file);
  };
} // namespace ok

#endif // OK_RUNNER_HPP