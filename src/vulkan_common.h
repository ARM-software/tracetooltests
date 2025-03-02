#pragma once

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <cmath>
#include <vector>
#include <string>
#include <variant>
#include <unordered_set>
#include <unordered_map>

#ifdef NDEBUG
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-variable"
#else
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#endif

#include <vulkan/vulkan.h>

#include "util.h"
#include "vulkan_ext.h"

// ---- Common code ----

#define MAKEINSTANCEPROCADDR(v, name) \
	PFN_ ## name pf_ ## name = (PFN_ ## name)vkGetInstanceProcAddr(v.instance, # name); \
	assert(pf_ ## name);
#define MAKEDEVICEPROCADDR(v, name) \
	PFN_ ## name pf_ ## name = (PFN_ ## name)vkGetDeviceProcAddr(v.device, # name); \
	assert(pf_ ## name);

const char* errorString(const VkResult errorCode);

inline void check(VkResult result)
{
	if (result != VK_SUCCESS)
	{
		fprintf(stderr, "Error 0x%04x: %s\n", result, errorString(result));
	}
	assert(result == VK_SUCCESS);
}

struct vulkan_req_t;
typedef void (*TOOLSTEST_CALLBACK_USAGE)();
typedef bool (*TOOLSTEST_CALLBACK_CMDOPT)(int& i, int argc, char **argv, vulkan_req_t& reqs);

struct vulkan_req_t // Vulkan context requirements
{
	VkPhysicalDeviceVulkan13Features reqfeat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, nullptr };
	VkPhysicalDeviceVulkan12Features reqfeat12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &reqfeat13 };
	VkPhysicalDeviceVulkan11Features reqfeat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &reqfeat12 };
	VkPhysicalDeviceFeatures2 reqfeat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &reqfeat11 };
	uint32_t apiVersion = VK_API_VERSION_1_1;
	uint32_t minApiVersion = VK_API_VERSION_1_0; // the minimum required for the test
	uint32_t queues = 1;
	std::vector<std::string> instance_extensions;
	std::vector<std::string> device_extensions;
	bool samplerAnisotropy = false;
	bool bufferDeviceAddress = false;
	TOOLSTEST_CALLBACK_USAGE usage = nullptr;
	TOOLSTEST_CALLBACK_CMDOPT cmdopt = nullptr;
	VkInstance instance = VK_NULL_HANDLE; // reuse existing instance if non-null
	VkBaseInStructure* extension_features = nullptr;
	uint32_t fence_delay = 0;
	std::unordered_map<std::string, std::variant<int, bool, std::string>> options;
};

struct vulkan_setup_t
{
	VkPhysicalDeviceVulkan13Features hasfeat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, nullptr };
	VkPhysicalDeviceVulkan12Features hasfeat12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &hasfeat13 };
	VkPhysicalDeviceVulkan11Features hasfeat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &hasfeat12 };
	VkPhysicalDeviceFeatures2 hasfeat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &hasfeat11 };
	VkInstance instance = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;

	PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = nullptr;
	PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabel = nullptr;
	PFN_vkGetBufferDeviceAddress vkGetBufferDeviceAddress = nullptr;

	PFN_vkAssertBufferTRACETOOLTEST vkAssertBuffer = nullptr;
	PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST vkGetDeviceTracingObjectProperty = nullptr;
	PFN_vkCmdUpdateBuffer2TRACETOOLTEST vkCmdUpdateBuffer2 = nullptr;
	PFN_vkCmdPushConstants2KHR vkCmdPushConstants2 = nullptr;
	PFN_vkUpdateBufferTRACETOOLTEST vkUpdateBuffer = nullptr;
	PFN_vkUpdateImageTRACETOOLTEST vkUpdateImage = nullptr;
	PFN_vkPatchBufferTRACETOOLTEST vkPatchBuffer = nullptr;
	PFN_vkPatchImageTRACETOOLTEST vkPatchImage = nullptr;
	PFN_vkThreadBarrierTRACETOOLTEST vkThreadBarrier = nullptr;

	uint32_t apiVersion = VK_API_VERSION_1_1;
	std::unordered_set<std::string> instance_extensions;
	std::unordered_set<std::string> device_extensions;
	VkPhysicalDeviceProperties device_properties = {};
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR device_ray_tracing_pipeline_properties = {};
	benchmarking bench;
	bool has_trace_helpers = false;
};

