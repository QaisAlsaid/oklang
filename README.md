# OKLang

A small embedded scripting language written in C99.

It is intended to be used in my other projects.

## Example

```oklang
let bird = "nice";

fu is_bird_nice() {
  if bird == "nice"? return true;
  else return false;
}

print(is_bird_nice()); // outputs: true
```

## Status

OKLang is currently in early development.

The C rewrite is the current active implementation, and the C++ version is deprecated.

Current implemented features in the rewrite:

* bytecode compiler and VM
* closures and upvalues
* garbage collection
* native functions and values
* minimal C embedding API

## Building

Clone the repository:

```bash
git clone https://codeberg.org/qais/oklang.git
cd oklang
mkdir build && cd build
cmake ..
cmake --build .
```

By default, this builds the CLI as well as the `libok` library.

### Running

You can either run a file:

```bash
./ok <your-oklang-script.ok>
```

or run the REPL:

```bash
./ok
```

## Tests

The project has an E2E test suite, unit tests for most critical parts, and a minimal embedding test.

Tests can be enabled with the `OK_BUILD_TESTS` CMake option:

```bash
cmake .. -DOK_BUILD_TESTS=ON
cmake --build .
```

Then you can run each test type from the build directory.

### E2E tests

```bash
./tests/e2e/oktest-e2e-runner ./ok ../tests/e2e/tests/
```

### Unit tests

```bash
ctest
```

### Embedding test

```bash
./tests/integration/embedding/oktest_embedding
```

## Embedding

OKLang can be embedded into C or C++ through a simple C API.

Currently the API is rather limited and only supports basic features:

1. define globals and native functions
2. create opaque native values
3. pass opaque native values into the runtime

The API is intentionally minimal for now. It was added in this version to enable early usage, and it will not remain this way.

The library can be built as either a static or shared library using the `OK_BUILD_SHARED` CMake option.

For example:

```bash
cmake .. -DOK_BUILD_SHARED=ON
cmake --build .
```

The public API is available through:

```c
#include <ok/ok.h>
```

## CMake options

The main CMake options are:

* `OK_BUILD_CLI` — build the CLI executable. Enabled by default.
* `OK_BUILD_TESTS` — build the tests. Disabled by default.
* `OK_BUILD_SHARED` — build `libok` as a shared library instead of a static library.
* `OK_ENABLE_SANITIZERS` — enable ASan.
* `OK_PARANOID` — enable additional runtime logging.
* `OK_PEDANTIC` — enable additional failure checking.
* `OK_AGGRESSIVE_GC` — trigger garbage collection on every allocation.

## License

[MIT License](LICENSE)

