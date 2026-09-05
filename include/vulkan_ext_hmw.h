#pragma once

#include <vulkan/vulkan.h>

// ---- Fake extensions ----

// -- VK_ARM_host_memory_writes --

/* ---

How it works - in general:
- Valid only for device memory that is both HOST_VISIBLE and HOST_COHERENT, and if the feature is enabled.
- The callback gives you a bitmask of unreported ("dirty") pages that have been modified by the host since the last call to the callback.
- May attach to any vkAllocateMemory() command that allocates host-visible coherent memory.
- Once a callback is registered, it must be called by the implementation on the follow conditions:
	- When vkUnmapMemory*() is called on its device memory.
	- When a vkQueueSubmit*() is called that touches its device memory and it has a non-zero dirty page bitmask.
- While a callback is run, the GPU must not write to its marked pages.
- While a callback is run, host writes to its marked pages must be intercepted and wait.
- While a callback is run, host writes to other pages in its device memory _can_ be allowed but must update the bitmask *after* callbacks return.
- The callback receiver must ensure thread safety in case of concurrent callers (following VK_EXT_device_memory_report)

Open questions:
- When called during vkQueueSubmit*(), must it be run *after* waiting for its synchronization dependencies?
- To what extent false positives are allowed. If all memory is always dirty, this extension becomes (worse than) pointless.
- There may be multiple chained VkHostMemoryWriteCallbackARM structures if there are multiple tools registering callbacks. This means the implementation would need
  to support more than one callback for each device memory object. Use case: Validation layers want to use it and run concurrently with a capture layer.
- Should imported external memory be supported? Can we always track these? Exclude from first iteration.

Assumptions:
- The driver implementation already intercepts and tracks writes to host-visible memory. If it does not, this functionality could be implement on Linux and
  Android using a layer that intercepts writes using userfaultfd.

Rejected ideas:
- vkFlushMappedMemoryRanges() should trigger callbacks or just quietly remove flushed pages from its dirty list. It is now ignored.
- Callbacks issued asynchronously with GPU jobs after queue submissions and before job synchronization. While this would allow queue submit commands to remain non-blocking
  while being captured and speed up capture, the resulting complexity on both implementation and tool side would probably be too much for anyone to actually support it.
- vkFreeMemory should issue a callback. The memory would no longer be usable, there is no reason to do so.

--- */

#define VK_ARM_HOST_MEMORY_WRITES_EXTENSION_NAME "VK_ARM_host_memory_writes"
#define VK_ARM_HOST_MEMORY_WRITES_SPEC_VERSION 1

/// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_HOST_MEMORY_WRITE_CALLBACK_ARM (VkStructureType)1001198001
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_MEMORY_WRITE_FEATURES_ARM (VkStructureType)1001198002
#define VK_STRUCTURE_TYPE_HOST_MEMORY_WRITE_CALLBACK_INFO_ARM (VkStructureType)1001198003
typedef VkFlags VkHostMemoryWriteFlagsARM; // for future use

typedef enum VkHostMemoryWriteTypeARM {
	VK_HOST_MEMORY_WRITE_TYPE_UNMAP_ARM = 0,
	VK_HOST_MEMORY_WRITE_TYPE_QUEUE_SUBMIT_ARM = 1,
} VkHostMemoryWriteTypeARM;

typedef struct VkHostMemoryWriteCallbackInfoARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_HOST_MEMORY_WRITE_CALLBACK_INFO_ARM
	const void* pNext; // must be null
	VkDeviceMemory memory;
	VkHostMemoryWriteTypeARM type;
	VkDeviceSize pageSize; // in bytes
	uint32_t maskSize;
	const uint8_t* pMask; // allocation relative bitmask of pages
	void* pUserData;
} VkHostMemoryWriteCallbackInfoARM;

typedef void (VKAPI_PTR *PFN_vkHostMemoryWriteCallbackARM)(VkDevice device, const VkHostMemoryWriteCallbackInfoARM* pInfo);

/// Feature struct
typedef struct VkPhysicalDeviceHostMemoryWriteFeaturesARM {
	VkStructureType    sType; // must be VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_MEMORY_WRITE_FEATURES_ARM
	void*              pNext;
	VkBool32           hostMemoryWriteTracking; // basic functionality
} VkPhysicalDeviceHostMemoryWriteFeaturesARM;

/// Append to pNext chain of VkMemoryAllocateInfo in vkAllocateMemory()
typedef struct VkHostMemoryWriteCallbackARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_HOST_MEMORY_WRITE_CALLBACK_ARM
	const void* pNext;
	VkHostMemoryWriteFlagsARM flags; // must be zero for now
	PFN_vkHostMemoryWriteCallbackARM callback;
	void* pUserData;
} VkHostMemoryWriteCallbackARM;
