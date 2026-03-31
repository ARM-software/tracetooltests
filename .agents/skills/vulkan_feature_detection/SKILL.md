---
name: vulkan_feature_detection
description: Add Vulkan unused feature detection
metadata:
  short-description: Generate code to detect whether a core Vulkan feature was actually used or not
---

1. Check that we were told which Vulkan extension we are supposed to check. If this was not specified, ask.
2. Download the feature descriptions from `https://docs.vulkan.org/spec/latest/chapters/features.html` and look
   for explanations of our feature.
3. If you cannot download the extension text due to sandbox permission issues, ask user to add the following to their `.codex/config.toml`:
```
[network]
enable_domain_allowlist = true
domain_allowlist = [
  "docs.vulkan.org",
  "github.com",
  "api.github.com"
]
```
4. Our usage detection code is in `include/vulkan_feature_detect.cpp` and `include/vulkan_feature_detect.h`. There is a test
   for it at `src/vulkan_feature.cpp`.
5. Consider whether it is possible at all for us to detect whether the feature is actually being used. It has to express its
   functionality through commands, structures and SPIRV capabilities for us to actually detect it. We do not track state or
   maintain internal bookkeeping and we cannot add this. If we cannot detect its usage with these limitations, stop and inform
   the user.
6. Features are tracked in structs containing atomic bools named `atomicPhysicalDeviceFeatures` (for Vulkan 1.0) and
   `atomicPhysicalDeviceVulkan<version>Features` for other Vulkan versions where `<version>` is the version number without a dot
   (eg `atomicPhysicalDeviceVulkan11Features` for Vulkan 1.1).
7. Find ways that the feature's usage would be visible to us through Vulkan commands or structs or SPIRV capabilities.
   If we do not already have entry points for them, add them. Then instrument them to check for feature usage.
   If the extension adds new SPIRV capabilities, check that in `parse_SPIRV`. If the header has a comment that says this feature
   is not handled, correct this comment.
8. Err on the side of caution - if we cannot tell for sure whether a feature is being used in some way or not, then we should
   assume it is being used. If this caution means we must always assume it is used, then this detection is pointless so stop and
   inform the user of the problem.
9. If feasible to do concisely, add a quick test for our feature to `src/vulkan_feature.cpp`. Compile and test that it all works fine.
