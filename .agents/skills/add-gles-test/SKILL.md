---
name: add-gles-test
description: Add a GLES / OpenGL ES test or microbenchmark to this repository.
---

Create `src/gles_<name>.cpp` and `benchmarking/gles_<name>.bench`, and register `gles_test(<name>)` in `CMakeLists.txt`.

Use `gles_common.h` and its `init()` lifecycle with setup, draw, and cleanup callbacks. `src/gles_draw_1.cpp` is a
working example; the common draw loop handles benchmark iterations. Set the required GLES version and extensions for the
workload.

Build and run `gles_<name>` directly from the build directory using a window system supported by the host.
