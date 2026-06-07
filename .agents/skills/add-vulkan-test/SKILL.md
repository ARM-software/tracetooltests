---
name: add-vulkan-test
description: Add a Vulkan test or microbenchmark
metadata:
  short-description: Add a Vulkan test
---

## Add A New Vulkan Test
- Create source `src/vulkan_<name>.cpp` and a bench file `benchmarking/vulkan_<name>.bench`.
- Register the test in `CMakeLists.txt` with `vulkan_test(<name>)` (or `vulkan_window_test` if it uses a surface).
- Build with 'make -j6'
- Run from build directory without GPU: `./vulkan_<name> -v --cpu`.
- Run from build directory with GPU: `./vulkan_<name> -v --gpu`.
- Fix any Vulkan validation errors shown in the output from the above runs.
- Use the check() function to test Vulkan call return values. Put it on a separate line after the Vulkan call. Do not wrap the Vulkan call.
- If asked to test an extension you are not familiar with, download the extension text from `https://docs.vulkan.org/refpages/latest/refpages/source/<extension name>.html`
  and read it.

Minimal `src/vulkan_<name>.cpp`:
```cpp
#include "vulkan_common.h"

int main(int argc, char** argv)
{
    vulkan_req_t reqs{};                       // Optional: set reqs.usage/cmdopt
    auto vk = test_init(argc, argv, "vulkan_<name>", reqs);
    bench_start_iteration(vk.bench);           // Start one deterministic iteration
    // ... do work here ...
    bench_stop_iteration(vk.bench);
    test_done(vk);
    return 0;
}
```

Minimal `benchmarking/vulkan_<name>.bench`:
```json
{ "name": "vulkan_<name>", "description": "<short description of test>" }
```

### Add a new shader file for Vulkan
- Shaders shall be in separate files with extension giving their type, eg .comp for compute shaders, like `vulkan_<name>.<ext>`.
- Generate SPIRV from the GLSL source code with `glslangValidator -V vulkan_<name>.<ext> -o vulkan_<name>_<ext>.spirv`
- Then generate an include file with `xxd -i vulkan_<name>_<ext>.spirv > vulkan_<name>_<ext>.inc`
- Include this file in the C++ source code with `#include "vulkan_<name>_<ext>.inc"`
