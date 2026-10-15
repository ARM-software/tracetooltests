---
name: add-vulkan-test
description: Add a Vulkan test or microbenchmark to this repository.
---

Create `src/vulkan_<name>.cpp` and `benchmarking/vulkan_<name>.bench`, and register `vulkan_test(<name>)` in
`CMakeLists.txt` (or `vulkan_window_test` for a surface).

Use `vulkan_common.h`, `vulkan_req_t`, `test_init()` and `test_done()`, or the relevant compute/graphics helper for the
workload. Bracket measured work with benchmark iteration calls on the setup state used by teardown.

Check `VkResult` values with `check(result)` on a separate line after the Vulkan call. Run `vulkan_<name> -v` directly
on an implementation supporting the required features; use `--cpu` or `--gpu` as appropriate. A skip is not execution
coverage.

Keep shaders in separate stage files under `src/` (e.g. `.comp`). Generate and include SPIR-V arrays using the naming of
nearby tests. For example, from `src/`:

```sh
glslangValidator -V vulkan_<name>.comp -o vulkan_<name>_comp.spirv
xxd -i vulkan_<name>_comp.spirv > vulkan_<name>_comp.inc
```

Use the Vulkan/SPIR-V target versions required by the test, and regenerate the include whenever shader source changes.
