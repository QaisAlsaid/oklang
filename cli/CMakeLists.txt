add_executable(ok_cli
  src/main.c
  src/project.c
)

target_link_libraries(ok_cli
  PRIVATE
  ok
  ok_sanitizers
)

# P1: the compiled-in OK_ROOT fallback - the checkout this binary was built
# from, exported to declared build commands when the environment has no
# OK_ROOT of its own (the test-path fix; the ../../ reaches are dead).
target_compile_definitions(ok_cli PRIVATE OK_ROOT_PREFIX="${PROJECT_SOURCE_DIR}")

# the P1 gate, same shape as libok's (see the root CMakeLists comment)
if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    if (OK_PURE)
        target_compile_options(ok_cli PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-braces)
    else()
        target_compile_options(ok_cli PRIVATE -Wall -Wextra -Werror -Wno-missing-braces)
    endif()
endif()

# give access to internal apis for now
target_include_directories(ok_cli
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

set_target_properties(ok_cli PROPERTIES
  OUTPUT_NAME ok
)

set_target_properties(ok_cli PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
