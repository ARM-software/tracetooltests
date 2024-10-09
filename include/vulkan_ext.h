#pragma once

#include <vulkan/vulkan.h>

// ---- Fake extensions ----

// -- VK_TRACETOOLTEST_checksum_validation --

#define VK_TRACETOOLTEST_CHECKSUM_VALIDATION_EXTENSION_NAME "VK_TRACETOOLTEST_checksum_validation"

typedef uint32_t (VKAPI_PTR *PFN_vkAssertBufferTRACETOOLTEST)(VkDevice device, VkBuffer buffer);


// -- VK_TRACETOOLTEST_object_property --

#define VK_TRACETOOLTEST_OBJECT_PROPERTY_EXTENSION_NAME "VK_TRACETOOLTEST_object_property"

typedef enum VkTracingObjectPropertyTRACETOOLTEST {
	VK_TRACING_OBJECT_PROPERTY_UPDATES_COUNT_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_UPDATES_BYTES_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_BACKING_STORE_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_ADDRESS_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_MARKED_RANGES_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_MARKED_BYTES_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_MARKED_OBJECTS_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_SIZE_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_INDEX_TRACETOOLTEST,
} VkTracingObjectPropertyTRACETOOLTEST;

typedef uint64_t (VKAPI_PTR *PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST)(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkTracingObjectPropertyTRACETOOLTEST valueType);

// -- VK_TRACETOOLTEST_trace_helpers --

#define VK_TRACETOOLTEST_TRACE_HELPERS_EXTENSION_NAME "VK_TRACETOOLTEST_trace_helpers"

// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_ADDRESS_REMAP_TRACETOOLTEST (VkStructureType)131313
#define VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_TRACETOOLTEST (VkStructureType)131314
#define VK_STRUCTURE_TYPE_PATCH_CHUNK_LIST_TRACETOOLTEST (VkStructureType)131315

typedef enum VkAddressRemapTargetTRACETOOLTEST
{
	VK_ADDRESS_REMAP_TARGET_BUFFER_TRACETOOLTEST,
	VK_ADDRESS_REMAP_TARGET_PUSH_CONSTANTS_TRACETOOLTEST,
	VK_ADDRESS_REMAP_TARGET_SPECIALIZATION_CONSTANTS_TRACETOOLTEST,
} VkAddressRemapTargetTRACETOOLTEST;

// Mark where in memory buffer device addresses or shader group handles are stored, as they may need to be
// remapped for trace replay.
// Passed to VkPipelineShaderStageCreateInfo for specialization constants, vkCmdPushConstants2KHR for push constants,
// vkCmdUpdateBuffer2TRACETOOLTEST for commandbuffer buffer updates, or vkUpdateBufferTRACETOOLTEST for mapped
// memory buffer updates. When used with vkCmdPushConstants2KHR, offsets given here are relative to the start
// of its dstOffset.
typedef struct VkAddressRemapTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_ADDRESS_REMAP_TRACETOOLTEST
	const void* pNext;
	VkAddressRemapTargetTRACETOOLTEST target; // this is just to make the intent explicit, not really needed
	uint32_t count; // the number of offsets in pOffsets
	VkDeviceSize* pOffsets; // address offsets
} VkAddressRemapTRACETOOLTEST;

typedef struct VkUpdateMemoryInfoTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_TRACETOOLTEST
	const void* pNext;
	VkDeviceSize dstOffset;
	VkDeviceSize dataSize; // may be VK_WHOLE_SIZE to signify the rest of the object's memory area
	const void* pData; // may be null if dataSize is zero
} VkUpdateBufferInfoTRACETOOLTEST;
// Adding a 2 version of vkCmdUpdateBuffer since it lacks a pNext chain.
typedef void (VKAPI_PTR *PFN_vkCmdUpdateBuffer2TRACETOOLTEST)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);
// Adding ways to update a memory object without an explicit memory mapping also outside of a commandbuffer. The given buffer or image must not be
// also be memory mapped by the caller when using these commands, but it must already be bound to memory.
typedef void (VKAPI_PTR *PFN_vkUpdateBufferTRACETOOLTEST)(VkDevice device, VkBuffer dstBuffer, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);
typedef void (VKAPI_PTR *PFN_vkUpdateImageTRACETOOLTEST)(VkDevice device, VkImage dstImage, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);
// Patching memory objects without an explicit memory mapping also outside of a commandbuffer. As above, the objects must not be memory mapped by the
// caller and must be bound to memory. They are meant to be used by tools.
typedef struct VkPatchChunkTRACETOOLTEST
{
	uint32_t offset;
	uint32_t size;
	uint8_t data[]; // may be empty if size is zero
} VkPatchChunkTRACETOOLTEST;
typedef struct vkPatchChunkListTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_PATCH_CHUNK_LIST_TRACETOOLTEST
	const void* pNext;
	VkPatchChunkTRACETOOLTEST* pChunks; // a patch chunk list continues until its offset == 0 and size == 0
} VkPatchChunkListTRACETOOLTEST;
typedef void (VKAPI_PTR *PFN_vkPatchBufferTRACETOOLTEST)(VkDevice device, VkBuffer dstBuffer, VkPatchChunkListTRACETOOLTEST* pList);
typedef void (VKAPI_PTR *PFN_vkPatchImageTRACETOOLTEST)(VkDevice device, VkImage dstImage, VkPatchChunkListTRACETOOLTEST* pList);

// All pending Vulkan work has been host synchronized at this point to prevent race conditions. On trace replay, all other threads
// must also synchronize to this point. When called outside of a replay context, this is a no-op. You should never need to add this
// yourself to code, but it could be useful as a debug tool for tracing issues. To call it yourself, set count to zero and pValues
// to null, and tools will fill it out with their internal tracking data for your threads.
typedef void (VKAPI_PTR *PFN_vkThreadBarrierTRACETOOLTEST)(uint32_t count, uint32_t* pValues);
