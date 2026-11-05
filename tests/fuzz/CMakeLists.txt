# the zero-dep fuzzer (P5): corpus-driven, deterministic, engineless. links
# the lib objects directly - the same internal lane the unit tests use - so
# it can drive parser + compiler without ever handing the vm a byte.
add_executable(okfuzz
  src/fuzz.c
)

target_link_libraries(okfuzz
  PRIVATE
  ok
  ok_sanitizers
)

# internal headers (parser/compiler/diag), same grant the unit tests run on
target_include_directories(okfuzz
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

# the committed corpus, found bare (no flags) in every CI-style run
target_compile_definitions(okfuzz PRIVATE OK_FUZZ_CORPUS_PREFIX="${CMAKE_SOURCE_DIR}/tests/fuzz")

# the P1 gate flags (no -Wpedantic: dirent is the harness lane, POSIX like the CLI)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
  target_compile_options(okfuzz PRIVATE -Wall -Wextra -Werror -Wno-missing-braces)
endif()

# the standing gate run: the default seed, deterministic, reproducible.
# findings change the corpus, never the seed.
add_test(NAME ok.fuzz COMMAND okfuzz 20000)
