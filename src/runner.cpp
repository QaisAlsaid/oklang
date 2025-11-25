#include "runner.hpp"
#include "vm.hpp"
#include "vm_stack.hpp"
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ok
{
  auto runner::start(const std::filesystem::path& p_file) -> std::expected<vm::interpret_result, error>
  {
    ok::vm vm;
    ok::vm_guard guard{&vm};
    vm.init();

    if(!std::filesystem::exists(p_file))
    {
      return std::unexpected{error::file_not_found};
    }

    const auto status = std::filesystem::status(p_file);
    if(!std::filesystem::is_regular_file(status))
    {
      return std::unexpected{error::not_a_file};
    }
    const auto perms = status.permissions();
    const auto read = (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
    if(!read)
    {
      return std::unexpected{error::no_permission};
    }

    const std::ifstream fstream(p_file);
    auto file_str = p_file.string();
    std::stringstream ss;
    ss << fstream.rdbuf();
    const auto& src_str = ss.str();

    const auto res = vm.interpret(file_str, src_str);
    switch(res)
    {
    case ok::vm::interpret_result::parse_error:
      vm.get_parse_errors().show();
      break;
    case ok::vm::interpret_result::compile_error:
      vm.get_compile_errors().show();
      break;
    case ok::vm::interpret_result::runtime_error:
    case ok::vm::interpret_result::ok:
      break;
    }
    return res;
  }
} // namespace ok