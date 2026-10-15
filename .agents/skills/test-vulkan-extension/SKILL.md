---
name: test-vulkan-extension
description: Add or extend a test exercising a named Vulkan extension.
---

Read `https://docs.vulkan.org/refpages/latest/refpages/source/<extension>.html` and inspect related tests. Honor a
requested starting point; otherwise choose an existing test or a small new one. For a new binary, follow
`$add-vulkan-test`.

Exercise the extension's commands and structures where practical, querying and enabling required feature bits before
use. Record any omitted behavior and why. Capability queries alone do not demonstrate extension use.

Use `vkAssertBuffer` for verifiable buffer output, and `test_set_name` / `test_marker_mention` for new object handles
where useful. For visual output, consider an `-i` image-dump option following existing tests.

Run the affected binary directly with `-v` on a supporting implementation. Use
`VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump` when available to verify the new commands execute; report skips and paths
that remain unexercised.

Check corresponding usage detection in `src/usagetracker/vulkan_feature_detect.{cpp,h}`; use
`$vulkan-extension-detection` to add it if missing as part of the requested extension work.
