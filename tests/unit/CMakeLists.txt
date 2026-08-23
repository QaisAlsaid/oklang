find_package(unity REQUIRED TRUE)
set(tests 
  test_array
  test_table
)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

foreach(name IN LISTS tests)
  add_executable(ok${name} src/ok${name}.c)
  target_link_libraries(ok${name} PRIVATE ok ok_sanitizers unity)
  target_include_directories(ok${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  add_test(NAME ok.${name} COMMAND ok${name})
endforeach()

# add_executable(ok_unit_tests
#   src/test_array.c
# )
#
# target_link_libraries(ok_unit_tests
#   PRIVATE
#   ok
#   ok_sanitizers
#   unity
# )
#
# target_include_directories(ok_unit_tests
#   PRIVATE
#   ${PROJECT_SOURCE_DIR}/src
# )
#
# add_test(NAME ok_unit_tests COMMAND ok_unit_tests)
