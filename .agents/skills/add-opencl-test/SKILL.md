---
name: add-opencl-test
description: Add an OpenCL test or microbenchmark to this repository.
---

Create `src/opencl_<name>.cpp` and `benchmarking/opencl_<name>.bench`, and register `cl_test(<name> <version>)` in
`CMakeLists.txt`, with the required header version (e.g. `300`). The resulting target and binary are
`opencl_<name>_v<version>`.

Use `opencl_common.h`, `cl_test_init()` and `cl_test_done()`; see `src/opencl_basic_1.cpp`. Bracket measured work with
`bench_start_iteration(cl.bench)` and `bench_stop_iteration(cl.bench)`.

Check return values with `cl_check(result)` on a separate line after the OpenCL call, rather than wrapping the call.

Run the binary directly with `--cpu` and/or `--gpu` on available implementations. Report unsupported-device skips
separately from passes.
