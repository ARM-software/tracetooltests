#pragma once

#include <vulkan/vulkan.h>

// ---- Fake extensions ----

// -- VK_TRACETOOLTEST_trace_helpers --

#define VK_TRACETOOLTEST_TRACE_HELPERS_EXTENSION_NAME "VK_TRACETOOLTEST_trace_helpers"

// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_ADDRESS_REMAP_TRACETOOLTEST (VkStructureType)131313
#define VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_TRACETOOLTEST (VkStructureType)131314

typedef enum VkAddressRemapTargetTRACETOOLTEST
{
	VK_ADDRESS_REMAP_TARGET_BUFFER_TRACETOOLTEST,
	VK_ADDRESS_REMAP_TARGET_PUSH_CONSTANTS_TRACETOOLTEST,
	VK_ADDRESS_REMAP_TARGET_SPECIALIZATION_CONSTANTS_TRACETOOLTEST,
} VkAddressRemapTargetTRACETOOLTEST;

// Mark where in memory buffer device addresses or shader group handles are stored, as they may need to be
// remapped for trace replay.
// Passed to the VkPipelineShaderStageCreateInfo of vkCreate*Pipelines for specialization constants,
// vkCmdPushConstants2KHR for push constants, vkCmdUpdateBuffer2TRACETOOLTEST for commandbuffer buffer updates,
// or vkFlushMappedMemoryRanges for mapped memory buffer updates. When used with vkCmdPushConstants2KHR,
// offsets given here are relative to the start of its dstOffset.
typedef struct VkAddressRemapTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_ADDRESS_REMAP_TRACETOOLTEST
	const void* pNext;
	VkAddressRemapTargetTRACETOOLTEST target; // this is just to make the intent explicit, not really needed
	uint32_t count; // the number of offsets in pOffsets
	VkDeviceSize* pOffsets; // address offsets
} VkAddressRemapTRACETOOLTEST;

typedef VkFlags VkUpdateMemoryInfoFlags;

typedef struct VkUpdateMemoryInfoTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_TRACETOOLTEST
	const void* pNext;
	VkUpdateMemoryInfoFlags flags;
	VkDeviceSize dstOffset;
	VkDeviceSize dataSize; // size of data payload in pData
	const void* pData; // must be null if dataSize is zero
} VkUpdateBufferInfoTRACETOOLTEST;

// Adding a version 2 of vkCmdUpdateBuffer that tools can upgrade to since the original lacks a pNext chain, and we may want to add
// a remap struct. The 'flags' member of pInfo must be zero.
typedef void (VKAPI_PTR *PFN_vkCmdUpdateBuffer2TRACETOOLTEST)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);

// Request validation of buffer contents by an Adler32 checksum. The command will return the checksum, and when stored in an API trace,
// the trace replayer may verify that the buffer contents are correct according to the stored checksum. 'size' may be VK_WHOLE_SIZE.
// The buffer must be host visible.
typedef uint32_t (VKAPI_PTR *PFN_vkAssertBufferTRACETOOLTEST)(VkDevice device, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

// -- VK_TRACETOOLTEST_trace_helpers2 --
//
// More controversial and experimental functions extending VK_TRACETOOLTEST_trace_helpers. Please ignore this section.
//

#define VK_TRACETOOLTEST_TRACE_HELPERS2_EXTENSION_NAME "VK_TRACETOOLTEST_trace_helpers2"

// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_TRACETOOLTEST (VkStructureType)131314
#define VK_STRUCTURE_TYPE_THREAD_BARRIER_TRACETOOLTEST (VkStructureType)131315

// PATCH_FORMAT is a very simple RLE type compression composed of byte sequences starting with
// a uint32_t for offset and another for size of the following bytes. These sequences continue
// until offset and size are both zero. If this flag is set, 'dataSize' must be zero.
typedef enum VkUpdateMemoryInfoFlagBits {
	VK_UPDATE_MEMORY_PATCH_FORMAT_BIT = 0x00000001,
	VK_UPDATE_MEMORY_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VkUpdateMemoryInfoFlagBits;
typedef VkFlags VkUpdateMemoryInfoFlags;

// Adding ways to update a memory object without a device memory mapping. The given buffer or image must not already be memory mapped by the caller
// when using these commands, and they must already be bound to some device memory. The update will be complete by the time the command returns.
// The bound device memory must be host-visible and the image must be linear.
typedef void (VKAPI_PTR *PFN_vkUpdateBufferTRACETOOLTEST)(VkDevice device, VkBuffer dstBuffer, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);
typedef void (VKAPI_PTR *PFN_vkUpdateImageTRACETOOLTEST)(VkDevice device, VkImage dstImage, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);
typedef void (VKAPI_PTR *PFN_vkUpdateAccelerationStructureTRACETOOLTEST)(VkDevice device, VkAccelerationStructureKHR dstObject, VkUpdateMemoryInfoTRACETOOLTEST* pInfo);

// Thread information for use with vkThreadBarrierTRACETOOLTEST. Each value in pCallIds should correspond to a layer-defined call number that the
// thread barrier shall wait for, one for each thread currently known and in the order they were discovered through captured API calls. pCallIds is 'count'
// in length.
typedef struct VkThreadBarrierTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_THREAD_BARRIER_TRACETOOLTEST
	const void* pNext;
	uint32_t count;
	uint32_t* pCallIds;
} VkThreadBarrierTRACETOOLTEST;

// Signal that all pending Vulkan work has been host synchronized at this point to prevent race conditions. On trace replay, all other threads
// must also synchronize to this point. When called outside of a replay context, this is a no-op. You should not need to add this yourself
// during normal operation, but it could be useful as a debug tool for tools issues. When you call it yourself, set pThreadBarrierInfo to null
// and the layer should add the parameter with the necessary values.
typedef void (VKAPI_PTR *PFN_vkThreadBarrierTRACETOOLTEST)(const VkThreadBarrierTRACETOOLTEST* pThreadBarrierInfo);
