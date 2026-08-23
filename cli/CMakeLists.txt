add_executable(ok_cli
  src/main.c
)

target_link_libraries(ok_cli
  PRIVATE
  ok
  ok_sanitizers
)

# give access to internal apis for now
target_include_directories(ok_cli
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

set_target_properties(ok_cli PROPERTIES
  OUTPUT_NAME ok
)
