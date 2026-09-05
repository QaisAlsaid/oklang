project (oktest_embedding LANGUAGES CXX)

add_executable(${PROJECT_NAME}
  src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
  ok
  ok_sanitizers
)
