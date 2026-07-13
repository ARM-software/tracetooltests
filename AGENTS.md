# Repository Guidelines

## Project Structure & Module Organization
- Root: `CMakeLists.txt` drives all builds and tests via CTest.
- Source: `src/` (C++17), public headers in `include/`.
- Bench configs: `benchmarking/*.bench`
- Scripts: `scripts/` for demo, tracing, and benchmarking helpers.
- Tooling: `cmake/` (modules, toolchains), `external/` (headers), `doc/` (standards), `asset/`, `patches/`.
- Vulkan headers: `external/Vulkan-Headers/include/vulkan/` (do not use the system headers)

## Build, Test, and Development Commands
- Configure (choose window system):
  - `mkdir build && cd build`
  - `cmake .. -DWINDOWSYSTEM=x11` (options: `sdl`, `fbdev`, `pbuffers`)
- Build: `make -j`  | Run tests: `ctest --output-on-failure`
- Example run: `./vulkan_general --help` (from `build/`).

## Coding Style & Naming Conventions
- Language: C++20 with `-Wall -g`. Match existing style in `src/` and `include/`.
- Names: test binaries follow `vulkan_<name>`, `gles_<name>`, `opencl_<name>`; sources in `src/<api>_<name>.cpp`.
- CMake helpers: prefer `vulkan_test(name)`, `gles_test(name)`, `cl_test(name, version)` to add tests and install bench files.
- Bench files: add `benchmarking/<api>_<name>.bench` aligned with the target name.
- Add assert() calls to verify assumptions. Do not work around problems with defensive coding.
- Avoid adding unnecessary namespaces.
- Do not use lambdas, anonymous namespaces or throw new exceptions.

## Testing Guidelines
- Run test binaries directly, do not run via CTest unless asked to.
- For Vulkan tests, you can add the command line parameter `--cpu` to workaround AI sandbox problems.
- For Vulkan tests, you can add the command line parameter `-v` to enable the Vulkan validation layer.
- Tests return error code 77 to indicate feature not supported.

## Notes & Configuration Tips
- For GLES, window system is selectable via `-DWINDOWSYSTEM=<x11|sdl|fbuffers|fbdev>`; default is X11.
- Optional components can be toggled at configure time, e.g., `-DNO_VULKAN=1`, `-DNO_GLES=1`, `-DNO_CL=1`.
- For Vulkan headers, install LunarG SDK or use provided `external/` headers as configured.
