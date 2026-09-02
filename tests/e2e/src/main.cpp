#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <print>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>

std::string read_file(const std::string_view path) {
  std::ifstream f(path.data());
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

struct run_result {
  int exit_code;
  std::string std_out;
  std::string std_err;
};

int decode_system_status(int status) {
  if (status == -1)
    return -1;
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
  return status;
}

run_result run_oklang(const std::string_view path, const std::string_view okpath) {
  std::stringstream cmd;
  std::string stdout_file = "out.log";
  std::string stderr_file = "err.log";
  cmd << "\"" << okpath << "\" " << "\"" << path << "\" > " << stdout_file << " 2> " << stderr_file;

  std::string cmd_str = cmd.str();
  int ret = system(cmd_str.c_str());
  return {decode_system_status(ret), read_file("out.log"), read_file("err.log")};
}

struct test_case {
  std::string name;
  std::string expected_result;
  std::optional<std::string> expected_error;
  bool has_expectation = false;
};

test_case parse_test_file(const std::filesystem::path& p_path) {
  test_case tc;
  tc.name = p_path;
  std::ifstream fs(p_path);
  std::stringstream expected;
  std::string line;
  std::regex expect_out_re(R"(//\s*expect:\s*(.*))");
  std::regex expect_err_re(R"(//\s*expect\s+error(?::\s*(.*))?)");

  while (std::getline(fs, line)) {
    std::smatch match;
    if (std::regex_search(line, match, expect_out_re)) {
      expected << match[1].str() << "\n";
      tc.has_expectation = true;
    } else if (std::regex_search(line, match, expect_err_re)) {
      if (match.size() > 1)
        tc.expected_error = match[1].str();
      else
        tc.expected_error = "";
      tc.has_expectation = true;
    }
  }
  tc.expected_result = expected.str();
  return tc;
}

void fail(uint32_t& p_notok, const std::string_view p_ok_debug_path, const std::string_view p_path) {
  p_notok++;
  if (!p_ok_debug_path.empty()) {
    std::println("running failed test using debug build");
    auto res = run_oklang(p_path, p_ok_debug_path);
    std::println("exit code: {}\nstdout:\n{}\nstderr:\n{}", res.exit_code, res.std_out, res.std_err);
  }
}

bool is_quarantined(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (part == "quarantine")
      return true;
  }
  return false;
}

int main(int argc, char** argv) {
  bool has_debug_build = false;
  if (argc < 3) {
    std::println(stderr, "expected 2 arguments <OK PATH> <TESTS PATH>");
    return 1;
  } else if (argc > 4) {
    std::println(
        stderr,
        "too many arguments, expected 2 arguments <OK PATH> <TESTS PATH>, and 1 optional argument <OK DEBUG PATH>");
    return 1;
  } else if (argc == 4) {
    has_debug_build = true;
  }

  std::string okpath = argv[1], tests_path = argv[2], ok_debug_path;
  if (has_debug_build) {
    ok_debug_path = argv[3];
  }

  uint32_t ok = 0, notok = 0, skipped = 0;

  for (auto& entry : std::filesystem::recursive_directory_iterator(tests_path)) {
    if (entry.path().extension() != ".ok")
      continue;

    std::string test_path = entry.path();
    if (is_quarantined(entry.path())) {
      skipped++;
      std::println("[SKIP]: {}", test_path);
      continue;
    }

    auto tc = parse_test_file(test_path);
    auto rr = run_oklang(test_path, okpath);

    if (!tc.has_expectation) {
      std::println("[NOTOK]: '{}'", test_path);
      std::println("no // expect or // expect error directive");
      fail(notok, ok_debug_path, test_path);
      continue;
    }

    if (tc.expected_error.has_value()) {
      if (rr.exit_code == 0) {
        std::println("[NOTOK]: '{}'", test_path);
        std::println("expected error, but non occurred");
        std::println("got:\nstdout: {}\nstderr: {}", rr.std_out, rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else if (!tc.expected_error->empty() && rr.std_err.find(tc.expected_error.value()) == std::string::npos) {
        std::println("[NOTOK]: '{}'", test_path);
        std::println("error message mismatch");
        std::println("expected:\n{}", tc.expected_error.value());
        std::println("got:\n{}", rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else {
        ok++;
        std::println("[OK]: {}", test_path);
      }
    } else {
      if (rr.exit_code != 0) {
        std::println("[NOTOK]: '{}'", test_path);
        std::println("expected success (exit 0), got exit {}", rr.exit_code);
        std::println("got:\nstdout: {}\nstderr: {}", rr.std_out, rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else if (rr.std_out != tc.expected_result) {
        std::println("[NOTOK]: '{}'", test_path);
        std::println("output mismatch");
        std::println("expected:\n{}", tc.expected_result);
        std::println("got:\nstdout: {}\nstderr: {}", rr.std_out, rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else {
        ok++;
        std::println("[OK]: {}", test_path);
      }
    }
  }

  std::println("\nsummary: [{} OK], [{} NOTOK], [{} SKIP]", ok, notok, skipped);
  return notok == 0 ? 0 : 1;
}
