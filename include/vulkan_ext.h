#pragma once

#include <vulkan/vulkan.h>

// ---- Fake extensions ----

// -- VK_TRACETOOLTEST_benchmarking --

#define VK_TRACETOOLTEST_BENCHMARKING_EXTENSION_NAME "VK_TRACETOOLTEST_benchmarking"

typedef enum VkTracingFlagsTRACETOOLTEST {
	VK_TRACING_NO_COHERENT_MEMORY_BIT_TRACETOOLTEST = 0x00000001,
	VK_TRACING_NO_SUBALLOCATION_BIT_TRACETOOLTEST = 0x00000002,
	VK_TRACING_NO_MEMORY_ALIASING_BIT_TRACETOOLTEST = 0x00000004,
	VK_TRACING_NO_POINTER_OFFSETS_BIT_TRACETOOLTEST = 0x00000008,
	VK_TRACING_NO_JUST_IN_TIME_REUSE_BIT_TRACETOOLTEST = 0x00000010,
} VkTracingFlagsTRACETOOLTEST;

typedef struct VkBenchmarkingTRACETOOLTEST {
	VkStructureType             sType;
	void*                       pNext;
	VkFlags                     flags;
//	VkBool32                    noninteractive;
	uint32_t                    fixedTimeStep;
	VkBool32                    disablePerformanceAdaptation;
	VkBool32                    disableVendorAdaptation;
	VkBool32                    disableLoadingFrames;
	uint32_t                    visualSettings;
	uint32_t                    scenario;
	uint32_t                    loopTime;
	VkTracingFlagsTRACETOOLTEST tracingFlags;
} VkBenchmarkingModesEXT;

const VkStructureType VK_STRUCTURE_TYPE_BENCHMARKING_TRACETOOLTEST = (VkStructureType)700000141u; // a pretty random number

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


// -- VK_TRACETOOLTEST_checksum_validation --

#define VK_TRACETOOLTEST_CHECKSUM_VALIDATION_EXTENSION_NAME "VK_TRACETOOLTEST_checksum_validation"

typedef uint32_t (VKAPI_PTR *PFN_vkAssertBufferTRACETOOLTEST)(VkDevice device, VkBuffer buffer);


// -- VK_TRACETOOLTEST_object_property --

#define VK_TRACETOOLTEST_OBJECT_PROPERTY_EXTENSION_NAME "VK_TRACETOOLTEST_object_property"

typedef uint64_t (VKAPI_PTR *PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST)(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkTracingObjectPropertyTRACETOOLTEST valueType);

// -- VK_TRACETOOLTEST_frame_end

#define VK_TRACETOOLTEST_FRAME_END_EXTENSION_NAME "VK_TRACETOOLTEST_frame_end"

typedef void (VKAPI_PTR *PFN_vkFrameEndTRACETOOLTEST)(VkDevice device);