namespace acceleration_structures
{
	struct functions
	{
		PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
		PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
		PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
		PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = nullptr;
		PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
		PFN_vkWriteAccelerationStructuresPropertiesKHR vkWriteAccelerationStructuresPropertiesKHR = nullptr;
		PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
		PFN_vkCopyAccelerationStructureKHR vkCopyAccelerationStructureKHR = nullptr;
		PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
		PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;

		PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
		PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
		PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;
	};

	functions query_acceleration_structure_functions(const vulkan_setup_t & vulkan);

	struct Buffer
	{
		VkBuffer handle{ VK_NULL_HANDLE };
		VkDeviceMemory memory{ VK_NULL_HANDLE };
		VkDeviceOrHostAddressConstKHR address{};
	};

	Buffer prepare_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, void *data, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties);
	VkDeviceAddress get_buffer_device_address(const vulkan_setup_t& vulkan, VkBuffer buffer);

	VkPipelineShaderStageCreateInfo prepare_shader_stage_create_info(const vulkan_setup_t & vulkan, const uint8_t * spirv, uint32_t spirv_length, VkShaderStageFlagBits shader_stage);

	struct AccelerationStructure
	{
		VkAccelerationStructureKHR handle{ VK_NULL_HANDLE};
		VkDeviceOrHostAddressConstKHR address{};
	};
};

/// Consistent top header for any extension struct. Used to iterate them and handle the ones we recognize.
struct dummy_ext { VkStructureType sType; dummy_ext* pNext; };

vulkan_setup_t test_init(int argc, char** argv, const std::string& testname, vulkan_req_t& reqs);
void test_done(vulkan_setup_t& vulkan, bool shared_instance = false);
uint32_t get_device_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
void test_set_name(const vulkan_setup_t& vulkan, VkObjectType type, uint64_t handle, const char* name);

uint32_t testAllocateBufferMemory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, std::vector<VkDeviceMemory>& memory, bool deviceaddress, bool dedicated, bool pattern, const char* name);
void testBindBufferMemory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, VkDeviceMemory memory, VkDeviceSize offset, const char* name = nullptr);
void testCmdCopyBuffer(const vulkan_setup_t& vulkan, VkCommandBuffer cmdbuf, const std::vector<VkBuffer>& origin, const std::vector<VkBuffer>& target, VkDeviceSize size);
void testFreeMemory(const vulkan_setup_t& vulkan, VkDeviceMemory memory);
void testFlushMemory(const vulkan_setup_t& vulkan, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size);

/// Adds a dummy queue submit with a pipeline barrier that references the passed buffers in order to make tools not ignore them.
void testQueueBuffer(const vulkan_setup_t& vulkan, VkQueue queue, const std::vector<VkBuffer>& buffers);

/// Copy one buffer into another
void testCopyBuffer(const vulkan_setup_t& vulkan, VkQueue queue, VkBuffer target, VkBuffer origin, VkDeviceSize size);

/// Get default number of repeated loops to be done, taken from an environment variable if available.
int repeats();

/// Select which GPU to use
void select_gpu(int chosen_gpu);

/// Takes an RGBA8888 image and saves it to disk as PNG
void test_save_image(const vulkan_setup_t& vulkan, const char* filename, VkDeviceMemory memory, uint32_t offset, uint32_t width, uint32_t height);

bool shader_has_buffer_devices_addresses(const uint32_t* code, uint32_t codeSize);
bool enable_frame_boundary(vulkan_req_t& reqs);

static inline std::vector<uint32_t> copy_shader(unsigned char* arr, uint32_t size)
{
	std::vector<uint32_t> code;
	const uint32_t code_size = long(ceil(size / 4.0)) * 4;
	code.resize(code_size);
	memcpy(code.data(), arr, size);
	return code;
}
