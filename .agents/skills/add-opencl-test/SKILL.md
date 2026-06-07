---
name: add-opencl-test
description: Add an OpenCL test or microbenchmark
metadata:
  short-description: Add an OpenCL test
---

## Add A New OpenCL Test
- Create source file `src/opencl_<name>.cpp`.
- Add a bench file `benchmarking/opencl_<name>.bench`.
- Register the test in `CMakeLists.txt` with `cl_test(<name> 300)`.
- Use the cl_check() function to test OpenCL call return values. Put it on a separate line after the OpenCL call. Do not wrap the OpenCL call.
- Build with 'make -j6'
- Run from build directory without GPU: `./opencl_<name> -v --cpu`.
- Run from build directory with GPU: `./opencl_<name> -v --gpu`.
- Fix any errors shown in the output from the above runs.

Minimal `src/opencl_<name>.cpp`:
```cpp
#include "opencl_common.h"

int main(int argc, char** argv)
{
    opencl_req_t reqs{}; // Set reqs.usage and reqs.cmdopt for cmd line options
    opencl_setup_t cl = cl_test_init(argc, argv, "opencl_<name>", reqs);
    bench_start_iteration(cl.bench);
    // ... do work here ...
    bench_stop_iteration(cl.bench);
    cl_test_done(cl);
    return 0;
}
```

Minimal `benchmarking/opencl_<name>.bench`:
```json
{ "name": "opencl_<name>", "description": "<short description of test>" }
```
