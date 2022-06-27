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

// ---- Fake extensions ----

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
	VK_TRACING_OBJECT_PROPERTY_ALLOCATIONS_COUNT_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_UPDATES_COUNT_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_UPDATES_BYTES_TRACETOOLTEST,
	VK_TRACING_OBJECT_PROPERTY_BACKING_DEVICEMEMORY_TRACETOOLTEST,
} VkTracingObjectPropertyTRACETOOLTEST;

typedef uint32_t (VKAPI_PTR *PFN_vkAssertBufferTRACETOOLTEST)(VkDevice device, VkBuffer buffer);
typedef uint64_t (VKAPI_PTR *PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST)(VkDevice device, VkObjectType objectType, uint64_t objectHandle, VkTracingObjectPropertyTRACETOOLTEST valueType);

// ---- Common code ----

#define check(result) \
	if (result != VK_SUCCESS) \
	{ \
		fprintf(stderr, "Error 0x%04x: %s\n", result, errorString(result)); \
	} \
	assert(result == VK_SUCCESS);

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
};

struct vulkan_req_t
{
	uint32_t apiVersion = VK_API_VERSION_1_1;
	uint32_t queues = 1;
	std::vector<std::string> extensions;
	bool samplerAnisotropy = false;
	TOOLSTEST_CALLBACK_USAGE usage = nullptr;
	TOOLSTEST_CALLBACK_CMDOPT cmdopt = nullptr;
};

const char* errorString(const VkResult errorCode);

void check_retval(VkResult stored_retval, VkResult retval);

/// Consistent top header for any extension struct. Used to iterate them and handle the ones we recognize.
struct dummy_ext { VkStructureType sType; dummy_ext* pNext; };

vulkan_setup_t test_init(int argc, char** argv, const std::string& testname, vulkan_req_t& reqs);
void test_done(vulkan_setup_t s);
uint32_t get_device_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
void test_set_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name);

void testFreeMemory(vulkan_setup_t vulkan, VkDeviceMemory memory);

/// Get default number of repeated loops to be done, taken from an environment variable if available.
int repeats();

/// Select which GPU to use
void select_gpu(int chosen_gpu);
