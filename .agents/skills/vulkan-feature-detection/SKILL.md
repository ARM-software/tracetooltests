---
name: vulkan-feature-detection
description: Add stateless usage detection and pruning for a named core Vulkan feature.
---

Read the feature definition in `https://docs.vulkan.org/spec/latest/chapters/features.html`. Detection lives in
`src/usagetracker/vulkan_feature_detect.{cpp,h}`; tests are in `src/vulkan_feature.cpp` (binary `vulkan_featuretest`).

Usage must be observable through commands, structures, or SPIR-V capabilities. Do not add state tracking or resource
bookkeeping. Capability queries are not usage. If safe detection is impossible within these limits, explain the missing
evidence rather than implementing a detector that always reports usage.

Track the feature in `atomicPhysicalDeviceFeatures` for Vulkan 1.0 or `atomicPhysicalDeviceVulkan<version>Features` for
later versions (e.g. `atomicPhysicalDeviceVulkan11Features`). Instrument relevant command/structure checks and
`parse_SPIRV`, and update the corresponding feature adjustment and any obsolete unsupported-feature comment.

Preserve enabled features whenever absence of usage cannot be established safely. Add focused tests for observed usage
and unused-feature pruning, then build and run `vulkan_featuretest` directly.
