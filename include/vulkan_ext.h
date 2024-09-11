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

// -- VK_TTT_buffer_device_address_marking --
// Starting to use TTT as a short-hand here to avoid the length of these names going off the rails.

#define VK_TTT_BUFFER_DEVICE_ADDRESS_MARKING_EXTENSION_NAME "VK_TTT_buffer_device_address_marking"

// Hope these random constants remain unused...
#define VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_SPECIALIZATION_CONSTANT_MARKING_TTT (VkStructureType)131301
#define VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_PUSH_CONSTANT_MARKING_TTT (VkStructureType)131302
#define VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_BUFFER_TTT (VkStructureType)131303

typedef struct VkBufferDeviceAddressPairTTT
{
	VkDeviceAddress offset_low;
	VkDeviceAddress offset_high;
} VkBufferDeviceAddressPairTTT;

typedef struct VkBufferDeviceAddressListTTT
{
	uint32_t pairCount;
	VkBufferDeviceAddressPairTTT* pPairs;
} VkBufferDeviceAddressListTTT;

// Passed to vkCreatePipelineLayout
typedef struct VkBufferDeviceAddressSpecializationConstantMarkingTTT
{
	VkStructureType sType;
	const void* pNext;
	VkBufferDeviceAddressListTTT markings;
} VkBufferDeviceAddressSpecializationConstantMarkingTTT;

// Passed to vkCreatePipelineLayout -- this is a bit inflexible because it is conceivable that
// the address location is moved around, but the only other alternative is to upgrade each
// vkCmdPushConstants to vkCmdPushConstants2KHR which can take a pNext
typedef struct VkBufferDeviceAddressPushConstantMarkingTTT
{
	VkStructureType sType;
	const void* pNext;
	VkBufferDeviceAddressListTTT* pMarkings; // one for each defined push constant range
} VkBufferDeviceAddressPushConstantMarkingTTT;

// Passed to vkQueueSubmit* variants
typedef struct VkBufferDeviceAddressBufferMarkingTTT
{
	VkStructureType sType;
	const void* pNext;
	uint32_t bufferCount;
	VkBuffer* pBuffers;
	VkBufferDeviceAddressListTTT* pMarkings;
} VkBufferDeviceAddressBufferMarkingTTT;
