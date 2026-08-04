cmake_minimum_required(VERSION 3.11)
project(oktest-regression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(OKTEST_REGRESSION_PATH ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE OKTEST_REGRESSION_SRC CONFIGURE_DEPENDS ${OKTEST_REGRESSION_PATH}/src/*.cpp)

add_executable(oktest-regression ${OKTEST_REGRESSION_SRC})
target_include_directories(oktest-regression PRIVATE ${OKTEST_REGRESSION_PATH}/src/)
set_target_properties(oktest-regression PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
