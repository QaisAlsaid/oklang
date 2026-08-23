cmake_minimum_required(VERSION 3.20)

project(ok LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(OK_BUILD_SHARED "build libok as a shared library" OFF) # no api yet
option(OK_BUILD_TESTS "build tests" OFF)
option(OK_BUILD_CLI "build the cli" ON)
option(OK_ENABLE_SANITIZERS "enable address sanitizer" OFF)
option(OK_PARANOID "enable a lot of logging" ON)
option(OK_PEDANTIC "enable checking every possible failure point" ON)

if(OK_BUILD_SHARED)
  set(OK_LIBRARY_TYPE SHARED)
  target_compile_definitions(ok PUBLIC OK_BUILD_SHARED)
else()
  set(OK_LIBRARY_TYPE STATIC)
endif()

add_library(ok ${OK_LIBRARY_TYPE})

if(build_type STREQUAL "debug")
  if (OK_PARANOID)
    target_compile_definitions(ok PUBLIC OK_PARANOID)
  endif()
  if (OK_PEDANTIC)
    target_compile_definitions(ok PUBLIC OK_PEDANTIC)
  endif()
endif()

target_sources(ok PRIVATE
  src/ok.c
  src/okast.c
  src/okchunk.c
  src/okcompiler.c
  src/okdebug.c
  src/okglobals_store.c
  src/oklexer.c
  src/okmm.c
  src/okobject.c
  src/okobject_store.c
  src/okoperator.c
  src/okparser.c
  src/oktoken.c
  src/okutils.c
  src/okvalue.c
  src/okvm.c
  src/okstring.c
)

target_include_directories(ok 
  #PUBLIC TODO
  #${PROJECT_SOURCE_DIR}/include 
  PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)

target_compile_options(ok PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

add_library(ok_sanitizers INTERFACE)

if (OK_ENABLE_SANITIZERS)
  target_compile_options(ok_sanitizers INTERFACE
    -fsanitize=address
    -fno-omit-frame-pointer
  )

  target_link_options(ok_sanitizers INTERFACE
    -fsanitize=address
  )
endif()

if (OK_BUILD_CLI)
  add_subdirectory(cli)
endif()

if(OK_BUILD_TESTS)
  add_subdirectory(vendor/unity)
  enable_testing()
  add_subdirectory(tests)
endif()
