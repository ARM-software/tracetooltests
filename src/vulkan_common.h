#pragma once

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <vector>
#include <string>

#ifdef NDEBUG
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-variable"
#else
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#endif

#include "vulkan/vulkan.h"

#include "util.h"

#define check(result) \
	if (result != VK_SUCCESS) \
	{ \
		fprintf(stderr, "Error 0x%04x: %s\n", result, errorString(result)); \
	} \
	assert(result == VK_SUCCESS);

struct vulkan_setup_t
{
	VkInstance instance;
	VkDevice device;
	VkPhysicalDevice physical;
};

struct vulkan_req_t
{
	uint32_t apiVersion = VK_API_VERSION_1_1;
	uint32_t queues = 1;
	std::vector<std::string> extensions;
};

const char* errorString(const VkResult errorCode);

void check_retval(VkResult stored_retval, VkResult retval);

/// Consistent top header for any extension struct. Used to iterate them and handle the ones we recognize.
struct dummy_ext { VkStructureType sType; dummy_ext* pNext; };

vulkan_setup_t test_init(const std::string& testname, const vulkan_req_t& reqs = vulkan_req_t());
void test_done(vulkan_setup_t s);
uint32_t get_device_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
void test_set_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name);

/// Get default number of repeated loops to be done, taken from an environment variable if available.
int repeats();

/// Select which GPU to use
void select_gpu(int chosen_gpu);
