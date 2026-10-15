---
name: vulkan-extension-detection
description: Add stateless usage detection and pruning for a named Vulkan extension.
---

Read `https://docs.vulkan.org/refpages/latest/refpages/source/<extension>.html`. Detection lives in
`src/usagetracker/vulkan_feature_detect.{cpp,h}`; tests are in `src/vulkan_feature.cpp` (binary `vulkan_featuretest`).

Usage must be observable through commands, structures, or SPIR-V capabilities. Do not add state tracking or resource
bookkeeping. Capability queries are not usage. If safe detection is impossible within these limits, explain the missing
evidence rather than implementing a detector that always reports usage.

- Add `std::atomic_bool has_<extension> { false };` to `feature_detection`, following existing naming.
- Instrument relevant command/structure checks and `parse_SPIRV` for the extension's capabilities.
- Decide whether feature enablement must count as usage in `check_vkCreateDevice`, or whether later commands provide
  sufficient evidence so unused feature structures can be pruned. Explain the choice; inspect each relevant feature bit.
- Update `adjust_device_extensions` or `adjust_instance_extensions` and the corresponding create-info adjustment for
  feature structures. Device structure pruning uses `check_prune_device`.
- Treat promoted core commands as core usage where safe. Preserve extensions and feature bits whenever absence of usage
  cannot be established safely.

Add focused detector tests covering usage and unused-extension/feature-structure pruning, then build and run
`vulkan_featuretest` directly.
