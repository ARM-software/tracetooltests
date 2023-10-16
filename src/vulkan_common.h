#pragma once

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>
#include <string>
#include <unordered_set>

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

struct vulkan_setup_t
{
	VkInstance instance = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	PFN_vkAssertBufferTRACETOOLTEST vkAssertBuffer = nullptr;
	PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST vkGetDeviceTracingObjectProperty = nullptr;
	PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectName = nullptr;
	PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabel = nullptr;
	PFN_vkFrameEndTRACETOOLTEST vkFrameEnd = nullptr;
	PFN_vkGetBufferDeviceAddress vkGetBufferDeviceAddress; 
	uint32_t apiVersion = VK_API_VERSION_1_1;
	std::unordered_set<std::string> instance_extensions;
	std::unordered_set<std::string> device_extensions;
	VkPhysicalDeviceProperties device_properties = {};
};

struct vulkan_req_t // Vulkan context requirements
{
	uint32_t apiVersion = VK_API_VERSION_1_1;
	uint32_t queues = 1;
	std::vector<std::string> instance_extensions;
	std::vector<std::string> device_extensions;
	bool samplerAnisotropy = false;
	bool bufferDeviceAddress = false;
	TOOLSTEST_CALLBACK_USAGE usage = nullptr;
	TOOLSTEST_CALLBACK_CMDOPT cmdopt = nullptr;
	VkInstance instance = VK_NULL_HANDLE; // reuse existing instance if non-null
	VkBaseInStructure* extension_features = nullptr;
};

namespace acceleration_structures{

	struct functions
	{
		PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
		PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
		PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
		PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
		PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
		PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructure;
	};

	functions query_acceleration_structure_functions(VkDevice device);

	struct Buffer
	{
		VkBuffer handle{ VK_NULL_HANDLE };
		VkDeviceMemory memory{ VK_NULL_HANDLE };
		VkDeviceOrHostAddressConstKHR address{};
	};

	Buffer prepare_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, void *data, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties);
	VkDeviceAddress get_buffer_device_address(const vulkan_setup_t& vulkan, VkBuffer buffer);

	struct BLAS
	{
		VkAccelerationStructureKHR handle{ VK_NULL_HANDLE};
		VkDeviceOrHostAddressConstKHR address{};
	};

};

/// Consistent top header for any extension struct. Used to iterate them and handle the ones we recognize.
struct dummy_ext { VkStructureType sType; dummy_ext* pNext; };

vulkan_setup_t test_init(int argc, char** argv, const std::string& testname, vulkan_req_t& reqs);
void test_done(vulkan_setup_t s, bool shared_instance = false);
uint32_t get_device_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
void test_set_name(const vulkan_setup_t& vulkan, VkObjectType type, uint64_t handle, const char* name);

uint32_t testAllocateBufferMemory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, std::vector<VkDeviceMemory>& memory, bool deviceaddress, bool dedicated, bool pattern, const char* name);
void testBindBufferMemory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, VkDeviceMemory memory, VkDeviceSize offset, const char* name = nullptr);
void testCmdCopyBuffer(const vulkan_setup_t& vulkan, VkCommandBuffer cmdbuf, const std::vector<VkBuffer>& origin, const std::vector<VkBuffer>& target, VkDeviceSize size);
void testFreeMemory(const vulkan_setup_t& vulkan, VkDeviceMemory memory);

/// Get default number of repeated loops to be done, taken from an environment variable if available.
int repeats();

/// Select which GPU to use
void select_gpu(int chosen_gpu);

void test_save_image(const vulkan_setup_t& vulkan, const char* filename, VkDeviceMemory memory, uint32_t offset, uint32_t size, uint32_t width, uint32_t height);
