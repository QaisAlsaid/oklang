# OKLang

<p align='center'>

![CI](https://img.shields.io/github/actions/workflow/status/QaisAlsaid/oklang/ci.yml?branch=master&style=for-the-badge&label=CI&passed_color=C3E3F2&failed_color=BD5F42&aborted_color=8196A4&logo=github)
![C99](https://img.shields.io/badge/Language-C99-748D9C?style=for-the-badge&logo=c&color=8196A4)
![Coverage](https://img.shields.io/codecov/c/github/QaisAlsaid/oklang?style=for-the-badge&color=206680)
![Release](https://img.shields.io/github/v/release/QaisAlsaid/oklang?style=for-the-badge&include_prereleases&color=BD5F42)
![License](https://img.shields.io/github/license/QaisAlsaid/oklang?style=for-the-badge&color=A31F34)

</p>

A small embedded scripting language written in C99.


## Why OKLang?

Honestly why not?
I needed a scripting language that i could use in my other projects, and thought, why not just make one.

But seriously, it is lightweight, has nice syntax, is steadily gaining features, and is written in highly portable C99.


## Example

```javascript
let mut total = 0;

fu feed(bird) {
  fu is_nice() {
    return bird != 'chicken';
  }
  if is_nice() {
    print(bird + " nice food nice!");
  } else {
    print(bird + " not nice " + bird + " food!");
  }
  total += 1;
}

let birds = ["crow", "sparrow", "owl", 'chicken'];

for let bird : birds {
  feed(bird);
}

print("i fed " + total as str + " birds.");
```
output:
```txt
crow nice food nice!
sparrow nice food nice!
owl nice food nice!
chicken not nice chicken food!
i fed 4.000000 birds.
```

## Status

OKLang is in very early development, currently sitting at v0.0.5.

The language is a bytecode compiler and a register-based VM.

The original C++ implementation is deprecated. The current C99 implementation is its successor, and it has reached feature parity with the original, and has since grown significantly beyond it.

Features:

* Register VM with fixed-width 32-bit and wide 64-bit instruction modes
* NaN-boxed values
* Functions, default arguments, closures, upvalues, and lambdas
* `if`, `while`, and 3 flavors of `for` (numeric, ranged, and C-style)
* Ternary expression that reads naturally `condition then consequence otherwise alternative`
* Mark-and-sweep garbage collection with a stress mode that caught a lot of bugs
* Native functions and opaque native values (handles)
* Minimal C embedding API
* Disassembler `ok --dump`

## Building

Clone the repository:

```bash
git clone https://codeberg.org/qais/oklang.git
cd oklang
mkdir build && cd build
cmake ..
cmake --build .
```

By default, this builds the CLI `ok` as well as the `libok` library.

### Running

You can either run a file:

```bash
./ok <your-oklang-script.ok>
```

or run the REPL:

```bash
./ok
```

## Embedding

OKLang can be embedded into C or C++ through a simple C API `#include <ok/ok.h>`.

1. define globals and native functions
2. create opaque native values
3. pass opaque native values into the runtime
4. get globals from the runtime
5. call oklang from native and get returned values

The API is intentionally minimal for now. This section will grow when the library boundary is stabilized.

You can build as shared library instead of static with `-DOK_BUILD_SHARED=ON` CMake flag.

### Example
```C
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
```
Output:

```txt
Hello from OKLang!
99
```

## Rough Roadmap

1. Maps
2. Classes
3. Error model
4. Coroutines
5. A small standard library
6. Performance and stabilization


## Editor support

See: [tree-sitter-oklang](https://codeberg.org/qais/tree-sitter-oklang)


## Tests

The project has an E2E test suite, unit tests for most critical parts, and a minimal embedding test.

Tests can be enabled with the `-DOK_BUILD_TESTS=ON` CMake flag:

```bash
cmake .. -DOK_BUILD_TESTS=ON
cmake --build .
ctest                                                    # unit tests
./tests/e2e/oktest-e2e-runner ./ok ../tests/e2e/tests/   # end-to-end
./tests/integration/embedding/oktest_embedding           # embedding
```
> The unit-testing framework [Unity](https://github.com/throwtheswitch/unity) comes as a git submodule, clone with `--recursive` if you care about running tests


## CMake options

* `OK_BUILD_CLI` - build the CLI. On by default.
* `OK_BUILD_TESTS` - build the tests. Off by default.
* `OK_BUILD_SHARED` - build `libok` as shared library instead of static library.
* `OK_ENABLE_SANITIZERS` - lovely ASan.
* `OK_PARANOID` - additional runtime checks/logging.
* `OK_PEDANTIC` - additional failure checking.
* `OK_AGGRESSIVE_GC` - collect on every allocation (GC stress testing).


## Thank You

* **The Lua Team:** For pioneering efficient embedded scripting, and register-based VMs, and for being a constant source of inspiration, that I will always look up to
* **Bob Nystrom (*Crafting Interpreters*):** For Writing the single most comprehensive book, that I learned a lot from, and providing it for [free](https://craftinginterpreters.com/) for everyone to learn from
* **Thorsten Ball (*Writing An Interpreter In Go*):** For writing the original book that demystified parsing and AST for me and got me into this

## License

[MIT License](LICENSE)
