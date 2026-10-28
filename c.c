#include <ok/ok.h>
#include <stdio.h>

int main() {
  ok* state = ok_create({0}); // no specs, default initializes, uses libc allocators, and stdout/stderr
  if (state == NULL) {
    fprintf(stderr, "failed to initialize oklang.");
    return -1;
  }
  if (!ok_global_define(state,
                        ok_create_string_view("c", // the global name
                                              OK_STRING_VIEW_CALCULATE_LENGTH,
                                              true /* is the provided string null terminated? */
                                              ),
                        ok_value_number(99), // the global value
                        false                /* should this global be mutable? */
                        )) {
    fprintf(stderr, "failed to define global.");
    return -1;
  }
  if (ok_run(state,
             {
                 OK_FROM_FILE,                             //  use the provided source name for diagnostics
                 "print('Hello from OKLang!'); print(c);", // the OKLang script to run
                 "embedded"                                // source name, used for diagnostics
             }) != OK) {
    fprintf(stderr, "failed to run oklang script.");
    return -1;
  }
  return 0;
}
