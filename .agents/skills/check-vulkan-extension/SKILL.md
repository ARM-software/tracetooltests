---
name: check-vulkan-extension
description: Check and improve test coverage and usage detection for a named Vulkan extension.
---

Read the extension specification at `https://docs.vulkan.org/refpages/latest/refpages/source/<extension>.html` and
compare its commands, structures, and feature gates with existing tests and
`src/usagetracker/vulkan_feature_detect.{cpp,h}`. Distinguish capability queries and feature enablement from exercised
functionality; queries such as `vkGetPhysicalDeviceFeatures2` are not usage evidence.

Close practical coverage gaps by improving an existing test or adding a small new one; use `$test-vulkan-extension` and
`$vulkan-extension-detection` for missing tests and detection. For review-only requests, report findings without making
changes. Describe remaining gaps with source references and suggest the most useful next improvement.

For test changes, verify the intended commands execute using `VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump` when
available. Report unexercised paths and missing-layer or device limitations.
