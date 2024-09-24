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

// -- VK_TRACETOOLTEST_memory_markup --
// Mark where in memory are stored buffer device addresses or shader group handles that may need to be rewritten during trace capture.

#define VK_TRACETOOLTEST_MEMORY_MARKUP_EXTENSION_NAME "VK_TRACETOOLTEST_memory_markup"

// Hope these random constants remain unused...
#define VK_STRUCTURE_TYPE_MEMORY_MARKUP_TRACETOOLTEST (VkStructureType)131313
#define VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_TRACETOOLTEST (VkStructureType)131314

typedef enum VkMemoryMarkupTargetTRACETOOLTEST
{
	VK_MEMORY_MARKUP_TARGET_BUFFER_TRACETOOLTEST,
	VK_MEMORY_MARKUP_TARGET_PUSH_CONSTANTS_TRACETOOLTEST,
	VK_MEMORY_MARKUP_TARGET_SPECIALIZATION_CONSTANTS_TRACETOOLTEST,
} VkMemoryMarkupTargetTRACETOOLTEST;

// Passed to vkCreatePipelineLayout for specialization constants, vkCmdPushConstants2KHR for push constants,
// or vkCmdUpdateBuffer2TRACETOOLTEST for buffer updates. Existing markup in the affected memory region may be removed.
// When used with vkCmdPushConstants2KHR, offsets given here are relative to the start of its dstOffset.
typedef struct VkMemoryMarkupTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_MEMORY_MARKUP_TRACETOOLTEST
	const void* pNext;
	VkMemoryMarkupTargetTRACETOOLTEST target; // this is just to make the intent explicit, not really needed
	VkDeviceSize clearSize; // size of region that will be cleared of old markers, set to zero to not clear old markers; may be VK_WHOLE_SIZE
	uint32_t count; // the number of offsets in pOffsets; may be zero if all you want to do is clear old markers
	VkDeviceSize* pOffsets; // memory marker offsets
} VkMemoryMarkupTRACETOOLTEST;

// Change markup contents of a buffer as containing buffer device addresses or shader group handles. This function is meant for tools.
typedef void (VKAPI_PTR *PFN_vkMemoryMarkupTRACETOOLTEST)(VkDevice device, VkBuffer buffer, VkMemoryMarkupTRACETOOLTEST* pInfo);

// Adding a 2 version of vkCmdUpdateBuffer since it lacks a pNext chain.
typedef struct VkUpdateBufferInfoTRACETOOLTEST
{
	VkStructureType sType; // must be VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_TRACETOOLTEST
	const void* pNext;
	VkBuffer dstBuffer;
	VkDeviceSize dstOffset;
	VkDeviceSize dataSize;
	const void* pData;
} VkUpdateBufferInfoTRACETOOLTEST;
typedef void (VKAPI_PTR *PFN_vkCmdUpdateBuffer2TRACETOOLTEST)(VkCommandBuffer commandBuffer, VkUpdateBufferInfoTRACETOOLTEST* pInfo);
