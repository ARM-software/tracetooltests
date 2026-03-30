---
name: vulkan_extension_detection
description: Add extension usage detection
metadata:
  short-description: Generate code to detect whether an extension was actually used or not
---

1. Check that we were told which Vulkan extension we are supposed to check. If this was not specified, ask.
2. Download the extension text from `https://docs.vulkan.org/refpages/latest/refpages/source/<extension name>.html` and read it.
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
5. Consider whether it is possible at all for us to detect whether the extension is actually being used. It has to express its
   functionality through commands, structures and SPIRV capabilities for us to actually detect it. If we cannot detect its usage,
   stop and inform the user.
6. Add our extension to the `feature_detection` struct in the header, like this:
```cpp
    std::atomic_bool has_<name of extension> { false };
```
7. Add support for our extension to `adjust_device_extensions` if it is a device extension, or `adjust_instance_extensions` if
   it is an instance extension. If we have a feature enable struct in our extension, then we should add a removal of it also
   to `adjust_VkDeviceCreateInfo` (if device extension) or to `adjust_VkInstanceCreateInfo` (if instance extension).
8. Make sure each new Vulkan command and command using any new struct defined in the given extension are checked in our detection
   code. If not, add them. If the extension adds new SPIRV capabilities, also check that in `parse_SPIRV`. If any of our new
   Vulkan commands or structs or SPIRV capabilities are used, we must flag the extension as in use.
9. If it is safe to do so, we should not consider command variants that have been promoted to core as using the extension -
   they would be using the relevant core version or core feature instead, which is out of scope here.
10. Err on the side of caution - if we cannot tell for sure whether an extension is being used in some way or not, then we should
   assume it is being used. If this caution means we always assume it is used, then this detection is pointless so stop and
   inform the user of the problem.
11. Add a quick test for our extension to `src/vulkan_feature.cpp`. Compile and test that it all works fine.
