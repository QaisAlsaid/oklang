cmake_minimum_required(VERSION 3.11)
project(okc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(OK_PATH ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE OK_SRC CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE OK_HDR CONFIGURE_DEPENDS src/*.hpp)


add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)
#add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options(-w)

include_directories(${OK_PATH}/src ${OK_PATH}/include)

function (add_okc_build target_name build_type) 
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
        target_compile_options(okc_debug PRIVATE -fsanitize=address -fsanitize=undefined)
        target_link_options(okc_debug PRIVATE -fsanitize=address -fsanitize=undefined)
    elseif(build_type STREQUAL "release")
        target_compile_definitions(${target_name} PRIVATE IDK)
    endif()
endfunction()

add_okc_build(okc_debug debug)
add_okc_build(okc_release release)


add_custom_target(okc ALL
    DEPENDS okc_release
)