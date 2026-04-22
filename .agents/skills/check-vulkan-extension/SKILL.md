---
name: check-vulkan-extension
description: Check support for a particular Vulkan extension
metadata:
  short-description: Investigate a Vulkan extension test coverage
---

1. Check that we were told which Vulkan extension we are supposed to check. If this was not specified, ask.
2. Download the extension text from `https://docs.vulkan.org/refpages/latest/refpages/source/<extension name>.html` and read it.
3. Check that we have a test for each new Vulkan command and struct defined in the given extension, if possible. If there are
   features gated by enable flags, make sure we only run these gates features when they are supported. Sometimes creating
   a new test is the right choice, sometimes we can just insert usage into existing tests.
4. If we are missing a test for the extension or parts of the extension, you can improve existing tests or use the AI skill
   $test-vulkan-extension to make one.
5. Make note of any commands and structs you could not add to the test, things you find that did not make sense, and suggest the
   best improvement that could be made to make the test even better.
6. Make sure usage detection of this extension is in `src/usagetracker/vulkan_feature_detect.cpp` and
   `src/usagetracker/vulkan_feature_detect.h`. You can use the skill
   $vulkan-extension-detection to add this if missing.
7. For any modified test, run the test with `VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_api_dump` environment variable set to verify that
   the expected changes actually got executed.
