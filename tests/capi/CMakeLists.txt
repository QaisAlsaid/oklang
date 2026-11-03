project (oktest_capi LANGUAGES C)

# the coro C API harness (patch 42): drives ok_coro_* + ok_native_suspend
# through real scripts from pure C99. doubles as OR-3's embedder-loader test
# bed when the module loader lands.

add_executable(${PROJECT_NAME}
  src/main.c
)

target_link_libraries(${PROJECT_NAME}
  PRIVATE
  ok
  ok_sanitizers
)
