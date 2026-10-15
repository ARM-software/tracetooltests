# Repository Guidelines

## Build and run

- Configure with `cmake -S . -B build -DWINDOWSYSTEM=x11`; GLES also supports `sdl`, `fbdev`, and `pbuffers`.
- Build affected targets with `cmake --build build --target <target> -j6`. Reconfigure after changing CMake
  registrations.
- Run test binaries directly from `build/`; use CTest only when requested. Vulkan tests accept `-v` for validation and
  `--cpu` for software execution, or `--gpu` for hardware.

## Repository conventions

- Tests live in `src/`, shared headers in `include/` and `src/`, and benchmark configs in `benchmarking/`.
- Use `external/Vulkan-Headers/include/vulkan/`, not system Vulkan headers.
- Follow the existing C++20 style. Use assertions for assumptions; do not mask defects with defensive fallbacks. Avoid
  unnecessary namespaces; do not introduce lambdas, anonymous namespaces, or exceptions.
- Add tests with the CMake helpers `vulkan_test(name)` (or `vulkan_window_test(name)` for surfaces), `gles_test(name)`,
  or `cl_test(name version)`.
- Sources and bench files use `<api>_<name>` with `.cpp` and `.bench` respectively. OpenCL binaries additionally have
  `_v<version>`, e.g. `opencl_basic_1_v300`.
- Bench files contain JSON with `name` matching the source basename and a short `description`; follow a similar existing
  file for additional fields.

## Test behavior

- Return 77 for unsupported features or required capabilities.
- Gate CPU checks of GPU results on `TOOLSTEST_NULL_RUN`; keep API calls and trace assertions in the recorded workload.
- Use `vkAssertBufferARM` through `vulkan.vkAssertBuffer` when available to record buffer assumptions in traces. See
  `src/vulkan_memory_tracking_2.cpp` for assertion and null-run handling.
