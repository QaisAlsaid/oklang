cmake_minimum_required(VERSION 3.11)

project(oktest-e2e-runner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(OKTEST_E2E_PATH ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE OKTEST_E2E_SRC CONFIGURE_DEPENDS ${OKTEST_E2E_PATH}/src/*.cpp)

add_executable(${PROJECT_NAME} ${OKTEST_E2E_SRC})
target_include_directories(${PROJECT_NAME} PRIVATE ${OKTEST_E2E_PATH}/src/)

# P1: the OK_ROOT fallback the runner exports when the environment has none
# (the fixtures reach the headers through it; the compiled-in prefix is the
# checkout cmake configured against)
target_compile_definitions(${PROJECT_NAME} PRIVATE OKTEST_ROOT_PREFIX="${CMAKE_SOURCE_DIR}")
