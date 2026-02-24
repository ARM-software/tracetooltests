---
name: test_vulkan_extension
description: Add a test case for a particular Vulkan extension
metadata:
  short-description: Generate a new Vulkan extension test case
---

1. Check that we were told which Vulkan extension we are supposed to check. If this was not specified, ask.
2. Check that we were told which test or common code to use as a starting point or if we should make a test
   from scratch. If this was not specified, ask.
3. Download the extension text from `https://docs.vulkan.org/refpages/latest/refpages/source/<extension name>.html` and read it.
4. If you cannot download the extension test due to sandbox permission issues, ask user to add the following to their `.condex/config.toml`:
```
[network]
enable_domain_allowlist = true
domain_allowlist = [
  "docs.vulkan.org",
  "github.com",
  "api.github.com"
]
```
5. Test each new Vulkan command and struct defined in the given extension, if possible. If there are features gated by
   enable flags, meaning they might not be supported on all platforms, consider adding command line options to enable them.
6. If we can verify some generated output with `vkAssertBuffer`, that's is good.
7. Try to use `test_set_name` and `test_marker_mention` with any new object handles so that we can test this works as well.
8. If the test generates a visual image, consider adding an `-i` option for user to dump the image to disk for verification.
9. Run `make -j 6` in the build directory. Fix any build issues.
10. Run `./<test name> -v --cpu` from the build directory and fix any validation issues.
11. Make note of any commands and structs you could not add to the test, things you find that did not make sense, and suggest the
   best improvement that could be made to make the test even better.
