#pragma once

#include <vulkan/vulkan.h>

// ---- Fake extensions ----

// -- VK_ARM_trace_helpers --

#define VK_ARM_TRACE_HELPERS_EXTENSION_NAME "VK_ARM_trace_helpers"

// Hope these random constants remain unused
#define VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM (VkStructureType)1000998001
#define VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM (VkStructureType)1000998000
#define VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM (VkStructureType)1000998002

typedef enum VkMarkingTypeARM {
	VK_MARKING_TYPE_DEVICE_ADDRESS_ARM = 0, // marks a device address in memory
	VK_MARKING_TYPE_DESCRIPTOR_SIZE_ARM = 1, // marks the size of a descriptor type
	VK_MARKING_TYPE_DESCRIPTOR_OFFSET_ARM = 2, // marks the offset to a descriptor
	VK_MARKING_TYPE_DESCRIPTOR_ARM = 3, // marks a descriptor in memory
	VK_MARKING_TYPE_SHADER_GROUP_HANDLE_ARM = 4, // marks a shader group handle in memory
} VkMarkingTypeARM;

typedef enum VkDeviceAddressTypeARM {
	VK_DEVICE_ADDRESS_TYPE_BUFFER_ARM = 0,
	VK_DEVICE_ADDRESS_TYPE_ACCELERATION_STRUCTURE_ARM = 1,
} VkDeviceAddressTypeARM;

typedef union VkMarkingSubTypeARM
{
	VkDescriptorType descriptorType;
	VkDeviceAddressTypeARM deviceAddressType;
	VkShaderGroupShaderKHR shaderGroupType;
	uint64_t reserved; // to pad to largest possible type
} VkMarkingSubTypeARM;

// Mark where in memory device addresses, shader group handles, descriptors or descriptor metainformation are
// stored, as they may need to be remapped for trace replay.
// Passed to the VkPipelineShaderStageCreateInfo of vkCreate*Pipelines for specialization constants,
// vkCmdPushConstants2KHR for push constants, vkCmdUpdateBuffer2ARM or vkCmdUpdateMemory2ARM for commandbuffer updates,
// or vkFlushMappedMemoryRanges for mapped memory updates. When used with vkCmdPushConstants2KHR,
// offsets given here are relative to the start of its dstOffset, and when used with vkCmdUpdateMemory2ARM they are
// relative to the start of pDstRange->address.
typedef struct VkMarkedOffsetsARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM
	const void* pNext;
	uint32_t count; // the number of entries in pMarkingTypes, pDescriptorType and pOffsets
	const VkMarkingTypeARM* pMarkingTypes; // the overall type of marking
	const VkMarkingSubTypeARM* pSubTypes; // the subtype of the marking
	const VkDeviceSize* pOffsets; // offsets into memory to items we want to mark
} VkMarkedOffsetsARM;

typedef struct VkUpdateBufferInfoARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM
	const void* pNext;
	VkBuffer dstBuffer;
	VkDeviceSize dstOffset;
	VkDeviceSize dataSize; // size of data payload in pData
	const void* pData; // must be null if dataSize is zero
} VkUpdateBufferInfoARM;

typedef struct VkUpdateMemoryInfoARM
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_UPDATE_MEMORY_INFO_ARM
	const void* pNext;
	const VkDeviceAddressRangeKHR* pDstRange;
	VkAddressCommandFlagsKHR dstFlags;
	VkDeviceSize dataSize; // size of data payload in pData
	const void* pData; // must be null if dataSize is zero
} VkUpdateMemoryInfoARM;

// Adding a version 2 of vkCmdUpdateBuffer that tools can upgrade to since the original lacks a pNext chain, and we may want to add
// a remap struct.
typedef void (VKAPI_PTR *PFN_vkCmdUpdateBuffer2ARM)(VkCommandBuffer commandBuffer, const VkUpdateBufferInfoARM* pInfo);

// Adding a version 2 of vkCmdUpdateMemoryKHR that tools can upgrade to which has a pNext chain.
typedef void (VKAPI_PTR *PFN_vkCmdUpdateMemory2ARM)(VkCommandBuffer commandBuffer, const VkUpdateMemoryInfoARM* pInfo);

// Request validation of buffer contents by an Adler32 checksum. The command will return the checksum, and when stored in an API trace,
// the trace replayer may verify that the buffer contents are correct according to the stored checksum. 'dataSize' may be VK_WHOLE_SIZE.
// If `pData` is not null, it must contain the correct output for the asserted data with the same size. This can be useful if the asserted
// buffer is device-generated. If it has a `VkMarkedOffsetsARM` in its `pNext` chain, this provides information on where marked content
// is placed, which may need to be reverted to capture values by tools in order to correctly checksum the memory region.
typedef VkResult (VKAPI_PTR *PFN_vkAssertBufferARM)(VkDevice device, const VkUpdateBufferInfoARM* pInfo, uint32_t* checksum, const char* comment);

// As above, just for memory ranges instead of buffers.
typedef VkResult (VKAPI_PTR *PFN_vkAssertMemoryARM)(VkDevice device, const VkUpdateMemoryInfoARM* pInfo, uint32_t* checksum, const char* comment);

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
