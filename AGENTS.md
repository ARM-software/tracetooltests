# Repository Guidelines

## Project Structure & Module Organization
- Root: `CMakeLists.txt` drives all builds and tests via CTest.
- Source: `src/` (C++17), public headers in `include/`.
- Bench configs: `benchmarking/*.bench` (installed and symlinked for local use).
- Scripts: `scripts/` for demo, tracing, and benchmarking helpers.
- Tooling: `cmake/` (modules, toolchains), `external/` (headers), `doc/` (standards), `asset/`, `patches/`.

## Build, Test, and Development Commands
- Configure (choose window system):
  - `mkdir build && cd build`
  - `cmake .. -DWINDOWSYSTEM=x11` (options: `sdl`, `fbdev`, `pbuffers`)
- Cross-compile examples:
  - ARMv7: `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fbdev_arm.cmake ..`
  - ARMv8: `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fbdev_aarch64.cmake ..`
- Build: `make -j`  | Run tests: `ctest --output-on-failure`
- Example run: `./vulkan_general --help` (from `build/`).

## Coding Style & Naming Conventions
- Language: C++17 with `-Wall -g`. Match existing style in `src/` and `include/`.
- Names: test binaries follow `vulkan_<name>`, `gles_<name>`, `opencl_<name>`; sources in `src/<api>_<name>.cpp`.
- CMake helpers: prefer `vulkan_test(name)`, `gles_test(name)`, `cl_test(name, version)` to add tests and install bench files.
- Bench files: add `benchmarking/<api>_<name>.bench` aligned with the target name.

## Testing Guidelines
- Run test binaries directly, do not run via CTest.
- For Vulkan tests, add the command line parameter `--gpu-simulated` to workaround AI sandbox problems.
- For Vulkan tests, also add the command line parameter `-v` to enable the Vulkan validation layer.
- Some tests return error code 77 to indicate feature not supported.

## Commit & Pull Request Guidelines
- Commits: concise, imperative present tense (e.g., “Add explicit flush for BDA”). Reference issues (`Fixes #123`) when applicable.
- PRs: include purpose, platform details (OS, GPU/driver), reproduction steps, and before/after metrics or screenshots when relevant. Update docs/bench files and CMake as needed.

## Notes & Configuration Tips
- For GLES, window system is selectable via `-DWINDOWSYSTEM=<x11|sdl|fbuffers|fbdev>`; default is X11.
- Optional components can be toggled at configure time, e.g., `-DNO_VULKAN=1`, `-DNO_GLES=1`, `-DNO_CL=1`.
- For Vulkan headers, install LunarG SDK or use provided `external/` headers as configured.

## Add A New Vulkan Test
- Create source `src/vulkan_<name>.cpp` and a bench file `benchmarking/vulkan_<name>.bench`.
- Register the test in `CMakeLists.txt` with `vulkan_test(<name>)`.
- Build and run, from build directory: `./vulkan_<name> -v --gpu-simulated`.
- Fix any Vulkan validation errors shown in the output from the run.
- Use the check() function to test Vulkan call return values. Put it on a separate line after the Vulkan call. Do not wrap the Vulkan call.

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
{ "name": "vulkan_<name>", "description": "Short description of test" }
```

## Add A New GLES Test
- Create source `src/gles_<name>.cpp` and a bench file `benchmarking/gles_<name>.bench`.
- Register the test in `CMakeLists.txt` with `gles_test(<name>)`.
- Build and run: `ctest -R gles_<name> --output-on-failure`.

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
{ "name": "gles_<name>", "description": "Short description of test" }
```

## Add A New OpenCL Test
- Create source `src/opencl_<name>.cpp` and a bench file `benchmarking/opencl_<name>.bench`.
- Register the test in `CMakeLists.txt` with `cl_test(<name> 300)`.
- Build and run: `ctest -R cl_<name> --output-on-failure`.

Minimal `src/opencl_<name>.cpp`:
```cpp
#include "opencl_common.h"

int main(int argc, char** argv)
{
    reqs.usage = show_usage;
    reqs.cmdopt = test_cmdopt;
    opencl_setup_t cl = cl_test_init(argc, argv, "opencl_general", reqs);
    bench_start_iteration(cl.bench);
    // ... do work here ...
    bench_stop_iteration(cl.bench);
    cl_test_done(cl);
    return 0;
}
```

Minimal `benchmarking/opencl_<name>.bench`:
```json
{ "name": "opencl_<name>", "description": "Short description of test" }
```

### Add a new shader file for Vulkan
- Shaders shall be in separate files with extension giving their type, eg .comp for compute shaders, like `vulkan_<name>.<ext>`.
- Generate SPIRV from the GLSL source code with `glslangValidator -V vulkan_<name>.<ext> -o vulkan_<name>_<ext>.spirv`
- Then generate an include file with `xxd -i vulkan_<name>_<ext>.spirv > vulkan_<name>_<ext>.inc`
- Include this file in the C++ source code with `#include "vulkan_<name>_<ext>.inc"`
