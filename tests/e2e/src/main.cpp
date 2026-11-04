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

#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLUE "\x1b[34m"
#define COLOR_RESET "\x1b[0m"

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

run_result run_oklang(const std::string_view path, const std::string_view okpath, const std::string_view okpath_value,
                      const std::string_view flags = "") {
  std::stringstream cmd;
  std::string stdout_file = "out.log";
  std::string stderr_file = "err.log";
  cmd << "OKPATH=\"" << okpath_value << "\" \"" << okpath << "\" " << flags << (flags.empty() ? "" : " ")
      << "\"" << path << "\" > " << stdout_file << " 2> " << stderr_file;

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
  // if (!p_ok_debug_path.empty()) {
  //   std::println("running failed test using debug build");
  //   auto res = run_oklang(p_path, p_ok_debug_path);
  //   std::println("exit code: {}\nstdout:\n{}\nstderr:\n{}", res.exit_code, res.std_out, res.std_err);
  // }
}

bool is_quarantined(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (part == "quarantine")
      return true;
  }
  return false;
}

// OR-1 module tests: a directory holding main.ok is ONE test case - main.ok
// is the entry, every other .ok under it is a fixture. any ancestor directory
// up to the tests root that holds a main.ok claims the file (except the entry
// itself); test directories are never nested, so the walk stays trivial.
bool is_fixture(const std::filesystem::path& path, const std::filesystem::path& tests_root) {
  for (std::filesystem::path dir = path.parent_path(); dir != tests_root && !dir.empty(); dir = dir.parent_path()) {
    const std::filesystem::path entry = dir / "main.ok";
    if (entry != path && std::filesystem::exists(entry)) {
      return true;
    }
  }
  return false;
}

// the `// okpath: a:b` directive: colon-separated import roots, each resolved
// against the test file's directory, exported as OKPATH for this one run.
std::string parse_okpath(const std::filesystem::path& path) {
  std::regex okpath_re(R"(//\s*okpath:\s*(.*))");
  std::ifstream fs(path);
  std::string line;
  while (std::getline(fs, line)) {
    std::smatch match;
    if (std::regex_search(line, match, okpath_re)) {
      const std::string value = match[1].str();
      const std::filesystem::path dir = path.parent_path();
      std::string expanded;
      size_t start = 0;
      for (;;) {
        const size_t colon = value.find(':', start);
        const std::string part = value.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!part.empty()) {
          if (!expanded.empty()) {
            expanded += ":";
          }
          expanded += (dir / part).string();
        }
        if (colon == std::string::npos) {
          break;
        }
        start = colon + 1;
      }
      return expanded;
    }
  }
  return "";
}

// the `// okflags: --restrict --native x.so` directive (OR-3): CLI flags for
// this one run. relative --native paths resolve against the test file's
// directory, exactly the okpath precedent.
std::string parse_okflags(const std::filesystem::path& path) {
  std::regex okflags_re(R"(//\s*okflags:\s*(.*))");
  std::ifstream fs(path);
  std::string line;
  while (std::getline(fs, line)) {
    std::smatch match;
    if (std::regex_search(line, match, okflags_re)) {
      const std::string value = match[1].str();
      const std::filesystem::path dir = path.parent_path();
      std::string expanded;
      std::string prev;
      size_t start = 0;
      for (;;) {
        const size_t space = value.find(' ', start);
        std::string part = value.substr(start, space == std::string::npos ? std::string::npos : space - start);
        if (!part.empty()) {
          if (prev == "--native" && !part.starts_with("/")) {
            part = (dir / part).string();
          }
          if (!expanded.empty()) {
            expanded += " ";
          }
          expanded += part;
          prev = part;
        }
        if (space == std::string::npos) {
          break;
        }
        start = space + 1;
      }
      return expanded;
    }
  }
  return "";
}

// the native fixture builds (OR-3): every build.sh under the tests root,
// from its own directory, before any test runs. the declared-build contract:
// whatever toolchain, idempotent, quiet on success.
void build_native_fixtures(const std::string& tests_path) {
  for (auto& entry : std::filesystem::recursive_directory_iterator(tests_path)) {
    if (entry.path().filename() != "build.sh") {
      continue;
    }
    const std::string dir = entry.path().parent_path().string();
    const std::string cmd = "cd \"" + dir + "\" && bash build.sh";
    if (decode_system_status(system(cmd.c_str())) != 0) {
      std::println(stderr, "FIXTURE BUILD FAILED: {}", entry.path().string());
      std::exit(1);
    }
  }
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

  build_native_fixtures(tests_path);

  for (auto& entry : std::filesystem::recursive_directory_iterator(tests_path)) {
    if (entry.path().extension() != ".ok")
      continue;

    std::string test_path = entry.path();
    if (is_quarantined(entry.path())) {
      skipped++;
      std::println("{}[SKIP]{}: {}", COLOR_BLUE, COLOR_RESET, test_path);
      continue;
    }
    if (is_fixture(entry.path(), tests_path)) {
      skipped++;
      continue;
    }

    const std::string okpath_value = parse_okpath(entry.path());
    const std::string flags = parse_okflags(entry.path());
    auto tc = parse_test_file(test_path);
    auto rr = run_oklang(test_path, okpath, okpath_value, flags);

    if (!tc.has_expectation) {
      std::println("{}[NOTOK]{}: '{}'", COLOR_RED, COLOR_RESET, test_path);
      std::println("no // expect or // expect error directive");
      fail(notok, ok_debug_path, test_path);
      continue;
    }

    if (tc.expected_error.has_value()) {
      if (rr.exit_code == 0) {
        std::println("{}[NOTOK]{}: '{}'", COLOR_RED, COLOR_RESET, test_path);
        std::println("expected error, but non occurred");
        std::println("got:\nstdout: {}\nstderr: {}", rr.std_out, rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else if (!tc.expected_error->empty() && rr.std_err.find(tc.expected_error.value()) == std::string::npos) {
        std::println("{}[NOTOK]{}: '{}'", COLOR_RED, COLOR_RESET, test_path);
        std::println("error message mismatch");
        std::println("expected:\n{}", tc.expected_error.value());
        std::println("got:\n{}", rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else {
        ok++;
        std::println("{}[OK]{}: {}", COLOR_GREEN, COLOR_RESET, test_path);
      }
    } else {
      if (rr.exit_code != 0) {
        std::println("{}[NOTOK]{}: '{}'", COLOR_RED, COLOR_RESET, test_path);
        std::println("expected success (exit 0), got exit {}", rr.exit_code);
        std::println("got:\nstdout: {}\nstderr: {}", rr.std_out, rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else if (rr.std_out != tc.expected_result) {
        std::println("{}[NOTOK]{}: '{}'", COLOR_RED, COLOR_RESET, test_path);
        std::println("output mismatch");
        std::println("expected:\n{}", tc.expected_result);
        std::println("got:\nstdout: {}\nstderr: {}", rr.std_out, rr.std_err);
        fail(notok, ok_debug_path, test_path);
      } else {
        ok++;
        std::println("{}[OK]{}: {}", COLOR_GREEN, COLOR_RESET, test_path);
      }
    }
  }

  std::println("\nsummary: [{} OK], [{} NOTOK], [{} SKIP]", ok, notok, skipped);
  return notok == 0 ? 0 : 1;
}
