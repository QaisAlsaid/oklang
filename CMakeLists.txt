cmake_minimum_required(VERSION 3.11)
project(ok LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED TRUE)

set(OK_PATH ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE OK_SRC CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE OK_HDR CONFIGURE_DEPENDS src/*.h)


add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)
add_compile_options(-Wall -Wextra -Wpedantic)
#add_compile_options(-w)

include_directories(${OK_PATH}/src ${OK_PATH}/include)

function (add_ok_build target_name build_type) 
  add_executable(${target_name} ${OK_SRC} ${OK_HDR})
  target_compile_options(${target_name} PRIVATE 
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Release>:-O3>
    )
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${build_type}
    )

    if(build_type STREQUAL "debug")
        target_compile_definitions(${target_name} PRIVATE PARANOID)
        #target_compile_options(ok_debug PRIVATE -fsanitize=address -fsanitize=undefined)
        #target_link_options(ok_debug PRIVATE -fsanitize=address -fsanitize=undefined)
    elseif(build_type STREQUAL "release")
        target_compile_definitions(${target_name} PRIVATE IDK)
    endif()
endfunction()

add_ok_build(ok_debug debug)
add_ok_build(ok release)
