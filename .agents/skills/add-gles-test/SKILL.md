---
name: add-gles-test
description: Add an GLES / OpenGL ES test or microbenchmark
metadata:
  short-description: Add a GLES test
---

## Add A New GLES Test
- Create source `src/gles_<name>.cpp` and a bench file `benchmarking/gles_<name>.bench`.
- Register the test in `CMakeLists.txt` with `gles_test(<name>)`.
- Build with 'make -j6'
- Run test as: `ctest -R gles_<name> --output-on-failure`.

Minimal `src/gles_<name>.cpp`:
```cpp
#include "gles_common.h"

static int setup_graphics(TOOLSTEST *handle)
{
    // ... set up context here ...
}

static void callback_draw(TOOLSTEST *handle)
{
    // ... do work here ...
}

static void test_cleanup(TOOLSTEST *handle)
{
    // ... cleanup here ...
}

int main(int argc, char** argv)
{
    return init(argc, argv, "gles_<name>.cpp", callback_draw, setup_graphics, test_cleanup);
}
```

Minimal `benchmarking/gles_<name>.bench`:
```json
{ "name": "gles_<name>", "description": "<short description of test>" }
```
