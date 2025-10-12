cmake_minimum_required(VERSION 3.11)
project(okc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(OK_PATH ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE OK_SRC CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE OK_HDR CONFIGURE_DEPENDS src/*.hpp)
add_definitions(-w)
add_compile_options(-O4)
#add_compile_options(-g)
#file(GLOB_RECURSE OK_INC CONFIGURE_DEPENDS include/OKLang/*.hpp)
#file(GLOB_RECURSE OK_INC_H CONFIGURE_DEPENDS include/OKLang/*.h)

#add_compile_definitions(PARANOID)
add_executable(${PROJECT_NAME} ${OK_SRC} ${OK_HDR} ${OK_INC} ${OK_INC_H})
target_include_directories(${PROJECT_NAME} PRIVATE ${OK_PATH}/src/ ${OK_PATH}/include/)