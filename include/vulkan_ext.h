#pragma once

#include <vulkan/vulkan.h>

// ---- Fake extensions ----

// -- VK_ARM_trace_helpers --

#define VK_ARM_TRACE_HELPERS_EXTENSION_NAME "VK_ARM_trace_helpers"

// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_DEVICE_ADDRESS_OFFSETS_ARM (VkStructureType)1000998001
#define VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM (VkStructureType)1000998000

// Mark where in memory buffer device addresses or shader group handles are stored, as they may need to be
// remapped for trace replay.
// Passed to the VkPipelineShaderStageCreateInfo of vkCreate*Pipelines for specialization constants,
// vkCmdPushConstants2KHR for push constants, vkCmdUpdateBuffer2ARM for commandbuffer buffer updates,
// or vkFlushMappedMemoryRanges for mapped memory buffer updates. When used with vkCmdPushConstants2KHR,
// offsets given here are relative to the start of its dstOffset.
typedef struct VkDeviceAddressOffsetsARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_DEVICE_ADDRESS_OFFSETS_ARM
	const void* pNext;
	uint32_t count; // the number of offsets in pOffsets
	const VkDeviceSize* pOffsets; // address offsets
} VkDeviceAddressOffsetsARM;

typedef VkFlags VkUpdateMemoryInfoFlags;

typedef struct VkUpdateMemoryInfoARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM
	const void* pNext;
	VkBuffer dstBuffer;
	VkDeviceSize dstOffset;
	VkDeviceSize dataSize; // size of data payload in pData
	const void* pData; // must be null if dataSize is zero
} VkUpdateBufferInfoARM;

// Adding a version 2 of vkCmdUpdateBuffer that tools can upgrade to since the original lacks a pNext chain, and we may want to add
// a remap struct. The 'flags' member of pInfo must be zero.
typedef void (VKAPI_PTR *PFN_vkCmdUpdateBuffer2ARM)(VkCommandBuffer commandBuffer, const VkUpdateMemoryInfoARM* pInfo);

// Request validation of buffer contents by an Adler32 checksum. The command will return the checksum, and when stored in an API trace,
// the trace replayer may verify that the buffer contents are correct according to the stored checksum. 'size' may be VK_WHOLE_SIZE.
typedef VkResult (VKAPI_PTR *PFN_vkAssertBufferARM)(VkDevice device, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t* checksum, const char* comment);

// -- VK_ARM_trace_descriptor_buffer
//

#define VK_ARM_TRACE_DESCRIPTOR_BUFFER_EXTENSION_NAME "VK_ARM_trace_descriptor_buffer"

#define VK_STRUCTURE_TYPE_DESCRIPTOR_OFFSETS_ARM (VkStructureType)131318

typedef enum VkMarkingTypeARM {
	VK_MARKING_TYPE_DESCRIPTOR_SIZE_BIT_ARM = 0x00000001, // denotes the size of a descriptor type
	VK_MARKING_TYPE_DESCRIPTOR_OFFSET_BIT_ARM = 0x00000002, // denotes the offset to a descriptor
	VK_MARKING_TYPE_DESCRIPTOR_BIT_ARM = 0x00000004, // denotes a descriptor in memory
} VkMarkingTypeARM;

typedef struct VkDescriptorOffsetsARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_DESCRIPTOR_OFFSETS_ARM
	const void* pNext;
	uint32_t count; // the number of entries in pMarkingTypes, pDescriptorType and pOffsets
	const VkMarkingTypeARM* pMarkingTypes; // the type of marking
	const VkDescriptorType* pDescriptorTypes; // the type of descriptor marked in pOffsets
	const VkDeviceSize* pOffsets; // offsets into memory to items we want to mark
} VkDescriptorOffsetsARM;

// -- VK_ARM_explicit_host_updates
//

#define VK_ARM_EXPLICIT_HOST_UPDATES_EXTENSION_NAME "VK_ARM_explicit_host_updates"

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXPLICIT_HOST_UPDATES_FEATURES_ARM (VkStructureType)131315
#define VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM (VkStructureType)131316

typedef struct VkPhysicalDeviceExplicitHostUpdatesFeaturesARM {
	VkStructureType                     sType; // must be VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXPLICIT_HOST_UPDATES_FEATURES_ARM
	void*                               pNext;
	VkBool32                            explicitHostUpdates;
} VkPhysicalDeviceExplicitHostUpdatesFeaturesARM;

typedef enum VkFlushOperationFlagBitsARM {
	VK_FLUSH_OPERATION_INFORMATIVE_BIT_ARM = 0x00000001,
} VkFlushOperationFlagBitsARM;
typedef VkFlags VkFlushOperationFlagsARM;

typedef struct VkFlushRangesFlagsARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM
	const void* pNext;
	VkFlushOperationFlagsARM flags;
} VkFlushRangesFlagsARM;

// -- VK_TRACETOOLTEST_trace_helpers2 --
//
// More controversial and experimental functions extending VK_TRACETOOLTEST_trace_helpers. Please ignore this section.
//

#define VK_TRACETOOLTEST_TRACE_HELPERS2_EXTENSION_NAME "VK_TRACETOOLTEST_trace_helpers2"

// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_THREAD_BARRIER_TRACETOOLTEST (VkStructureType)131322

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
typedef void (VKAPI_PTR *PFN_vkUpdateBufferTRACETOOLTEST)(VkDevice device, VkBuffer dstBuffer, VkUpdateMemoryInfoARM* pInfo);
typedef void (VKAPI_PTR *PFN_vkUpdateImageTRACETOOLTEST)(VkDevice device, VkImage dstImage, VkUpdateMemoryInfoARM* pInfo);
typedef void (VKAPI_PTR *PFN_vkUpdateAccelerationStructureTRACETOOLTEST)(VkDevice device, VkAccelerationStructureKHR dstObject, VkUpdateMemoryInfoARM* pInfo);

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
