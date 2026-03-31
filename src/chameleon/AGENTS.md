# Repository Guidelines

## Project Structure & Module Organization
- Chameleon source lives primarily in `vulkan.cpp` with VkCommandBuffer evaluation in `commandbuffer.cpp`. Definitions are in `vulkan_defs.h`.
- GPU definitions and overrides live in `share/chameleon/devices/<gpu>/` and are selected at runtime with `CHAMELEON_GPU`.
- Chameleon codegen scripts live in `scripts/`: `chameleon_gen.py`, `tostring.py`, `json.py`, and shared parsing support in `spec.py`.
- Generated files are emitted into the build directory: `vulkan_auto.*`, `tostring.*`, and `vkjson.*`.
- The ICD manifest template is `cmake/chameleon_icd.json.in`.
- Chameleon-specific helper binaries currently include `chameleon_loader_icd_smoketest` and `chameleon_icd_*` test wrappers.
- Vulkan headers come from this repo’s `external/Vulkan-Headers/`. JsonCpp is taken from `external/SPIRV-Headers/tools/buildHeaders/jsoncpp/dist/`.

## Build, Test, and Development Commands
- Loader smoke test:
  `env VK_DRIVER_FILES=$PWD/build/chameleon_icd.json CHAMELEON_GPU=$PWD/share/chameleon/devices/Mali-G925 ./build/chameleon_loader_icd_smoketest`
- Headless ICD test example:
  `env VK_DRIVER_FILES=$PWD/build/chameleon_icd.json CHAMELEON_GPU=$PWD/share/chameleon/devices/Mali-G925 ./build/chameleon_icd_general -v --gpu`
- Window ICD test example:
  `env VK_DRIVER_FILES=$PWD/build/chameleon_icd.json CHAMELEON_GPU=$PWD/share/chameleon/devices/Mali-G925 xvfb-run -a ./build/chameleon_icd_window_1 -v --gpu`

## Testing Guidelines
- Use `VK_DRIVER_FILES=build/chameleon_icd.json` so the Vulkan loader picks up Chameleon as an ICD.
- You may set `CHAMELEON_GPU` to a concrete directory under `share/chameleon/devices/` to pick a particular GPU to fake.
- Use `--gpu` with the `chameleon_icd_*` wrappers. Chameleon advertises GPU profiles, so `--cpu` may intentionally skip with exit code `77`.
- A minimal verification set is:
  `chameleon_loader_icd_smoketest`,
  `chameleon_icd_init -v --gpu`,
  `chameleon_icd_general -v --gpu`,
  `chameleon_icd_multiinstance -v --gpu`,
  and `xvfb-run -a chameleon_icd_window_1 -v --gpu`.

## Implementing Vulkan Functions
- All Chameleon Vulkan entry points belong in `src/chameleon/vulkan.cpp`.
- Chameleon is a mock driver. It should track and simulate Vulkan usage, not produce real rendering.
- Usually Vulkan entry points should return success unless there is a clear modeled failure.
- Every Vulkan object type has a matching tracked type prefixed with `c`, for example `cVkCommandBuffer`.
- Every Vulkan function implementation shall start with `ENTRY(<function name>)` as its first line.
- If Vulkan objects are passed as parameters, create the matching local tracked object with the appropriate `<type>_cast` helper even if it is only used for bookkeeping.
- For `vkCreate*` and `vkAllocate*`, create the tracked object with `owner_create`.
- For `vkDestroy*` and `vkFree*`, destroy the tracked object with `destroy`.
- For `vkCmd*`, bind relevant tracked objects onto the command buffer, and add host-side simulation in `execute_command_buffer_command` in `src/chameleon/commandbuffer.cpp` when later CPU-visible effects matter.
- If two entry points are aliases, prefer a shared `common*` helper instead of duplicating logic and `ENTRY()` accounting.
- Statistics or logging-only tracking that should disappear from the fast build must stay under `#ifndef FAST` / `#endif`.
- Do not add new global state casually. Persistent tracked state should live in the metadata types defined in `src/chameleon/vulkan_defs.h`.
