---
name: update-chameleon
description: Update the Vulkan-Headers submodule and adapt Chameleon's Vulkan implementation to its registry.
---

Update `external/Vulkan-Headers` to the requested revision, or its tracked upstream revision with
`git submodule update --remote external/Vulkan-Headers`. Preserve local submodule work; avoid forced or unrelated
recursive updates.

CMake generates Chameleon registry code through `scripts/chameleon_gen.py` and `scripts/json.py`. Build with the updated
headers to identify missing entry points and registry incompatibilities.

Adapt `src/chameleon/vulkan.cpp` and related implementation files following neighboring commands. Entry points use
`ENTRY(vk<name>)` and touch Vulkan object parameters through the appropriate casts, such as `device_cast()` and
`commandbuffer_cast()`. For deliberately incomplete commands, follow existing `TBD_UNSUPPORTED` stub conventions and
keep their incomplete status explicit.

Build the affected Chameleon targets, including symbol validation (`chameleon_validate`), and run relevant binaries
directly with `VK_DRIVER_FILES` pointing to the build's `chameleon_icd.json`. Report the submodule revision and any
remaining unsupported functionality; a successful build alone does not prove implementation completeness.
