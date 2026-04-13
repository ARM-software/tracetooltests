---
name: update-chameleon
description: Use this skill to update chameleon to the latest Khronos specs
metadata:
  short-description: Use whenever we update our Khronos headers submodule to also update Chameleon to match
---

# Update Chameleon

## Overview

Use git to update the Vulkan headers dependency, then build the source to find issues caused by the update.
Typical issues include missing implementations of new functions defined in the Vulkan spec.

## Instructions

1. `git submodule update --force --remote --recursive external/Vulkan-Headers`
	- If sandboxed auth status fails, rerun the command with `sandbox_permissions=require_escalated` to allow network/keyring access.
2. `( cd build ; make -j 6 )`
	- Build with the updated submodule and look for issues. Typically problems would be due to missing functions.
	- Create function stubs in `vulkan.cpp` for any missing functions defined in the Vulkan spec and not implemented yet by us.

Minimal example stub function that is not `vkCmd` type:
```cpp
VKAPI_ATTR VkResult VKAPI_CALL vk<name>(<function parameters>)
{
        ENTRY(vk<name>);
        cVkDevice* cdevice = device_cast(device); // for each Vulkan object passed in as parameter, touch it in this way
        TBD_UNSUPPORTED; // mark function as incomplete
        return VK_SUCCESS; // fake success
}
```

Minimal example stub function that is `vkCmd` type:

```cpp
VKAPI_ATTR void VKAPI_CALL vkCmd<name>(VkCommandBuffer commandBuffer, <rest of function parameters>)
{
	ENTRY(vkCmd<name>);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	// TBD touch any other function parameters that are Vulkan objects
	TBD_UNSUPPORTED; // mark function as incomplete
}
```

3. Build again with ``( cd build ; make -j 6 )` and verify that it now works correctly.
