#include <atomic>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <bitset>
#include <algorithm>

#include "util.h"
#include "vulkan_defs.h"
#include "vulkan_print.h"
#include "vulkan_auto.h"
#include "commandbuffer.h"
#include "vkjson.h"

/// Used to turn on writing report files to disk
const char* report_env = "CHAMELEON_REPORT";

#ifndef CHAMELEON_DEFAULT_GPU_PATH
#define CHAMELEON_DEFAULT_GPU_PATH "GPUs/Mali-G925"
#endif

static constexpr uint32_t chameleon_api_version = VK_HEADER_VERSION_COMPLETE;
static constexpr uint32_t chameleon_min_icd_interface_version = 5;
static constexpr uint32_t chameleon_max_icd_interface_version = CURRENT_LOADER_ICD_INTERFACE_VERSION;

// -- Extensions
// extension name, and last checked extension update version from the extension registry

static const std::vector<VkExtensionProperties> supported_instance_extensions =
{
	VkExtensionProperties { "VK_KHR_get_physical_device_properties2", 1 },
	VkExtensionProperties { "VK_KHR_surface", 25 },
	VkExtensionProperties { "VK_KHR_display", 21 },
	VkExtensionProperties { "VK_EXT_debug_report", 9 },
	VkExtensionProperties { "VK_EXT_display_surface_counter", 1 },
	VkExtensionProperties { "VK_EXT_direct_mode_display", 1  },
	VkExtensionProperties { "VK_KHR_external_semaphore_capabilities", 1 },
	VkExtensionProperties { "VK_EXT_headless_surface", 1 },
	VkExtensionProperties { "VK_KHR_external_fence_capabilities", 1 },
	VkExtensionProperties { "VK_KHR_device_group_creation", 1 },
	VkExtensionProperties { "VK_KHR_get_surface_capabilities2", 1 },
	VkExtensionProperties { "VK_KHR_external_memory_capabilities", 1 },
	VkExtensionProperties { "VK_EXT_swapchain_colorspace", 4 },
#ifdef VK_USE_PLATFORM_XCB_KHR
	VkExtensionProperties { "VK_KHR_xcb_surface", 6 },
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
	VkExtensionProperties { "VK_KHR_xlib_surface", 6 },
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	VkExtensionProperties { "VK_KHR_wayland_surface", 6 },
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
	VkExtensionProperties { "VK_KHR_android_surface", 6 },
#endif
};

static const std::vector<VkExtensionProperties> additional_device_extensions =
{
	VkExtensionProperties { "VK_ARM_tensors", 1 },
	VkExtensionProperties { "VK_ARM_data_graph", 1 },
};

#ifndef FAST
/// Track particular frames of interest
static std::unordered_set<int> frames_of_interest;
#endif

// -- Vulkan internals static instanciation

/// Our thread ID, stored in thread local storage.
static std::atomic_int thread_count { 0 };
thread_local long thread_id = thread_count++;

/// If we initialize multiple instances, then the frame count is not individual for
/// each of them. This is intentional.
std::atomic_int cVkBase::current_frame;

/// Count and distinguish between different instances, in case an implementation creates
/// multiple instances. (Not sure in what use cases it would ever do this, though.)
std::atomic_int cVkInstance::instance_counter;

/// We need a non-atomic to figure out if we have initialized our atomics yet...
static bool atomics_initialized = false;

/// Keep all memory allocations around, in case we want to dump their contents later.
static bool store_allocations = false;

/// Keep track of last global UID number used.
std::atomic_int cVkBase::last_uid;


// -- Helpers
// Helper function names are always lowercase and underscore separated

// The line above needs to be the same as the one in Vulkan.h
// There really should be a prettier way to do this...
#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__) ) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
#define NHANDLE "%p"
#else
#define NHANDLE "%llu"
#endif

#define ENTRY(_name) count_ ## _name++;

#define instance_cast(c) ccast<cVkInstance, VkInstance>(c)
#define physicaldevice_cast(c) ccast<cVkPhysicalDevice, VkPhysicalDevice>(c)
#define devicememory_cast(c) ccast<cVkDeviceMemory, VkDeviceMemory>(c)
#define surface_cast(c) ccast<cVkSurfaceKHR, VkSurfaceKHR>(c)
#define swapchain_cast(c) ccast<cVkSwapchainKHR, VkSwapchainKHR>(c)
#define device_cast(c) ccast<cVkDevice, VkDevice>(c)
#define queue_cast(c) ccast<cVkQueue, VkQueue>(c)
#define fence_cast(c) ccast<cVkFence, VkFence>(c)
#define semaphore_cast(c) ccast<cVkSemaphore, VkSemaphore>(c)
#define event_cast(c) ccast<cVkEvent, VkEvent>(c)
#define privatedataslot_cast(c) ccast<cVkPrivateDataSlot, VkPrivateDataSlot>(c)
#define commandpool_cast(c) ccast<cVkCommandPool, VkCommandPool>(c)
#define commandbuffer_cast(c) ccast<cVkCommandBuffer, VkCommandBuffer>(c)
#ifndef FAST
#define commandbuffer_command(_name, _c, _metrics) \
	ccast<cVkCommandBuffer, VkCommandBuffer>(_c); \
        vk_command _cmd = ENUM_ ## _name; \
	p->commands.push_back(cVkCommand(_cmd, _metrics)); \
	p->count[ENUM_ ## _name] += _metrics; \
	p->sum += _metrics;
#else
#define commandbuffer_command(_name, _c, _metrics) \
	ccast<cVkCommandBuffer, VkCommandBuffer>(_c); \
        vk_command _cmd = ENUM_ ## _name; \
	p->commands.push_back(cVkCommand(_cmd, _metrics));
#endif
#define buffer_cast(c) ccast<cVkBuffer, VkBuffer>(c)
#define bufferview_cast(c) ccast<cVkBufferView, VkBufferView>(c)
#define image_cast(c) ccast<cVkImage, VkImage>(c)
#define imageview_cast(c) ccast<cVkImageView, VkImageView>(c)
#define querypool_cast(c) ccast<cVkQueryPool, VkQueryPool>(c)
#define pipelinecache_cast(c) ccast<cVkPipelineCache, VkPipelineCache>(c)
#define pipelinebinary_cast(c) ccast<cVkPipelineBinary, VkPipelineBinaryKHR>(c)
#define shadermodule_cast(c) ccast<cVkShaderModule, VkShaderModule>(c)
#define pipeline_cast(c) ccast<cVkPipeline, VkPipeline>(c)
#define pipelinelayout_cast(c) ccast<cVkPipelineLayout, VkPipelineLayout>(c)
#define sampler_cast(c) ccast<cVkSampler, VkSampler>(c)
#define descriptorsetlayout_cast(c) ccast<cVkDescriptorSetLayout, VkDescriptorSetLayout >(c)
#define descriptorset_cast(c) ccast<cVkDescriptorSet, VkDescriptorSet>(c)
#define renderpass_cast(c) ccast<cVkRenderPass, VkRenderPass>(c)
#define descriptorpool_cast(c) ccast<cVkDescriptorPool, VkDescriptorPool>(c)
#define framebuffer_cast(c) ccast<cVkFramebuffer, VkFramebuffer>(c)
#define displaymode_cast(c) ccast<cVkDisplayModeKHR, VkDisplayModeKHR>(c)
#define display_cast(c) ccast<cVkDisplayKHR, VkDisplayKHR>(c)
#define descriptorupdatetemplate_cast(c) ccast<cVkDescriptorUpdateTemplate, VkDescriptorUpdateTemplate>(c)
#define accelerationstructure_cast(c) ccast<cVkAccelerationStructureKHR, VkAccelerationStructureKHR>(c)
#define weights_cast(c) ccast<cVkWeights, VkWeightsARM>(c)
#define tensor_cast(c) ccast<cVkTensor, VkTensorARM>(c)
#ifdef VK_ARM_SHADER_INSTRUMENTATION_SPEC_VERSION
#define shaderinstrumentationarm_cast(c) ccast<cVkShaderInstrumentationARM, VkShaderInstrumentationARM>(c)
#endif
#define tensorview_cast(c) ccast<cVkTensorView, VkTensorViewARM>(c)
#define datagraphpipelinesession_cast(c) ccast<cVkDataGraphPipelineSession, VkDataGraphPipelineSessionARM>(c)
#define samplerycbcrconversion_cast(c) ccast<cVkSamplerYcbcrConversion, VkSamplerYcbcrConversion>(c)

/// Create a Vulkan object and sets its basic properties. If ptr is set, then this is
/// a variable that should contain a pointer to the new object in Vk pointer form.
template<typename T, typename U> // T = cVk, U = Vk
static inline T& owner_create(std::list<T>& list, U* ptr, const VkAllocationCallbacks* c)
{
	(void)c;
	list.emplace_back();
	touch(&list.back());
	if (ptr)
	{
		*ptr = reinterpret_cast<U>(&list.back());
	}
	return list.back();
}

template<typename T, typename U> // T = cVk, U = Vk
static inline T* destroy(U ptr, const VkAllocationCallbacks* c)
{
	(void)c;
	T* p = reinterpret_cast<T*>(ptr);
	if (p)
	{
		touch(p);
		p->destroyed = true;
		p->destroyed_frame = p->current_frame;
	}
	return p;
}

#define TBD_UNSUPPORTED printf("%s is not yet supported!\n", __FUNCTION__);


// -- Implementation

/// Called from each function entry point that could be the first one the application calls.
static void maybe_enable_logging()
{
	const char* logging = getenv("CHAMELEON_LOGGING");

	if (logging && !logfp)
	{
		const char* logfile_path = getenv("CHAMELEON_LOG_FILE_PATH");
		if (logfile_path) {
			logfp = fopen(logfile_path, "w");
		} else {
			logfp = stderr;
		}
	}

	if (logging && strcasecmp(logging, "verbose") == 0)
	{
		verbose_logging = 1;
	}
	else if (logging && strcasecmp(logging, "full") == 0)
	{
		verbose_logging = 2;
	}
}

static PFN_vkVoidFunction lookup_raw_proc(const char* pName)
{
	if (!pName) return nullptr;

	auto it = function_map.find(pName);
	if (it != function_map.end())
	{
		return it->second;
	}

	return nullptr;
}

static PFN_vkVoidFunction lookup_global_proc(const char* pName)
{
	if (!pName) return nullptr;

	if (strcmp(pName, "vkCreateInstance") == 0) return (PFN_vkVoidFunction)vkCreateInstance;
	if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
	if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties;
	if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
	if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;

	return nullptr;
}

static PFN_vkVoidFunction lookup_icd_proc(const char* pName)
{
	if (!pName) return nullptr;

	if (strcmp(pName, "vk_icdNegotiateLoaderICDInterfaceVersion") == 0) return (PFN_vkVoidFunction)vk_icdNegotiateLoaderICDInterfaceVersion;
	if (strcmp(pName, "vk_icdGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vk_icdGetInstanceProcAddr;
	if (strcmp(pName, "vk_icdGetPhysicalDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vk_icdGetPhysicalDeviceProcAddr;

	return nullptr;
}

static bool is_physical_device_proc(const char* pName)
{
	if (!pName) return false;

	if (strncmp(pName, "vkGetPhysicalDevice", 19) == 0) return true;
	if (strcmp(pName, "vkReleaseDisplayEXT") == 0) return true;
	if (strcmp(pName, "vkAcquireXlibDisplayEXT") == 0) return true;
	if (strcmp(pName, "vkGetRandROutputDisplayEXT") == 0) return true;
	if (strcmp(pName, "vkAcquireDrmDisplayEXT") == 0) return true;
	if (strcmp(pName, "vkGetDrmDisplayEXT") == 0) return true;

	return false;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
	ENTRY(vkGetInstanceProcAddr);
	CLOG("instance=%p, pName=%s", instance, pName ? pName : "(null)");

	PFN_vkVoidFunction proc = nullptr;
	if (!instance)
	{
		proc = lookup_global_proc(pName);
	}
	else
	{
		proc = lookup_raw_proc(pName);
	}

	if (proc) return proc;
	XLOG("Asked for unsupported instance function: %s", pName ? pName : "(null)");
	return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice                                    device,
    const char*                                 pName)
{
	ENTRY(vkGetDeviceProcAddr);
	CLOG("instance=%p, pName=%s", device, pName ? pName : "(null)");

	if (device) (void)device_cast(device);

	PFN_vkVoidFunction proc = lookup_raw_proc(pName);
	if (proc) return proc;
	XLOG("Asked for unsupported device function: %s", pName ? pName : "(null)");
	return nullptr;
}

__attribute__((visibility("default"))) VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(
    uint32_t*                                   pVersion)
{
	maybe_enable_logging();

	if (!pVersion)
	{
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	if (*pVersion < chameleon_min_icd_interface_version)
	{
		return VK_ERROR_INCOMPATIBLE_DRIVER;
	}

	*pVersion = std::min(*pVersion, chameleon_max_icd_interface_version);
	return VK_SUCCESS;
}

__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
	maybe_enable_logging();

	PFN_vkVoidFunction proc = lookup_icd_proc(pName);
	if (proc) return proc;

	if (!instance)
	{
		return lookup_global_proc(pName);
	}

	return lookup_raw_proc(pName);
}

__attribute__((visibility("default"))) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
	maybe_enable_logging();
	(void)instance;

	if (!is_physical_device_proc(pName))
	{
		return nullptr;
	}

	return lookup_raw_proc(pName);
}

static void loadGpu(cVkPhysicalDevice& gpu, const std::string& gpu_path, const std::string& gpu_override_path)
{
	// Loading GPU JSON

	Json::Value gpu_json = readJson(gpu_path);
	mergeJson(gpu_json, readJson(gpu_override_path));

	const Json::Value& deviceRoot = gpu_json["capabilities"]["device"];

	// Extensions

	const Json::Value& extensionsRoot = deviceRoot["extensions"];
	for (const std::string& extensionName: extensionsRoot.getMemberNames())
	{
		assert(gpu.extensions.find(extensionName) == gpu.extensions.end());
		gpu.extensions[extensionName] = extensionsRoot[extensionName].asUInt();
	}

	for (const VkExtensionProperties& extensionProperties: additional_device_extensions)
	{
		gpu.extensions[extensionProperties.extensionName] = extensionProperties.specVersion;
	}

	// Features

	readVulkanFeatures(deviceRoot["features"], gpu.features);
	assert(gpu.features.find(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) != gpu.features.end());

	// Force support for this, since it is really useful for learning
	reinterpret_cast<VkPhysicalDeviceFeatures*>(gpu.features[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2].first)->pipelineStatisticsQuery = true;

	// Physical device properties

	const Json::Value& propertiesRoot = deviceRoot["properties"];
	const Json::Value& physicalDevicePropertiesRoot = deviceRoot["properties"]["VkPhysicalDeviceProperties"];
	readVkPhysicalDeviceProperties(physicalDevicePropertiesRoot, gpu.properties);

	// Ray tracing & acceleration structure properties (provide sane defaults if missing)
	if (deviceRoot["properties"].isMember("VkPhysicalDeviceRayTracingPipelinePropertiesKHR"))
	{
		const Json::Value& rtProps = deviceRoot["properties"]["VkPhysicalDeviceRayTracingPipelinePropertiesKHR"];
		gpu.rayTracingPipelineProperties.shaderGroupHandleSize = rtProps["shaderGroupHandleSize"].asUInt();
		gpu.rayTracingPipelineProperties.maxRayRecursionDepth = rtProps["maxRayRecursionDepth"].asUInt();
		gpu.rayTracingPipelineProperties.maxShaderGroupStride = rtProps["maxShaderGroupStride"].asUInt();
		gpu.rayTracingPipelineProperties.shaderGroupBaseAlignment = rtProps["shaderGroupBaseAlignment"].asUInt();
		gpu.rayTracingPipelineProperties.shaderGroupHandleCaptureReplaySize = rtProps["shaderGroupHandleCaptureReplaySize"].asUInt();
		gpu.rayTracingPipelineProperties.maxRayDispatchInvocationCount = rtProps["maxRayDispatchInvocationCount"].asUInt64();
		gpu.rayTracingPipelineProperties.shaderGroupHandleAlignment = rtProps["shaderGroupHandleAlignment"].asUInt();
		gpu.rayTracingPipelineProperties.maxRayHitAttributeSize = rtProps["maxRayHitAttributeSize"].asUInt();
	}

	if (deviceRoot["properties"].isMember("VkPhysicalDeviceAccelerationStructurePropertiesKHR"))
	{
		const Json::Value& asProps = deviceRoot["properties"]["VkPhysicalDeviceAccelerationStructurePropertiesKHR"];
		gpu.accelerationStructureProperties.maxGeometryCount = asProps["maxGeometryCount"].asUInt64();
		gpu.accelerationStructureProperties.maxInstanceCount = asProps["maxInstanceCount"].asUInt64();
		gpu.accelerationStructureProperties.maxPrimitiveCount = asProps["maxPrimitiveCount"].asUInt64();
		gpu.accelerationStructureProperties.maxPerStageDescriptorAccelerationStructures = asProps["maxPerStageDescriptorAccelerationStructures"].asUInt();
		gpu.accelerationStructureProperties.maxPerStageDescriptorUpdateAfterBindAccelerationStructures = asProps["maxPerStageDescriptorUpdateAfterBindAccelerationStructures"].asUInt();
		gpu.accelerationStructureProperties.maxDescriptorSetAccelerationStructures = asProps["maxDescriptorSetAccelerationStructures"].asUInt();
		gpu.accelerationStructureProperties.maxDescriptorSetUpdateAfterBindAccelerationStructures = asProps["maxDescriptorSetUpdateAfterBindAccelerationStructures"].asUInt();
		gpu.accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment = asProps["minAccelerationStructureScratchOffsetAlignment"].asUInt();
	}

	if (deviceRoot["properties"].isMember("VkPhysicalDeviceDriverPropertiesKHR"))
	{
		const Json::Value& driverPropertiesRoot = deviceRoot["properties"]["VkPhysicalDeviceDriverPropertiesKHR"];
		VkPhysicalDeviceDriverProperties driverProperties = {};
		readVkPhysicalDeviceDriverProperties(driverPropertiesRoot, driverProperties);
		gpu.properties.driverVersion = VK_MAKE_VERSION(driverProperties.conformanceVersion.major,
			driverProperties.conformanceVersion.minor, driverProperties.conformanceVersion.patch);
		gpu.properties.deviceID = 0; // override from JSON?
		gpu.properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER; // override from JSON?
		strncpy(gpu.properties.deviceName, driverProperties.driverName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
	}

	if (propertiesRoot.isMember("VkPhysicalDeviceDriverPropertiesKHR"))
	{
		const Json::Value& root = propertiesRoot["VkPhysicalDeviceDriverPropertiesKHR"];
		size_t property_size = sizeof(VkPhysicalDeviceDriverProperties);
		VkPhysicalDeviceDriverProperties* property =
			reinterpret_cast<VkPhysicalDeviceDriverProperties*>(malloc(property_size));
		readVkPhysicalDeviceDriverProperties(root, *property);
		gpu.extendedProperties[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES] = { property, property_size };
	}

	if (propertiesRoot.isMember("VkPhysicalDeviceVulkan12Properties"))
	{
		const Json::Value& root = propertiesRoot["VkPhysicalDeviceVulkan12Properties"];
		size_t property_size = sizeof(VkPhysicalDeviceVulkan12Properties);
		VkPhysicalDeviceVulkan12Properties* property =
			reinterpret_cast<VkPhysicalDeviceVulkan12Properties*>(malloc(property_size));
		readVkPhysicalDeviceVulkan12Properties(root, *property);
		gpu.extendedProperties[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES] = { property, property_size };
	}

	if (propertiesRoot.isMember("VkPhysicalDeviceAccelerationStructurePropertiesKHR"))
	{
		const Json::Value& root = propertiesRoot["VkPhysicalDeviceAccelerationStructurePropertiesKHR"];
		size_t property_size = sizeof(VkPhysicalDeviceAccelerationStructurePropertiesKHR);
		VkPhysicalDeviceAccelerationStructurePropertiesKHR* property =
			reinterpret_cast<VkPhysicalDeviceAccelerationStructurePropertiesKHR*>(malloc(property_size));
		readVkPhysicalDeviceAccelerationStructurePropertiesKHR(root, *property);
		gpu.extendedProperties[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR] = { property, property_size };
	}

	if (propertiesRoot.isMember("VkPhysicalDeviceRayTracingPipelinePropertiesKHR"))
	{
		const Json::Value& root = propertiesRoot["VkPhysicalDeviceRayTracingPipelinePropertiesKHR"];
		size_t property_size = sizeof(VkPhysicalDeviceRayTracingPipelinePropertiesKHR);
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR* property =
			reinterpret_cast<VkPhysicalDeviceRayTracingPipelinePropertiesKHR*>(malloc(property_size));
		readVkPhysicalDeviceRayTracingPipelinePropertiesKHR(root, *property);
		gpu.extendedProperties[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR] = { property, property_size };
	}

	// Memory

	const Json::Value& memoryPropertiesRoot = deviceRoot["properties"]["VkPhysicalDeviceMemoryProperties"];
	readVkPhysicalDeviceMemoryProperties(memoryPropertiesRoot, gpu.memoryProperties);

	// Formats

	readFormats(deviceRoot["formats"], gpu.formats);

	// Queue families

	for (const Json::Value& queueFamily : deviceRoot["queueFamiliesProperties"])
	{
		const Json::Value& queueFamilyRoot = queueFamily["VkQueueFamilyProperties"];

		gpu.queueFamilies.emplace_back();
		readVkQueueFamilyProperties(queueFamilyRoot, gpu.queueFamilies.back());
	}

	// Create one display

	gpu.displays.emplace_back(); 
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
	ENTRY(vkCreateInstance);
	maybe_enable_logging();

	CLOG("pCreateInfo=%p[%u layers, %u extensions], pAllocator=%p, pInstance=%p", pCreateInfo,
	     pCreateInfo->enabledLayerCount, pCreateInfo->enabledExtensionCount, pAllocator, pInstance);

	(void)pAllocator; // ignored

	assert(pCreateInfo);
	assert(pInstance);

	if (!atomics_initialized)
	{
		cVkInstance::current_frame = 0;
		cVkInstance::last_uid = 0;
		cVkInstance::instance_counter = 0;
		atomics_initialized = true;
		if (getenv("CHAMELEON_DUMP"))
		{
			store_allocations = true;
		}
	}

#ifndef FAST
	frames_of_interest = get_env_ints("CHAMELEON_FRAMES");
#endif

	const char* env_key = "CHAMELEON_GPU";
	const char* path = getenv(env_key);
	if (!path || !path[0])
	{
		path = CHAMELEON_DEFAULT_GPU_PATH;
		XLOG("%s was not set, falling back to %s", env_key, path);
	}

	std::string gpu_path = std::string(path) + "/gpu.json";
	std::string gpu_override_path = std::string(path) + "/gpu_override.json";

	if (access(gpu_path.c_str(), R_OK) != 0)
	{
		ELOG("Could not access file: %s", gpu_path.c_str());
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	if (access(gpu_override_path.c_str(), R_OK) != 0)
	{
		ELOG("Could not access file: %s", gpu_override_path.c_str());
		return VK_ERROR_INITIALIZATION_FAILED;
	}

	cVkInstance *instance = new cVkInstance;
#ifndef FAST
	instance->accessed_by_thread.insert(thread_id);
	instance->used_in_frame.insert(instance->current_frame);
	instance->used_in_frame_transitive.insert(instance->current_frame);
#endif

	cVkPhysicalDevice gpu;
	loadGpu(gpu, gpu_path, gpu_override_path);

	instance->GPUs.emplace_back(gpu);

	for (unsigned i = 0; i < pCreateInfo->enabledLayerCount; i++)
	{
		XLOG("requested instance layer: %s", pCreateInfo->ppEnabledLayerNames[i]);
	}

	instance->enabledExtensions.reserve(pCreateInfo->enabledExtensionCount);
	for (unsigned i = 0; i < pCreateInfo->enabledExtensionCount; i++)
	{
		XLOG("requested instance extension: %s", pCreateInfo->ppEnabledExtensionNames[i]);
		instance->enabledExtensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
	}

	*pInstance = reinterpret_cast<VkInstance>(instance);
	XLOG("created instance %p", *pInstance);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyInstance);
	CLOG("instance=%p, pAllocator=%p", instance, pAllocator);
	if (instance == 0) return;

	const char* report_hw_env = "CHAMELEON_REPORT_HW";
	(void)pAllocator; // ignored
	cVkInstance* cinstance = instance_cast(instance);
	cinstance->destroyed = true;
	cinstance->destroyed_frame = cinstance->current_frame;
#ifndef FAST
	// Write out json report and callstats to disk, if report option given
	const char* report_name = getenv(report_env);
	const char* report_hw = getenv(report_hw_env);
	if (report_name)
	{
		json_overview(report_name, cinstance, report_hw != nullptr);
		std::string callstats_name = std::string(report_name) + "_callstats.csv";
		save_counts(callstats_name.c_str());
	}
#endif

	for (cVkPhysicalDevice& gpu: cinstance->GPUs)
	{
		for (auto& elt: gpu.features)
		{
			free(elt.second.first);
		}
		for (auto& elt: gpu.extendedProperties)
		{
			free(elt.second.first);
		}
	}

	delete cinstance;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices)
{
	ENTRY(vkEnumeratePhysicalDevices);
	CLOG("instance=%p, pPhysicalDeviceCount=%u, pPhysicalDevices=%p", instance, (uint32_t)*pPhysicalDeviceCount, pPhysicalDevices);

	cVkInstance* cinstance = instance_cast(instance);
	if (pPhysicalDevices)
	{
		assert(pPhysicalDeviceCount);
		assert(*pPhysicalDeviceCount > 0);
		assert(*pPhysicalDeviceCount <= cinstance->GPUs.size());
		int i = 0;
		for (auto& dev : cinstance->GPUs)
		{
			pPhysicalDevices[i] = reinterpret_cast<VkPhysicalDevice>(&dev);
			i++;
			if (i >= (int)*pPhysicalDeviceCount) break;
		}
	}
	*pPhysicalDeviceCount = cinstance->GPUs.size();
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures)
{
	ENTRY(vkGetPhysicalDeviceFeatures);
	CLOG("physicalDevice=%p, pFeatures=%p", physicalDevice, pFeatures);

	memset(pFeatures, 0, sizeof(*pFeatures));
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	// The key is "features_2" because there is no key for just "features" and the structure associated to "features" has no fields to be saved in the map
	*pFeatures = *reinterpret_cast<VkPhysicalDeviceFeatures*>(device->features[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2].first);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceFormatProperties);
	CLOG("physicalDevice=%p, format=%u, pFormatProperties=%p", physicalDevice, format, pFormatProperties);

	memset(pFormatProperties, 0, sizeof(*pFormatProperties));
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	*pFormatProperties = device->formats[format]; // struct copy
}

static VkResult commonGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties)
{
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);

	// make stuff up for now - problem is this info is hard to get by, only supported by interrogate
	pImageFormatProperties->maxExtent.width = 8192;
	pImageFormatProperties->maxExtent.height = 8192;
	pImageFormatProperties->maxExtent.depth = 4096;
	pImageFormatProperties->maxMipLevels = 16;
	pImageFormatProperties->maxArrayLayers = 256;
	pImageFormatProperties->sampleCounts = 1;
	pImageFormatProperties->maxResourceSize = 2147483648;

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceImageFormatProperties);
	CLOG("physicalDevice=%p, format=%u, type=%u, tiling=%u, usage=%u, flags=%u, pImageFormatProperties=%p",
	     physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
	return commonGetPhysicalDeviceImageFormatProperties(physicalDevice, format, type, tiling, usage, flags, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties)
{
	ENTRY(vkGetPhysicalDeviceProperties);
	CLOG("physicalDevice=%p, pProperties=%p", physicalDevice, pProperties);

	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	*pProperties = device->properties; // struct copy
}

static void commonGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);

	assert(pQueueFamilyPropertyCount);

	uint32_t count = device->queueFamilies.size();
	if (pQueueFamilyProperties)
	{
		count = std::min(*pQueueFamilyPropertyCount, count);
		auto it = device->queueFamilies.begin();
		for (uint32_t i = 0; i < count; ++i, ++it)
		{
			pQueueFamilyProperties[i] = *it;
		}
	}

	*pQueueFamilyPropertyCount = count;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
	ENTRY(vkGetPhysicalDeviceQueueFamilyProperties);
	CLOG("physicalDevice=%p, pQueueFamilyPropertyCount=%u, pQueueFamilyProperties=%p", physicalDevice, *pQueueFamilyPropertyCount, pQueueFamilyProperties);
	commonGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

static void commonGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);

	assert(pMemoryProperties);
	*pMemoryProperties = device->memoryProperties;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
	ENTRY(vkGetPhysicalDeviceMemoryProperties);
	CLOG("physicalDevice=%p, pMemoryProperties=%p", physicalDevice, pMemoryProperties);
	commonGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
	ENTRY(vkCreateDevice);
	CLOG("physicalDevice=%p, pCreateInfo=%p, pAllocator=%p, pDevice=%p", physicalDevice, pCreateInfo, pAllocator, pDevice);

	assert(physicalDevice);
	assert(pCreateInfo);
	assert(pDevice);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
	assert(pCreateInfo->queueCreateInfoCount > 0);

	cVkPhysicalDevice* pdevice = physicaldevice_cast(physicalDevice);
	cVkDevice& dev = owner_create<cVkDevice, VkDevice>(pdevice->devices, pDevice, pAllocator);

	int num_queues = 0;
	for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
	{
		const VkDeviceQueueCreateInfo& q = pCreateInfo->pQueueCreateInfos[i];
		assert(q.sType == VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
		num_queues += q.queueCount;
	}
	int index = 0;
	for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
	{
		const VkDeviceQueueCreateInfo& q = pCreateInfo->pQueueCreateInfos[i];
		assert(q.sType == VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
		for (unsigned j = 0; j < q.queueCount; j++, index++)
		{
			cVkQueue queue;
			queue.index = index;
			queue.priority = q.pQueuePriorities[j];
			dev.queues.push_back(queue);
			dev.queue_ptrs.push_back(reinterpret_cast<VkQueue>(&dev.queues.back()));
		}
	}

	for (unsigned i = 0; i < pdevice->memoryProperties.memoryTypeCount; i++)
	{
		dev.memoryTypeBits |= 1 << i;
	}

	for (unsigned i = 0; i < pCreateInfo->enabledLayerCount; i++)
	{
		XLOG("requested device layer: %s", pCreateInfo->ppEnabledLayerNames[i]);
	}

	dev.enabledExtensions.reserve(pCreateInfo->enabledExtensionCount);
	for (unsigned i = 0; i < pCreateInfo->enabledExtensionCount; i++)
	{
		XLOG("requested device extension: %s", pCreateInfo->ppEnabledExtensionNames[i]);
		dev.enabledExtensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
	}
	XLOG("created device %p", *pDevice);

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDevice);
	CLOG("device=%p, pAllocator=%p", device, pAllocator);

	(void)pAllocator; // ignored
	cVkDevice* dev = device_cast(device);
	if (dev) dev->destroyed = true;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
	ENTRY(vkEnumerateInstanceExtensionProperties);
	maybe_enable_logging();

	CLOG("pLayerName=%s, pPropertyCount=%u, pProperties=%p", pLayerName, *pPropertyCount, pProperties);

	assert(pPropertyCount);

	uint32_t count = supported_instance_extensions.size();
	if (pProperties)
	{
		count = std::min<unsigned>(*pPropertyCount, count);
		for (unsigned i = 0; i < count; i++)
		{
			strcpy(pProperties[i].extensionName, supported_instance_extensions.at(i).extensionName);
			pProperties[i].specVersion = supported_instance_extensions.at(i).specVersion;
			XLOG("%u : returning %s : %u", i, pProperties[i].extensionName, pProperties[i].specVersion);
		}
	}

	*pPropertyCount = count;
	XLOG("setting pPropertyCount to %u", *pPropertyCount);

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
	ENTRY(vkEnumerateDeviceExtensionProperties);
	CLOG("physicalDevice=%p, pLayerName=%s, pPropertyCount=%u, pProperties=%p", physicalDevice, pLayerName, *pPropertyCount, pProperties);

	assert(pPropertyCount);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	uint32_t count = dev->extensions.size();
	if (pProperties)
	{
		count = std::min(*pPropertyCount, count);
		auto it = dev->extensions.begin();
		for (uint32_t i = 0; i < count; ++i, ++it)
		{
			strcpy(pProperties[i].extensionName, it->first.c_str());
			pProperties[i].specVersion = it->second;
			XLOG("%u : returning %s : %u", i, pProperties[i].extensionName, pProperties[i].specVersion);
		}
	}

	*pPropertyCount = count;
	XLOG("setting pPropertyCount to %u", *pPropertyCount);

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
	ENTRY(vkEnumerateInstanceLayerProperties);
	maybe_enable_logging();

	CLOG("pPropertyCount=%u, pProperties=%p", *pPropertyCount, pProperties);

	*pPropertyCount = 0;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
	ENTRY(vkEnumerateDeviceLayerProperties);
	CLOG("physicalDevice=%p, pPropertyCount=%u, pProperties=%p", physicalDevice, *pPropertyCount, pProperties);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	*pPropertyCount = 0;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice                                    device,
    uint32_t                                    queueFamilyIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue)
{
	ENTRY(vkGetDeviceQueue);
	CLOG("device=%p, queueFamilyIndex=%u, queueIndex=%u, pQueue=%p", device, queueFamilyIndex, queueIndex, pQueue);

	cVkDevice* dev = device_cast(device);
	assert(queueIndex < dev->queues.size());
	*pQueue = dev->queue_ptrs.at(queueIndex);
}

static void update_stageflag_usage(cVkQueue* q, uint64_t flags)
{
#ifndef FAST
	for (int stage = 0; stage < 32; stage++) // test all possible bits
	{
		const unsigned bit = 1 << stage;
		if (flags & bit) q->stageflag_usage[stage]++;
	}
#endif
}

static void internalQueueSubmit(cVkQueue* q, VkCommandBuffer cmdbuffer)
{
	cVkCommandBuffer* cmdbuf = commandbuffer_cast(cmdbuffer);

#ifndef FAST
	cmdbuf->lifetime_calls++;
	if (frames_of_interest.count(cmdbuf->current_frame) > 0)
	{
		cmdbuf->frames_of_interest_calls++;
	}

	update_stageflag_usage(q, cmdbuf->maxStageFlags);
#endif

	// Execute commands immediately
	cVkCmdState cmdstate;
	for (const cVkCommand& cmd : cmdbuf->commands)
	{
		execute_command_buffer_command(cmd, cmdstate, true);
	}

	// Store references to commandbuffers until results presented
	q->commandbuffers.push_back(cmdbuf);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence)
{
	ENTRY(vkQueueSubmit);
	CLOG("queue=%p, submitCount=%u, pSubmits=%p, fence=" NHANDLE, queue, submitCount, pSubmits, fence);
	cVkQueue* q = queue_cast(queue);

	for (unsigned i = 0; i < submitCount; i++)
	{
		VkTimelineSemaphoreSubmitInfo* timeline_semaphore = (VkTimelineSemaphoreSubmitInfo*)find_extension(&pSubmits[i], VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
		XLOG("%u: %u wait semaphores, %u commandbuffers, %u signal semaphores (timeline=%s)", i, pSubmits[i].waitSemaphoreCount, pSubmits[i].commandBufferCount,
		     pSubmits[i].signalSemaphoreCount, timeline_semaphore ? "yes" : "no");

		if (timeline_semaphore)
		{
			assert(timeline_semaphore->signalSemaphoreValueCount == pSubmits[i].signalSemaphoreCount);
			assert(timeline_semaphore->waitSemaphoreValueCount == pSubmits[i].waitSemaphoreCount);
		}

		// Wait execution start
		for (unsigned j = 0; j < pSubmits[i].waitSemaphoreCount; j++)
		{
			cVkSemaphore* c = semaphore_cast(pSubmits[i].pWaitSemaphores[j]);
			// no need to wait - everything here is immediate!

			update_stageflag_usage(q, pSubmits[i].pWaitDstStageMask[j]);
		}

		for (unsigned j = 0; j < pSubmits[i].commandBufferCount; j++)
		{
			internalQueueSubmit(q, pSubmits[i].pCommandBuffers[j]);
		}

		// Signal execution complete
		for (unsigned j = 0; j < pSubmits[i].signalSemaphoreCount; j++)
		{
			cVkSemaphore* c = semaphore_cast(pSubmits[i].pSignalSemaphores[j]);
			if (timeline_semaphore)
			{
				assert(c->type == VK_SEMAPHORE_TYPE_TIMELINE);
				c->value = timeline_semaphore->pSignalSemaphoreValues[j];
			}
			else
			{
				assert(c->type == VK_SEMAPHORE_TYPE_BINARY);
				c->value = 1;
			}
		}
	}

	// If user sent in a fence, lift it to signal that we're done executing the queue now
	if (fence != VK_NULL_HANDLE)
	{
		fence_cast(fence)->signalled = true;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(
    VkQueue                                     queue)
{
	ENTRY(vkQueueWaitIdle);
	CLOG("queue=%p", queue);
	cVkQueue* q = queue_cast(queue);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(
    VkDevice                                    device)
{
	ENTRY(vkDeviceWaitIdle);
	CLOG("device=%p", device);
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory)
{
	ENTRY(vkAllocateMemory);
	CLOG("device=%p, pAllocateInfo=%p, pAllocator=%p, pMemory=%p", device, pAllocateInfo, pAllocator, pMemory);

	(void)pAllocator; // ignored
	cVkDevice* dev = device_cast(device);
	assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
	cVkDeviceMemory memory;
	memory.allocationSize = pAllocateInfo->allocationSize;
	memory.memoryTypeIndex = pAllocateInfo->memoryTypeIndex;
	posix_memalign((void**)&memory.ptr, sizeof(void*), memory.allocationSize);
	dev->deviceMemory.push_back(memory);
#ifndef FAST
	dev->memory_allocated[memory.memoryTypeIndex] += memory.allocationSize;
	// did we reach a new allocation record for this memory type?
	dev->memory_highest[memory.memoryTypeIndex] = std::max(dev->memory_highest[memory.memoryTypeIndex], dev->memory_allocated[memory.memoryTypeIndex]);
#endif
	VkMemoryDedicatedAllocateInfo* mda = (VkMemoryDedicatedAllocateInfo*)find_extension(pAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
	(void)mda; // ignored for now
	*pMemory = reinterpret_cast<VkDeviceMemory>(&dev->deviceMemory.back());
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkFreeMemory);
	CLOG("device=%p, memory=" NHANDLE ", pAllocator=%p", device, memory, pAllocator);

	cVkDevice* dev = device_cast(device);
	auto* mem = destroy<cVkDeviceMemory, VkDeviceMemory>(memory, pAllocator);
	if (mem && !store_allocations)
	{
		free(mem->ptr);
		mem->ptr = nullptr;
#ifndef FAST
		dev->memory_allocated[mem->memoryTypeIndex] -= mem->allocationSize;
		assert(dev->memory_allocated[mem->memoryTypeIndex] >= 0);
#endif
	}
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData)
{
	ENTRY(vkMapMemory);
	CLOG("device=%p, memory=" NHANDLE ", offset=%llu, size=%llu, flags=%u, ppData=%p", device, memory,
	     (unsigned long long)offset, (unsigned long long)size, flags, ppData);

	cVkDevice* dev = device_cast(device);
	cVkDeviceMemory* mem = devicememory_cast(memory);
	*ppData = mem->ptr + offset;
	mem->mapped = true;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory)
{
	ENTRY(vkUnmapMemory);
	CLOG("device=%p, memory=" NHANDLE, device, memory);

	cVkDevice* dev = device_cast(device);
	cVkDeviceMemory* mem = devicememory_cast(memory);
	mem->mapped = false;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
	ENTRY(vkFlushMappedMemoryRanges);
	CLOG("device=%p, memoryRangeCount=%u, pMemoryRanges=%p", device, memoryRangeCount, pMemoryRanges);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < memoryRangeCount; i++)
	{
		cVkDeviceMemory* mem = devicememory_cast(pMemoryRanges[i].memory);
	}
	// pass through
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice                                    device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
	ENTRY(vkInvalidateMappedMemoryRanges);
	CLOG("device=%p, memoryRangeCount=%u, pMemoryRanges=%p", device, memoryRangeCount, pMemoryRanges);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < memoryRangeCount; i++)
	{
		cVkDeviceMemory* mem = devicememory_cast(pMemoryRanges[i].memory);
	}
	// pass through
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize*                               pCommittedMemoryInBytes)
{
	ENTRY(vkGetDeviceMemoryCommitment);
	CLOG("device=%p, memory=" NHANDLE ", pCommittedMemoryInBytes=%p", device, memory, pCommittedMemoryInBytes);

	cVkDevice* dev = device_cast(device);
	cVkDeviceMemory* mem = devicememory_cast(memory);
	*pCommittedMemoryInBytes = mem->allocationSize;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
	ENTRY(vkBindBufferMemory);
	CLOG("device=%p, buffer=" NHANDLE ", memory=" NHANDLE ", memoryOffset=%llu", device, buffer, memory, (unsigned long long)memoryOffset);

	cVkDevice* dev = device_cast(device);
	cVkBuffer* p = buffer_cast(buffer);
	p->memory = devicememory_cast(memory);
	p->memoryOffset = memoryOffset;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
	ENTRY(vkBindImageMemory);
	CLOG("device=%p, image=" NHANDLE ", memory=" NHANDLE ", memoryOffset=%llu", device, image, memory, (unsigned long long)memoryOffset);

	cVkDevice* dev = device_cast(device);
	cVkImage* p = image_cast(image);
	p->memory = devicememory_cast(memory);
	p->memoryOffset = memoryOffset;
	return VK_SUCCESS;
}

static void internalGetBufferMemoryRequirements(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements,
    void*                                       pExtension)
{
	cVkDevice* dev = device_cast(device);
	cVkBuffer* b = buffer_cast(buffer);
	pMemoryRequirements->size = b->size;
	pMemoryRequirements->memoryTypeBits = dev->memoryTypeBits; // supports every memory type for now
	pMemoryRequirements->alignment = sizeof(void*);
	if (pExtension)
	{
		VkMemoryDedicatedRequirements* mdr = (VkMemoryDedicatedRequirements*)find_extension(pExtension, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
		if (mdr)
		{
			mdr->prefersDedicatedAllocation = VK_FALSE;
			mdr->requiresDedicatedAllocation = VK_FALSE;
		}
	}
	XLOG("reporting size=%llu, alignment=%llu, memoryTypeBits=%u", (unsigned long long)pMemoryRequirements->size,
	     (unsigned long long)pMemoryRequirements->alignment, pMemoryRequirements->memoryTypeBits);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements)
{
	ENTRY(vkGetBufferMemoryRequirements);
	CLOG("device=%p, buffer=" NHANDLE ", pMemoryRequirements=%p", device, buffer, pMemoryRequirements);
	internalGetBufferMemoryRequirements(device, buffer, pMemoryRequirements, nullptr);
}

static void internalGetImageMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements,
    void*                                       pExtension)
{
	cVkDevice* dev = device_cast(device);
	cVkImage* cimage = image_cast(image);
	pMemoryRequirements->memoryTypeBits = dev->memoryTypeBits; // supports everything
	pMemoryRequirements->alignment = sizeof(void*);
	pMemoryRequirements->size = cimage->size;
	if (pExtension)
	{
		VkMemoryDedicatedRequirements* mdr = (VkMemoryDedicatedRequirements*)find_extension(pExtension, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
		if (mdr)
		{
			mdr->prefersDedicatedAllocation = VK_FALSE;
			mdr->requiresDedicatedAllocation = VK_FALSE;
		}
	}
	XLOG("reporting size=%llu, alignment=%llu, memoryTypeBits=%u", (unsigned long long)pMemoryRequirements->size,
	     (unsigned long long)pMemoryRequirements->alignment, pMemoryRequirements->memoryTypeBits);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements)
{
	ENTRY(vkGetImageMemoryRequirements);
	CLOG("device=%p, image=" NHANDLE ", pMemoryRequirements=%p", device, image, pMemoryRequirements);
	internalGetImageMemoryRequirements(device, image, pMemoryRequirements, nullptr);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
	ENTRY(vkGetImageSparseMemoryRequirements);
	CLOG("device=%p, image= " NHANDLE ", pSparseMemoryRequirementCount=%u, pSparseMemoryRequirements=%p", device, image, *pSparseMemoryRequirementCount, pSparseMemoryRequirements);

	cVkDevice* dev = device_cast(device);
	if (pSparseMemoryRequirements == VK_NULL_HANDLE)
	{
		*pSparseMemoryRequirementCount = 0;
		XLOG("reporting sparse memory requirements count %u", *pSparseMemoryRequirementCount);
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkSampleCountFlagBits                       samples,
    VkImageUsageFlags                           usage,
    VkImageTiling                               tiling,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties*              pProperties)
{
	ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties);
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	*pPropertyCount = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueBindSparse(
    VkQueue                                     queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence)
{
	ENTRY(vkQueueBindSparse);
	TBD_UNSUPPORTED;
	cVkQueue* q = queue_cast(queue);
	cVkFence* cfence = fence_cast(fence);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice                                    device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
	ENTRY(vkCreateFence);
	CLOG("device=%p, pCreateInfo=%p[flags=%u], pAllocator=%p, pFence=%p", device, pCreateInfo, pCreateInfo->flags, pAllocator, pFence);

	cVkDevice* dev = device_cast(device);
	cVkFence& p = owner_create<cVkFence, VkFence>(dev->fences, pFence, pAllocator);
	p.flags = pCreateInfo->flags;
	if (p.flags & VK_FENCE_CREATE_SIGNALED_BIT)
	{
		p.signalled = true;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice                                    device,
    VkFence                                     fence,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyFence);
	CLOG("device=%p, fence=" NHANDLE ", pAllocator=%p", device, fence, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkFence, VkFence>(fence, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences)
{
	ENTRY(vkResetFences);
	CLOG("device=%p, fenceCount=%u, pFences=%p", device, fenceCount, pFences);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < fenceCount; i++)
	{
		cVkFence* fence = fence_cast(pFences[i]);
		fence->signalled = false;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(
    VkDevice                                    device,
    VkFence                                     fence)
{
	ENTRY(vkGetFenceStatus);
	CLOG("device=%p, fence=" NHANDLE, device, fence);

	cVkDevice* dev = device_cast(device);
	cVkFence* f = fence_cast(fence);

	if (!f->signalled)
	{
		return VK_NOT_READY;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice                                    device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    timeout)
{
	ENTRY(vkWaitForFences);
	CLOG("device=%p, fenceCount=%u, pFences=%p, waitAll=%s, timeout=%llu", device, fenceCount, pFences, bool2str(waitAll), (unsigned long long)timeout);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < fenceCount; i++)
	{
		cVkFence* fence = fence_cast(pFences[i]);

		while (!fence->signalled && timeout)
		{
			timeout--;
		}
		if (!fence->signalled)
		{
			return VK_TIMEOUT;
		}

		if (!waitAll)
		{
			return VK_SUCCESS;
		}
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore)
{
	ENTRY(vkCreateSemaphore);
	CLOG("device=%p, pCreateInfo=%p[flags=%u], pAllocator=%p, pSemaphore=%p", device, pCreateInfo, pCreateInfo->flags, pAllocator, pSemaphore);

	cVkDevice* dev = device_cast(device);
	cVkSemaphore& p = owner_create<cVkSemaphore, VkSemaphore>(dev->semaphores, pSemaphore, pAllocator);
	p.flags = pCreateInfo->flags;

	VkSemaphoreTypeCreateInfo* scsti = (VkSemaphoreTypeCreateInfo*)find_extension(pCreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
	if (scsti && scsti->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE)
	{
		p.value = scsti->initialValue;
		p.type = scsti->semaphoreType;
	}
	else
	{
		p.type = VK_SEMAPHORE_TYPE_BINARY;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroySemaphore);
	CLOG("device=%p, semaphore=" NHANDLE ", pAllocator=%p", device, semaphore, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkSemaphore, VkSemaphore>(semaphore, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(
    VkDevice                                    device,
    const VkEventCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkEvent*                                    pEvent)
{
	ENTRY(vkCreateEvent);
	CLOG("device=%p, pCreateInfo=%p[flags=%u], pAllocator=%p, pEvent=%p", device, pCreateInfo, pCreateInfo->flags, pAllocator, pEvent);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);
	cVkDevice* dev = device_cast(device);
	cVkEvent& p = owner_create<cVkEvent, VkEvent>(dev->events, pEvent, pAllocator);
	p.flags = pCreateInfo->flags;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice                                    device,
    VkEvent                                     event,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyEvent);
	CLOG("device=%p, event=" NHANDLE ", pAllocator=%p", device, event, pAllocator);

	cVkDevice* dev = device_cast(device);
	cVkEvent* evt = event_cast(event);
	destroy<cVkEvent, VkEvent>(event, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(
    VkDevice                                    device,
    VkEvent                                     event)
{
	ENTRY(vkGetEventStatus);
	CLOG("device=%p, event=" NHANDLE, device, event);

	cVkDevice* dev = device_cast(device);
	cVkEvent* evt = event_cast(event);
	return VK_EVENT_SET; // or VK_EVENT_RESET
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(
    VkDevice                                    device,
    VkEvent                                     event)
{
	ENTRY(vkSetEvent);
	CLOG("device=%p, event=" NHANDLE, device, event);

	cVkDevice* dev = device_cast(device);
	cVkEvent* evt = event_cast(event);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(
    VkDevice                                    device,
    VkEvent                                     event)
{
	ENTRY(vkResetEvent);
	CLOG("device=%p, event=" NHANDLE, device, event);

	cVkDevice* dev = device_cast(device);
	cVkEvent* evt = event_cast(event);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice                                    device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool)
{
	ENTRY(vkCreateQueryPool);
	CLOG("device=%p, pCreateInfo=%p[flags=%u, queryType=%u, queryCount=%u], pAllocator=%p, pQueryPool=%p",
	     device, pCreateInfo, pCreateInfo->flags, pCreateInfo->queryType, pCreateInfo->queryCount, pAllocator, pQueryPool);

	cVkDevice* dev = device_cast(device);
	cVkQueryPool& p = owner_create<cVkQueryPool, VkQueryPool>(dev->queryPools, pQueryPool, pAllocator);
	p.flags = pCreateInfo->flags;
	p.queryType = pCreateInfo->queryType;
	p.queryCount = pCreateInfo->queryCount;
	if (pCreateInfo->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS)
	{
		p.pipelineStatistics = pCreateInfo->pipelineStatistics;
		std::bitset<11> flagbits(p.pipelineStatistics);
		p.stride = flagbits.count(); // stride in 64bit chunks
		p.data.resize(pCreateInfo->queryCount * p.stride);
	}
	else
	{
		p.data.resize(pCreateInfo->queryCount);
	}
	p.availability.resize(pCreateInfo->queryCount);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyQueryPool);
	CLOG("device=%p, queryPool=" NHANDLE ", pAllocator=%p", device, queryPool, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkQueryPool, VkQueryPool>(queryPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride, // for pipeline stats, user needs to keep in mind how many bits were set
    VkQueryResultFlags                          flags)
{
	ENTRY(vkGetQueryPoolResults);
	CLOG("device=%p, queryPool=" NHANDLE ", firstQuery=%u, queryCount=%u, dataSize=%zu, pData=%p, stride=%llu, flags=%u",
	     device, queryPool, firstQuery, queryCount, dataSize, pData, (unsigned long long)stride, flags);

	cVkDevice* dev = device_cast(device);
	cVkQueryPool* pool = querypool_cast(queryPool);
	write_queries(pool, firstQuery, queryCount, dataSize, stride, pData, flags);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer)
{
	ENTRY(vkCreateBuffer);
	CLOG("device=%p, pCreateInfo=%p[flags=%u, size=%llu, usage=%u, sharingMode=%u], pAllocator=%p, pBuffer=%p",
	     device, pCreateInfo, pCreateInfo->flags, (unsigned long long)pCreateInfo->size, pCreateInfo->usage,
	     pCreateInfo->sharingMode, pAllocator, pBuffer);

	cVkDevice* dev = device_cast(device);
	cVkBuffer& p = owner_create<cVkBuffer, VkBuffer>(dev->buffers, pBuffer, pAllocator);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
	p.flags = pCreateInfo->flags;
	p.size = pCreateInfo->size;
	p.usage = pCreateInfo->usage;
	p.sharingMode = pCreateInfo->sharingMode;
	if (p.sharingMode == VK_SHARING_MODE_CONCURRENT)
	{
		p.queueFamilyIndices.resize(pCreateInfo->queueFamilyIndexCount);
		for (unsigned i = 0; i < pCreateInfo->queueFamilyIndexCount; i++)
		{
			p.queueFamilyIndices[i] = pCreateInfo->pQueueFamilyIndices[i];
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyBuffer);
	CLOG("device=%p, buffer=" NHANDLE ", pAllocator=%p", device, buffer, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkBuffer, VkBuffer>(buffer, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice                                    device,
    const VkBufferViewCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBufferView*                               pView)
{
	ENTRY(vkCreateBufferView);
	CLOG("device=%p, pCreateInfo=%p[flags=%u, buffer=" NHANDLE ", format=%u, offset=%llu, range=%llu], pAllocator=%p, pView=%p",
	     device, pCreateInfo, pCreateInfo->flags, pCreateInfo->buffer, pCreateInfo->format, (unsigned long long)pCreateInfo->offset,
	     (unsigned long long)pCreateInfo->range, pAllocator, pView);

	cVkDevice* dev = device_cast(device);
	cVkBufferView& p = owner_create<cVkBufferView, VkBufferView>(dev->bufferViews, pView, pAllocator);
	p.flags = pCreateInfo->flags;
	p.format = pCreateInfo->format;
	p.offset = pCreateInfo->offset;
	p.range = pCreateInfo->range;
	p.buffer = buffer_cast(pCreateInfo->buffer);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(
    VkDevice                                    device,
    VkBufferView                                bufferView,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyBufferView);
	CLOG("device=%p, bufferView=" NHANDLE ", pAllocator=%p", device, bufferView, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkBufferView, VkBufferView>(bufferView, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage)
{
	ENTRY(vkCreateImage);
	CLOG("device=%p, pCreateInfo=%p[flags=%u, imageType=%u, format=%u, extent=%u,%u,%u, mipLevels=%u, arrayLayers=%u, samples=%u, tiling=%u, usage=%u], pAllocator=%p, pImage=%p",
	     device, pCreateInfo, pCreateInfo->flags, pCreateInfo->imageType, pCreateInfo->format, pCreateInfo->extent.width, pCreateInfo->extent.height, pCreateInfo->extent.depth,
	     pCreateInfo->mipLevels, pCreateInfo->arrayLayers, pCreateInfo->samples, pCreateInfo->tiling, pCreateInfo->usage, pAllocator, pImage);

	cVkDevice* dev = device_cast(device);
	cVkImage& p = owner_create<cVkImage, VkImage>(dev->images, pImage, pAllocator);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
	p.flags = pCreateInfo->flags;
	p.imageType = pCreateInfo->imageType;
	p.format = pCreateInfo->format;
	p.extent = pCreateInfo->extent;
	p.mipLevels = pCreateInfo->mipLevels;
	p.arrayLayers = pCreateInfo->arrayLayers;
	p.samples = pCreateInfo->samples;
	p.tiling = pCreateInfo->tiling;
	p.usage = pCreateInfo->usage;
	p.sharingMode = pCreateInfo->sharingMode;
	p.initialLayout = pCreateInfo->initialLayout;
	p.size = (VkDeviceSize)p.extent.width * p.extent.height * p.arrayLayers * p.samples * 4u;
	if (p.sharingMode == VK_SHARING_MODE_CONCURRENT)
	{
		p.queueFamilyIndices.resize(pCreateInfo->queueFamilyIndexCount);
		for (unsigned i = 0; i < pCreateInfo->queueFamilyIndexCount; i++)
		{
			p.queueFamilyIndices[i] = pCreateInfo->pQueueFamilyIndices[i];
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyImage);
	CLOG("device=%p, image=" NHANDLE ", pAllocator=%p", device, image, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkImage, VkImage>(image, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource*                   pSubresource,
    VkSubresourceLayout*                        pLayout)
{
	ENTRY(vkGetImageSubresourceLayout);
	CLOG("device=%p, image=" NHANDLE ", pSubresource=%p, pLayout=%p", device, image, pSubresource, pLayout);

	cVkDevice* dev = device_cast(device);
	cVkImage* img = image_cast(image);

	pLayout->offset = img->memoryOffset;
	if (img->memory)
	{
		pLayout->size = img->memory->allocationSize;
	}
	else
	{
		// 4 * 4 = 32-bit/sample with RGBA channels
		pLayout->size = img->extent.width * img->extent.height * img->extent.depth * 4 * 4;
	}
	pLayout->rowPitch = 1;
	pLayout->arrayPitch = 1;
	pLayout->depthPitch = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView)
{
	ENTRY(vkCreateImageView);
	CLOG("device=%p, pCreateInfo=%p[flags=%u, image=" NHANDLE ", viewType=%u, format=%u], pAllocator=%p, pView=%p",
	     device, pCreateInfo, pCreateInfo->flags, pCreateInfo->image, pCreateInfo->viewType, pCreateInfo->format, pAllocator, pView);

	cVkDevice* dev = device_cast(device);
	cVkImageView& p = owner_create<cVkImageView, VkImageView>(dev->imageViews, pView, pAllocator);
	p.flags = pCreateInfo->flags;
	p.image = image_cast(pCreateInfo->image);
	p.viewType = pCreateInfo->viewType;
	p.format = pCreateInfo->format;
	p.components = pCreateInfo->components;
	p.subresourceRange = pCreateInfo->subresourceRange;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyImageView);
	CLOG("device=%p, imageView=" NHANDLE ", pAllocator=%p", device, imageView, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkImageView, VkImageView>(imageView, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule)
{
	ENTRY(vkCreateShaderModule);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pShaderModule=%p", device, pCreateInfo, pAllocator, pShaderModule);

	cVkDevice* dev = device_cast(device);
	cVkShaderModule& p = owner_create<cVkShaderModule, VkShaderModule>(dev->shaderModules, pShaderModule, pAllocator);
	p.flags = pCreateInfo->flags;
	p.code.resize(pCreateInfo->codeSize);
	memcpy(p.code.data(), pCreateInfo->pCode, pCreateInfo->codeSize);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyShaderModule);
	CLOG("device=%p, shaderModule=" NHANDLE ", pAllocator=%p", device, shaderModule, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkShaderModule, VkShaderModule>(shaderModule, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice                                    device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache)
{
	ENTRY(vkCreatePipelineCache);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pPipelineCache=%p", device, pCreateInfo, pAllocator, pPipelineCache);

	cVkDevice* dev = device_cast(device);
	cVkPipelineCache& p = owner_create<cVkPipelineCache, VkPipelineCache>(dev->pipelineCaches, pPipelineCache, pAllocator);
	p.flags = pCreateInfo->flags;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyPipelineCache);
	CLOG("device=%p, pipelineCache=" NHANDLE ", pAllocator=%p", device, pipelineCache, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkPipelineCache, VkPipelineCache>(pipelineCache, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    size_t*                                     pDataSize,
    void*                                       pData)
{
	ENTRY(vkGetPipelineCacheData);
	CLOG("device=%p, pipelineCache=" NHANDLE ", pDataSize=%zu, pData=%p", device, pipelineCache, *pDataSize, pData);

	cVkDevice* dev = device_cast(device);
	cVkPipelineCache* cache = pipelinecache_cast(pipelineCache);
	// create fake shader cache, which we can later safely ignore, but lets us pass our integration tests
	if (!pData) *pDataSize = 256;
	else memset(pData, 77, *pDataSize);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(
    VkDevice                                    device,
    VkPipelineCache                             dstCache,
    uint32_t                                    srcCacheCount,
    const VkPipelineCache*                      pSrcCaches)
{
	ENTRY(vkMergePipelineCaches);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	cVkPipelineCache* dst = pipelinecache_cast(dstCache);
	for (uint32_t i = 0; i < srcCacheCount; i++)
	{
		cVkPipelineCache* src = pipelinecache_cast(pSrcCaches[i]);
	}
	return VK_SUCCESS;
}

static cVkShaderModule* get_pipeline_stage_module(cVkDevice* dev, const VkPipelineShaderStageCreateInfo& stageInfo, bool& ownsModule)
{
	ownsModule = false;

	cVkShaderModule* module = shadermodule_cast(stageInfo.module);
	if (module)
		return module;

	const VkShaderModuleCreateInfo* moduleCreateInfo = reinterpret_cast<const VkShaderModuleCreateInfo*>(
		find_extension(stageInfo.pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO));
	if (moduleCreateInfo)
	{
		VkShaderModule tempHandle = VK_NULL_HANDLE;
		cVkShaderModule& created = owner_create<cVkShaderModule, VkShaderModule>(dev->shaderModules, &tempHandle, nullptr);
		created.flags = moduleCreateInfo->flags;
		created.code.resize(moduleCreateInfo->codeSize);
		memcpy(created.code.data(), moduleCreateInfo->pCode, moduleCreateInfo->codeSize);
		ownsModule = true;
		return &created;
	}

	return nullptr;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
	ENTRY(vkCreateGraphicsPipelines);
	CLOG("device=%p, pipelineCache=" NHANDLE ", createInfoCount=%u, pCreateInfos=%p, pAllocator=%p, pPipelines=%p",
	     device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

	cVkDevice* dev = device_cast(device);
	cVkPipelineCache* cache = pipelinecache_cast(pipelineCache);
	for (unsigned i = 0; i < createInfoCount; i++)
	{
		cVkPipeline& p = owner_create<cVkPipeline, VkPipeline>(dev->pipelines, &pPipelines[i], pAllocator);
		if (cache)
		{
			cache->pipelines[&dev->pipelines.back()] = true;
			p.cache = cache;
		}
		p.flags = pCreateInfos[i].flags;
		p.stages.resize(pCreateInfos[i].stageCount);
		for (unsigned j = 0; j < pCreateInfos[i].stageCount; j++)
		{
			p.stages[j].flags = pCreateInfos[i].pStages[j].flags;
			p.stages[j].stage = pCreateInfos[i].pStages[j].stage;
			assert(pCreateInfos[i].pStages[j].stage != VK_SHADER_STAGE_ALL_GRAPHICS);
			assert(pCreateInfos[i].pStages[j].stage != VK_SHADER_STAGE_ALL);
			bool ownsModule = false;
			p.stages[j].module = get_pipeline_stage_module(dev, pCreateInfos[i].pStages[j], ownsModule);
			p.stages[j].ownsModule = ownsModule;
			if (p.stages[j].module)
			{
				p.stages[j].module->type = pCreateInfos[i].pStages[j].stage;
				p.stages[j].module->pipelines.push_back(p.uid);
			}
			if (pCreateInfos[i].pStages[j].pName)
			{
				p.stages[j].name = pCreateInfos[i].pStages[j].pName;
			}
			if (pCreateInfos[i].pStages[j].pSpecializationInfo)
			{
				if (pCreateInfos[i].pStages[j].pSpecializationInfo->pData)
				{
					p.stages[j].specializationData.resize(pCreateInfos[i].pStages[j].pSpecializationInfo->dataSize);
					memcpy(p.stages[j].specializationData.data(), pCreateInfos[i].pStages[j].pSpecializationInfo->pData,
					       pCreateInfos[i].pStages[j].pSpecializationInfo->dataSize);
				}
				for (unsigned k = 0; k < pCreateInfos[i].pStages[j].pSpecializationInfo->mapEntryCount; k++)
				{
					p.stages[j].specializationMap.push_back(pCreateInfos[i].pStages[j].pSpecializationInfo->pMapEntries[k]);
				}
			}
		}
		if (pCreateInfos[i].pVertexInputState)
		{
			p.vertexInputState.enabled = true;
			for (unsigned k = 0; k < pCreateInfos[i].pVertexInputState->vertexBindingDescriptionCount; k++)
			{
				p.vertexInputState.vertexBindingDescriptions.push_back(pCreateInfos[i].pVertexInputState->pVertexBindingDescriptions[k]);
			}
			for (unsigned k = 0; k < pCreateInfos[i].pVertexInputState->vertexAttributeDescriptionCount; k++)
			{
				p.vertexInputState.vertexAttributeDescriptions.push_back(pCreateInfos[i].pVertexInputState->pVertexAttributeDescriptions[k]);
			}
			p.vertexInputState.flags = pCreateInfos[i].pVertexInputState->flags;
		}
		if (pCreateInfos[i].pInputAssemblyState)
		{
			p.vertexInputAssemblyState.enabled = true;
			p.vertexInputAssemblyState.flags = pCreateInfos[i].pInputAssemblyState->flags;
			p.vertexInputAssemblyState.topology = pCreateInfos[i].pInputAssemblyState->topology;
			p.vertexInputAssemblyState.primitiveRestartEnable = pCreateInfos[i].pInputAssemblyState->primitiveRestartEnable;
		}
		if (pCreateInfos[i].pTessellationState)
		{
			p.tessellationState.enabled = true;
			p.tessellationState.flags = pCreateInfos[i].pTessellationState->flags;
			p.tessellationState.patchControlPoints = pCreateInfos[i].pTessellationState->patchControlPoints;
		}
		if (pCreateInfos[i].pViewportState)
		{
			p.viewportState.enabled = true;
			p.viewportState.flags = pCreateInfos[i].pViewportState->flags;
			for (unsigned k = 0; k < pCreateInfos[i].pViewportState->viewportCount && pCreateInfos[i].pViewportState->pViewports; k++)
			{
				p.viewportState.viewports.push_back(pCreateInfos[i].pViewportState->pViewports[k]);
			}
			for (unsigned k = 0; k < pCreateInfos[i].pViewportState->scissorCount && pCreateInfos[i].pViewportState->pScissors; k++)
			{
				p.viewportState.scissors.push_back(pCreateInfos[i].pViewportState->pScissors[k]);
			}
		}
		if (pCreateInfos[i].pRasterizationState)
		{
			p.rasterizationState.enabled = true;
			p.rasterizationState.flags = pCreateInfos[i].pRasterizationState->flags;
			p.rasterizationState.depthClampEnable = pCreateInfos[i].pRasterizationState->depthClampEnable;
			p.rasterizationState.rasterizerDiscardEnable = pCreateInfos[i].pRasterizationState->rasterizerDiscardEnable;
			p.rasterizationState.polygonMode = pCreateInfos[i].pRasterizationState->polygonMode;
			p.rasterizationState.cullMode = pCreateInfos[i].pRasterizationState->cullMode;
			p.rasterizationState.frontFace = pCreateInfos[i].pRasterizationState->frontFace;
			p.rasterizationState.depthBiasEnable = pCreateInfos[i].pRasterizationState->depthBiasEnable;
			p.rasterizationState.depthBiasConstantFactor = pCreateInfos[i].pRasterizationState->depthBiasConstantFactor;
			p.rasterizationState.depthBiasClamp = pCreateInfos[i].pRasterizationState->depthBiasClamp;
			p.rasterizationState.depthBiasSlopeFactor = pCreateInfos[i].pRasterizationState->depthBiasSlopeFactor;
			p.rasterizationState.lineWidth = pCreateInfos[i].pRasterizationState->lineWidth;
		}
		if (pCreateInfos[i].pMultisampleState)
		{
			p.multisampleState.enabled = true;
			p.multisampleState.flags = pCreateInfos[i].pMultisampleState->flags;
			p.multisampleState.rasterizationSamples = pCreateInfos[i].pMultisampleState->rasterizationSamples;
			p.multisampleState.sampleShadingEnable = pCreateInfos[i].pMultisampleState->sampleShadingEnable;
			p.multisampleState.minSampleShading = pCreateInfos[i].pMultisampleState->minSampleShading;
			p.multisampleState.alphaToCoverageEnable = pCreateInfos[i].pMultisampleState->alphaToCoverageEnable;
			p.multisampleState.alphaToOneEnable = pCreateInfos[i].pMultisampleState->alphaToOneEnable;
		}
		if (pCreateInfos[i].pColorBlendState)
		{
			p.pipelineColorBlendState.enabled = true;
			for (unsigned k = 0; k < pCreateInfos[i].pColorBlendState->attachmentCount; k++)
			{
				p.pipelineColorBlendState.attachments.push_back(pCreateInfos[i].pColorBlendState->pAttachments[k]);
			}
			p.pipelineColorBlendState.flags = pCreateInfos[i].pColorBlendState->flags;
			p.pipelineColorBlendState.logicOpEnable = pCreateInfos[i].pColorBlendState->logicOpEnable;
			p.pipelineColorBlendState.logicOp = pCreateInfos[i].pColorBlendState->logicOp;
			memcpy(p.pipelineColorBlendState.blendConstants, pCreateInfos[i].pColorBlendState->blendConstants, sizeof(pCreateInfos[i].pColorBlendState->blendConstants));
		}
		if (pCreateInfos[i].pDynamicState)
		{
			p.dynamicState.enabled = true;
			p.dynamicState.flags = pCreateInfos[i].pDynamicState->flags;
			for (unsigned k = 0; k < pCreateInfos[i].pDynamicState->dynamicStateCount; k++)
			{
				p.dynamicState.states.push_back(pCreateInfos[i].pDynamicState->pDynamicStates[k]);
			}
		}
		p.layout = pipelinelayout_cast(pCreateInfos[i].layout);
		p.renderPass = renderpass_cast(pCreateInfos[i].renderPass);
		p.subpass = pCreateInfos[i].subpass;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
	ENTRY(vkCreateComputePipelines);
	CLOG("device=%p, pipelineCache=" NHANDLE ", createInfoCount=%u, pCreateInfos=%p, pAllocator=%p, pPipelines=%p",
	     device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

	cVkDevice* dev = device_cast(device);
	cVkPipelineCache* cache = pipelinecache_cast(pipelineCache);
	for (unsigned i = 0; i < createInfoCount; i++)
	{
		cVkPipeline& p = owner_create<cVkPipeline, VkPipeline>(dev->pipelines, &pPipelines[i], pAllocator);
		if (cache)
		{
			cache->pipelines[&dev->pipelines.back()] = true;
			p.cache = cache;
		}
		p.flags = pCreateInfos[i].flags;
		p.stages.resize(1);
		p.stages[0].flags = pCreateInfos[i].stage.flags;
		p.stages[0].stage = pCreateInfos[i].stage.stage;
		assert(pCreateInfos[i].stage.stage == VK_SHADER_STAGE_COMPUTE_BIT);
		bool ownsModule = false;
		p.stages[0].module = get_pipeline_stage_module(dev, pCreateInfos[i].stage, ownsModule);
		p.stages[0].ownsModule = ownsModule;
		if (p.stages[0].module)
		{
			p.stages[0].module->type = pCreateInfos[i].stage.stage;
			p.stages[0].module->pipelines.push_back(p.uid);
		}
		if (pCreateInfos[i].stage.pName)
		{
			p.stages[0].name = pCreateInfos[i].stage.pName;
		}
		if (pCreateInfos[i].stage.pSpecializationInfo)
		{
			if (pCreateInfos[i].stage.pSpecializationInfo->pData)
			{
				p.stages[0].specializationData.resize(pCreateInfos[i].stage.pSpecializationInfo->dataSize);
				memcpy(p.stages[0].specializationData.data(), pCreateInfos[i].stage.pSpecializationInfo->pData,
				       pCreateInfos[i].stage.pSpecializationInfo->dataSize);
			}
			for (unsigned k = 0; k < pCreateInfos[i].stage.pSpecializationInfo->mapEntryCount; k++)
			{
				p.stages[0].specializationMap.push_back(pCreateInfos[i].stage.pSpecializationInfo->pMapEntries[k]);
			}
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyPipeline);
	CLOG("device=%p, pipeline=" NHANDLE ", pAllocator=%p", device, pipeline, pAllocator);

	cVkDevice* dev = device_cast(device);
	cVkPipeline* pipe = pipeline_cast(pipeline);
	if (pipe)
	{
		for (auto& stage : pipe->stages)
		{
			if (stage.ownsModule && stage.module)
			{
				destroy<cVkShaderModule, VkShaderModule>(reinterpret_cast<VkShaderModule>(stage.module), pAllocator);
			}
		}
	}
	if (pipe && pipe->cache)
	{
		pipe->cache->pipelines[pipe] = false;
	}
	destroy<cVkPipeline, VkPipeline>(pipeline, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout)
{
	ENTRY(vkCreatePipelineLayout);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pPipelineLayout=%p", device, pCreateInfo, pAllocator, pPipelineLayout);

	cVkDevice* dev = device_cast(device);
	cVkPipelineLayout& p = owner_create<cVkPipelineLayout, VkPipelineLayout>(dev->pipelineLayouts, pPipelineLayout, pAllocator);
	p.flags = pCreateInfo->flags;
	p.setLayouts.resize(pCreateInfo->setLayoutCount);
	for (unsigned i = 0; i < pCreateInfo->setLayoutCount; i++)
	{
		p.setLayouts[i] = descriptorsetlayout_cast(pCreateInfo->pSetLayouts[i]);
	}
	p.pushConstantRanges.resize(pCreateInfo->pushConstantRangeCount);
	for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; i++)
	{
		p.pushConstantRanges[i] = pCreateInfo->pPushConstantRanges[i];
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyPipelineLayout);
	CLOG("device=%p, pipelineLayout=" NHANDLE ", pAllocator=%p", device, pipelineLayout, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkPipelineLayout, VkPipelineLayout>(pipelineLayout, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler)
{
	ENTRY(vkCreateSampler);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pSampler=%p", device, pCreateInfo, pAllocator, pSampler);

	cVkDevice* dev = device_cast(device);
	cVkSampler& p = owner_create<cVkSampler, VkSampler>(dev->samplers, pSampler, pAllocator);
	p.info = *pCreateInfo;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroySampler);
	CLOG("device=%p, sampler=" NHANDLE ", pAllocator=%p", device, sampler, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkSampler, VkSampler>(sampler, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout)
{
	ENTRY(vkCreateDescriptorSetLayout);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pSetLayout=%p", device, pCreateInfo, pAllocator, pSetLayout);

	cVkDevice* dev = device_cast(device);
	cVkDescriptorSetLayout& p = owner_create<cVkDescriptorSetLayout, VkDescriptorSetLayout>(dev->descriptorSetLayouts, pSetLayout, pAllocator);
	p.flags = pCreateInfo->flags;
	p.bindings.resize(pCreateInfo->bindingCount);
	for (unsigned i = 0; i < pCreateInfo->bindingCount; i++)
	{
		p.bindings[i].binding = pCreateInfo->pBindings[i].binding;
		p.bindings[i].descriptorType = pCreateInfo->pBindings[i].descriptorType;
		p.bindings[i].descriptorCount = pCreateInfo->pBindings[i].descriptorCount;
		p.bindings[i].stageFlags = pCreateInfo->pBindings[i].stageFlags;

		if (pCreateInfo->pBindings[i].pImmutableSamplers
		    && (p.bindings[i].descriptorType & VK_DESCRIPTOR_TYPE_SAMPLER || p.bindings[i].descriptorType & VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER))
		{
			p.bindings[i].immutableSamplers.resize(p.bindings[i].descriptorCount);
			for (unsigned j = 0; j < p.bindings[i].descriptorCount; j++)
			{
				p.bindings[i].immutableSamplers[j] = pCreateInfo->pBindings[i].pImmutableSamplers[j];
			}
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDescriptorSetLayout);
	CLOG("device=%p, descriptorSetLayout=" NHANDLE ", pAllocator=%p", device, descriptorSetLayout, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkDescriptorSetLayout, VkDescriptorSetLayout>(descriptorSetLayout, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool)
{
	ENTRY(vkCreateDescriptorPool);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pDescriptorPool=%p", device, pCreateInfo, pAllocator, pDescriptorPool);

	cVkDevice* dev = device_cast(device);
	cVkDescriptorPool& p = owner_create<cVkDescriptorPool, VkDescriptorPool>(dev->descriptorPools, pDescriptorPool, pAllocator);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
	p.flags = pCreateInfo->flags;
	p.maxSets = pCreateInfo->maxSets;
	p.poolSizeCount = pCreateInfo->poolSizeCount;
	// TBD actually implement a pool here, rather than cheat.
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDescriptorPool);
	CLOG("device=%p, descriptorPool=" NHANDLE ", pAllocator=%p", device, descriptorPool, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkDescriptorPool, VkDescriptorPool>(descriptorPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    VkDescriptorPoolResetFlags                  flags)
{
	ENTRY(vkResetDescriptorPool);
	CLOG("device=%p, descriptorPool=" NHANDLE ", flags=%u", device, descriptorPool, flags);

	cVkDevice* dev = device_cast(device);
	cVkDescriptorPool* pool = descriptorpool_cast(descriptorPool);
	for (auto& set : pool->sets)
	{
		destroy<cVkDescriptorSet, VkDescriptorSet>(reinterpret_cast<VkDescriptorSet>(&set), nullptr);
	}
	pool->sets.clear();
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets)
{
	ENTRY(vkAllocateDescriptorSets);
	CLOG("device=%p, pAllocateInfo=%p, pDescriptorSets=%p", device, pAllocateInfo, pDescriptorSets);

	cVkDevice* dev = device_cast(device);
	cVkDescriptorPool* pool = descriptorpool_cast(pAllocateInfo->descriptorPool);
	assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
	for (unsigned i = 0; i < pAllocateInfo->descriptorSetCount; i++)
	{
		// Allocate a descriptor set from our fake "pool".
		pDescriptorSets[i] = 0;
		cVkDescriptorSetLayout* wanted_layout = descriptorsetlayout_cast(pAllocateInfo->pSetLayouts[i]);
		cVkDescriptorSet& set = owner_create<cVkDescriptorSet, VkDescriptorSet>(pool->sets, &pDescriptorSets[i], nullptr);
		set.layout = wanted_layout;

		// This is not spec specific, we use a copy of the layout to track the current values of the descriptor set
		set.state_bindings = wanted_layout->bindings;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets)
{
	ENTRY(vkFreeDescriptorSets);
	CLOG("device=%p, descriptorPool=" NHANDLE ", descriptorSetCount=%u, pDescriptorSets=%p", device, descriptorPool, descriptorSetCount, pDescriptorSets);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < descriptorSetCount; i++)
	{
		destroy<cVkDescriptorSet, VkDescriptorSet>(pDescriptorSets[i], nullptr);
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies)
{
	ENTRY(vkUpdateDescriptorSets);
	CLOG("device=%p, descriptorWriteCount=%u, pDescriptorWrites=%p, descriptorCopyCount=%u, pDescriptorCopies=%p",
	     device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);

	cVkDevice* dev = device_cast(device);

	for (uint32_t i = 0; i < descriptorWriteCount; ++i)
	{
		cVkDescriptorSet* target_descriptor_set = descriptorset_cast(pDescriptorWrites[i].dstSet);
		target_descriptor_set->handle_write(&pDescriptorWrites[i]);
	}

	for (uint32_t i = 0; i < descriptorCopyCount; ++i)
	{
		cVkDescriptorSet* source_descriptor_set = descriptorset_cast(pDescriptorCopies[i].srcSet);
		cVkDescriptorSet* target_descriptor_set = descriptorset_cast(pDescriptorCopies[i].dstSet);
		for (const cVkDescriptorSetLayoutBinding& layout_binding : source_descriptor_set->layout->bindings) {
			if (layout_binding.binding != pDescriptorCopies[i].srcBinding) continue;

			VkWriteDescriptorSet desc_write{};
			desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			desc_write.dstSet = pDescriptorCopies[i].dstSet;
			desc_write.dstBinding = pDescriptorCopies[i].dstBinding;
			desc_write.dstArrayElement = pDescriptorCopies[i].dstArrayElement;
			desc_write.descriptorCount = pDescriptorCopies[i].descriptorCount;
			desc_write.descriptorType = layout_binding.descriptorType;

			switch (layout_binding.descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				if (layout_binding.pImageInfo.size() >= (pDescriptorCopies[i].srcArrayElement + pDescriptorCopies[i].descriptorCount))
				{
					desc_write.pImageInfo = &(layout_binding.pImageInfo[pDescriptorCopies[i].srcArrayElement]);
				}
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
				if (layout_binding.pBufferInfo.size() >= (pDescriptorCopies[i].srcArrayElement + pDescriptorCopies[i].descriptorCount))
				{
					desc_write.pBufferInfo = &(layout_binding.pBufferInfo[pDescriptorCopies[i].srcArrayElement]);
				}
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				if (layout_binding.pTexelBufferView.size() >= (pDescriptorCopies[i].srcArrayElement + pDescriptorCopies[i].descriptorCount))
				{
					desc_write.pTexelBufferView = &(layout_binding.pTexelBufferView[pDescriptorCopies[i].srcArrayElement]);
				}
				break;
			default:
				break;
			}

			if (!desc_write.pImageInfo && !desc_write.pBufferInfo && !desc_write.pTexelBufferView)
			{
				// Source descriptor is undefined or missing; skip copying to avoid touching garbage.
				break;
			}

			target_descriptor_set->handle_write(&desc_write);
			break;
		}
	}
}


VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice                                    device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer)
{
	ENTRY(vkCreateFramebuffer);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pFramebuffer=%p", device, pCreateInfo, pAllocator, pFramebuffer);

	cVkDevice* dev = device_cast(device);
	cVkFramebuffer& p = owner_create<cVkFramebuffer, VkFramebuffer>(dev->framebuffers, pFramebuffer, pAllocator);
	p.flags = pCreateInfo->flags;
	p.renderPass = renderpass_cast(pCreateInfo->renderPass);
	p.attachments.resize(pCreateInfo->attachmentCount);
	for (unsigned i = 0; i < pCreateInfo->attachmentCount; i++)
	{
		p.attachments[i] = imageview_cast(pCreateInfo->pAttachments[i]);
	}
	p.width = pCreateInfo->width;
	p.height = pCreateInfo->height;
	p.layers = pCreateInfo->layers;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice                                    device,
    VkFramebuffer                               framebuffer,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyFramebuffer);
	CLOG("device=%p, framebuffer=" NHANDLE ", pAllocator=%p", device, framebuffer, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkFramebuffer, VkFramebuffer>(framebuffer, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice                                    device,
    const VkRenderPassCreateInfo*               pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
	ENTRY(vkCreateRenderPass);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pRenderPass=%p", device, pCreateInfo, pAllocator, pRenderPass);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
	cVkDevice* dev = device_cast(device);
	cVkRenderPass& p = owner_create<cVkRenderPass, VkRenderPass>(dev->renderpasses, pRenderPass, pAllocator);
	p.attachments.reserve(pCreateInfo->attachmentCount);
	for (unsigned i = 0; i < pCreateInfo->attachmentCount; i++)
	{
		cVkAttachmentDescription attachment;
		attachment.config.pNext = nullptr;
		attachment.config.sType = VK_STRUCTURE_TYPE_MAX_ENUM; // ie was not VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2 originally
		attachment.config.flags = pCreateInfo->pAttachments[i].flags;
		attachment.config.format = pCreateInfo->pAttachments[i].format;
		attachment.config.samples = pCreateInfo->pAttachments[i].samples;
		attachment.config.loadOp = pCreateInfo->pAttachments[i].loadOp;
		attachment.config.storeOp = pCreateInfo->pAttachments[i].storeOp;
		attachment.config.stencilLoadOp = pCreateInfo->pAttachments[i].stencilLoadOp;
		attachment.config.stencilStoreOp = pCreateInfo->pAttachments[i].stencilStoreOp;
		attachment.config.initialLayout = pCreateInfo->pAttachments[i].initialLayout;
		attachment.config.finalLayout = pCreateInfo->pAttachments[i].finalLayout;
		p.attachments.push_back(attachment);
	}
	p.subpassCount = pCreateInfo->subpassCount;
	p.dependencyCount = pCreateInfo->dependencyCount;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyRenderPass);
	CLOG("device=%p, renderPass=" NHANDLE ", pAllocator=%p", device, renderPass, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkRenderPass, VkRenderPass>(renderPass, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderAreaGranularity(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    VkExtent2D*                                 pGranularity)
{
	ENTRY(vkGetRenderAreaGranularity);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	cVkRenderPass* pass = renderpass_cast(renderPass);
	if (pGranularity)
	{
		pGranularity->width = 1;
		pGranularity->height = 1;
	}
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice                                    device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCommandPool)
{
	ENTRY(vkCreateCommandPool);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pCommandPool=%p", device, pCreateInfo, pAllocator, pCommandPool);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
	cVkDevice* dev = device_cast(device);
	cVkCommandPool& p = owner_create<cVkCommandPool, VkCommandPool>(dev->commandPools, pCommandPool, pAllocator);
	p.queueFamilyIndex = pCreateInfo->queueFamilyIndex;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyCommandPool);
	CLOG("device=%p, commandPool=" NHANDLE ", pAllocator=%p", device, commandPool, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkCommandPool, VkCommandPool>(commandPool, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolResetFlags                     flags)
{
	ENTRY(vkResetCommandPool);
	CLOG("device=%p, commandPool=" NHANDLE ", flags=%u", device, commandPool, flags);

	cVkDevice* dev = device_cast(device);
	if (commandPool != VK_NULL_HANDLE)
	{
		cVkCommandPool* pool = commandpool_cast(commandPool);
		for (auto& cmdbuf : pool->commandBuffers)
		{
			reset_command_buffer(&cmdbuf, flags & VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice                                    device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers)
{
	ENTRY(vkAllocateCommandBuffers);
	CLOG("device=%p, pAllocateInfo=%p, pCommandBuffers=%p", device, pAllocateInfo, pCommandBuffers);

	assert(pAllocateInfo);
	assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
	cVkDevice* dev = device_cast(device);
	cVkCommandPool* pool = commandpool_cast(pAllocateInfo->commandPool);
	for (unsigned i = 0; i < pAllocateInfo->commandBufferCount; i++)
	{
		cVkCommandBuffer buffer;
		buffer.level = pAllocateInfo->level;
		pool->commandBuffers.push_back(buffer);
		pCommandBuffers[i] = reinterpret_cast<VkCommandBuffer>(&pool->commandBuffers.back());
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers)
{
	ENTRY(vkFreeCommandBuffers);
	CLOG("device=%p, commandPool=" NHANDLE ", commandBufferCount=%u, pCommandBuffers=%p", device, commandPool, commandBufferCount, pCommandBuffers);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < commandBufferCount; i++)
	{
		cVkCommandBuffer* buffer = commandbuffer_cast(pCommandBuffers[i]);
		buffer->destroyed = true;
	}
}

VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo)
{
	ENTRY(vkBeginCommandBuffer);
	CLOG("commandBuffer=%p, pBeginInfo=%p[flags=%u, pInheritanceInfo=%p]", commandBuffer, pBeginInfo, pBeginInfo->flags, pBeginInfo->pInheritanceInfo);
	cVkCommandBuffer* buffer = commandbuffer_cast(commandBuffer);
	reset_command_buffer(buffer, true);
	buffer->flags = pBeginInfo->flags;
	if (pBeginInfo->pInheritanceInfo)
	{
		assert(pBeginInfo->pInheritanceInfo->sType == VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO);
		buffer->secondary.renderPass = renderpass_cast(pBeginInfo->pInheritanceInfo->renderPass);
		buffer->secondary.subpass = pBeginInfo->pInheritanceInfo->subpass;
		buffer->secondary.framebuffer = framebuffer_cast(pBeginInfo->pInheritanceInfo->framebuffer);
		buffer->secondary.occlusionQueryEnable = pBeginInfo->pInheritanceInfo->occlusionQueryEnable;
		buffer->secondary.queryFlags = pBeginInfo->pInheritanceInfo->queryFlags;
		buffer->secondary.pipelineStatistics = pBeginInfo->pInheritanceInfo->pipelineStatistics;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkEndCommandBuffer);
	CLOG("commandBuffer=%p", commandBuffer);
	cVkCommandBuffer* buffer = commandbuffer_cast(commandBuffer);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags)
{
	ENTRY(vkResetCommandBuffer);
	CLOG("commandBuffer=%p, flags=%u", commandBuffer, flags);

	reset_command_buffer(commandbuffer_cast(commandBuffer), flags & VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline)
{
	ENTRY(vkCmdBindPipeline);
	CMDLOG("commandBuffer=%p, pipelineBindPoint=%u, pipeline=" NHANDLE, commandBuffer, pipelineBindPoint, pipeline);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindPipeline, commandBuffer, MetricUnit(1));
	p->currently_bound_pipeline = pipeline_cast(pipeline);
	p->commands.back().bindings.push_back(pipeline_cast(pipeline));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports)
{
	ENTRY(vkCmdSetViewport);
	CMDLOG("commandBuffer=%p, firstViewport=%u, viewportCount=%u, pViewports=%p", commandBuffer, firstViewport, viewportCount, pViewports);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetViewport, commandBuffer, MetricUnit(1, viewportCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
	ENTRY(vkCmdSetScissor);
	CMDLOG("commandBuffer=%p, firstScissor=%u, scissorCount=%u, pScissors=%p", commandBuffer, firstScissor, scissorCount, pScissors);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetScissor, commandBuffer, MetricUnit(1, scissorCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineWidth(
    VkCommandBuffer                             commandBuffer,
    float                                       lineWidth)
{
	ENTRY(vkCmdSetLineWidth);
	CMDLOG("commandBuffer=%p, lineWidth=%f", commandBuffer, lineWidth);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetLineWidth, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias(
    VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor)
{
	ENTRY(vkCmdSetDepthBias);
	CMDLOG("commandBuffer=%p, depthBiasConstantFactor=%f, depthBiasClamp=%f, depthBiasSlopeFactor=%f", commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetDepthBias, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetBlendConstants(
    VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4])
{
	ENTRY(vkCmdSetBlendConstants);
	CMDLOG("commandBuffer=%p, blendConstants={%f, %f, %f, %f}", commandBuffer, blendConstants[0], blendConstants[1], blendConstants[2], blendConstants[3]);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetBlendConstants, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBounds(
    VkCommandBuffer                             commandBuffer,
    float                                       minDepthBounds,
    float                                       maxDepthBounds)
{
	ENTRY(vkCmdSetDepthBounds);
	CMDLOG("commandBuffer=%p, minDepthBounds=%f, maxDepthBounds=%f", commandBuffer, minDepthBounds, maxDepthBounds);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetDepthBounds, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilCompareMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask)
{
	ENTRY(vkCmdSetStencilCompareMask);
	CMDLOG("commandBuffer=%p, faceMask=%u, compareMask=%u", commandBuffer, faceMask, compareMask);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetStencilCompareMask, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilWriteMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask)
{
	ENTRY(vkCmdSetStencilWriteMask);
	CMDLOG("commandBuffer=%p, faceMask=%u, writeMask=%u", commandBuffer, faceMask, writeMask);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetStencilWriteMask, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilReference(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference)
{
	ENTRY(vkCmdSetStencilReference);
	CMDLOG("commandBuffer=%p, faceMask=%u, reference=%u", commandBuffer, faceMask, reference);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetStencilReference, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets)
{
	ENTRY(vkCmdBindDescriptorSets);
	CMDLOG("commandBuffer=%p, pipelineBindPoint=%u, layout=" NHANDLE ", firstSet=%u, descriptorSetCount=%u, pDescriptorSets=%p, dynamicOffsetCount=%u, pDynamicOffsets=%p",
	       commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindDescriptorSets, commandBuffer, MetricUnit(1, descriptorSetCount, dynamicOffsetCount));
	p->commands.back().bindings.push_back(pipelinelayout_cast(layout));
	for (unsigned i = 0; i < descriptorSetCount; i++)
	{
		p->commands.back().bindings.push_back(descriptorset_cast(pDescriptorSets[i]));
	}
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
	ENTRY(vkCmdBindIndexBuffer);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, indexType=%u", commandBuffer, buffer, (unsigned long long)offset, indexType);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindIndexBuffer, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(buffer_cast(buffer));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets)
{
	ENTRY(vkCmdBindVertexBuffers);
	CMDLOG("commandBuffer=%p, firstBinding=%u, bindingCount=%u, pBuffers=%p, pOffsets=%p", commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindVertexBuffers, commandBuffer, MetricUnit(1, bindingCount));
	for (unsigned i = 0; i < bindingCount; i++)
	{
		p->commands.back().bindings.push_back(buffer_cast(pBuffers[i]));
	}
}

static uint32_t get_primitive_count(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexIndexCount)
{
	cVkPipeline* pPipeline = ccast<cVkCommandBuffer, VkCommandBuffer>(commandBuffer)->currently_bound_pipeline;
	uint32_t primitives = 0;

	switch (pPipeline->vertexInputAssemblyState.topology)
	{
		case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
		{
			primitives = vertexIndexCount;
			break;
		}

		case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
		{
			primitives = vertexIndexCount / 2;
			break;
		}

		case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
		{
			if (vertexIndexCount > 1)
				primitives = vertexIndexCount - 1;
			break;
		}

		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		{
			primitives = vertexIndexCount / 3;
			break;
		}

		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
		{
			if (vertexIndexCount > 2)
				primitives = vertexIndexCount - 2;
			break;
		}

		case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
		{
			primitives = vertexIndexCount / 4;
			break;
		}

		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
		{
			primitives = vertexIndexCount / 6;
			break;
		}
		case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
		{
			if (vertexIndexCount > 5)
				primitives = (vertexIndexCount - 4) / 2;
			break;
		}
		case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
		default:
			break;
	}

	return primitives;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDraw(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance)
{
	ENTRY(vkCmdDraw);
	CMDLOG("commandBuffer=%p, vertexCount=%u, instanceCount=%u, firstVertex=%u, firstInstance=%u",
	       commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDraw, commandBuffer, MetricUnit(1, 1, vertexCount, instanceCount, get_primitive_count(commandBuffer, vertexCount)));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance)
{
	ENTRY(vkCmdDrawIndexed);
	CMDLOG("commandBuffer=%p, indexCount=%u, instanceCount=%u, firstIndex=%u, vertexOffset=%d, firstInstance=%u",
	       commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndexed, commandBuffer, MetricUnit(1, 1, indexCount, instanceCount, get_primitive_count(commandBuffer, indexCount)));
}

static MetricUnit indirect_draw(
    VkCommandBuffer                             commandBuffer,
    cVkBuffer*                                  pbuf,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
	assert(drawCount <= 1 || stride >= sizeof(VkDrawIndirectCommand));
	const char* ptr = pbuf->memory->ptr + pbuf->memoryOffset + offset;
	int instance_count = 0;
	int vertex_count = 0;
	for (unsigned i = 0; i < drawCount; i++)
	{
		VkDrawIndirectCommand* params = (VkDrawIndirectCommand*)ptr;
		vertex_count += params->vertexCount;
		instance_count += params->instanceCount;
		ptr += stride;
	}
	return MetricUnit(1, drawCount, vertex_count, instance_count, get_primitive_count(commandBuffer, vertex_count));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount, // can be zero!
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawIndirect);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, drawCount=%u, stride=%u", commandBuffer, buffer, (unsigned long long)offset, drawCount, stride);

	// TBD - move counting into command buffer implementation, since buffer contents could change
	cVkBuffer* pbuf = buffer_cast(buffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndirect, commandBuffer, indirect_draw(commandBuffer, pbuf, offset, drawCount, stride));
	p->commands.back().bindings.push_back(pbuf);
}

static MetricUnit indirect_draw_indexed(
    VkCommandBuffer                             commandBuffer,
    cVkBuffer*                                  pbuf,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
	assert(drawCount <= 1 || stride >= sizeof(VkDrawIndexedIndirectCommand));
	const char* ptr = pbuf->memory->ptr + pbuf->memoryOffset + offset;;
	int instance_count = 0;
	int index_count = 0;
	for (unsigned i = 0; i < drawCount; i++)
	{
		VkDrawIndexedIndirectCommand* params = (VkDrawIndexedIndirectCommand*)ptr;
		instance_count += params->instanceCount;
		index_count += params->indexCount;
		ptr += stride;
	}
	return MetricUnit(1, drawCount, index_count, instance_count, get_primitive_count(commandBuffer, index_count));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount, // can be zero!
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawIndexedIndirect);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, drawCount=%u, stride=%u", commandBuffer, buffer, (unsigned long long)offset, drawCount, stride);

	// TBD - move counting into command buffer implementation, since buffer contents could change
	cVkBuffer* pbuf = buffer_cast(buffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndexedIndirect, commandBuffer, indirect_draw_indexed(commandBuffer, pbuf, offset, drawCount, stride));
	p->commands.back().bindings.push_back(pbuf);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    x,
    uint32_t                                    y,
    uint32_t                                    z)
{
	ENTRY(vkCmdDispatch);
	CMDLOG("commandBuffer=%p, x=%u, y=%u, z=%u", commandBuffer, x, y, z);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDispatch, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset)
{
	ENTRY(vkCmdDispatchIndirect);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu", commandBuffer, buffer, (unsigned long long)offset);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDispatchIndirect, commandBuffer, MetricUnit(1));
	cVkBuffer* cbuf = buffer_cast(buffer);
	p->commands.back().bindings.push_back(cbuf);
	const char* ptr = cbuf->memory->ptr + cbuf->memoryOffset + offset;;
	const VkDispatchIndirectCommand* params = (VkDispatchIndirectCommand*)ptr; // contains x, y, z
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferCopy*                         pRegions)
{
	ENTRY(vkCmdCopyBuffer);
	CMDLOG("commandBuffer=%p, srcBuffer=" NHANDLE ", dstBuffer=" NHANDLE ", regionCount=%u, pRegions=%p",
	       commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyBuffer, commandBuffer, MetricUnit(1, regionCount));
	p->commands.back().bindings.push_back(buffer_cast(srcBuffer));
	p->commands.back().bindings.push_back(buffer_cast(dstBuffer));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageCopy*                          pRegions)
{
	ENTRY(vkCmdCopyImage);
	CMDLOG("commandBuffer=%p, srcImage=" NHANDLE ", srcImageLayout=%u, dstImage=" NHANDLE ", dstImageLayout=%u, regionCount=%u, pRegions=%p",
	       commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyImage, commandBuffer, MetricUnit(1, regionCount));
	p->commands.back().bindings.push_back(image_cast(srcImage));
	p->commands.back().bindings.push_back(image_cast(dstImage));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageBlit*                          pRegions,
    VkFilter                                    filter)
{
	ENTRY(vkCmdBlitImage);
	CMDLOG("commandBuffer=%p, srcImage=" NHANDLE ", srcImageLayout=%u, dstImage=" NHANDLE " dstImageLayout=%u, regionCount=%u, pRegions=%p, filter=%u",
	       commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBlitImage, commandBuffer, MetricUnit(1, regionCount));
	p->commands.back().bindings.push_back(image_cast(srcImage));
	p->commands.back().bindings.push_back(image_cast(dstImage));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions)
{
	ENTRY(vkCmdCopyBufferToImage);
	CMDLOG("commandBuffer=%p, srcBuffer=" NHANDLE ", dstImage=" NHANDLE ", dstImageLayout=%u, regionCount=%u, pRegions=%p",
	       commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyBufferToImage, commandBuffer, MetricUnit(1, regionCount));
	p->commands.back().bindings.push_back(buffer_cast(srcBuffer));
	p->commands.back().bindings.push_back(image_cast(dstImage));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkBuffer                                    dstBuffer,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions)
{
	ENTRY(vkCmdCopyImageToBuffer);
	CMDLOG("commandBuffer=%p, srcImage=" NHANDLE ", srcImageLayout=%u, dstBuffer=" NHANDLE ", regionCount=%u, pRegions=%p",
	       commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyImageToBuffer, commandBuffer, MetricUnit(1, regionCount));
	p->commands.back().bindings.push_back(image_cast(srcImage));
	p->commands.back().bindings.push_back(buffer_cast(dstBuffer));
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                dataSize,
    const void*                                 pData)
{
	ENTRY(vkCmdUpdateBuffer);
	CMDLOG("commandBuffer=%p, dstBuffer=" NHANDLE ", dstOffset=%llu, dataSize=%llu, pData=%p",
	       commandBuffer, dstBuffer, (unsigned long long)dstOffset, (unsigned long long)dataSize, pData);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdUpdateBuffer, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(buffer_cast(dstBuffer));
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                size,
    uint32_t                                    data)
{
	ENTRY(vkCmdFillBuffer);
	CMDLOG("commandBuffer=%p, dstBuffer=" NHANDLE ", dstOffset=%llu, size=%llu, data=%u",
	       commandBuffer, dstBuffer, (unsigned long long)dstOffset, (unsigned long long)size, data);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdFillBuffer, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(buffer_cast(dstBuffer));
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearColorValue*                    pColor,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
	ENTRY(vkCmdClearColorImage);
	CMDLOG("commandBuffer=%p, image=" NHANDLE ", imageLayout=%u, pColor=%p, rangeCount=%u, pRanges=%p",
	       commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdClearColorImage, commandBuffer, MetricUnit(1, rangeCount));
	p->commands.back().bindings.push_back(image_cast(image));
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearDepthStencilImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     image,
    VkImageLayout                               imageLayout,
    const VkClearDepthStencilValue*             pDepthStencil,
    uint32_t                                    rangeCount,
    const VkImageSubresourceRange*              pRanges)
{
	ENTRY(vkCmdClearDepthStencilImage);
	CMDLOG("commandBuffer=%p, image=" NHANDLE ", imageLayout=%u, pDepthStencil=%p, rangeCount=%u, pRanges=%p",
	       commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdClearDepthStencilImage, commandBuffer, MetricUnit(1, rangeCount));
	p->commands.back().bindings.push_back(image_cast(image));
}

VKAPI_ATTR void VKAPI_CALL vkCmdClearAttachments(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkClearAttachment*                    pAttachments,
    uint32_t                                    rectCount,
    const VkClearRect*                          pRects)
{
	ENTRY(vkCmdClearAttachments);
	CMDLOG("commandBuffer=%p, attachmentCount=%u, pAttachments=%p, rectCount=%u, pRects=%p",
	       commandBuffer, attachmentCount, pAttachments, rectCount, pRects);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdClearAttachments, commandBuffer, MetricUnit(1, rectCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage(
    VkCommandBuffer                             commandBuffer,
    VkImage                                     srcImage,
    VkImageLayout                               srcImageLayout,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkImageResolve*                       pRegions)
{
	ENTRY(vkCmdResolveImage);
	CMDLOG("commandBuffer=%p, srcImage=" NHANDLE ", srcImageLayout=%u, dstImage=" NHANDLE ", dstImageLayout=%u, regionCount=%u, pRegions=%p",
	       commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdResolveImage, commandBuffer, MetricUnit(1, regionCount));
	p->commands.back().bindings.push_back(image_cast(srcImage));
	p->commands.back().bindings.push_back(image_cast(dstImage));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask)
{
	ENTRY(vkCmdSetEvent);
	CMDLOG("commandBuffer=%p, event=" NHANDLE ", stageMask=%u", commandBuffer, event, stageMask);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetEvent, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(event_cast(event));
	p->maxStageFlags |= stageMask;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask)
{
	ENTRY(vkCmdResetEvent);
	CMDLOG("commandBuffer=%p, event=" NHANDLE ", stageMask=%u", commandBuffer, event, stageMask);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdResetEvent, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(event_cast(event));
	p->maxStageFlags |= stageMask;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
	ENTRY(vkCmdWaitEvents);
	CMDLOG("commandBuffer=%p, eventCount=%u, pEvents=%p, srcStageMask=%u, dstStageMask=%u, memoryBarrierCount=%u, pMemoryBarriers=%p, "
	       "bufferMemoryBarrierCount=%u, pBufferMemoryBarriers=%p, imageMemoryBarrierCount=%u, pImageMemoryBarriers=%p",
	       commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
	       pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdWaitEvents, commandBuffer, MetricUnit(1, eventCount, memoryBarrierCount + bufferMemoryBarrierCount + imageMemoryBarrierCount));
	for (uint32_t i = 0; i < eventCount; i++)
	{
		p->commands.back().bindings.push_back(event_cast(pEvents[i]));
	}
	p->maxStageFlags |= srcStageMask;
	p->maxStageFlags |= dstStageMask;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
	ENTRY(vkCmdPipelineBarrier);
	CMDLOG("commandBuffer=%p, srcStageMask=%u, dstStageMask=%u, dependencyFlags=%u, memoryBarrierCount=%u, pMemoryBarriers=%p, bufferMemoryBarrierCount=%u, "
	       "pBufferMemoryBarriers=%p, imageMemoryBarrierCount=%u, pImageMemoryBarriers=%p", commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
	       memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdPipelineBarrier, commandBuffer, MetricUnit(1, memoryBarrierCount + bufferMemoryBarrierCount + imageMemoryBarrierCount));
	p->maxStageFlags |= srcStageMask;
	p->maxStageFlags |= dstStageMask;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
	ENTRY(vkCmdBeginQuery);
	CMDLOG("commandBuffer=%p, queryPool=" NHANDLE ", query=%u, flags=%u", commandBuffer, queryPool, query, flags);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginQuery, commandBuffer, MetricUnit(1));
	cVkQueryPool* qp = querypool_cast(queryPool);
	p->commands.back().bindings.push_back(qp);
	cVkPayloadQuery* payload = new cVkPayloadQuery;
	payload->queryPool = qp;
	payload->query = query;
	payload->flags = flags;
	p->commands.back().payload = payload;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	ENTRY(vkCmdEndQuery);
	CMDLOG("commandBuffer=%p, queryPool=" NHANDLE ", query=%u", commandBuffer, queryPool, query);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndQuery, commandBuffer, MetricUnit(1));
	cVkQueryPool* qp = querypool_cast(queryPool);
	p->commands.back().bindings.push_back(qp);
	cVkPayloadQuery* payload = new cVkPayloadQuery;
	payload->queryPool = nullptr;
	payload->query = 0;
	payload->flags = 0;
	p->commands.back().payload = payload;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
	ENTRY(vkCmdResetQueryPool);
	CMDLOG("commandBuffer=%p, queryPool=" NHANDLE ", firstQuery=%u, queryCount=%u", commandBuffer, queryPool, firstQuery, queryCount);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdResetQueryPool, commandBuffer, MetricUnit(1, queryCount));
	p->commands.back().bindings.push_back(querypool_cast(queryPool));
	cVkPayloadQueryReset* payload = new cVkPayloadQueryReset;
	payload->firstQuery = firstQuery;
	payload->queryCount = queryCount;
	p->commands.back().payload = payload;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	ENTRY(vkCmdWriteTimestamp);
	CMDLOG("commandBuffer=%p, pipelineStage=%u, queryPool=" NHANDLE ", query=%u", commandBuffer, pipelineStage, queryPool, query);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdWriteTimestamp, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(querypool_cast(queryPool));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags)
{
	ENTRY(vkCmdCopyQueryPoolResults);
	CMDLOG("commandBuffer=%p, queryPool=" NHANDLE ", firstQuery=%u, queryCount=%u, dstBuffer=" NHANDLE ", dstOffset=%llu, stride=%llu, flags=%u",
	       commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, (unsigned long long)dstOffset, (unsigned long long)stride, flags);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyQueryPoolResults, commandBuffer, MetricUnit(1, queryCount));
	cVkQueryPool* qp = querypool_cast(queryPool);
	p->commands.back().bindings.push_back(qp);
	cVkPayloadCopyQuery* payload = new cVkPayloadCopyQuery;
	payload->queryPool = qp;
	payload->firstQuery = firstQuery;
	payload->queryCount = queryCount;
	payload->dstBuffer = buffer_cast(dstBuffer);
	payload->dstOffset = dstOffset;
	payload->stride = stride;
	payload->flags = flags;
	p->commands.back().payload = payload;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(
    VkCommandBuffer                             commandBuffer,
    VkPipelineLayout                            layout,
    VkShaderStageFlags                          stageFlags,
    uint32_t                                    offset,
    uint32_t                                    size,
    const void*                                 pValues)
{
	ENTRY(vkCmdPushConstants);
	CMDLOG("commandBuffer=%p, VkPipelineLayout=" NHANDLE ", stageFlags=%u, offset=%u, size=%u, pValues=%p",
	       commandBuffer, layout, stageFlags, offset, size, pValues);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdPushConstants, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(pipelinelayout_cast(layout));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    VkSubpassContents                           contents)
{
	ENTRY(vkCmdBeginRenderPass);
	CMDLOG("commandBuffer=%p, pRenderPassBegin=%p, contents=%u", commandBuffer, pRenderPassBegin, contents);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginRenderPass, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents)
{
	ENTRY(vkCmdNextSubpass);
	CMDLOG("commandBuffer=%p, contents=%u", commandBuffer, contents);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdNextSubpass, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdEndRenderPass);
	CMDLOG("commandBuffer=%p", commandBuffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndRenderPass, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteCommands(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers)
{
	ENTRY(vkCmdExecuteCommands);
	CMDLOG("commandBuffer=%p, commandBufferCount=%u, pCommandBuffers=%p", commandBuffer, commandBufferCount, pCommandBuffers);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdExecuteCommands, commandBuffer, MetricUnit(1, commandBufferCount));
	for (unsigned i = 0; i < commandBufferCount; i++)
	{
		cVkCommandBuffer* secondary = commandbuffer_cast(pCommandBuffers[i]);
		p->commands.back().bindings.push_back(secondary);

		// Add secondary commandbuffer's counts to primary commandbuffer
		for (unsigned j = 0; j < p->count.size(); j++)
		{
			p->count[j] += secondary->count[j];
		}
	}
}

// Vulkan 1.1

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(
    uint32_t*                                   pApiVersion)
{
	ENTRY(vkEnumerateInstanceVersion);
	*pApiVersion = chameleon_api_version;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfo*               pBindInfos)
{
	ENTRY(vkBindBufferMemory2);
	CLOG("device=%p, bindInfoCount=%u, pBindInfos=%p", device, bindInfoCount, pBindInfos);
	cVkDevice* dev = device_cast(device);

	for (unsigned i = 0; i < bindInfoCount; ++i)
	{
		cVkBuffer* p = buffer_cast(pBindInfos[i].buffer);
		p->memory = devicememory_cast(pBindInfos[i].memory);
		p->memoryOffset = pBindInfos[i].memoryOffset;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfo*                pBindInfos)
{
	ENTRY(vkBindImageMemory2);
	CLOG("device=%p, bindInfoCount=%u, pBindInfos=%p", device, bindInfoCount, pBindInfos);
	cVkDevice* dev = device_cast(device);

	for (unsigned i = 0; i < bindInfoCount; ++i)
	{
		cVkImage* p = image_cast(pBindInfos[i].image);
		p->memory = devicememory_cast(pBindInfos[i].memory);
		p->memoryOffset = pBindInfos[i].memoryOffset;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeatures(
    VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlags*                   pPeerMemoryFeatures)
{
	ENTRY(vkGetDeviceGroupPeerMemoryFeatures);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMask(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask)
{
	ENTRY(vkCmdSetDeviceMask);
	CMDLOG("commandBuffer=%p, deviceMask=%u", commandBuffer, (unsigned)deviceMask);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetDeviceMask, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    baseGroupX,
    uint32_t                                    baseGroupY,
    uint32_t                                    baseGroupZ,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ)
{
	ENTRY(vkCmdDispatchBase);
	CMDLOG("commandBuffer=%p ...", commandBuffer);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDispatchBase, commandBuffer, MetricUnit(1));
}

static VkResult commonEnumeratePhysicalDeviceGroups(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties)
{
	CLOG("instance=%p, pPhysicalDeviceGroupCount=%u, pPhysicalDeviceGroupProperties=%p", instance, (unsigned)*pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
	cVkInstance* cinstance = instance_cast(instance);
	*pPhysicalDeviceGroupCount = 1;
	if (pPhysicalDeviceGroupProperties)
	{
		pPhysicalDeviceGroupProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
		pPhysicalDeviceGroupProperties->pNext = nullptr;
		pPhysicalDeviceGroupProperties->physicalDeviceCount = 1;
		pPhysicalDeviceGroupProperties->physicalDevices[0] = reinterpret_cast<VkPhysicalDevice>(&cinstance->GPUs.front());
		pPhysicalDeviceGroupProperties->subsetAllocation = VK_FALSE;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties)
{
	ENTRY(vkEnumeratePhysicalDeviceGroups);
	return commonEnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(
    VkDevice                                    device,
    const VkImageMemoryRequirementsInfo2*       pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetImageMemoryRequirements2);
	internalGetImageMemoryRequirements(device, pInfo->image, &pMemoryRequirements->memoryRequirements, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(
    VkDevice                                    device,
    const VkBufferMemoryRequirementsInfo2*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetBufferMemoryRequirements2);
	internalGetBufferMemoryRequirements(device, pInfo->buffer, &pMemoryRequirements->memoryRequirements, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements)
{
	ENTRY(vkGetImageSparseMemoryRequirements2);
	cVkDevice* dev = device_cast(device);
	*pSparseMemoryRequirementCount = 0;
}

static void commonGetPhysicalDeviceFeatures2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2*                  pFeatures)
{
	assert(pFeatures);

	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	// The key is "features_2" because there is no key for just "features" and the structure associated to "features" has no fields to be saved in the map
	pFeatures->features = *reinterpret_cast<VkPhysicalDeviceFeatures*>(device->features[VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2].first);

	VkBaseOutStructure* current = reinterpret_cast<VkBaseOutStructure*>(pFeatures->pNext);
	while (current != nullptr)
	{
		auto it = device->features.find(current->sType);
		if (it != device->features.end())
		{
			VkBaseOutStructure* next = current->pNext;

			memcpy(current, it->second.first, it->second.second);
			current->pNext = next;
		}

		current = current->pNext;
	}
}

static void commonGetPhysicalDeviceProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2*                pProperties)
{
	assert(pProperties);

	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	pProperties->properties = device->properties; // struct copy

	VkBaseOutStructure* current = reinterpret_cast<VkBaseOutStructure*>(pProperties->pNext);
	while (current != nullptr)
	{
		auto it = device->extendedProperties.find(current->sType);
		if (it != device->extendedProperties.end())
		{
			VkBaseOutStructure* next = current->pNext;
			memcpy(current, it->second.first, it->second.second);
			current->pNext = next;
		}
		current = current->pNext;
	}

	VkPhysicalDeviceMaintenance3Properties* pdm3p = (VkPhysicalDeviceMaintenance3Properties*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES);
	if (pdm3p)
	{
		// return big numbers
		pdm3p->maxMemoryAllocationSize = 5858492416;
		pdm3p->maxPerSetDescriptors = 500000;
	}
	VkPhysicalDeviceMaintenance4Properties* pdm4p = (VkPhysicalDeviceMaintenance4Properties*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES);
	if (pdm4p)
	{
		pdm4p->maxBufferSize = 4294967295; // big value
	}
	VkPhysicalDeviceMultiviewPropertiesKHR* pdmvp = (VkPhysicalDeviceMultiviewPropertiesKHR*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES);
	if (pdmvp)
	{
		pdmvp->maxMultiviewViewCount = 1024;
		pdmvp->maxMultiviewInstanceIndex = 4294967295;
	}
	VkPhysicalDeviceDepthStencilResolvePropertiesKHR* pddsrp = (VkPhysicalDeviceDepthStencilResolvePropertiesKHR*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES);
	if (pddsrp)
	{
		pddsrp->supportedDepthResolveModes = 0; // TBD root["supportedDepthResolveModes"]
		pddsrp->supportedStencilResolveModes = 0; // TBD root["supportedStencilResolveModes"]
		pddsrp->independentResolveNone = VK_TRUE;
		pddsrp->independentResolve = VK_TRUE;
	}
	VkPhysicalDeviceCooperativeMatrixPropertiesKHR* coopProps = (VkPhysicalDeviceCooperativeMatrixPropertiesKHR*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR);
	if (coopProps)
	{
		coopProps->cooperativeMatrixSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
	}

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR* rtProps =
		(VkPhysicalDeviceRayTracingPipelinePropertiesKHR*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR);
	if (rtProps)
	{
		VkBaseOutStructure* next = reinterpret_cast<VkBaseOutStructure*>(rtProps)->pNext;
		*rtProps = device->rayTracingPipelineProperties;
		rtProps->pNext = next;
	}

	VkPhysicalDeviceAccelerationStructurePropertiesKHR* asProps =
		(VkPhysicalDeviceAccelerationStructurePropertiesKHR*)find_extension(pProperties, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR);
	if (asProps)
	{
		VkBaseOutStructure* next = reinterpret_cast<VkBaseOutStructure*>(asProps)->pNext;
		*asProps = device->accelerationStructureProperties;
		asProps->pNext = next;
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2*                  pFeatures)
{
	ENTRY(vkGetPhysicalDeviceFeatures2);
	commonGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2*                pProperties)
{
	ENTRY(vkGetPhysicalDeviceProperties2);
	pProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	commonGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties2*                        pFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceFormatProperties2);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	pFormatProperties->pNext = nullptr;
	pFormatProperties->sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
	pFormatProperties->formatProperties = device->formats[format]; // struct copy
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2*     pImageFormatInfo,
    VkImageFormatProperties2*                   pImageFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceImageFormatProperties2);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	assert(pImageFormatInfo->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
	pImageFormatProperties->pNext = nullptr;
	pImageFormatProperties->sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
	return commonGetPhysicalDeviceImageFormatProperties(physicalDevice, pImageFormatInfo->format, pImageFormatInfo->type,
	        pImageFormatInfo->tiling, pImageFormatInfo->usage, pImageFormatInfo->flags, &pImageFormatProperties->imageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2*                   pQueueFamilyProperties)
{
	ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2);
	if (pQueueFamilyProperties)
	{
		pQueueFamilyProperties->sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
		pQueueFamilyProperties->pNext = nullptr;
		commonGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, &pQueueFamilyProperties->queueFamilyProperties);
	}
	else
	{
		commonGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, nullptr);
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties2*          pMemoryProperties)
{
	ENTRY(vkGetPhysicalDeviceMemoryProperties2);
	pMemoryProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
	pMemoryProperties->pNext = nullptr;
	commonGetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties->memoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties2*             pProperties)
{
	ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties2);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	*pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlags                      flags)
{
	ENTRY(vkTrimCommandPool);
	TBD_UNSUPPORTED;
	cVkCommandPool* pool = commandpool_cast(commandPool);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue2(
    VkDevice                                    device,
    const VkDeviceQueueInfo2*                   pQueueInfo,
    VkQueue*                                    pQueue)
{
	ENTRY(vkGetDeviceQueue2);
	cVkDevice* dev = device_cast(device);
	*pQueue = dev->queue_ptrs.at(pQueueInfo->queueIndex);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversion(
    VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfo*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversion*                   pYcbcrConversion)
{
	ENTRY(vkCreateSamplerYcbcrConversion);
	cVkDevice* dev = device_cast(device);
	cVkSamplerYcbcrConversion& p = owner_create<cVkSamplerYcbcrConversion, VkSamplerYcbcrConversion>(dev->samplerycbcrconversions, pYcbcrConversion, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversion(
    VkDevice                                    device,
    VkSamplerYcbcrConversion                    ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroySamplerYcbcrConversion);
	cVkDevice* dev = device_cast(device);
	destroy<cVkSamplerYcbcrConversion, VkSamplerYcbcrConversion>(ycbcrConversion, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(
    VkDevice                                    device,
    const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorUpdateTemplate*                 pDescriptorUpdateTemplate)
{
	ENTRY(vkCreateDescriptorUpdateTemplate);
	cVkDevice* dev = device_cast(device);
	cVkDescriptorUpdateTemplate& p = owner_create<cVkDescriptorUpdateTemplate, VkDescriptorUpdateTemplate>(dev->descriptorupdatetemplates, pDescriptorUpdateTemplate, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(
    VkDevice                                    device,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDescriptorUpdateTemplate);
	cVkDevice* dev = device_cast(device);
	destroy<cVkDescriptorUpdateTemplate, VkDescriptorUpdateTemplate>(descriptorUpdateTemplate, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    const void*                                 pData)
{
	ENTRY(vkUpdateDescriptorSetWithTemplate);
	cVkDevice* dev = device_cast(device);
	auto* templ = descriptorupdatetemplate_cast(descriptorUpdateTemplate);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo*   pExternalBufferInfo,
    VkExternalBufferProperties*                 pExternalBufferProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalBufferProperties);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo*    pExternalFenceInfo,
    VkExternalFenceProperties*                  pExternalFenceProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalFenceProperties);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties*              pExternalSemaphoreProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalSemaphoreProperties);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupport(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    VkDescriptorSetLayoutSupport*               pSupport)
{
	ENTRY(vkGetDescriptorSetLayoutSupport);
	CLOG("device=%p, pCreateInfo=%p, pSupport=%p", device, pCreateInfo, pSupport);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	cVkDevice* dev = device_cast(device);
	for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++)
	{
		pSupport[i].supported = VK_TRUE;
	}
}

// VK_KHR_surface extension

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance                                  instance,
    VkSurfaceKHR                                surface,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroySurfaceKHR);
	CLOG("instance=%p, surface=" NHANDLE ", pAllocator=%p", instance, surface, pAllocator);

	cVkInstance* cinstance = instance_cast(instance);
	destroy<cVkSurfaceKHR, VkSurfaceKHR>(surface, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32*                                   pSupported)
{
	ENTRY(vkGetPhysicalDeviceSurfaceSupportKHR);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u, surface=" NHANDLE ", pSupported=%p", physicalDevice, queueFamilyIndex, surface, pSupported);

	// We probably need to be more circumspect here, but for now this should work for us.
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	*pSupported = VK_TRUE;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
	ENTRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	CLOG("physicalDevice=%p, surface=" NHANDLE ", pSurfaceCapabilities=%p", physicalDevice, surface, pSurfaceCapabilities);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkSurfaceKHR* s = surface_cast(surface);
	pSurfaceCapabilities->minImageCount = s->capabilities.minImageCount;
	pSurfaceCapabilities->maxImageCount = s->capabilities.maxImageCount;
	pSurfaceCapabilities->currentExtent = s->capabilities.currentExtent;
	pSurfaceCapabilities->minImageExtent = s->capabilities.minImageExtent;
	pSurfaceCapabilities->maxImageExtent = s->capabilities.maxImageExtent;
	pSurfaceCapabilities->maxImageArrayLayers = s->capabilities.maxImageArrayLayers;
	pSurfaceCapabilities->supportedTransforms = s->capabilities.supportedTransforms;
	pSurfaceCapabilities->currentTransform = s->capabilities.currentTransform;
	pSurfaceCapabilities->supportedCompositeAlpha = s->capabilities.supportedCompositeAlpha;
	pSurfaceCapabilities->supportedUsageFlags = s->capabilities.supportedUsageFlags;
	XLOG("reporting minImageCount=%u maxImageCount=%u", pSurfaceCapabilities->minImageCount, pSurfaceCapabilities->maxImageCount);

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats)
{
	ENTRY(vkGetPhysicalDeviceSurfaceFormatsKHR);
	CLOG("physicalDevice=%p, surface=" NHANDLE ", pSurfaceFormatCount=%u, pSurfaceFormats=%p", physicalDevice, surface, *pSurfaceFormatCount, pSurfaceFormats);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkSurfaceKHR* s = surface_cast(surface);
	if (!pSurfaceFormats)
	{
		*pSurfaceFormatCount = s->formats.size();
	}
	else
	{
		for (unsigned i = 0; i < *pSurfaceFormatCount; i++)
		{
			pSurfaceFormats[i] = s->formats[i];
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes)
{
	ENTRY(vkGetPhysicalDeviceSurfacePresentModesKHR);
	CLOG("physicalDevice=%p, surface=" NHANDLE ", pPresentModeCount=%u, pPresentModes=%p", physicalDevice, surface, *pPresentModeCount, pPresentModes);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkSurfaceKHR* s = surface_cast(surface);
	if (!pPresentModes)
	{
		*pPresentModeCount = s->presentModes.size();
	}
	else
	{
		for (unsigned i = 0; i < *pPresentModeCount; i++)
		{
			pPresentModes[i] = s->presentModes[i];
		}
	}
	return VK_SUCCESS;
}

// VK_KHR_swapchain extension

static void add_swapchain_image(cVkDevice* dev, cVkSwapchainKHR* chain, const VkAllocationCallbacks* pAllocator)
{
	VkImage ptr = 0;
	cVkImage& p = owner_create<cVkImage, VkImage>(dev->images, &ptr, pAllocator);
	p.format = chain->imageFormat;
	p.extent = VkExtent3D{ chain->imageExtent.width, chain->imageExtent.height };
	p.arrayLayers = chain->imageArrayLayers;
	p.tiling = VK_IMAGE_TILING_OPTIMAL;
	p.usage = chain->imageUsage;
	p.sharingMode = chain->imageSharingMode;
	p.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	chain->images.push_back(image_cast(ptr));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice                                    device,
    const VkSwapchainCreateInfoKHR*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchain)
{
	ENTRY(vkCreateSwapchainKHR);
	CLOG("device=%p, pCreateInfo=%p[surface=" NHANDLE ", minImageCount=%u, imageFormat=%u, clipped=%s], pAllocator=%p, pSwapchain=%p",
	     device, pCreateInfo, pCreateInfo->surface, pCreateInfo->minImageCount, pCreateInfo->imageFormat, bool2str(pCreateInfo->clipped),
	     pAllocator, pSwapchain);

	cVkDevice* dev = device_cast(device);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
	cVkSwapchainKHR& swapchain = owner_create<cVkSwapchainKHR, VkSwapchainKHR>(dev->swapchains, pSwapchain, pAllocator);
	swapchain.imageExtent = pCreateInfo->imageExtent;
	swapchain.surface = surface_cast(pCreateInfo->surface);
	swapchain.minImageCount = pCreateInfo->minImageCount;
	swapchain.imageFormat = pCreateInfo->imageFormat;
	swapchain.queueFamilyIndexCount = pCreateInfo->queueFamilyIndexCount;
	swapchain.oldSwapchain = swapchain_cast(pCreateInfo->oldSwapchain);
	swapchain.clipped = pCreateInfo->clipped;
	swapchain.presentMode = pCreateInfo->presentMode;
	swapchain.imageArrayLayers = pCreateInfo->imageArrayLayers;
	swapchain.imageUsage = pCreateInfo->imageUsage;
	swapchain.imageSharingMode = pCreateInfo->imageSharingMode;
	for (unsigned i = 0; i < swapchain.imageCount; i++)
	{
		add_swapchain_image(dev, &swapchain, pAllocator);
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroySwapchainKHR);
	CLOG("device=%p, swapchain=" NHANDLE ", pAllocator=%p", device, swapchain, pAllocator);

	cVkDevice* dev = device_cast(device);
	destroy<cVkSwapchainKHR, VkSwapchainKHR>(swapchain, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pSwapchainImageCount,
    VkImage*                                    pSwapchainImages)
{
	ENTRY(vkGetSwapchainImagesKHR);
	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	CLOG("device=%p, swapchain=" NHANDLE ", pSwapchainImageCount=%u (chain has %u), pSwapchainImages=%p", device, swapchain, *pSwapchainImageCount, chain->imageCount, pSwapchainImages);

	if (pSwapchainImages)
	{
		for (unsigned i = 0; i < std::min(chain->imageCount, *pSwapchainImageCount); i++)
		{
			pSwapchainImages[i] = reinterpret_cast<VkImage>(chain->images[i]);
			XLOG("returning image %u : %p", i, (void*)pSwapchainImages[i]);
		}
		// Handle traces with higher number of recorded swapchain images than the minimum given to vkCreateSwapchainKHR
		for (unsigned i = std::min(chain->imageCount, *pSwapchainImageCount); i < *pSwapchainImageCount; i++)
		{
			add_swapchain_image(dev, chain, nullptr);
		}
		if (chain->imageCount < *pSwapchainImageCount) chain->imageCount = *pSwapchainImageCount;
	}
	else
	{
		*pSwapchainImageCount = chain->imageCount;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    timeout,
    VkSemaphore                                 semaphore,
    VkFence                                     fence,
    uint32_t*                                   pImageIndex)
{
	ENTRY(vkAcquireNextImageKHR);
	CLOG("device=%p, swapchain=" NHANDLE ", timeout=%llu, semaphore=" NHANDLE ", fence=" NHANDLE ", pImageIndex=%p",
	     device, swapchain, (unsigned long long)timeout, semaphore, fence, pImageIndex);

	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	*pImageIndex = chain->currentImage++ % chain->imageCount; // since we infinitely fast, we can always get an image in the swap chain
	// "A successful call to vkAcquireNextImageKHR counts as a signal operation on semaphore for the purposes of
	// queue forward-progress requirements. The semaphore is guaranteed to signal, so a wait operation can be
	// queued for the semaphore without risk of deadlock."
	// TBD

	if (fence != VK_NULL_HANDLE)
	{
		cVkFence* f = fence_cast(fence);
		f->signalled = true;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue                                     queue,
    const VkPresentInfoKHR*                     pPresentInfo)
{
	ENTRY(vkQueuePresentKHR);
	CLOG("queue=%p, pPresentInfo=%p", queue, pPresentInfo);
	cVkQueue* c = queue_cast(queue);

	for (unsigned i = 0; i < pPresentInfo->swapchainCount; i++)
	{
		const cVkSwapchainKHR* swapchain = swapchain_cast(pPresentInfo->pSwapchains[i]);
		c->commandbuffers.clear();
	}
	c->current_frame++;

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects)
{
	ENTRY(vkGetPhysicalDevicePresentRectanglesKHR);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkSurfaceKHR* s = surface_cast(surface);
	return VK_SUCCESS;
}

// VK_KHR_display extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPropertiesKHR*                     pProperties)
{
	ENTRY(vkGetPhysicalDeviceDisplayPropertiesKHR);
	CLOG("physicalDevice=%p, pPropertyCount=%u, pProperties=%p", physicalDevice, *pPropertyCount, pProperties);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	if (pProperties == nullptr)
	{
		*pPropertyCount = dev->displays.size();
	}
	else
	{
		int i = 0;
		for (auto& disp : dev->displays)
		{
			pProperties[i].display = reinterpret_cast<VkDisplayKHR>(&disp);
			pProperties[i].displayName = disp.displayName.c_str();
			pProperties[i].physicalDimensions.width = disp.physicalDimensions.width;
			pProperties[i].physicalDimensions.height = disp.physicalDimensions.height;
			pProperties[i].physicalResolution.width  = disp.physicalResolution.width;
			pProperties[i].physicalResolution.height = disp.physicalResolution.height;
			pProperties[i].supportedTransforms = disp.supportedTransforms;
			pProperties[i].planeReorderPossible = disp.planeReorderPossible;
			pProperties[i].persistentContent = disp.persistentContent;
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPlanePropertiesKHR*                pProperties)
{
	ENTRY(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	CLOG("physicalDevice=%p, pPropertyCount=%p[%u], pProperties=%p", physicalDevice, pPropertyCount, *pPropertyCount, pProperties);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	if (pProperties == nullptr)
	{
		*pPropertyCount = dev->displays.size();
	}
	else
	{
		unsigned i = 0;
		for (cVkDisplayKHR& display : dev->displays)
		{
			pProperties[i].currentDisplay = reinterpret_cast<VkDisplayKHR>(&display);
			pProperties[i].currentStackIndex = 0;
			i++;
			if (i >= *pPropertyCount)
			{
				break;
			}
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneSupportedDisplaysKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    planeIndex,
    uint32_t*                                   pDisplayCount,
    VkDisplayKHR*                               pDisplays)
{
	ENTRY(vkGetDisplayPlaneSupportedDisplaysKHR);
	CLOG("physicalDevice=%p, planeIndex=%u, pDisplayCount=%p[%u], pDisplays=%p", physicalDevice, planeIndex, pDisplayCount, *pDisplayCount, pDisplays);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	(void)planeIndex; // TBD
	if (pDisplays == NULL)
	{
		*pDisplayCount = dev->displays.size();
	}
	else
	{
		unsigned i = 0;
		for (cVkDisplayKHR& display : dev->displays)
		{
			pDisplays[i] = reinterpret_cast<VkDisplayKHR>(&display);
			i++;
			if (i >= *pDisplayCount)
			{
				break;
			}
		}
	}
	return VK_SUCCESS;
}

// "Each display has one or more supported modes associated with it by default. These built-in modes are queried by calling:"
VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModePropertiesKHR*                 pProperties)
{
	ENTRY(vkGetDisplayModePropertiesKHR);
	CLOG("physicalDevice=%p, display=" NHANDLE ", pPropertyCount=%p[%u], pProperties=%p", physicalDevice, display, pPropertyCount, *pPropertyCount, pProperties);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkDisplayKHR* disp = display_cast(display);

	if (pProperties == nullptr)
	{
		*pPropertyCount = disp->displayModes.size();
		XLOG("returning %u number of display modes for display %p", *pPropertyCount, disp);
	}
	else
	{
		unsigned i = 0;
		for (cVkDisplayModeKHR& mode : disp->displayModes)
		{
			pProperties[i].displayMode = reinterpret_cast<VkDisplayModeKHR>(&mode);
			pProperties[i].parameters = mode.parameters;
			i++;
			if (i >= *pPropertyCount)
			{
				break;
			}
		}
	}
	return VK_SUCCESS;
}

// "Additional modes may also be created by calling:"
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayModeKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    const VkDisplayModeCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDisplayModeKHR*                           pMode)
{
	ENTRY(vkCreateDisplayModeKHR);
	CLOG("physicalDevice=%p, display=" NHANDLE ", pCreateInfo=%p, pAllocator=%p, pMode=%p", physicalDevice, display, pCreateInfo, pAllocator, pMode);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkDisplayKHR* d = display_cast(display);
	cVkDisplayModeKHR& p = owner_create<cVkDisplayModeKHR, VkDisplayModeKHR>(d->displayModes, pMode, pAllocator);
	p.flags = pCreateInfo->flags;
	p.parameters = pCreateInfo->parameters;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayModeKHR                            mode,
    uint32_t                                    planeIndex,
    VkDisplayPlaneCapabilitiesKHR*              pCapabilities)
{
	ENTRY(vkGetDisplayPlaneCapabilitiesKHR);
	CLOG("physicalDevice=%p, mode=" NHANDLE ", planeIndex=%u, pCapabilities=%p", physicalDevice, mode, planeIndex, pCapabilities);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkDisplayModeKHR* pmode = displaymode_cast(mode);
	(void)planeIndex; // TBD
	pCapabilities->supportedAlpha      = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
	pCapabilities->minSrcPosition.x    = 0;
	pCapabilities->minSrcPosition.y    = 0;
	pCapabilities->maxSrcPosition.x    = pmode->parameters.visibleRegion.width;
	pCapabilities->maxSrcPosition.y    = pmode->parameters.visibleRegion.height;
	pCapabilities->minSrcExtent.width  = 1;
	pCapabilities->minSrcExtent.height = 1;
	pCapabilities->maxSrcExtent.width  = pmode->parameters.visibleRegion.width;
	pCapabilities->maxSrcExtent.height = pmode->parameters.visibleRegion.height;
	pCapabilities->minDstPosition.x    = 0;
	pCapabilities->minDstPosition.y    = 0;
	pCapabilities->maxDstPosition.x    = pmode->parameters.visibleRegion.width;
	pCapabilities->maxDstPosition.y    = pmode->parameters.visibleRegion.height;
	pCapabilities->minDstExtent.width  = 1;
	pCapabilities->minDstExtent.height = 1;
	pCapabilities->maxDstExtent.width  = pmode->parameters.visibleRegion.width;
	pCapabilities->maxDstExtent.height = pmode->parameters.visibleRegion.height;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDisplayPlaneSurfaceKHR(
    VkInstance                                  instance,
    const VkDisplaySurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateDisplayPlaneSurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	p.displayMode = displaymode_cast(pCreateInfo->displayMode);
	p.planeIndex = pCreateInfo->planeIndex;
	p.planeStackIndex = pCreateInfo->planeStackIndex;
	p.transform = pCreateInfo->transform;
	p.globalAlpha = pCreateInfo->globalAlpha;
	p.alphaMode = pCreateInfo->alphaMode;
	p.imageExtent = pCreateInfo->imageExtent;
	return VK_SUCCESS;
}

// VK_KHR_display_swapchain extension

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSharedSwapchainsKHR(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainCreateInfoKHR*             pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkSwapchainKHR*                             pSwapchains)
{
	ENTRY(vkCreateSharedSwapchainsKHR);
	CLOG("device=%p, swapchainCount=%u, pCreateInfos=%p, pAllocator=%p, pSwapchains=%p", device, swapchainCount,
	     pCreateInfos, pAllocator, pSwapchains);

	VkResult overall = VK_SUCCESS;
	for (unsigned i = 0; i < swapchainCount; i++)
	{
		assert(pCreateInfos[i].sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
		VkResult result = vkCreateSwapchainKHR(device, &pCreateInfos[i], pAllocator, &pSwapchains[i]);
		if (result != VK_SUCCESS)
		{
			overall = result;
		}
	}
	return overall;
}

// VK_KHR_xlib_surface extension

#ifdef VK_USE_PLATFORM_XLIB_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(
    VkInstance                                  instance,
    const VkXlibSurfaceCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateXlibSurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    Display*                                    dpy,
    VisualID                                    visualID)
{
	ENTRY(vkGetPhysicalDeviceXlibPresentationSupportKHR);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u, dpy=%p, visualID=%lu", physicalDevice, queueFamilyIndex, dpy, visualID);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return true;
}

#endif

// VK_KHR_xcb_surface extension

#ifdef VK_USE_PLATFORM_XCB_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(
    VkInstance                                  instance,
    const VkXcbSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateXcbSurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR);
	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	(void)pCreateInfo->connection;
	(void)pCreateInfo->window;
	return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    xcb_connection_t*                           connection,
    xcb_visualid_t                              visual_id)
{
	ENTRY(vkGetPhysicalDeviceXcbPresentationSupportKHR);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u, connection=%p, visual_id=%u", physicalDevice, queueFamilyIndex, connection, visual_id);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return true;
}

#endif

// VK_KHR_wayland_surface extension

#ifdef VK_USE_PLATFORM_WAYLAND_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWaylandSurfaceKHR(
    VkInstance                                  instance,
    const VkWaylandSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateWaylandSurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    struct wl_display*                          display)
{
	ENTRY(vkGetPhysicalDeviceWaylandPresentationSupportKHR);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u, display=%p", physicalDevice, queueFamilyIndex, display);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return true;
}

#endif

// VK_KHR_mir_surface extension

#ifdef VK_USE_PLATFORM_MIR_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMirSurfaceKHR(
    VkInstance                                  instance,
    const VkMirSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateMirSurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceMirPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    MirConnection*                              connection)
{
	ENTRY(vkGetPhysicalDeviceMirPresentationSupportKHR);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u, connection=%p", physicalDevice, queueFamilyIndex, connection);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

#endif

// VK_KHR_android_surface extension

#ifdef VK_USE_PLATFORM_ANDROID_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAndroidSurfaceKHR(
    VkInstance                                  instance,
    const VkAndroidSurfaceCreateInfoKHR*        pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateAndroidSurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	return VK_SUCCESS;
}

#endif

// VK_KHR_win32_surface extension

#ifdef VK_USE_PLATFORM_WIN32_KHR

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(
    VkInstance                                  instance,
    const VkWin32SurfaceCreateInfoKHR*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateWin32SurfaceKHR);
	CLOG("instance=%p, pCreateInfo=%p, pAllocator=%p, pSurface=%p", instance, pCreateInfo, pAllocator, pSurface);

	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex)
{
	ENTRY(vkGetPhysicalDeviceWin32PresentationSupportKHR);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u", physicalDevice, queueFamilyIndex);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return true;
}

#endif

// VK_EXT_debug_report extension

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugReportCallbackEXT(
    VkInstance                                  instance,
    const VkDebugReportCallbackCreateInfoEXT*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDebugReportCallbackEXT*                   pCallback)
{
	ENTRY(vkCreateDebugReportCallbackEXT);
	CLOG("instance=%p, pCreateInfo=%p[flags=%u], pAllocator=%p, callback=%p", instance, pCreateInfo, pCreateInfo->flags, pAllocator, pCallback);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT);
	cVkInstance* cinstance = instance_cast(instance);
	auto& p = owner_create<cVkDebugReportCallbackEXT, VkDebugReportCallbackEXT>(cinstance->callbacks, pCallback, pAllocator);
	p.flags = pCreateInfo->flags;
	p.callback = pCreateInfo->pfnCallback;
	p.userdata = pCreateInfo->pUserData;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugReportCallbackEXT(
    VkInstance                                  instance,
    VkDebugReportCallbackEXT                    callback,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDebugReportCallbackEXT);
	CLOG("instance=%p, callback=" NHANDLE ", pAllocator=%p", instance, callback, pAllocator);

	cVkInstance* cinstance = instance_cast(instance);
	destroy<cVkDebugReportCallbackEXT, VkDebugReportCallbackEXT>(callback, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkDebugReportMessageEXT(
    VkInstance                                  instance,
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char*                                 pLayerPrefix,
    const char*                                 pMessage)
{
	ENTRY(vkDebugReportMessageEXT);
	CLOG("instance=%p, flags=%u, objectType=%u, object=%p, location=%llu, messageCode=%d, pLayerPrefix=%s, pMessage=%s",
	     instance, flags, objectType, (void*)object, (unsigned long long)location, messageCode, pLayerPrefix, pMessage);

	cVkInstance* cinstance = instance_cast(instance);
	for (auto& c : instance_cast(instance)->callbacks)
	{
		c.callback(flags, objectType, object, location,messageCode, pLayerPrefix, pMessage, c.userdata);
	}
}

// VK_EXT_debug_marker extension

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectTagInfoEXT*        pTagInfo)
{
	ENTRY(vkDebugMarkerSetObjectTagEXT);
	CLOG("device=%p, pTagInfo=%p[object=%p]", device, pTagInfo, (void*)pTagInfo->object);

	cVkDevice* dev = device_cast(device);
	assert(pTagInfo->sType == VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT);
	cVkBase *object = ccast<cVkBase, uint64_t>(pTagInfo->object);
	assert(pTagInfo->objectType == object->debug_object_type);
	object->pTag = pTagInfo->pTag;
	object->tagSize = pTagInfo->tagSize;
	object->tagName = pTagInfo->tagName;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectNameInfoEXT*        pNameInfo)
{
	ENTRY(vkDebugMarkerSetObjectNameEXT);
	CLOG("device=%p, pNameInfo=%p[pObjectName=%s, object=%p]", device, pNameInfo, pNameInfo->pObjectName, (void*)pNameInfo->object);

	assert(pNameInfo->sType == VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT);
	cVkBase *object = ccast<cVkBase, uint64_t>(pNameInfo->object);
	object->marker_name = pNameInfo->pObjectName;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo)
{
	ENTRY(vkCmdDebugMarkerBeginEXT);
	CMDLOG("commandBuffer=%p, pMarkerInfo=%p[pMarkerName=%s]", commandBuffer, pMarkerInfo, pMarkerInfo->pMarkerName);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDebugMarkerBeginEXT, commandBuffer, MetricUnit(1));
	assert(pMarkerInfo->sType == VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT);
	p->commands.back().payload = new cVkPayloadMarker(pMarkerInfo->pMarkerName);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdDebugMarkerEndEXT);
	CMDLOG("commandBuffer=%p", commandBuffer);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDebugMarkerEndEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo)
{
	ENTRY(vkCmdDebugMarkerInsertEXT);
	CMDLOG("commandBuffer=%p, pMarkerInfo=%p[pMarkerName=%s]", commandBuffer, pMarkerInfo, pMarkerInfo->pMarkerName);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDebugMarkerInsertEXT, commandBuffer, MetricUnit(1));
	assert(pMarkerInfo->sType == VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT);
	p->commands.back().payload = new cVkPayloadMarker(pMarkerInfo->pMarkerName);
}

// VK_EXT_transform_feedback extension

VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes)
{
	ENTRY(vkCmdBindTransformFeedbackBuffersEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedbackEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
	ENTRY(vkCmdBeginTransformFeedbackEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedbackEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
	ENTRY(vkCmdEndTransformFeedbackEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginQueryIndexedEXT(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags,
    uint32_t                                    index)
{
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndQueryIndexedEXT(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    uint32_t                                    index)
{
	ENTRY(vkCmdEndQueryIndexedEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    VkBuffer                                    counterBuffer,
    VkDeviceSize                                counterBufferOffset,
    uint32_t                                    counterOffset,
    uint32_t                                    vertexStride)
{
	ENTRY(vkCmdDrawIndirectByteCountEXT);
	CMDLOG("commandBuffer=%p, instanceCount=%u, firstInstance=%u, counterBuffer=" NHANDLE ", counterBufferOffset=%lu, counterOffset=%u, vertexStride=%u",
	       commandBuffer, instanceCount, firstInstance, counterBuffer, (unsigned long)counterBufferOffset, counterOffset, vertexStride);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawIndirectCount);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, countBuffer=" NHANDLE ", countBufferOffset=%llu, maxDrawCount=%u, stride=%u",
	       commandBuffer, buffer, (unsigned long long)offset, countBuffer, (unsigned long long)countBufferOffset, maxDrawCount, stride);

	cVkBuffer* cbuf = buffer_cast(countBuffer);
	uint32_t drawCount = *reinterpret_cast<uint32_t *>(cbuf->memory->ptr + cbuf->memoryOffset + countBufferOffset);
	cVkBuffer* pbuf = buffer_cast(buffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndirectCount, commandBuffer, indirect_draw(commandBuffer, pbuf, offset, drawCount, stride));
	p->commands.back().bindings.push_back(pbuf);
	p->commands.back().bindings.push_back(cbuf);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawIndexedIndirectCount);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, countBuffer=" NHANDLE ", countBufferOffset=%llu, maxDrawCount=%u, stride=%u",
	       commandBuffer, buffer, (unsigned long long)offset, countBuffer, (unsigned long long)countBufferOffset, maxDrawCount, stride);

	cVkBuffer* cbuf = buffer_cast(countBuffer);
	uint32_t drawCount = *reinterpret_cast<uint32_t *>(cbuf->memory->ptr + cbuf->memoryOffset + countBufferOffset);
	cVkBuffer* pbuf = buffer_cast(buffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndexedIndirectCount, commandBuffer, indirect_draw_indexed(commandBuffer, pbuf, offset, drawCount, stride));
	p->commands.back().bindings.push_back(pbuf);
	p->commands.back().bindings.push_back(cbuf);
}

// VK_KHR_draw_indirect_count extension

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountKHR(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawIndirectCountKHR);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, countBuffer=" NHANDLE ", countBufferOffset=%llu, maxDrawCount=%u, stride=%u",
	       commandBuffer, buffer, (unsigned long long)offset, countBuffer, (unsigned long long)countBufferOffset, maxDrawCount, stride);

	// TBD - move counting into command buffer implementation, since buffer contents could change
	cVkBuffer* cbuf = buffer_cast(countBuffer);
	uint32_t drawCount = *reinterpret_cast<uint32_t *>(cbuf->memory->ptr + cbuf->memoryOffset + countBufferOffset);
	cVkBuffer* pbuf = buffer_cast(buffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndirectCountKHR, commandBuffer, indirect_draw(commandBuffer, pbuf, offset, drawCount, stride));
	p->commands.back().bindings.push_back(pbuf);
	p->commands.back().bindings.push_back(cbuf);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountKHR(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawIndexedIndirectCountKHR);
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, countBuffer=" NHANDLE ", countBufferOffset=%llu, maxDrawCount=%u, stride=%u",
	       commandBuffer, buffer, (unsigned long long)offset, countBuffer, (unsigned long long)countBufferOffset, maxDrawCount, stride);

	// TBD - move counting into command buffer implementation, since buffer contents could change
	cVkBuffer* cbuf = buffer_cast(countBuffer);
	uint32_t drawCount = *reinterpret_cast<uint32_t *>(cbuf->memory->ptr + cbuf->memoryOffset + countBufferOffset);
	cVkBuffer* pbuf = buffer_cast(buffer);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndexedIndirectCountKHR, commandBuffer, indirect_draw_indexed(commandBuffer, pbuf, offset, drawCount, stride));
	p->commands.back().bindings.push_back(pbuf);
	p->commands.back().bindings.push_back(cbuf);
}

// VK_KHR_get_physical_device_properties2 extension

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2KHR*               pFeatures)
{
	ENTRY(vkGetPhysicalDeviceFeatures2KHR);
	commonGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2KHR*             pProperties)
{
	ENTRY(vkGetPhysicalDeviceProperties2KHR);
	pProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
	commonGetPhysicalDeviceProperties2(physicalDevice, reinterpret_cast<VkPhysicalDeviceProperties2*>(pProperties));
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties2KHR*                     pFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceFormatProperties2KHR);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	pFormatProperties->pNext = nullptr;
	pFormatProperties->sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2_KHR;
	pFormatProperties->formatProperties = device->formats[format]; // struct copy
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2KHR*  pImageFormatInfo,
    VkImageFormatProperties2KHR*                pImageFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceImageFormatProperties2KHR);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	assert(pImageFormatInfo->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
	pImageFormatProperties->pNext = nullptr;
	pImageFormatProperties->sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
	return commonGetPhysicalDeviceImageFormatProperties(physicalDevice, pImageFormatInfo->format, pImageFormatInfo->type,
	        pImageFormatInfo->tiling, pImageFormatInfo->usage, pImageFormatInfo->flags, &pImageFormatProperties->imageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2KHR*                pQueueFamilyProperties)
{
	ENTRY(vkGetPhysicalDeviceQueueFamilyProperties2KHR);
	if (pQueueFamilyProperties)
	{
		pQueueFamilyProperties->sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2_KHR;
		pQueueFamilyProperties->pNext = nullptr;
		commonGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, &pQueueFamilyProperties->queueFamilyProperties);
	}
	else
	{
		commonGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, nullptr);
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties2KHR*       pMemoryProperties)
{
	ENTRY(vkGetPhysicalDeviceMemoryProperties2KHR);
	pMemoryProperties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2_KHR;
	pMemoryProperties->pNext = nullptr;
	commonGetPhysicalDeviceMemoryProperties(physicalDevice, &pMemoryProperties->memoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties2KHR*          pProperties)
{
	ENTRY(vkGetPhysicalDeviceSparseImageFormatProperties2KHR);
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	*pPropertyCount = 0;
}

// VK_KHR_maintenance1 extension

VKAPI_ATTR void VKAPI_CALL vkTrimCommandPoolKHR(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlagsKHR                   flags)
{
	ENTRY(vkTrimCommandPoolKHR);
	CLOG("device=%p, commandPool=" NHANDLE ", flags=%u", device, commandPool, flags);

	cVkDevice* dev = device_cast(device);
	// no-op
}

// VK_KHR_maintenance3 extension

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSupportKHR(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    VkDescriptorSetLayoutSupport*               pSupport)
{
	ENTRY(vkGetDescriptorSetLayoutSupportKHR);
	CLOG("device=%p, pCreateInfo=%p, pSupport=%p", device, pCreateInfo, pSupport);
	cVkDevice* dev = device_cast(device);
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++)
	{
		pSupport[i].supported = VK_TRUE;
	}
}

// VK_KHR_maintenance4

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirementsKHR(
    VkDevice                                    device,
    const VkDeviceBufferMemoryRequirements*     pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetDeviceBufferMemoryRequirementsKHR);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.size = pInfo->pCreateInfo->size;
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports every memory type for now
	pMemoryRequirements->memoryRequirements.alignment = sizeof(void*);
	VkMemoryDedicatedRequirements* mdr = (VkMemoryDedicatedRequirements*)find_extension(pMemoryRequirements, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
	if (mdr)
	{
		mdr->prefersDedicatedAllocation = VK_FALSE;
		mdr->requiresDedicatedAllocation = VK_FALSE;
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirementsKHR(
    VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetDeviceImageMemoryRequirementsKHR);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports everything
	pMemoryRequirements->memoryRequirements.alignment = sizeof(void*);
	pMemoryRequirements->memoryRequirements.size = pInfo->pCreateInfo->extent.width * pInfo->pCreateInfo->extent.height * pInfo->pCreateInfo->extent.depth * std::max(1u, pInfo->pCreateInfo->arrayLayers)
		* std::max(1u, pInfo->pCreateInfo->mipLevels) * 4 * 4; // return worst case
	VkMemoryDedicatedRequirements* mdr = (VkMemoryDedicatedRequirements*)find_extension(pMemoryRequirements, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
	if (mdr)
	{
		mdr->prefersDedicatedAllocation = VK_FALSE;
		mdr->requiresDedicatedAllocation = VK_FALSE;
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirementsKHR(
    VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements)
{
	ENTRY(vkGetDeviceImageSparseMemoryRequirementsKHR);
	TBD_UNSUPPORTED;
}

// VK_NN_vi_surface extension

#ifdef VK_USE_PLATFORM_VI_NN

VKAPI_ATTR VkResult VKAPI_CALL vkCreateViSurfaceNN(
    VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateViSurfaceNN);
	TBD_UNSUPPORTED;
	cVkInstance* cinstance = instance_cast(instance);
	return VK_SUCCESS;
}

#endif

// VK_EXT_direct_mode_display extension

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display)
{
	ENTRY(vkReleaseDisplayEXT);
	CLOG("physicalDevice=%p, display=" NHANDLE, physicalDevice, display);
	XLOG("Releasing exclusive control of the display");
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

// VK_EXT_acquire_xlib_display extension

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireXlibDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    Display*                                    dpy,
    VkDisplayKHR                                display)
{
	ENTRY(vkAcquireXlibDisplayEXT);
	CLOG("physicalDevice=%p, dpy=%p, display=" NHANDLE, physicalDevice, dpy, display);
	XLOG("Taking exclusive control of the display");
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetRandROutputDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    Display*                                    dpy,
    RROutput                                    rrOutput,
    VkDisplayKHR*                               pDisplay)
{
	ENTRY(vkGetRandROutputDisplayEXT);
	//CLOG("physicalDevice=%p, dpy=%p, rrOutput=%ld, pDisplay=%p", physicalDevice, dpy, (long)rrOutput, pDisplay);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}
#endif

// VK_EXT_display_surface_counter extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilities2EXT*                  pSurfaceCapabilities)
{
	ENTRY(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
	CLOG("physicalDevice=%p, surface=" NHANDLE ", pSurfaceCapabilities=%p", physicalDevice, surface, pSurfaceCapabilities);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkSurfaceKHR* s = surface_cast(surface);
	*pSurfaceCapabilities = s->capabilities; // struct copy

	return VK_SUCCESS;
}

// VK_EXT_display_control extension

VKAPI_ATTR VkResult VKAPI_CALL vkDisplayPowerControlEXT(
    VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayPowerInfoEXT*                pDisplayPowerInfo)
{
	ENTRY(vkDisplayPowerControlEXT);
	// Set the power state of a display
	CLOG("device=%p, display=" NHANDLE ", pDisplayPowerInfo=%p", device, display, pDisplayPowerInfo);

	cVkDevice* dev = device_cast(device);
	switch (pDisplayPowerInfo->powerState)
	{
	case VK_DISPLAY_POWER_STATE_OFF_EXT:
		XLOG("Turning display off");
		break;
	case VK_DISPLAY_POWER_STATE_SUSPEND_EXT:
		XLOG("Display suspended");
		break;
	case VK_DISPLAY_POWER_STATE_ON_EXT:
		XLOG("Turning display on");
		break;
	case VK_DISPLAY_POWER_STATE_MAX_ENUM_EXT:
		return VK_ERROR_VALIDATION_FAILED_EXT; // not standards mandated but well within "undefined behaviour"
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDeviceEventEXT(
    VkDevice                                    device,
    const VkDeviceEventInfoEXT*                 pDeviceEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
	ENTRY(vkRegisterDeviceEventEXT);
	CLOG("device=%p, pDeviceEventInfo=%p, pAllocator=%p, pFence=%p", device, pDeviceEventInfo, pAllocator, pFence);

	// Signal a fence when a device event occurs.
	cVkDevice* dev = device_cast(device);
	cVkFence& p = owner_create<cVkFence, VkFence>(dev->fences, pFence, pAllocator);
	// The only defined event so far is VK_DEVICE_EVENT_TYPE_DISPLAY_HOTPLUG_EXT
	// which we'll never signal. So this implementation is "complete".
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterDisplayEventEXT(
    VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayEventInfoEXT*                pDisplayEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
	ENTRY(vkRegisterDisplayEventEXT);
	// Signal a fence when a display event occurs.
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	cVkFence& p = owner_create<cVkFence, VkFence>(dev->fences, pFence, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainCounterEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t*                                   pCounterValue)
{
	ENTRY(vkGetSwapchainCounterEXT);
	// Query the current value of a surface counter.
	// "If a counter is not available because the swapchain is out of date, the implementation may return VK_ERROR_OUT_OF_DATE_KHR."
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	return VK_SUCCESS;
}

// VK_KHR_push_descriptor extension

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetKHR(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites)
{
	ENTRY(vkCmdPushDescriptorSetKHR);
	CMDLOG("commandBuffer=%p, pipelineBindPoint=%u, layout=" NHANDLE ", set=%u, descriptorWriteCount=%u, pDescriptorWrites=%p",
	       commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdPushDescriptorSetKHR, commandBuffer, MetricUnit(1));
}

// VK_KHR_descriptor_update_template extension

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplateKHR(
    VkDevice                                    device,
    const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorUpdateTemplateKHR*              pDescriptorUpdateTemplate)
{
	ENTRY(vkCreateDescriptorUpdateTemplateKHR);
	cVkDevice* dev = device_cast(device);
	cVkDescriptorUpdateTemplate& p = owner_create<cVkDescriptorUpdateTemplate, VkDescriptorUpdateTemplate>(dev->descriptorupdatetemplates, pDescriptorUpdateTemplate, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplateKHR(
    VkDevice                                    device,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDescriptorUpdateTemplateKHR);
	cVkDevice* dev = device_cast(device);
	destroy<cVkDescriptorUpdateTemplate, VkDescriptorUpdateTemplate>(descriptorUpdateTemplate, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateKHR(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    const void*                                 pData)
{
	ENTRY(vkUpdateDescriptorSetWithTemplateKHR);
	cVkDevice* dev = device_cast(device);
	auto* templ = descriptorupdatetemplate_cast(descriptorUpdateTemplate);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplateKHR(
    VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    const void*                                 pData)
{
	ENTRY(vkCmdPushDescriptorSetWithTemplateKHR);
	CMDLOG("commandBuffer=%p, descriptorUpdateTemplate=" NHANDLE ", layout=" NHANDLE ", set=%u, pData=%p",
	       commandBuffer, descriptorUpdateTemplate, layout, set, pData);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdPushDescriptorSetWithTemplateKHR, commandBuffer, MetricUnit(1));
	auto* templ = descriptorupdatetemplate_cast(descriptorUpdateTemplate);
}

// VK_KHR_bind_memory2 extension

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfoKHR*            pBindInfos)
{
	return vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfoKHR*             pBindInfos)
{
	return vkBindImageMemory2(device, bindInfoCount, pBindInfos);
}

// VK_KHR_device_group extension

VKAPI_ATTR void VKAPI_CALL vkGetDeviceGroupPeerMemoryFeaturesKHR(
    VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlags*                   pPeerMemoryFeatures)
{
	ENTRY(vkGetDeviceGroupPeerMemoryFeaturesKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDeviceMaskKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask)
{
	ENTRY(vkCmdSetDeviceMaskKHR);
	CMDLOG("commandBuffer=%p, deviceMask=%u", commandBuffer, deviceMask);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetDeviceMaskKHR, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupPresentCapabilitiesKHR(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHR*        pDeviceGroupPresentCapabilities)
{
	ENTRY(vkGetDeviceGroupPresentCapabilitiesKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModesKHR(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHR*           pModes)
{
	ENTRY(vkGetDeviceGroupSurfacePresentModesKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHR*            pAcquireInfo,
    uint32_t*                                   pImageIndex)
{
	ENTRY(vkAcquireNextImage2KHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBaseKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    baseGroupX,
    uint32_t                                    baseGroupY,
    uint32_t                                    baseGroupZ,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ)
{
	ENTRY(vkCmdDispatchBaseKHR);
	CMDLOG("commandBuffer=%p, baseGroupX=%u, baseGroupY=%u, baseGroupZ=%u, groupCountX=%u, groupCountY=%u, groupCountZ=%u",
	       commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDispatchBaseKHR, commandBuffer, MetricUnit(1)); // TBD should not be 1
}

// VK_KHR_external_memory_capabilities extension

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalBufferPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo,
    VkExternalBufferPropertiesKHR*              pExternalBufferProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalBufferPropertiesKHR);
	CLOG("physicalDevice=%p, pExternalBufferInfo=%p, pExternalBufferProperties=%p", physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	pExternalBufferProperties->sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
	pExternalBufferProperties->pNext = nullptr;
	pExternalBufferProperties->externalMemoryProperties.externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_FLAG_BITS_MAX_ENUM;
	pExternalBufferProperties->externalMemoryProperties.exportFromImportedHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
	pExternalBufferProperties->externalMemoryProperties.compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
}

// VK_KHR_external_memory_win32 extension

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryWin32HandleKHR(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    HANDLE*                                     pHandle)
{
	ENTRY(vkGetMemoryWin32HandleKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryWin32HandlePropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    HANDLE                                      handle,
    VkMemoryWin32HandlePropertiesKHR*           pMemoryWin32HandleProperties)
{
	ENTRY(vkGetMemoryWin32HandlePropertiesKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}
#endif

// VK_KHR_external_memory_fd extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdKHR(
    VkDevice                                    device,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd)
{
	ENTRY(vkGetMemoryFdKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryFdPropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties)
{
	ENTRY(vkGetMemoryFdPropertiesKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

// VK_KHR_external_semaphore_capabilities extension

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalSemaphorePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo,
    VkExternalSemaphorePropertiesKHR*           pExternalSemaphoreProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalSemaphorePropertiesKHR);
	CLOG("physicalDevice=%p, pExternalSemaphoreInfo=%p, pExternalSemaphoreProperties=%p", physicalDevice, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	pExternalSemaphoreProperties->sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES;
	pExternalSemaphoreProperties->pNext = nullptr;
	pExternalSemaphoreProperties->exportFromImportedHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
	pExternalSemaphoreProperties->compatibleHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
	pExternalSemaphoreProperties->externalSemaphoreFeatures = VK_EXTERNAL_SEMAPHORE_FEATURE_FLAG_BITS_MAX_ENUM;
}

// VK_KHR_external_semaphore_win32 extension

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreWin32HandleKHR(
    VkDevice                                    device,
    const VkImportSemaphoreWin32HandleInfoKHR*  pImportSemaphoreWin32HandleInfo)
{
	ENTRY(vkImportSemaphoreWin32HandleKHR);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* c = semaphore_cast(pImportSemaphoreWin32HandleInfo->semaphore);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreWin32HandleKHR(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    VkExternalSemaphoreHandleTypeFlagBitsKHR    handleType,
    HANDLE*                                     pHandle)
{
	ENTRY(vkGetSemaphoreWin32HandleKHR);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* c = semaphore_cast(semaphore);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif

// VK_KHR_external_semaphore_fd extension

VKAPI_ATTR VkResult VKAPI_CALL vkImportSemaphoreFdKHR(
    VkDevice                                    device,
    const VkImportSemaphoreFdInfoKHR*           pImportSemaphoreFdInfo)
{
	ENTRY(vkImportSemaphoreFdKHR);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* sem = semaphore_cast(pImportSemaphoreFdInfo->semaphore);
	// TBD implement something better here, for now this works with trace replayers
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreFdKHR(
    VkDevice                                    device,
    const VkSemaphoreGetFdInfoKHR*              pGetFdInfo,
    int*                                        pFd)
{
	ENTRY(vkGetSemaphoreFdKHR);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* sem = semaphore_cast(pGetFdInfo->semaphore);
	*pFd = -1; // tell caller that using this return value would be an error
	// TBD implement something better here, for now this works with trace replayers
	return VK_SUCCESS;
}

// VK_EXT_discard_rectangles extension

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstDiscardRectangle,
    uint32_t                                    discardRectangleCount,
    const VkRect2D*                             pDiscardRectangles)
{
	ENTRY(vkCmdSetDiscardRectangleEXT);
	CMDLOG("commandBuffer=%p, firstDiscardRectangle=%u, discardRectangleCount=%u, pDiscardRectangles=%p",
	       commandBuffer, firstDiscardRectangle, discardRectangleCount, pDiscardRectangles);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetDiscardRectangleEXT, commandBuffer, MetricUnit(1, discardRectangleCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    discardRectangleEnable)
{
	ENTRY(vkCmdSetDiscardRectangleEnableEXT);
	TBD_UNSUPPORTED
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDiscardRectangleModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkDiscardRectangleModeEXT                   discardRectangleMode)
{
	ENTRY(vkCmdSetDiscardRectangleModeEXT);
	TBD_UNSUPPORTED
}

// VK_MVK_ios_surface extension

#ifdef VK_USE_PLATFORM_IOS_MVK
VKAPI_ATTR VkResult VKAPI_CALL vkCreateIOSSurfaceMVK(
    VkInstance                                  instance,
    const VkIOSSurfaceCreateInfoMVK*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateIOSSurfaceMVK);
	TBD_UNSUPPORTED;
	cVkInstance* cinstance = instance_cast(instance);
	return VK_SUCCESS;
}
#endif

// VK_MVK_macos_surface extension

#ifdef VK_USE_PLATFORM_IOS_MVK
VKAPI_ATTR VkResult VKAPI_CALL vkCreateMacOSSurfaceMVK(
    VkInstance                                  instance,
    const VkMacOSSurfaceCreateInfoMVK*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateMacOSSurfaceMVK);
	TBD_UNSUPPORTED;
	cVkInstance* cinstance = instance_cast(instance);
	return VK_SUCCESS;
}
#endif

// VK_GOOGLE_display_timing extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetRefreshCycleDurationGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkRefreshCycleDurationGOOGLE*               pDisplayTimingProperties)
{
	ENTRY(vkGetRefreshCycleDurationGOOGLE);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pPresentationTimingCount,
    VkPastPresentationTimingGOOGLE*             pPresentationTimings)
{
	ENTRY(vkGetPastPresentationTimingGOOGLE);
	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	*pPresentationTimingCount = 0;
	return VK_SUCCESS;
}

// VK_EXT_hdr_metadata extension

VKAPI_ATTR void VKAPI_CALL vkSetHdrMetadataEXT(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainKHR*                       pSwapchains,
    const VkHdrMetadataEXT*                     pMetadata)
{
	ENTRY(vkSetHdrMetadataEXT);
	CLOG("device=%p, swapchainCount=%u, pSwapchains=%p, pMetadata=%p", device, swapchainCount, pSwapchains, pMetadata);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < swapchainCount; i++)
	{
		cVkSwapchainKHR* chain = swapchain_cast(pSwapchains[i]);
		// ... and then do nothing with the provided information
	}
}

// VK_KHR_get_surface_capabilities2 extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    VkSurfaceCapabilities2KHR*                  pSurfaceCapabilities)
{
	ENTRY(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
	CLOG("physicalDevice=%p, pSurfaceInfo=%p, pSurfaceCapabilities=%p", physicalDevice, pSurfaceInfo, pSurfaceCapabilities);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	pSurfaceCapabilities->sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
	pSurfaceCapabilities->pNext = nullptr;
	pSurfaceCapabilities->surfaceCapabilities.minImageCount = 1;
	pSurfaceCapabilities->surfaceCapabilities.maxImageCount = 0; // meaning no limit
	pSurfaceCapabilities->surfaceCapabilities.currentExtent = { 0xFFFFFFFF, 0xFFFFFFFF }; // "the surface size will be determined by the extent of a swapchain targeting the surface"
	pSurfaceCapabilities->surfaceCapabilities.minImageExtent = { 1, 1 };
	pSurfaceCapabilities->surfaceCapabilities.maxImageExtent = { 0xFFFFFFFF, 0xFFFFFFFF };
	pSurfaceCapabilities->surfaceCapabilities.maxImageArrayLayers = 0xFF;
	pSurfaceCapabilities->surfaceCapabilities.supportedTransforms = VK_SURFACE_TRANSFORM_FLAG_BITS_MAX_ENUM_KHR;
	pSurfaceCapabilities->surfaceCapabilities.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	pSurfaceCapabilities->surfaceCapabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_FLAG_BITS_MAX_ENUM_KHR;
	pSurfaceCapabilities->surfaceCapabilities.supportedUsageFlags = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
	VkSharedPresentSurfaceCapabilitiesKHR* vkspsc = (VkSharedPresentSurfaceCapabilitiesKHR*)find_extension(pSurfaceCapabilities, VK_STRUCTURE_TYPE_SHARED_PRESENT_SURFACE_CAPABILITIES_KHR);
	if (vkspsc)
	{
		vkspsc->sharedPresentSupportedUsageFlags = VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM; // support all usage cases
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormats2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormat2KHR*                        pSurfaceFormats)
{
	ENTRY(vkGetPhysicalDeviceSurfaceFormats2KHR);
	CLOG("physicalDevice=%p, pSurfaceInfo=%p, pSurfaceFormatCount=%u, pSurfaceFormats=%p", physicalDevice, pSurfaceInfo, *pSurfaceFormatCount, pSurfaceFormats);

	cVkPhysicalDevice* dev = physicaldevice_cast(physicalDevice);
	cVkSurfaceKHR* s = surface_cast(pSurfaceInfo->surface);
	if (!pSurfaceFormats)
	{
		*pSurfaceFormatCount = s->formats.size();
	}
	else
	{
		for (unsigned i = 0; i < *pSurfaceFormatCount; i++)
		{
			pSurfaceFormats[i].surfaceFormat = s->formats[i];
		}
	}
	return VK_SUCCESS;
}

// VK_KHR_shared_presentable_image extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainStatusKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain)
{
	ENTRY(vkGetSwapchainStatusKHR);
	CLOG("device=%p swapchain=" NHANDLE, device, swapchain);
	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	return VK_SUCCESS; // "the presentation engine is presenting the contents of the shared presentable image"
}

// VK_KHR_external_fence_capabilities extension

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalFencePropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo,
    VkExternalFencePropertiesKHR*               pExternalFenceProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalFencePropertiesKHR);
	CLOG("physicalDevice=%p, pExternalFenceInfo=%p, pExternalFenceProperties=%p", physicalDevice, pExternalFenceInfo, pExternalFenceProperties);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	pExternalFenceProperties->sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES;
	pExternalFenceProperties->pNext = nullptr;
	pExternalFenceProperties->exportFromImportedHandleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
	pExternalFenceProperties->compatibleHandleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_FLAG_BITS_MAX_ENUM;
	pExternalFenceProperties->externalFenceFeatures = VK_EXTERNAL_FENCE_FEATURE_FLAG_BITS_MAX_ENUM;
}

// VK_KHR_external_fence_fd extension

VKAPI_ATTR VkResult VKAPI_CALL vkImportFenceFdKHR(
    VkDevice                                    device,
    const VkImportFenceFdInfoKHR*               pImportFenceFdInfo)
{
	ENTRY(vkImportFenceFdKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceFdKHR(
    VkDevice                                    device,
    const VkFenceGetFdInfoKHR*                  pGetFdInfo,
    int*                                        pFd)
{
	ENTRY(vkGetFenceFdKHR);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

// VK_KHR_get_memory_requirements2 extension

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageMemoryRequirementsInfo2KHR*    pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements)
{
	ENTRY(vkGetImageMemoryRequirements2KHR);
	internalGetImageMemoryRequirements(device, pInfo->image, &pMemoryRequirements->memoryRequirements, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkBufferMemoryRequirementsInfo2KHR*   pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements)
{
	ENTRY(vkGetBufferMemoryRequirements2KHR);
	internalGetBufferMemoryRequirements(device, pInfo->buffer, &pMemoryRequirements->memoryRequirements, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2KHR* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2KHR*        pSparseMemoryRequirements)
{
	ENTRY(vkGetImageSparseMemoryRequirements2KHR);
	cVkDevice* dev = device_cast(device);
	*pSparseMemoryRequirementCount = 0;
}

// VK_EXT_sample_locations extension

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEXT(
    VkCommandBuffer                             commandBuffer,
    const VkSampleLocationsInfoEXT*             pSampleLocationsInfo)
{
	ENTRY(vkCmdSetSampleLocationsEXT);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdSetSampleLocationsEXT, commandBuffer, MetricUnit(1, pSampleLocationsInfo->sampleLocationsCount));
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMultisamplePropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties)
{
	ENTRY(vkGetPhysicalDeviceMultisamplePropertiesEXT);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
}

// VK_EXT_validation_cache extension

VKAPI_ATTR VkResult VKAPI_CALL vkCreateValidationCacheEXT(
    VkDevice                                    device,
    const VkValidationCacheCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkValidationCacheEXT*                       pValidationCache)
{
	ENTRY(vkCreateValidationCacheEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyValidationCacheEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyValidationCacheEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMergeValidationCachesEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        dstCache,
    uint32_t                                    srcCacheCount,
    const VkValidationCacheEXT*                 pSrcCaches)
{
	ENTRY(vkMergeValidationCachesEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetValidationCacheDataEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    size_t*                                     pDataSize,
    void*                                       pData)
{
	ENTRY(vkGetValidationCacheDataEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

// VK_KHR_sampler_ycbcr_conversion extension

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSamplerYcbcrConversionKHR(
    VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversionKHR*                pYcbcrConversion)
{
	ENTRY(vkCreateSamplerYcbcrConversionKHR);
	cVkDevice* dev = device_cast(device);
	cVkSamplerYcbcrConversion& p = owner_create<cVkSamplerYcbcrConversion, VkSamplerYcbcrConversion>(dev->samplerycbcrconversions, pYcbcrConversion, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySamplerYcbcrConversionKHR(
    VkDevice                                    device,
    VkSamplerYcbcrConversionKHR                 ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroySamplerYcbcrConversionKHR);
	cVkDevice* dev = device_cast(device);
	destroy<cVkSamplerYcbcrConversion, VkSamplerYcbcrConversion>(ycbcrConversion, pAllocator);
}

// VK_EXT_external_memory_host extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryHostPointerPropertiesEXT(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    const void*                                 pHostPointer,
    VkMemoryHostPointerPropertiesEXT*           pMemoryHostPointerProperties)
{
	ENTRY(vkGetMemoryHostPointerPropertiesEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

// VK_EXT_headless_surface

VKAPI_ATTR VkResult VKAPI_CALL vkCreateHeadlessSurfaceEXT(
    VkInstance                                  instance,
    const VkHeadlessSurfaceCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
	ENTRY(vkCreateHeadlessSurfaceEXT);
	cVkInstance* cinstance = instance_cast(instance);
	cVkSurfaceKHR& p = owner_create<cVkSurfaceKHR, VkSurfaceKHR>(cinstance->surfaces, pSurface, pAllocator);
	p.sType = pCreateInfo->sType;
	p.capabilities.currentExtent.width = 0xFFFFFFFF;
	p.capabilities.currentExtent.height = 0xFFFFFFFF;
	p.capabilities.minImageExtent.width = 1;
	p.capabilities.minImageExtent.height = 1;
	p.capabilities.maxImageExtent.width = 4096;
	p.capabilities.maxImageExtent.height = 4096;
	p.capabilities.maxImageArrayLayers = 1;
	p.capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	p.capabilities.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	p.capabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
	p.capabilities.minImageCount = 1;
	p.capabilities.maxImageCount = 0; // no limit
	p.presentModes = { VK_PRESENT_MODE_IMMEDIATE_KHR };
	p.formats = { { .format = VK_FORMAT_R8G8B8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_R8G8B8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_B8G8R8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_B8G8R8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_R8G8B8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
	              { .format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };
	return VK_SUCCESS;
}

// VK_KHR_device_group_creation extension

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroupsKHR(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties)
{
	ENTRY(vkEnumeratePhysicalDeviceGroupsKHR);
	return commonEnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
}

// VK_EXT_debug_utils extension

VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(
    VkDevice                                    device,
    const VkDebugUtilsObjectNameInfoEXT*        pNameInfo)
{
	ENTRY(vkSetDebugUtilsObjectNameEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectTagEXT(
    VkDevice                                    device,
    const VkDebugUtilsObjectTagInfoEXT*         pTagInfo)
{
	ENTRY(vkSetDebugUtilsObjectTagEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkQueueBeginDebugUtilsLabelEXT(
    VkQueue                                     queue,
    const VkDebugUtilsLabelEXT*                 pLabelInfo)
{
	ENTRY(vkQueueBeginDebugUtilsLabelEXT);
	TBD_UNSUPPORTED;
	cVkQueue* c = queue_cast(queue);
}

VKAPI_ATTR void VKAPI_CALL vkQueueEndDebugUtilsLabelEXT(
    VkQueue                                     queue)
{
	ENTRY(vkQueueEndDebugUtilsLabelEXT);
	TBD_UNSUPPORTED;
	cVkQueue* c = queue_cast(queue);
}

VKAPI_ATTR void VKAPI_CALL vkQueueInsertDebugUtilsLabelEXT(
    VkQueue                                     queue,
    const VkDebugUtilsLabelEXT*                 pLabelInfo)
{
	ENTRY(vkQueueInsertDebugUtilsLabelEXT);
	TBD_UNSUPPORTED;
	cVkQueue* c = queue_cast(queue);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugUtilsLabelEXT*                 pLabelInfo)
{
	ENTRY(vkCmdBeginDebugUtilsLabelEXT);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginDebugUtilsLabelEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdEndDebugUtilsLabelEXT);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndDebugUtilsLabelEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdInsertDebugUtilsLabelEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugUtilsLabelEXT*                 pLabelInfo)
{
	ENTRY(vkCmdInsertDebugUtilsLabelEXT);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdInsertDebugUtilsLabelEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
    VkInstance                                  instance,
    const VkDebugUtilsMessengerCreateInfoEXT*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDebugUtilsMessengerEXT*                   pMessenger)
{
	ENTRY(vkCreateDebugUtilsMessengerEXT);
	TBD_UNSUPPORTED;
	cVkInstance* cinstance = instance_cast(instance);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
    VkInstance                                  instance,
    VkDebugUtilsMessengerEXT                    messenger,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDebugUtilsMessengerEXT);
	TBD_UNSUPPORTED;
	cVkInstance* cinstance = instance_cast(instance);
}

VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(
    VkInstance                                  instance,
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData)
{
	ENTRY(vkSubmitDebugUtilsMessageEXT);
	TBD_UNSUPPORTED;
	cVkInstance* cinstance = instance_cast(instance);
}

// VK_KHR_get_display_properties2 extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayProperties2KHR*                    pProperties)
{
	ENTRY(vkGetPhysicalDeviceDisplayProperties2KHR);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceDisplayPlaneProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkDisplayPlaneProperties2KHR*               pProperties)
{
	ENTRY(vkGetPhysicalDeviceDisplayPlaneProperties2KHR);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayModeProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkDisplayKHR                                display,
    uint32_t*                                   pPropertyCount,
    VkDisplayModeProperties2KHR*                pProperties)
{
	ENTRY(vkGetDisplayModeProperties2KHR);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDisplayPlaneCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkDisplayPlaneInfo2KHR*               pDisplayPlaneInfo,
    VkDisplayPlaneCapabilities2KHR*             pCapabilities)
{
	ENTRY(vkGetDisplayPlaneCapabilities2KHR);
	TBD_UNSUPPORTED;
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	return VK_SUCCESS;
}

// VK_ANDROID_external_memory_android_hardware_buffer extension

#ifdef VK_USE_PLATFORM_ANDROID_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkGetAndroidHardwareBufferPropertiesANDROID(
    VkDevice                                    device,
    const struct AHardwareBuffer*               buffer,
    VkAndroidHardwareBufferPropertiesANDROID*   pProperties)
{
	ENTRY(vkGetAndroidHardwareBufferPropertiesANDROID);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryAndroidHardwareBufferANDROID(
    VkDevice                                    device,
    const VkMemoryGetAndroidHardwareBufferInfoANDROID* pInfo,
    struct AHardwareBuffer**                    pBuffer)
{
	ENTRY(vkGetMemoryAndroidHardwareBufferANDROID);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}
#endif

// VK_KHR_create_renderpass2 extension

static VkResult commonCreateRenderpass2(
    VkDevice                                    device,
    const VkRenderPassCreateInfo2*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);
	cVkDevice* dev = device_cast(device);
	cVkRenderPass& p = owner_create<cVkRenderPass, VkRenderPass>(dev->renderpasses, pRenderPass, pAllocator);
	p.attachments.reserve(pCreateInfo->attachmentCount);
	for (unsigned i = 0; i < pCreateInfo->attachmentCount; i++)
	{
		cVkAttachmentDescription attachment;
		attachment.config = pCreateInfo->pAttachments[i]; // struct copy
		p.attachments.push_back(attachment);
	}
	p.subpassCount = pCreateInfo->subpassCount;
	p.dependencyCount = pCreateInfo->dependencyCount;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(
    VkDevice                                    device,
    const VkRenderPassCreateInfo2*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
	ENTRY(vkCreateRenderPass2);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pRenderPass=%p", device, pCreateInfo, pAllocator, pRenderPass);
	return commonCreateRenderpass2(device, pCreateInfo, pAllocator, pRenderPass);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo)
{
	ENTRY(vkCmdBeginRenderPass2);
	CMDLOG("commandBuffer=%p, pRenderPassBegin=%p, pSubpassBeginInfo=%p", commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginRenderPass2, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo,
    const VkSubpassEndInfo*                     pSubpassEndInfo)
{
	ENTRY(vkCmdNextSubpass2);
	CMDLOG("commandBuffer=%p, pSubpassEndInfo=%p", commandBuffer, pSubpassEndInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdNextSubpass2, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassEndInfo*                     pSubpassEndInfo)
{
	ENTRY(vkCmdEndRenderPass2);
	CMDLOG("commandBuffer=%p pSubpassEndInfo=%p", commandBuffer, pSubpassEndInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndRenderPass2, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2KHR(
    VkDevice                                    device,
    const VkRenderPassCreateInfo2KHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
	ENTRY(vkCreateRenderPass2KHR);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pRenderPass=%p", device, pCreateInfo, pAllocator, pRenderPass);
	return commonCreateRenderpass2(device, pCreateInfo, pAllocator, pRenderPass);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    const VkSubpassBeginInfoKHR*                pSubpassBeginInfo)
{
	ENTRY(vkCmdBeginRenderPass2KHR);
	CMDLOG("commandBuffer=%p, pRenderPassBegin=%p, pSubpassBeginInfo=%p", commandBuffer, pRenderPassBegin, pSubpassBeginInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginRenderPass2KHR, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdNextSubpass2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassBeginInfoKHR*                pSubpassBeginInfo,
    const VkSubpassEndInfoKHR*                  pSubpassEndInfo)
{
	ENTRY(vkCmdNextSubpass2);
	CMDLOG("commandBuffer=%p, pSubpassEndInfo=%p", commandBuffer, pSubpassEndInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdNextSubpass2KHR, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassEndInfoKHR*                  pSubpassEndInfo)
{
	ENTRY(vkCmdEndRenderPass2KHR);
	CMDLOG("commandBuffer=%p pSubpassEndInfo=%p", commandBuffer, pSubpassEndInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndRenderPass2KHR, commandBuffer, MetricUnit(1));
}

// VK_EXT_conditional_rendering extension

VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRenderingEXT(
    VkCommandBuffer                             commandBuffer,
    const VkConditionalRenderingBeginInfoEXT*   pConditionalRenderingBegin)
{
	ENTRY(vkCmdBeginConditionalRenderingEXT);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginConditionalRenderingEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndConditionalRenderingEXT(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdEndConditionalRenderingEXT);
	TBD_UNSUPPORTED;
	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndConditionalRenderingEXT, commandBuffer, MetricUnit(1));
}

// VK_EXT_calibrated_timestamps extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pTimeDomainCount,
    VkTimeDomainEXT*                            pTimeDomains)
{
	ENTRY(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT);
	CLOG("physicalDevice=%p, pTimeDomainCount=%p, pTimeDomains=%p", physicalDevice, pTimeDomainCount, pTimeDomains);

	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	if (pTimeDomains == nullptr)
	{
		*pTimeDomainCount = 0;
		return VK_SUCCESS;
	}
	else
	{
		// we could support VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT and VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT here
		return VK_SUCCESS; // return VK_INCOMPLETE if not enough space to write all to pTimeDomains
	}
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsEXT(
    VkDevice                                    device,
    uint32_t                                    timestampCount,
    const VkCalibratedTimestampInfoEXT*         pTimestampInfos,
    uint64_t*                                   pTimestamps,
    uint64_t*                                   pMaxDeviation)
{
	ENTRY(vkGetCalibratedTimestampsEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

// VK_EXT_image_drm_format_modifier extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageDrmFormatModifierPropertiesEXT(
    VkDevice                                    device,
    VkImage                                     image,
    VkImageDrmFormatModifierPropertiesEXT*      pProperties)
{
	ENTRY(vkGetImageDrmFormatModifierPropertiesEXT);
	TBD_UNSUPPORTED;
	cVkDevice* dev = device_cast(device);
	return VK_SUCCESS;
}

// VK_EXT_buffer_device_address extension
// VK_KHR_buffer_device_address extension
// Vulkan 1.2 buffer address feature

static VkDeviceAddress internal_vkGetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo)
{
	// "To query a 64-bit buffer device address value through which buffer memory can be accessed in a shader"
	cVkDevice* dev = device_cast(device);
	assert(pInfo->sType == VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO);
	assert(pInfo->pNext == nullptr);
	// "buffer must have been created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT"
	assert(pInfo->buffer != 0);

	cVkBuffer* cbuffer = buffer_cast(pInfo->buffer);
	assert(cbuffer->usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

	return reinterpret_cast<VkDeviceAddress>(cbuffer->memory->ptr + cbuffer->memoryOffset);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressEXT(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfoEXT*         pInfo)
{
	ENTRY(vkGetBufferDeviceAddressEXT);
	return internal_vkGetBufferDeviceAddress(device, pInfo);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfoKHR*         pInfo)
{
	ENTRY(vkGetBufferDeviceAddressKHR);
	return internal_vkGetBufferDeviceAddress(device, pInfo);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo)
{
	ENTRY(vkGetBufferDeviceAddress);
	return internal_vkGetBufferDeviceAddress(device, pInfo);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo)
{
	ENTRY(vkGetBufferOpaqueCaptureAddress);
	TBD_UNSUPPORTED;
	return 0;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
	ENTRY(vkGetDeviceMemoryOpaqueCaptureAddress);
	TBD_UNSUPPORTED;
	return 0;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddressKHR(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo)
{
	return vkGetBufferOpaqueCaptureAddress(device, pInfo);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddressKHR(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
	return vkGetDeviceMemoryOpaqueCaptureAddress(device, pInfo);
}

// VK_EXT_host_query_reset

static void commonResetQueryPool(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
	CLOG("device=%p, queryPool=" NHANDLE ", firstQuery=%u, queryCount=%u", device, queryPool, firstQuery, queryCount);

	cVkQueryPool* qp = querypool_cast(queryPool);
	for (unsigned i = firstQuery; i < queryCount; i++)
	{
		memset(qp->data.data(), 0, qp->data.size());
		qp->availability[i] = false;
	}
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
	ENTRY(vkResetQueryPool);
	commonResetQueryPool(device, queryPool, firstQuery, queryCount);
}

VKAPI_ATTR void VKAPI_CALL vkResetQueryPoolEXT(
    VkDevice                                    device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
	ENTRY(vkResetQueryPoolEXT);
	commonResetQueryPool(device, queryPool, firstQuery, queryCount);
}

// VK_KHR_performance_query

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    uint32_t*                                   pCounterCount,
    VkPerformanceCounterKHR*                    pCounters,
    VkPerformanceCounterDescriptionKHR*         pCounterDescriptions)
{
	ENTRY(vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR);
	TBD_UNSUPPORTED;
	*pCounterCount = 0;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkQueryPoolPerformanceCreateInfoKHR*  pPerformanceQueryCreateInfo,
    uint32_t*                                   pNumPasses)
{
	ENTRY(vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireProfilingLockKHR(
    VkDevice                                    device,
    const VkAcquireProfilingLockInfoKHR*        pInfo)
{
	ENTRY(vkAcquireProfilingLockKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkReleaseProfilingLockKHR(
    VkDevice                                    device)
{
	ENTRY(vkReleaseProfilingLockKHR);
	TBD_UNSUPPORTED;
}

// VK_KHR_timeline_semaphore

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    uint64_t*                                   pValue)
{
	ENTRY(vkGetSemaphoreCounterValue);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* c = semaphore_cast(semaphore);
	*pValue = c->value;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValueKHR(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    uint64_t*                                   pValue)
{
	ENTRY(vkGetSemaphoreCounterValueKHR);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* c = semaphore_cast(semaphore);
	*pValue = c->value;
	return VK_SUCCESS;
}

static VkResult internalWaitSemaphores(
    VkDevice                                    device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout)
{
	cVkDevice* dev = device_cast(device);
	do
	{
		bool success = true;
		for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; i++)
		{
			cVkSemaphore* c = semaphore_cast(pWaitInfo->pSemaphores[i]);
			if (c->value >= pWaitInfo->pValues[i] && (pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT_KHR)) return VK_SUCCESS;
			if (c->value < pWaitInfo->pValues[i]) success = false;
		}
		if (timeout) timeout--;
		if (success) return VK_SUCCESS;
	} while (timeout);
	return VK_TIMEOUT;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(
    VkDevice                                    device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout)
{
	ENTRY(vkWaitSemaphores);
	return internalWaitSemaphores(device, pWaitInfo, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(
    VkDevice                                    device,
    const VkSemaphoreSignalInfo*                pSignalInfo)
{
	ENTRY(vkSignalSemaphore);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* c = semaphore_cast(pSignalInfo->semaphore);
	c->value = pSignalInfo->value;
	return VK_SUCCESS; // probably wrong
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphoresKHR(
    VkDevice                                    device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout)
{
	ENTRY(vkWaitSemaphoresKHR);
	return internalWaitSemaphores(device, pWaitInfo, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphoreKHR(
    VkDevice                                    device,
    const VkSemaphoreSignalInfo*                pSignalInfo)
{
	ENTRY(vkSignalSemaphoreKHR);
	cVkDevice* dev = device_cast(device);
	cVkSemaphore* c = semaphore_cast(pSignalInfo->semaphore);
	c->value = pSignalInfo->value;
	return VK_SUCCESS;
}

// VK_KHR_acceleration_structure

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructureKHR(
    VkDevice                                    device,
    const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkAccelerationStructureKHR*                 pAccelerationStructure)
{
	ENTRY(vkCreateAccelerationStructureKHR);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pAccelerationStructure=%p", device, pCreateInfo, pAllocator, pAccelerationStructure);

	cVkDevice* dev = device_cast(device);
	cVkAccelerationStructureKHR& acc = owner_create<cVkAccelerationStructureKHR, VkAccelerationStructureKHR>(dev->accelerationStructures, pAccelerationStructure, pAllocator);
	acc.flags = pCreateInfo->createFlags;
	acc.buffer = buffer_cast(pCreateInfo->buffer);
	acc.memoryOffset = pCreateInfo->offset;
	acc.memorySize = pCreateInfo->size;
	acc.type = pCreateInfo->type;

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyAccelerationStructureKHR(
    VkDevice                                    device,
    VkAccelerationStructureKHR                  accelerationStructure,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyAccelerationStructureKHR);
	CLOG("device=%p, pAccelerationStructure=" NHANDLE ", pAllocator=%p", device, accelerationStructure, pAllocator);

	destroy<cVkAccelerationStructureKHR, VkAccelerationStructureKHR>(accelerationStructure, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
	ENTRY(vkCmdBuildAccelerationStructuresKHR);
	CLOG("commandBuffer=%p, infoCount=%u, pInfos=%p, ppBuildRangeInfos=%p", commandBuffer, infoCount, pInfos, ppBuildRangeInfos);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBuildAccelerationStructuresKHR, commandBuffer, MetricUnit(1, infoCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdBuildAccelerationStructuresIndirectKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkDeviceAddress*                      pIndirectDeviceAddresses,
    const uint32_t*                             pIndirectStrides,
    const uint32_t* const*                      ppMaxPrimitiveCounts)
{
	ENTRY(vkCmdBuildAccelerationStructuresIndirectKHR);
	CLOG("commandBuffer=%p, infoCount=%u, pInfos=%p, pIndirectDeviceAddresses=%p, pIndirectStrides=%p, ppMaxPrimitiveCounts=%p", commandBuffer, infoCount, pInfos, pIndirectDeviceAddresses, pIndirectStrides, ppMaxPrimitiveCounts);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBuildAccelerationStructuresIndirectKHR, commandBuffer, MetricUnit(1, infoCount));
}

VKAPI_ATTR VkResult VKAPI_CALL vkBuildAccelerationStructuresKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
	ENTRY(vkBuildAccelerationStructuresKHR);
	CLOG("device=%p, deferredOperation=" NHANDLE ", infoCount=%u, pInfos=%p, ppBuildRangeInfos=%p", device, deferredOperation, infoCount, pInfos, ppBuildRangeInfos);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyAccelerationStructureInfoKHR*   pInfo)
{
	ENTRY(vkCopyAccelerationStructureKHR);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pInfo=%p", device, deferredOperation, pInfo);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyAccelerationStructureToMemoryKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
{
	ENTRY(vkCopyAccelerationStructureToMemoryKHR);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pInfo=%p", device, deferredOperation, pInfo);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToAccelerationStructureKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
{
	ENTRY(vkCopyMemoryToAccelerationStructureKHR);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pInfo=%p", device, deferredOperation, pInfo);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteAccelerationStructuresPropertiesKHR(
    VkDevice                                    device,
    uint32_t                                    accelerationStructureCount,
    const VkAccelerationStructureKHR*           pAccelerationStructures,
    VkQueryType                                 queryType,
    size_t                                      dataSize,
    void*                                       pData,
    size_t                                      stride)
{
	ENTRY(vkWriteAccelerationStructuresPropertiesKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyAccelerationStructureInfoKHR*   pInfo)
{
	ENTRY(vkCmdCopyAccelerationStructureKHR);
	CLOG("commandBuffer=%p, pInfo=%p", commandBuffer, pInfo);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyAccelerationStructureKHR, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyAccelerationStructureToMemoryKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
{
	ENTRY(vkCmdCopyAccelerationStructureToMemoryKHR);
	CLOG("commandBuffer=%p, pInfo=%p", commandBuffer, pInfo);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyAccelerationStructureToMemoryKHR, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToAccelerationStructureKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
{
	ENTRY(vkCmdCopyMemoryToAccelerationStructureKHR);
	CLOG("commandBuffer=%p, pInfo=%p", commandBuffer, pInfo);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyMemoryToAccelerationStructureKHR, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetAccelerationStructureDeviceAddressKHR(
    VkDevice                                    device,
    const VkAccelerationStructureDeviceAddressInfoKHR* pInfo)
{
	ENTRY(vkGetAccelerationStructureDeviceAddressKHR);
	CLOG("device=%p, pInfo=%p", device, pInfo);

	cVkAccelerationStructureKHR* cacc = accelerationstructure_cast(pInfo->accelerationStructure);

	return reinterpret_cast<VkDeviceAddress>(cacc->buffer->memory->ptr + cacc->buffer->memoryOffset + cacc->memoryOffset);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteAccelerationStructuresPropertiesKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    accelerationStructureCount,
    const VkAccelerationStructureKHR*           pAccelerationStructures,
    VkQueryType                                 queryType,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery)
{
	ENTRY(vkCmdWriteAccelerationStructuresPropertiesKHR);
	CMDLOG("commandBuffer=%p, accelerationStructureCount=%u, pAccelerationStructures=%p, queryType=%u, queryPool=" NHANDLE ", firstQuery=%u", commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdWriteAccelerationStructuresPropertiesKHR, commandBuffer, MetricUnit(1, accelerationStructureCount));
	cVkQueryPool* qp = querypool_cast(queryPool);
	p->commands.back().bindings.push_back(qp);
	cVkPayloadWriteAccelerationStructuresPropertiesKHR* payload = new cVkPayloadWriteAccelerationStructuresPropertiesKHR;
	payload->accelerationStructureCount = accelerationStructureCount;
	payload->firstQuery = firstQuery;
	payload->sizes.reserve(accelerationStructureCount);
	for (unsigned i = 0; i < accelerationStructureCount; i++)
	{
		cVkAccelerationStructureKHR* acc = accelerationstructure_cast(pAccelerationStructures[i]);
		const uint64_t size = acc ? acc->memorySize : 1;
		payload->sizes.push_back(std::max<uint64_t>(size / 2, 1));
	}
	p->commands.back().payload = payload;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceAccelerationStructureCompatibilityKHR(
    VkDevice                                    device,
    const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
    VkAccelerationStructureCompatibilityKHR*    pCompatibility)
{
	ENTRY(vkGetDeviceAccelerationStructureCompatibilityKHR);
	CLOG("device=%p, pVersionInfo=%p, pCompatibility=%p", device, pVersionInfo, pCompatibility);

	*pCompatibility = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR;
}

VKAPI_ATTR void VKAPI_CALL vkGetAccelerationStructureBuildSizesKHR(
    VkDevice                                    device,
    VkAccelerationStructureBuildTypeKHR         buildType,
    const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
    const uint32_t*                             pMaxPrimitiveCounts,
    VkAccelerationStructureBuildSizesInfoKHR*   pSizeInfo)
{
	ENTRY(vkGetAccelerationStructureBuildSizesKHR);
	CLOG("device=%p, buildType=%u, pBuildInfo=%p, pMaxPrimitiveCounts=%p, pSizeInfo=%p", device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);

	// Provide a deterministic, non-zero size so tests exercising compaction have room to work.
	const uint64_t primitives = pMaxPrimitiveCounts ? pMaxPrimitiveCounts[0] : 1;
	const uint64_t base_size = std::max<uint64_t>(primitives * 1024ull, 4096ull);
	pSizeInfo->accelerationStructureSize = base_size;
	pSizeInfo->updateScratchSize = std::max<uint64_t>(base_size / 2, 1024ull);
	pSizeInfo->buildScratchSize = pSizeInfo->updateScratchSize;
}

// VK_KHR_ray_tracing_pipeline

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingShaderGroupHandlesKHR(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData)
{
	ENTRY(vkGetRayTracingShaderGroupHandlesKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysKHR(
    VkCommandBuffer                             commandBuffer,
    const VkStridedDeviceAddressRegionKHR*      pRaygenShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pMissShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pHitShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pCallableShaderBindingTable,
    uint32_t                                    width,
    uint32_t                                    height,
    uint32_t                                    depth)
{
	ENTRY(vkCmdTraceRaysKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRayTracingPipelinesKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkRayTracingPipelineCreateInfoKHR*    pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
	ENTRY(vkCreateRayTracingPipelinesKHR);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pipelineCache=" NHANDLE ", createInfoCount=%u, pCreateInfos=%p, pAllocator=%p, pPipelines=%p",
	     device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

	cVkDevice* dev = device_cast(device);
	cVkPipelineCache* cache = pipelinecache_cast(pipelineCache);
	for (unsigned i = 0; i < createInfoCount; i++)
	{
		cVkPipeline& p = owner_create<cVkPipeline, VkPipeline>(dev->pipelines, &pPipelines[i], pAllocator);
		if (cache)
		{
			cache->pipelines[&dev->pipelines.back()] = true;
			p.cache = cache;
		}
		p.flags = pCreateInfos[i].flags;
		p.stages.resize(pCreateInfos[i].stageCount);
		for (unsigned j = 0; j < pCreateInfos[i].stageCount; j++)
		{
			p.stages[j].flags = pCreateInfos[i].pStages[j].flags;
			p.stages[j].stage = pCreateInfos[i].pStages[j].stage;
			assert(pCreateInfos[i].pStages[j].stage != VK_SHADER_STAGE_ALL_GRAPHICS);
			assert(pCreateInfos[i].pStages[j].stage != VK_SHADER_STAGE_ALL);
			bool ownsModule = false;
			p.stages[j].module = get_pipeline_stage_module(dev, pCreateInfos[i].pStages[j], ownsModule);
			p.stages[j].ownsModule = ownsModule;
			if (p.stages[j].module)
			{
				p.stages[j].module->type = pCreateInfos[i].pStages[j].stage;
				p.stages[j].module->pipelines.push_back(p.uid);
			}
			if (pCreateInfos[i].pStages[j].pName)
			{
				p.stages[j].name = pCreateInfos[i].pStages[j].pName;
			}
			if (pCreateInfos[i].pStages[j].pSpecializationInfo)
			{
				if (pCreateInfos[i].pStages[j].pSpecializationInfo->pData)
				{
					p.stages[j].specializationData.resize(pCreateInfos[i].pStages[j].pSpecializationInfo->dataSize);
					memcpy(p.stages[j].specializationData.data(), pCreateInfos[i].pStages[j].pSpecializationInfo->pData,
					       pCreateInfos[i].pStages[j].pSpecializationInfo->dataSize);
				}
				for (unsigned k = 0; k < pCreateInfos[i].pStages[j].pSpecializationInfo->mapEntryCount; k++)
				{
					p.stages[j].specializationMap.push_back(pCreateInfos[i].pStages[j].pSpecializationInfo->pMapEntries[k]);
				}
			}
		}
		// TBD ray-tracing specific stuff (shaderGroups, libraries...)
		if (pCreateInfos[i].pDynamicState)
		{
			p.dynamicState.enabled = true;
			p.dynamicState.flags = pCreateInfos[i].pDynamicState->flags;
			for (unsigned k = 0; k < pCreateInfos[i].pDynamicState->dynamicStateCount; k++)
			{
				p.dynamicState.states.push_back(pCreateInfos[i].pDynamicState->pDynamicStates[k]);
			}
		}
		p.layout = pipelinelayout_cast(pCreateInfos[i].layout);
	}

	if (deferredOperation == VK_NULL_HANDLE)
	{
		return VK_SUCCESS;
	}
	else
	{
		return VK_OPERATION_NOT_DEFERRED_KHR;
	}
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetRayTracingCaptureReplayShaderGroupHandlesKHR(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData)
{
	ENTRY(vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirectKHR(
    VkCommandBuffer                             commandBuffer,
    const VkStridedDeviceAddressRegionKHR*      pRaygenShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pMissShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pHitShaderBindingTable,
    const VkStridedDeviceAddressRegionKHR*      pCallableShaderBindingTable,
    VkDeviceAddress                             indirectDeviceAddress)
{
	ENTRY(vkCmdTraceRaysIndirectKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetRayTracingShaderGroupStackSizeKHR(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    uint32_t                                    group,
    VkShaderGroupShaderKHR                      groupShader)
{
	ENTRY(vkGetRayTracingShaderGroupStackSizeKHR);
	TBD_UNSUPPORTED;
	return 0;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRayTracingPipelineStackSizeKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    pipelineStackSize)
{
	ENTRY(vkCmdSetRayTracingPipelineStackSizeKHR);
	TBD_UNSUPPORTED;
}

// VK_KHR_fragment_shading_rate

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceFragmentShadingRatesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pFragmentShadingRateCount,
    VkPhysicalDeviceFragmentShadingRateKHR*     pFragmentShadingRates)
{
	ENTRY(vkGetPhysicalDeviceFragmentShadingRatesKHR);
	CLOG("physicaldevice=%p, pFragmentShadingRateCount=%u, pFragmentShadingRates=%p", physicalDevice, (unsigned)*pFragmentShadingRateCount, pFragmentShadingRates);
	const VkStructureType t = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
	const VkPhysicalDeviceFragmentShadingRateKHR rates[] = {
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT, { 4, 4 } },
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT, { 4, 2 } },
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT, { 2, 4 } },
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT, { 2, 2 } },
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT, { 2, 1 } },
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT, { 1, 2 } },
		{ t, nullptr, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT | VK_SAMPLE_COUNT_16_BIT, { 1, 1 } },
	};

	VkResult res = VK_SUCCESS;
	if (pFragmentShadingRates == nullptr)
	{
		*pFragmentShadingRateCount = ARRAY_SIZE(rates);
		return VK_SUCCESS;
	}
	if (*pFragmentShadingRateCount < ARRAY_SIZE(rates)) res = VK_INCOMPLETE;
	for (unsigned i = 0; i < ARRAY_SIZE(rates); i++) if (*pFragmentShadingRateCount > i) pFragmentShadingRates[i] = rates[i];
	return res;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFragmentShadingRateKHR(
    VkCommandBuffer                             commandBuffer,
    const VkExtent2D*                           pFragmentSize,
    const VkFragmentShadingRateCombinerOpKHR    combinerOps[2])
{
	ENTRY(vkCmdSetFragmentShadingRateKHR);
	CMDLOG("commandBuffer=%p, pFragmentSize=(%u,%u), combinerOps=[%u, %u]", commandBuffer, (unsigned)pFragmentSize->width, (unsigned)pFragmentSize->height, (unsigned)combinerOps[0], (unsigned)combinerOps[1]);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindDescriptorSets, commandBuffer, MetricUnit(1, pFragmentSize->width, pFragmentSize->height));
}

// VK_EXT_line_rasterization

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    lineStippleFactor,
    uint16_t                                    lineStipplePattern)
{
	ENTRY(vkCmdSetLineStippleEXT);
	TBD_UNSUPPORTED;
}

// VK_EXT_extended_dynamic_state

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkCullModeFlags                             cullMode)
{
	ENTRY(vkCmdSetCullModeEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFaceEXT(
    VkCommandBuffer                             commandBuffer,
    VkFrontFace                                 frontFace)
{
	ENTRY(vkCmdSetFrontFaceEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopologyEXT(
    VkCommandBuffer                             commandBuffer,
    VkPrimitiveTopology                         primitiveTopology)
{
	ENTRY(vkCmdSetPrimitiveTopologyEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports)
{
	ENTRY(vkCmdSetViewportWithCountEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
	ENTRY(vkCmdSetScissorWithCountEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2EXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes,
    const VkDeviceSize*                         pStrides)
{
	ENTRY(vkCmdBindVertexBuffers2EXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthTestEnable)
{
	ENTRY(vkCmdSetDepthTestEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthWriteEnable)
{
	ENTRY(vkCmdSetDepthWriteEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOpEXT(
    VkCommandBuffer                             commandBuffer,
    VkCompareOp                                 depthCompareOp)
{
	ENTRY(vkCmdSetDepthCompareOpEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBoundsTestEnable)
{
	ENTRY(vkCmdSetDepthBoundsTestEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    stencilTestEnable)
{
	ENTRY(vkCmdSetStencilTestEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOpEXT(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    VkStencilOp                                 failOp,
    VkStencilOp                                 passOp,
    VkStencilOp                                 depthFailOp,
    VkCompareOp                                 compareOp)
{
	ENTRY(vkCmdSetStencilOpEXT);
	TBD_UNSUPPORTED;
}

// VK_KHR_copy_commands2

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyBufferInfo2KHR*                 pCopyBufferInfo)
{
	ENTRY(vkCmdCopyBuffer2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyImageInfo2KHR*                  pCopyImageInfo)
{
	ENTRY(vkCmdCopyImage2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyBufferToImageInfo2KHR*          pCopyBufferToImageInfo)
{
	ENTRY(vkCmdCopyBufferToImage2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyImageToBufferInfo2KHR*          pCopyImageToBufferInfo)
{
	ENTRY(vkCmdCopyImageToBuffer2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkBlitImageInfo2KHR*                  pBlitImageInfo)
{
	ENTRY(vkCmdBlitImage2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkResolveImageInfo2KHR*               pResolveImageInfo)
{
	ENTRY(vkCmdResolveImage2KHR);
	TBD_UNSUPPORTED;
}

// VK_KHR_deferred_host_operations

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDeferredOperationKHR(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator,
    VkDeferredOperationKHR*                     pDeferredOperation)
{
	ENTRY(vkCreateDeferredOperationKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDeferredOperationKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      operation,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDeferredOperationKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR uint32_t VKAPI_CALL vkGetDeferredOperationMaxConcurrencyKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      operation)
{
	ENTRY(vkGetDeferredOperationMaxConcurrencyKHR);
	TBD_UNSUPPORTED;
	return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeferredOperationResultKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      operation)
{
	ENTRY(vkGetDeferredOperationResultKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeferredOperationJoinKHR(
    VkDevice                                    device,
    VkDeferredOperationKHR                      operation)
{
	ENTRY(vkDeferredOperationJoinKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_pipeline_executable_properties

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutablePropertiesKHR(
    VkDevice                                    device,
    const VkPipelineInfoKHR*                    pPipelineInfo,
    uint32_t*                                   pExecutableCount,
    VkPipelineExecutablePropertiesKHR*          pProperties)
{
	ENTRY(vkGetPipelineExecutablePropertiesKHR);
	TBD_UNSUPPORTED;
	*pExecutableCount = 0;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableStatisticsKHR(
    VkDevice                                    device,
    const VkPipelineExecutableInfoKHR*          pExecutableInfo,
    uint32_t*                                   pStatisticCount,
    VkPipelineExecutableStatisticKHR*           pStatistics)
{
	ENTRY(vkGetPipelineExecutableStatisticsKHR);
	TBD_UNSUPPORTED;
	*pStatisticCount = 0;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableInternalRepresentationsKHR(
    VkDevice                                    device,
    const VkPipelineExecutableInfoKHR*          pExecutableInfo,
    uint32_t*                                   pInternalRepresentationCount,
    VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations)
{
	ENTRY(vkGetPipelineExecutableInternalRepresentationsKHR);
	TBD_UNSUPPORTED;
	*pInternalRepresentationCount = 0;
	return VK_SUCCESS;
}

// VK_EXT_private_data

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlotEXT(
    VkDevice                                    device,
    const VkPrivateDataSlotCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPrivateDataSlotEXT*                       pPrivateDataSlot)
{
	ENTRY(vkCreatePrivateDataSlotEXT);
	cVkDevice* dev = device_cast(device);
	cVkPrivateDataSlot& slot = owner_create<cVkPrivateDataSlot, VkPrivateDataSlot>(dev->slots, pPrivateDataSlot, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlotEXT(
    VkDevice                                    device,
    VkPrivateDataSlotEXT                        privateDataSlot,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyPrivateDataSlotEXT);
	destroy<cVkPrivateDataSlot, VkPrivateDataSlotEXT>(privateDataSlot, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateDataEXT(
    VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlotEXT                        privateDataSlot,
    uint64_t                                    data)
{
	ENTRY(vkSetPrivateDataEXT);
	cVkDevice* dev = device_cast(device);
	cVkPrivateDataSlot* slot = privatedataslot_cast(privateDataSlot);
	cVkBase* obj = (cVkBase*)objectHandle;
	obj->slots[privateDataSlot] = data;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPrivateDataEXT(
    VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlotEXT                        privateDataSlot,
    uint64_t*                                   pData)
{
	ENTRY(vkGetPrivateDataEXT);
	cVkDevice* dev = device_cast(device);
	cVkPrivateDataSlot* slot = privatedataslot_cast(privateDataSlot);
	cVkBase* obj = (cVkBase*)objectHandle;
	*pData = obj->slots.at(privateDataSlot);
}

// VK_EXT_tooling_info

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolPropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pToolCount,
    VkPhysicalDeviceToolPropertiesEXT*          pToolProperties)
{
	ENTRY(vkGetPhysicalDeviceToolPropertiesEXT);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	if (pToolProperties == nullptr) *pToolCount = 0;
	return VK_SUCCESS;
}

// VK_KHR_synchronization2

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2KHR(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    const VkDependencyInfoKHR*                  pDependencyInfo)
{
	ENTRY(vkCmdSetEvent2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2KHR(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags2KHR                    stageMask)
{
	ENTRY(vkCmdResetEvent2KHR);
	TBD_UNSUPPORTED;
	//p->maxStageFlags |= dstStageMask;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2KHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    const VkDependencyInfoKHR*                  pDependencyInfos)
{
	ENTRY(vkCmdWaitEvents2KHR);
	TBD_UNSUPPORTED;
}

static void internalCmdPipelineBarrier2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDependencyInfoKHR*                  pDependencyInfo)
{
	cVkCommandBuffer* p = commandbuffer_command(vkCmdPipelineBarrier2KHR, commandBuffer, MetricUnit(1, pDependencyInfo->memoryBarrierCount + pDependencyInfo->bufferMemoryBarrierCount + pDependencyInfo->imageMemoryBarrierCount));
	for (uint32_t i = 0; i < pDependencyInfo->memoryBarrierCount; i++)
	{
		p->maxStageFlags |= pDependencyInfo->pMemoryBarriers[i].srcStageMask;
		p->maxStageFlags |= pDependencyInfo->pMemoryBarriers[i].dstStageMask;
	}
	for (uint32_t i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++)
	{
		p->maxStageFlags |= pDependencyInfo->pBufferMemoryBarriers[i].srcStageMask;
		p->maxStageFlags |= pDependencyInfo->pBufferMemoryBarriers[i].dstStageMask;
	}
	for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++)
	{
		p->maxStageFlags |= pDependencyInfo->pImageMemoryBarriers[i].srcStageMask;
		p->maxStageFlags |= pDependencyInfo->pImageMemoryBarriers[i].dstStageMask;
	}
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDependencyInfoKHR*                  pDependencyInfo)
{
	ENTRY(vkCmdPipelineBarrier2KHR);
	internalCmdPipelineBarrier2KHR(commandBuffer, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2KHR(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags2KHR                    stage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	ENTRY(vkCmdWriteTimestamp2KHR);
	TBD_UNSUPPORTED;
	//p->maxStageFlags |= stage;
}

VkResult internalQueueSubmit2(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo2KHR*                     pSubmits,
    VkFence                                     fence)
{
	ENTRY(vkQueueSubmit2KHR);
	CLOG("queue=%p, submitCount=%u, pSubmits=%p, fence=" NHANDLE, queue, submitCount, pSubmits, fence);
	cVkQueue* q = queue_cast(queue);

	for (unsigned i = 0; i < submitCount; i++)
	{
		VkTimelineSemaphoreSubmitInfo* timeline_semaphore = (VkTimelineSemaphoreSubmitInfo*)find_extension(&pSubmits[i], VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
		XLOG("%u: %u wait semaphores, %u commandbuffers, %u signal semaphores (timeline=%s)", i, pSubmits[i].waitSemaphoreInfoCount, pSubmits[i].commandBufferInfoCount,
		     pSubmits[i].signalSemaphoreInfoCount, timeline_semaphore ? "yes" : "no");

		if (timeline_semaphore)
		{
			assert(timeline_semaphore->signalSemaphoreValueCount == pSubmits[i].signalSemaphoreInfoCount);
			assert(timeline_semaphore->waitSemaphoreValueCount == pSubmits[i].waitSemaphoreInfoCount);
		}

		// Wait execution start
		for (unsigned j = 0; j < pSubmits[i].waitSemaphoreInfoCount; j++)
		{
			cVkSemaphore* c = semaphore_cast(pSubmits[i].pWaitSemaphoreInfos[j].semaphore);
			// no need to wait - everything here is immediate!
			update_stageflag_usage(q, pSubmits[i].pWaitSemaphoreInfos[j].stageMask);
		}

		for (unsigned j = 0; j < pSubmits[i].commandBufferInfoCount; j++)
		{
			internalQueueSubmit(q, pSubmits[i].pCommandBufferInfos[j].commandBuffer);
		}

		// Signal execution complete
		for (unsigned j = 0; j < pSubmits[i].signalSemaphoreInfoCount; j++)
		{
			cVkSemaphore* c = semaphore_cast(pSubmits[i].pSignalSemaphoreInfos[j].semaphore);
			// here we would signal it, but no need to think about this yet
			update_stageflag_usage(q, pSubmits[i].pSignalSemaphoreInfos[j].stageMask);
			if (timeline_semaphore)
			{
				assert(c->type == VK_SEMAPHORE_TYPE_TIMELINE);
				c->value = timeline_semaphore->pSignalSemaphoreValues[j];
			}
			else
			{
				assert(c->type == VK_SEMAPHORE_TYPE_BINARY);
				c->value = 1;
			}
		}
	}

	// If user sent in a fence, lift it to signal that we're done executing the queue now
	if (fence != VK_NULL_HANDLE)
	{
		fence_cast(fence)->signalled = true;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo2KHR*                     pSubmits,
    VkFence                                     fence)
{
	ENTRY(vkQueueSubmit2KHR);
	return internalQueueSubmit2(queue, submitCount, pSubmits, fence);
}

// VK_EXT_vertex_input_dynamic_state

VKAPI_ATTR void VKAPI_CALL vkCmdSetVertexInputEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexBindingDescriptionCount,
    const VkVertexInputBindingDescription2EXT*  pVertexBindingDescriptions,
    uint32_t                                    vertexAttributeDescriptionCount,
    const VkVertexInputAttributeDescription2EXT* pVertexAttributeDescriptions)
{
	ENTRY(vkCmdSetVertexInputEXT);
	TBD_UNSUPPORTED;
}

// VK_KHR_present_wait

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresentKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint64_t                                    presentId,
    uint64_t                                    timeout)
{
	ENTRY(vkWaitForPresentKHR);
	cVkDevice* dev = device_cast(device);
	cVkSwapchainKHR* chain = swapchain_cast(swapchain);
	// we have no work to wait on
	return VK_SUCCESS;
}

// VK_EXT_extended_dynamic_state2

VKAPI_ATTR void VKAPI_CALL vkCmdSetPatchControlPointsEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    patchControlPoints)
{
	ENTRY(vkCmdSetPatchControlPointsEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    rasterizerDiscardEnable)
{
	ENTRY(vkCmdSetRasterizerDiscardEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBiasEnable)
{
	ENTRY(vkCmdSetDepthBiasEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEXT(
    VkCommandBuffer                             commandBuffer,
    VkLogicOp                                   logicOp)
{
	ENTRY(vkCmdSetLogicOpEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    primitiveRestartEnable)
{
	ENTRY(vkCmdSetPrimitiveRestartEnableEXT);
	TBD_UNSUPPORTED;
}

// VK_EXT_acquire_drm_display

VKAPI_ATTR VkResult VKAPI_CALL vkAcquireDrmDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    int32_t                                     drmFd,
    VkDisplayKHR                                display)
{
	ENTRY(vkAcquireDrmDisplayEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDrmDisplayEXT(
    VkPhysicalDevice                            physicalDevice,
    int32_t                                     drmFd,
    uint32_t                                    connectorId,
    VkDisplayKHR*                               display)
{
	ENTRY(vkGetDrmDisplayEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_EXT_multi_draw

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawInfoEXT*                   pVertexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawMultiEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMultiIndexedEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawIndexedInfoEXT*            pIndexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride,
    const int32_t*                              pVertexOffset)
{
	ENTRY(vkCmdDrawMultiIndexedEXT);
	TBD_UNSUPPORTED;
}

// VK_EXT_pageable_device_local_memory

VKAPI_ATTR void VKAPI_CALL vkSetDeviceMemoryPriorityEXT(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    float                                       priority)
{
	ENTRY(vkSetDeviceMemoryPriorityEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

// VK_EXT_color_write_enable

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteEnableEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    attachmentCount,
    const VkBool32*                             pColorWriteEnables)
{
	ENTRY(vkCmdSetColorWriteEnableEXT);
	TBD_UNSUPPORTED;
}

// VK_VERSION_1_3

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceToolProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pToolCount,
    VkPhysicalDeviceToolProperties*             pToolProperties)
{
	ENTRY(vkGetPhysicalDeviceToolProperties);
	cVkPhysicalDevice* device = physicaldevice_cast(physicalDevice);
	if (pToolProperties == nullptr) *pToolCount = 0;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePrivateDataSlot(
    VkDevice                                    device,
    const VkPrivateDataSlotCreateInfo*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPrivateDataSlot*                          pPrivateDataSlot)
{
	ENTRY(vkCreatePrivateDataSlot);
	cVkDevice* dev = device_cast(device);
	cVkPrivateDataSlot& slot = owner_create<cVkPrivateDataSlot, VkPrivateDataSlot>(dev->slots, pPrivateDataSlot, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPrivateDataSlot(
    VkDevice                                    device,
    VkPrivateDataSlot                           privateDataSlot,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyPrivateDataSlot);
	cVkDevice* dev = device_cast(device);
	destroy<cVkPrivateDataSlot, VkPrivateDataSlot>(privateDataSlot, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetPrivateData(
    VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlot                           privateDataSlot,
    uint64_t                                    data)
{
	ENTRY(vkSetPrivateData);
	cVkDevice* dev = device_cast(device);
	cVkPrivateDataSlot* slot = privatedataslot_cast(privateDataSlot);
	cVkBase* obj = (cVkBase*)objectHandle;
	obj->slots[privateDataSlot] = data;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPrivateData(
    VkDevice                                    device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlot                           privateDataSlot,
    uint64_t*                                   pData)
{
	ENTRY(vkGetPrivateData);
	cVkDevice* dev = device_cast(device);
	cVkPrivateDataSlot* slot = privatedataslot_cast(privateDataSlot);
	cVkBase* obj = (cVkBase*)objectHandle;
	*pData = obj->slots.at(privateDataSlot);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    const VkDependencyInfo*                     pDependencyInfo)
{
	ENTRY(vkCmdSetEvent2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags2                       stageMask)
{
	ENTRY(vkCmdResetEvent2);
	TBD_UNSUPPORTED;
	//p->maxStageFlags |= stageMask;
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    const VkDependencyInfo*                     pDependencyInfos)
{
	ENTRY(vkCmdWaitEvents2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(
    VkCommandBuffer                             commandBuffer,
    const VkDependencyInfo*                     pDependencyInfo)
{
	ENTRY(vkCmdPipelineBarrier2);
	internalCmdPipelineBarrier2KHR(commandBuffer, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags2                       stage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	ENTRY(vkCmdWriteTimestamp2);
	TBD_UNSUPPORTED;
	//p->maxStageFlags |= stage;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo2*                        pSubmits,
    VkFence                                     fence)
{
	ENTRY(vkQueueSubmit2);
	return internalQueueSubmit2(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyBufferInfo2*                    pCopyBufferInfo)
{
	ENTRY(vkCmdCopyBuffer2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyImageInfo2*                     pCopyImageInfo)
{
	ENTRY(vkCmdCopyImage2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyBufferToImageInfo2*             pCopyBufferToImageInfo)
{
	ENTRY(vkCmdCopyBufferToImage2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(
    VkCommandBuffer                             commandBuffer,
    const VkCopyImageToBufferInfo2*             pCopyImageToBufferInfo)
{
	ENTRY(vkCmdCopyImageToBuffer2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(
    VkCommandBuffer                             commandBuffer,
    const VkBlitImageInfo2*                     pBlitImageInfo)
{
	ENTRY(vkCmdBlitImage2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(
    VkCommandBuffer                             commandBuffer,
    const VkResolveImageInfo2*                  pResolveImageInfo)
{
	ENTRY(vkCmdResolveImage2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(
    VkCommandBuffer                             commandBuffer,
    const VkRenderingInfo*                      pRenderingInfo)
{
	ENTRY(vkCmdBeginRendering);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdEndRendering);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(
    VkCommandBuffer                             commandBuffer,
    VkCullModeFlags                             cullMode)
{
	ENTRY(vkCmdSetCullMode);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(
    VkCommandBuffer                             commandBuffer,
    VkFrontFace                                 frontFace)
{
	ENTRY(vkCmdSetFrontFace);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(
    VkCommandBuffer                             commandBuffer,
    VkPrimitiveTopology                         primitiveTopology)
{
	ENTRY(vkCmdSetPrimitiveTopology);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports)
{
	ENTRY(vkCmdSetViewportWithCount);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
	ENTRY(vkCmdSetScissorWithCount);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers2(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes,
    const VkDeviceSize*                         pStrides)
{
	ENTRY(vkCmdBindVertexBuffers2);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthTestEnable)
{
	ENTRY(vkCmdSetDepthTestEnable);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthWriteEnable)
{
	ENTRY(vkCmdSetDepthWriteEnable);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(
    VkCommandBuffer                             commandBuffer,
    VkCompareOp                                 depthCompareOp)
{
	ENTRY(vkCmdSetDepthCompareOp);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBoundsTestEnable)
{
	ENTRY(vkCmdSetDepthBoundsTestEnable);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    stencilTestEnable)
{
	ENTRY(vkCmdSetStencilTestEnable);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    VkStencilOp                                 failOp,
    VkStencilOp                                 passOp,
    VkStencilOp                                 depthFailOp,
    VkCompareOp                                 compareOp)
{
	ENTRY(vkCmdSetStencilOp);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(
    VkDevice                                    device,
    const VkDeviceBufferMemoryRequirements*     pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetDeviceBufferMemoryRequirements);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.size = pInfo->pCreateInfo->size;
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports every memory type for now
	pMemoryRequirements->memoryRequirements.alignment = sizeof(void*);
	VkMemoryDedicatedRequirements* mdr = (VkMemoryDedicatedRequirements*)find_extension(pMemoryRequirements, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
	if (mdr)
	{
		mdr->prefersDedicatedAllocation = VK_FALSE;
		mdr->requiresDedicatedAllocation = VK_FALSE;
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(
    VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetDeviceImageMemoryRequirements);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports everything
	pMemoryRequirements->memoryRequirements.alignment = sizeof(void*);
	pMemoryRequirements->memoryRequirements.size = pInfo->pCreateInfo->extent.width * pInfo->pCreateInfo->extent.height * pInfo->pCreateInfo->extent.depth * std::max(1u, pInfo->pCreateInfo->arrayLayers)
		* std::max(1u, pInfo->pCreateInfo->mipLevels) * 4 * 4; // return worst case
	VkMemoryDedicatedRequirements* mdr = (VkMemoryDedicatedRequirements*)find_extension(pMemoryRequirements, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
	if (mdr)
	{
		mdr->prefersDedicatedAllocation = VK_FALSE;
		mdr->requiresDedicatedAllocation = VK_FALSE;
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(
    VkDevice                                    device,
    const VkDeviceImageMemoryRequirements*      pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements)
{
	ENTRY(vkGetDeviceImageSparseMemoryRequirements);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    rasterizerDiscardEnable)
{
	ENTRY(vkCmdSetRasterizerDiscardEnable);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBiasEnable)
{
	ENTRY(vkCmdSetDepthBiasEnable);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    primitiveRestartEnable)
{
	ENTRY(vkCmdSetPrimitiveRestartEnable);
	TBD_UNSUPPORTED;
}

// VK_KHR_dynamic_rendering

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderingKHR(
    VkCommandBuffer                             commandBuffer,
    const VkRenderingInfo*                      pRenderingInfo)
{
	ENTRY(vkCmdBeginRenderingKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderingKHR(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdEndRenderingKHR);
	TBD_UNSUPPORTED;
}

// VK_EXT_descriptor_buffer

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBuffersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    bufferCount,
    const VkDescriptorBufferBindingInfoEXT*     pBindingInfos)
{
	ENTRY(vkCmdBindDescriptorBuffersEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsetsEXT(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    setCount,
    const uint32_t*                             pBufferIndices,
    const VkDeviceSize*                         pOffsets)
{
	ENTRY(vkCmdSetDescriptorBufferOffsetsEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplersEXT(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set)
{
	ENTRY(vkCmdBindDescriptorBufferEmbeddedSamplersEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutSizeEXT(
    VkDevice                                    device,
    VkDescriptorSetLayout                       layout,
    VkDeviceSize*                               pLayoutSizeInBytes)
{
	ENTRY(vkGetDescriptorSetLayoutSizeEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorSetLayoutBindingOffsetEXT(
    VkDevice                                    device,
    VkDescriptorSetLayout                       layout,
    uint32_t                                    binding,
    VkDeviceSize*                               pOffset)
{
	ENTRY(vkGetDescriptorSetLayoutBindingOffsetEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetDescriptorEXT(
    VkDevice                                    device,
    const VkDescriptorGetInfoEXT*               pDescriptorInfo,
    size_t                                      dataSize,
    void*                                       pDescriptor)
{
	ENTRY(vkGetDescriptorEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetBufferOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    device,
    const VkBufferCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
	ENTRY(vkGetBufferOpaqueCaptureDescriptorDataEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    device,
    const VkImageCaptureDescriptorDataInfoEXT*  pInfo,
    void*                                       pData)
{
	ENTRY(vkGetImageOpaqueCaptureDescriptorDataEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageViewOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    device,
    const VkImageViewCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
	ENTRY(vkGetImageViewOpaqueCaptureDescriptorDataEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSamplerOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    device,
    const VkSamplerCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
	ENTRY(vkGetSamplerOpaqueCaptureDescriptorDataEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    device,
    const VkAccelerationStructureCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
	ENTRY(vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_EXT_device_fault

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultInfoEXT(
    VkDevice                                    device,
    VkDeviceFaultCountsEXT*                     pFaultCounts,
    VkDeviceFaultInfoEXT*                       pFaultInfo)
{
	ENTRY(vkGetDeviceFaultInfoEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_device_fault

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultReportsKHR(
    VkDevice                                    device,
    uint64_t                                    timeout,
    uint32_t*                                   pFaultCounts,
    VkDeviceFaultInfoKHR*                       pFaultInfo)
{
	ENTRY(vkGetDeviceFaultReportsKHR);
	cVkDevice* cdevice = device_cast(device);
	(void)timeout;
	TBD_UNSUPPORTED;
	if (pFaultCounts)
	{
		*pFaultCounts = 0;
	}
	if (pFaultInfo)
	{
		memset(pFaultInfo, 0, sizeof(*pFaultInfo));
		pFaultInfo->sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceFaultDebugInfoKHR(
    VkDevice                                    device,
    VkDeviceFaultDebugInfoKHR*                  pDebugInfo)
{
	ENTRY(vkGetDeviceFaultDebugInfoKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	if (pDebugInfo)
	{
		pDebugInfo->vendorBinarySize = 0;
	}
	return VK_SUCCESS;
}

// VK_EXT_image_compression_control

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2EXT(
    VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource2EXT*               pSubresource,
    VkSubresourceLayout2EXT*                    pLayout)
{
	ENTRY(vkGetImageSubresourceLayout2EXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

// VK_EXT_pipeline_properties

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelinePropertiesEXT(
    VkDevice                                    device,
    const VkPipelineInfoEXT*                    pPipelineInfo,
    VkBaseOutStructure*                         pPipelineProperties)
{
	ENTRY(vkGetPipelinePropertiesEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_map_memory2

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2KHR(
    VkDevice                                    device,
    const VkMemoryMapInfoKHR*                   pMemoryMapInfo,
    void**                                      ppData)
{
	ENTRY(vkMapMemory2KHR);
	CLOG("device=%p, memory=" NHANDLE ", offset=%llu, size=%llu, flags=%u, ppData=%p", device, pMemoryMapInfo->memory,
	     (unsigned long long)pMemoryMapInfo->offset, (unsigned long long)pMemoryMapInfo->size, pMemoryMapInfo->flags, ppData);

	cVkDevice* dev = device_cast(device);
	cVkDeviceMemory* mem = devicememory_cast(pMemoryMapInfo->memory);
	*ppData = mem->ptr + pMemoryMapInfo->offset;
	mem->mapped = true;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2KHR(
    VkDevice                                    device,
    const VkMemoryUnmapInfoKHR*                 pMemoryUnmapInfo)
{
	ENTRY(vkUnmapMemory2KHR);
	CLOG("device=%p, memory=" NHANDLE, device, pMemoryUnmapInfo->memory);
	cVkDevice* dev = device_cast(device);
	cVkDeviceMemory* mem = devicememory_cast(pMemoryUnmapInfo->memory);
	mem->mapped = false;
	return VK_SUCCESS;
}

// VK_EXT_opacity_micromap

VKAPI_ATTR void VKAPI_CALL vkCmdBuildMicromapsEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkMicromapBuildInfoEXT*               pInfos)
{
	ENTRY(vkCmdBuildMicromapsEXT);
	CLOG("commandBuffer=%p, infoCount=%u, pInfos=%p", commandBuffer, infoCount, pInfos);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBuildMicromapsEXT, commandBuffer, MetricUnit(1, infoCount));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapEXT(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMicromapInfoEXT*                pInfo)
{
	ENTRY(vkCmdCopyMicromapEXT);
	CLOG("commandBuffer=%p, pInfo=%p", commandBuffer, pInfo);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyMicromapEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMicromapToMemoryEXT(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMicromapToMemoryInfoEXT*        pInfo)
{
	ENTRY(vkCmdCopyMicromapToMemoryEXT);
	CLOG("commandBuffer=%p, pInfo=%p", commandBuffer, pInfo);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyMicromapToMemoryEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToMicromapEXT(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToMicromapInfoEXT*        pInfo)
{
	ENTRY(vkCmdCopyMemoryToMicromapEXT);
	CLOG("commandBuffer=%p, pInfo=%p", commandBuffer, pInfo);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyMemoryToMicromapEXT, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteMicromapsPropertiesEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    micromapCount,
    const VkMicromapEXT*                        pMicromaps,
    VkQueryType                                 queryType,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery)
{
	ENTRY(vkCmdWriteMicromapsPropertiesEXT);
	CLOG("commandBuffer=%p, micromapCount=%u, pMicromaps=%p, queryType=%u, queryPool=" NHANDLE ", firstQuery=%u", commandBuffer, micromapCount, pMicromaps, queryType, queryPool, firstQuery);
	
	cVkCommandBuffer* p = commandbuffer_command(vkCmdWriteMicromapsPropertiesEXT, commandBuffer, MetricUnit(1, micromapCount));
	cVkQueryPool* qp = querypool_cast(queryPool);
	p->commands.back().bindings.push_back(qp);
	cVkPayloadWriteMicromapsPropertiesEXT* payload = new cVkPayloadWriteMicromapsPropertiesEXT;
	payload->micromapCount = micromapCount;
	payload->firstQuery = firstQuery;
	p->commands.back().payload = payload;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateMicromapEXT(
    VkDevice                                    device,
    const VkMicromapCreateInfoEXT*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkMicromapEXT*                              pMicromap)
{
	ENTRY(vkCreateMicromapEXT);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pMicromap=%p", device, pCreateInfo, pAllocator, pMicromap);

	cVkDevice* dev = device_cast(device);
	cVkMicromapEXT& micromap = owner_create<cVkMicromapEXT, VkMicromapEXT>(dev->micromaps, pMicromap, pAllocator);

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyMicromapEXT(
    VkDevice                                    device,
    VkMicromapEXT                               micromap,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyMicromapEXT);
	CLOG("device=%p, micromap=" NHANDLE ", pAllocator=%p", device, micromap, pAllocator);

	destroy<cVkMicromapEXT, VkMicromapEXT>(micromap, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkBuildMicromapsEXT(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    uint32_t                                    infoCount,
    const VkMicromapBuildInfoEXT*               pInfos)
{
	ENTRY(vkBuildMicromapsEXT);
	CLOG("device=%p, deferredOperation=" NHANDLE ", infoCount=%u, pInfos=%p", device, deferredOperation, infoCount, pInfos);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapEXT(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMicromapInfoEXT*                pInfo)
{
	ENTRY(vkCopyMicromapEXT);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pInfo=%p", device, deferredOperation, pInfo);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMicromapToMemoryEXT(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMicromapToMemoryInfoEXT*        pInfo)
{
	ENTRY(vkCopyMicromapToMemoryEXT);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pInfo=%p", device, deferredOperation, pInfo);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToMicromapEXT(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    const VkCopyMemoryToMicromapInfoEXT*        pInfo)
{
	ENTRY(vkCopyMemoryToMicromapEXT);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pInfo=%p", device, deferredOperation, pInfo);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteMicromapsPropertiesEXT(
    VkDevice                                    device,
    uint32_t                                    micromapCount,
    const VkMicromapEXT*                        pMicromaps,
    VkQueryType                                 queryType,
    size_t                                      dataSize,
    void*                                       pData,
    size_t                                      stride)
{
	ENTRY(vkWriteMicromapsPropertiesEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMicromapCompatibilityEXT(
    VkDevice                                    device,
    const VkMicromapVersionInfoEXT*             pVersionInfo,
    VkAccelerationStructureCompatibilityKHR*    pCompatibility)
{
	ENTRY(vkGetDeviceMicromapCompatibilityEXT);
	CLOG("device=%p, pVersionInfo=%p, pCompatibility=%p", device, pVersionInfo, pCompatibility);

	*pCompatibility = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR;
}

VKAPI_ATTR void VKAPI_CALL vkGetMicromapBuildSizesEXT(
    VkDevice                                    device,
    VkAccelerationStructureBuildTypeKHR         buildType,
    const VkMicromapBuildInfoEXT*               pBuildInfo,
    VkMicromapBuildSizesInfoEXT*                pSizeInfo)
{
	ENTRY(vkGetMicromapBuildSizesEXT);
	CLOG("device=%p, buildType=%u, pBuildInfo=%p, pSizeInfo=%p", device, buildType, pBuildInfo, pSizeInfo);

	pSizeInfo->micromapSize = 1;
	pSizeInfo->buildScratchSize = 1;
	pSizeInfo->discardable = VK_TRUE;
}

// VK_KHR_video_queue

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkVideoProfileInfoKHR*                pVideoProfile,
    VkVideoCapabilitiesKHR*                     pCapabilities)
{
	ENTRY(vkGetPhysicalDeviceVideoCapabilitiesKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoFormatPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceVideoFormatInfoKHR*   pVideoFormatInfo,
    uint32_t*                                   pVideoFormatPropertyCount,
    VkVideoFormatPropertiesKHR*                 pVideoFormatProperties)
{
	ENTRY(vkGetPhysicalDeviceVideoFormatPropertiesKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateVideoSessionKHR(
    VkDevice                                    device,
    const VkVideoSessionCreateInfoKHR*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkVideoSessionKHR*                          pVideoSession)
{
	ENTRY(vkCreateVideoSessionKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyVideoSessionKHR(
    VkDevice                                    device,
    VkVideoSessionKHR                           videoSession,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyVideoSessionKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetVideoSessionMemoryRequirementsKHR(
    VkDevice                                    device,
    VkVideoSessionKHR                           videoSession,
    uint32_t*                                   pMemoryRequirementsCount,
    VkVideoSessionMemoryRequirementsKHR*        pMemoryRequirements)
{
	ENTRY(vkGetVideoSessionMemoryRequirementsKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindVideoSessionMemoryKHR(
    VkDevice                                    device,
    VkVideoSessionKHR                           videoSession,
    uint32_t                                    bindSessionMemoryInfoCount,
    const VkBindVideoSessionMemoryInfoKHR*      pBindSessionMemoryInfos)
{
	ENTRY(vkBindVideoSessionMemoryKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateVideoSessionParametersKHR(
    VkDevice                                    device,
    const VkVideoSessionParametersCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkVideoSessionParametersKHR*                pVideoSessionParameters)
{
	ENTRY(vkCreateVideoSessionParametersKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkUpdateVideoSessionParametersKHR(
    VkDevice                                    device,
    VkVideoSessionParametersKHR                 videoSessionParameters,
    const VkVideoSessionParametersUpdateInfoKHR* pUpdateInfo)
{
	ENTRY(vkUpdateVideoSessionParametersKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyVideoSessionParametersKHR(
    VkDevice                                    device,
    VkVideoSessionParametersKHR                 videoSessionParameters,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyVideoSessionParametersKHR);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginVideoCodingKHR(
    VkCommandBuffer                             commandBuffer,
    const VkVideoBeginCodingInfoKHR*            pBeginInfo)
{
	ENTRY(vkCmdBeginVideoCodingKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndVideoCodingKHR(
    VkCommandBuffer                             commandBuffer,
    const VkVideoEndCodingInfoKHR*              pEndCodingInfo)
{
	ENTRY(vkCmdEndVideoCodingKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdControlVideoCodingKHR(
    VkCommandBuffer                             commandBuffer,
    const VkVideoCodingControlInfoKHR*          pCodingControlInfo)
{
	ENTRY(vkCmdControlVideoCodingKHR);
	TBD_UNSUPPORTED;
}

// VK_EXT_swapchain_maintenance1

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseSwapchainImagesEXT(
    VkDevice                                    device,
    const VkReleaseSwapchainImagesInfoEXT*      pReleaseInfo)
{
	ENTRY(vkReleaseSwapchainImagesEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_EXT_shader_module_identifier

VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleIdentifierEXT(
    VkDevice                                    device,
    VkShaderModule                              shaderModule,
    VkShaderModuleIdentifierEXT*                pIdentifier)
{
	ENTRY(vkGetShaderModuleIdentifierEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetShaderModuleCreateInfoIdentifierEXT(
    VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    VkShaderModuleIdentifierEXT*                pIdentifier)
{
	ENTRY(vkGetShaderModuleCreateInfoIdentifierEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

// VK_EXT_shader_object

VKAPI_ATTR void VKAPI_CALL vkCmdBindShadersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    stageCount,
    const VkShaderStageFlagBits*                pStages,
    const VkShaderEXT*                          pShaders)
{
	ENTRY(vkCmdBindShadersEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShadersEXT(
    VkDevice                                    device,
    uint32_t                                    createInfoCount,
    const VkShaderCreateInfoEXT*                pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderEXT*                                pShaders)
{
	ENTRY(vkCreateShadersEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderEXT(
    VkDevice                                    device,
    VkShaderEXT                                 shader,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyShaderEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderBinaryDataEXT(
    VkDevice                                    device,
    VkShaderEXT                                 shader,
    size_t*                                     pDataSize,
    void*                                       pData)
{
	ENTRY(vkGetShaderBinaryDataEXT);
	cVkDevice* dev = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_cooperative_matrix

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkCooperativeMatrixPropertiesKHR*           pProperties)
{
	ENTRY(vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	(void)cphysicalDevice;

	if (!pPropertyCount)
		return VK_ERROR_INITIALIZATION_FAILED;

	const uint32_t available = 1;
	if (!pProperties)
	{
		*pPropertyCount = available;
		return VK_SUCCESS;
	}

	uint32_t count = std::min(*pPropertyCount, available);
	if (count > 0)
	{
		pProperties[0].sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
		pProperties[0].pNext = nullptr;
		pProperties[0].MSize = 16;
		pProperties[0].NSize = 16;
		pProperties[0].KSize = 16;
		pProperties[0].AType = VK_COMPONENT_TYPE_FLOAT16_KHR;
		pProperties[0].BType = VK_COMPONENT_TYPE_FLOAT16_KHR;
		pProperties[0].CType = VK_COMPONENT_TYPE_FLOAT32_KHR;
		pProperties[0].ResultType = VK_COMPONENT_TYPE_FLOAT32_KHR;
		pProperties[0].saturatingAccumulation = VK_FALSE;
		pProperties[0].scope = VK_SCOPE_SUBGROUP_KHR;
	}
	*pPropertyCount = count;
	return VK_SUCCESS;
}

// VK_KHR_ray_tracing_maintenance1

VKAPI_ATTR void VKAPI_CALL vkCmdTraceRaysIndirect2KHR(
    VkCommandBuffer                             commandBuffer,
    VkDeviceAddress                             indirectDeviceAddress)
{
	ENTRY(vkCmdTraceRaysIndirect2KHR);
	TBD_UNSUPPORTED;
}

// VK_EXT_mesh_shader

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ)
{
	ENTRY(vkCmdDrawMeshTasksEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectEXT(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawMeshTasksIndirectEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCountEXT(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDrawMeshTasksIndirectCountEXT);
	TBD_UNSUPPORTED;
}

// VK_EXT_extended_dynamic_state3

VKAPI_ATTR void VKAPI_CALL vkCmdSetTessellationDomainOriginEXT(
    VkCommandBuffer                             commandBuffer,
    VkTessellationDomainOrigin                  domainOrigin)
{
	ENTRY(vkCmdSetTessellationDomainOriginEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthClampEnable)
{
	ENTRY(vkCmdSetDepthClampEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPolygonModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkPolygonMode                               polygonMode)
{
	ENTRY(vkCmdSetPolygonModeEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationSamplesEXT(
    VkCommandBuffer                             commandBuffer,
    VkSampleCountFlagBits                       rasterizationSamples)
{
	ENTRY(vkCmdSetRasterizationSamplesEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleMaskEXT(
    VkCommandBuffer                             commandBuffer,
    VkSampleCountFlagBits                       samples,
    const VkSampleMask*                         pSampleMask)
{
	ENTRY(vkCmdSetSampleMaskEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToCoverageEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    alphaToCoverageEnable)
{
	ENTRY(vkCmdSetAlphaToCoverageEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetAlphaToOneEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    alphaToOneEnable)
{
	ENTRY(vkCmdSetAlphaToOneEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLogicOpEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    logicOpEnable)
{
	ENTRY(vkCmdSetLogicOpEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEnableEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkBool32*                             pColorBlendEnables)
{
	ENTRY(vkCmdSetColorBlendEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendEquationEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkColorBlendEquationEXT*              pColorBlendEquations)
{
	ENTRY(vkCmdSetColorBlendEquationEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorWriteMaskEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkColorComponentFlags*                pColorWriteMasks)
{
	ENTRY(vkCmdSetColorWriteMaskEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizationStreamEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    rasterizationStream)
{
	ENTRY(vkCmdSetRasterizationStreamEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetConservativeRasterizationModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkConservativeRasterizationModeEXT          conservativeRasterizationMode)
{
	ENTRY(vkCmdSetConservativeRasterizationModeEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetExtraPrimitiveOverestimationSizeEXT(
    VkCommandBuffer                             commandBuffer,
    float                                       extraPrimitiveOverestimationSize)
{
	ENTRY(vkCmdSetExtraPrimitiveOverestimationSizeEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthClipEnable)
{
	ENTRY(vkCmdSetDepthClipEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetSampleLocationsEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    sampleLocationsEnable)
{
	ENTRY(vkCmdSetSampleLocationsEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetColorBlendAdvancedEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstAttachment,
    uint32_t                                    attachmentCount,
    const VkColorBlendAdvancedEXT*              pColorBlendAdvanced)
{
	ENTRY(vkCmdSetColorBlendAdvancedEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetProvokingVertexModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkProvokingVertexModeEXT                    provokingVertexMode)
{
	ENTRY(vkCmdSetProvokingVertexModeEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineRasterizationModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkLineRasterizationModeEXT                  lineRasterizationMode)
{
	ENTRY(vkCmdSetLineRasterizationModeEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    stippledLineEnable)
{
	ENTRY(vkCmdSetLineStippleEnableEXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClipNegativeOneToOneEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    negativeOneToOne)
{
	ENTRY(vkCmdSetDepthClipNegativeOneToOneEXT);
	TBD_UNSUPPORTED;
}

// VK_KHR_video_decode_queue

VKAPI_ATTR void VKAPI_CALL vkCmdDecodeVideoKHR(
    VkCommandBuffer                             commandBuffer,
    const VkVideoDecodeInfoKHR*                 pDecodeInfo)
{
	ENTRY(vkCmdDecodeVideoKHR);
	TBD_UNSUPPORTED;
}

// VK_EXT_depth_bias_control

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBias2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkDepthBiasInfoEXT*                   pDepthBiasInfo)
{
	ENTRY(vkCmdSetDepthBias2EXT);
	TBD_UNSUPPORTED;
}

// VK_EXT_attachment_feedback_loop_dynamic_state

VKAPI_ATTR void VKAPI_CALL vkCmdSetAttachmentFeedbackLoopEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkImageAspectFlags                          aspectMask)
{
	ENTRY(vkCmdSetAttachmentFeedbackLoopEnableEXT);
	TBD_UNSUPPORTED;
}

// VK_ARM_tensors

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceExternalTensorPropertiesARM(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalTensorInfoARM* pExternalTensorInfo, VkExternalTensorPropertiesARM* pExternalTensorProperties)
{
	ENTRY(vkGetPhysicalDeviceExternalTensorPropertiesARM);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateTensorARM(
    VkDevice                                    device,
    const VkTensorCreateInfoARM*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkTensorARM*                                pTensor)
{
	ENTRY(vkCreateTensorARM);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pTensor=%p", device, pCreateInfo, pAllocator, pTensor);
	cVkDevice* dev = device_cast(device);
	cVkTensor& p = owner_create<cVkTensor, VkTensorARM>(dev->tensors, pTensor, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyTensorARM(
    VkDevice                                    device,
    VkTensorARM                                 tensor,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyTensorARM);
	CLOG("device=%p, tensor=" NHANDLE ", pAllocator=%p", device, tensor, pAllocator);
	cVkDevice* dev = device_cast(device);
	destroy<cVkTensor, VkTensorARM>(tensor, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateTensorViewARM(
    VkDevice                                    device,
    const VkTensorViewCreateInfoARM*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkTensorViewARM*                            pView)
{
	ENTRY(vkCreateTensorViewARM);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pView=%p", device, pCreateInfo, pAllocator, pView);
	cVkDevice* dev = device_cast(device);
	cVkTensorView& p = owner_create<cVkTensorView, VkTensorViewARM>(dev->tensorviews, pView, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyTensorViewARM(
    VkDevice                                    device,
    VkTensorViewARM                             tensorView,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyTensorViewARM);
	CLOG("device=%p, tensorView=" NHANDLE ", pAllocator=%p", device, tensorView, pAllocator);
	cVkDevice* dev = device_cast(device);
	destroy<cVkTensorView, VkTensorViewARM>(tensorView, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkGetTensorMemoryRequirementsARM(
    VkDevice                                    device,
    const VkTensorMemoryRequirementsInfoARM*    pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetTensorMemoryRequirementsARM);
	CLOG("device=%p, pInfo=%p, pMemoryRequirements=%p", device, pInfo, pMemoryRequirements);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.size = 1024; // TBD fake values for now
	pMemoryRequirements->memoryRequirements.alignment = 64;
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports every memory type for now
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindTensorMemoryARM(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindTensorMemoryInfoARM*            pBindInfos)
{
	ENTRY(vkBindTensorMemoryARM);
	CLOG("device=%p, bindInfoCount=%u, pBindInfos=%p", device, (unsigned)bindInfoCount, pBindInfos);
	cVkDevice* dev = device_cast(device);
	for (uint32_t i = 0; i < bindInfoCount; i++)
	{
		cVkTensor* ct = tensor_cast(pBindInfos[i].tensor);
		ct->memory = devicememory_cast(pBindInfos[i].memory);
		ct->memoryOffset = pBindInfos[i].memoryOffset;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceTensorMemoryRequirementsARM(
    VkDevice                                    device,
    const VkDeviceTensorMemoryRequirementsARM*  pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetDeviceTensorMemoryRequirementsARM);
	CLOG("device=%p, pInfo=%p, pMemoryRequirements=%p", device, pInfo, pMemoryRequirements);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.size = 1024; // TBD fake values for now
	pMemoryRequirements->memoryRequirements.alignment = 64;
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports every memory type for now
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyTensorARM(
    VkCommandBuffer                             commandBuffer,
     const VkCopyTensorInfoARM*                 pCopyTensorInfo)
{
	ENTRY(vkCmdCopyTensorARM);
	CMDLOG("commandBuffer=%p, pCopyTensorInfo=%p", commandBuffer, pCopyTensorInfo);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyTensorARM, commandBuffer, MetricUnit(1, pCopyTensorInfo->regionCount));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDescriptorDataARM(
    VkDevice                                    device,
    const VkTensorCaptureDescriptorDataInfoARM* pInfo,
    void*                                       pData)
{
	ENTRY(vkGetTensorOpaqueCaptureDescriptorDataARM);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorViewOpaqueCaptureDescriptorDataARM(
    VkDevice                                    device,
    const VkTensorViewCaptureDescriptorDataInfoARM* pInfo,
    void*                                       pData)
{
	ENTRY(vkGetTensorViewOpaqueCaptureDescriptorDataARM);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_ARM_data_graph

VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelineAvailablePropertiesARM(VkDevice device, const VkDataGraphPipelineInfoARM* pPipelineInfo, uint32_t* pPropertiesCount, VkDataGraphPipelinePropertyARM* pProperties)
{
	ENTRY(vkGetDataGraphPipelineAvailablePropertiesARM);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, uint32_t* pQueueFamilyDataGraphPropertyCount, VkQueueFamilyDataGraphPropertiesARM* pQueueFamilyDataGraphProperties)
{
	ENTRY(vkGetPhysicalDeviceQueueFamilyDataGraphPropertiesARM);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceQueueFamilyDataGraphProcessingEngineInfoARM* pQueueFamilyDataGraphProcessingEngineInfo, VkQueueFamilyDataGraphProcessingEnginePropertiesARM* pQueueFamilyDataGraphProcessingEngineProperties)
{
	ENTRY(vkGetPhysicalDeviceQueueFamilyDataGraphProcessingEnginePropertiesARM);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDataGraphPipelinesARM(
    VkDevice                                    device,
    VkDeferredOperationKHR                      deferredOperation,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkDataGraphPipelineCreateInfoARM*     pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
	ENTRY(vkCreateDataGraphPipelinesARM);
	CLOG("device=%p, deferredOperation=" NHANDLE ", pipelineCache=" NHANDLE ", createInfoCount=%u, pCreateInfos=%p, pAllocator=%p, pPipelines=%p",
			device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);

	cVkDevice* dev = device_cast(device);
	for (unsigned i = 0; i < createInfoCount; i++)
	{
		cVkPipeline& pipeline = owner_create<cVkPipeline, VkPipeline>(dev->pipelines, pPipelines + i, pAllocator);
	}

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDataGraphPipelineSessionARM(
    VkDevice                                    device,
    const VkDataGraphPipelineSessionCreateInfoARM* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDataGraphPipelineSessionARM*              pSession)
{
	ENTRY(vkCreateDataGraphPipelineSessionARM);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pSession=%p", device, pCreateInfo, pAllocator, pSession);

	cVkDevice* dev = device_cast(device);
	owner_create<cVkDataGraphPipelineSession, VkDataGraphPipelineSessionARM>(dev->dataGraphPipelineSessions, pSession, pAllocator);

	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelineSessionBindPointRequirementsARM(
    VkDevice                                    device,
    const VkDataGraphPipelineSessionBindPointRequirementsInfoARM* pInfo,
    uint32_t*                                   pBindPointRequirementCount,
    VkDataGraphPipelineSessionBindPointRequirementARM* pBindPointRequirements)
{
	ENTRY(vkGetDataGraphPipelineSessionBindPointRequirementsARM);
	CLOG("device=%p, pInfo=%p, pBindPointRequirementCount=%p, pBindPointRequirements=%p", device, pInfo, pBindPointRequirementCount, pBindPointRequirements);

	if (pBindPointRequirements)
	{
		*pBindPointRequirementCount = std::min<uint32_t>(*pBindPointRequirementCount, 1);
		if (*pBindPointRequirementCount >= 1)
		{
			pBindPointRequirements[0].bindPoint = VK_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_TRANSIENT_ARM;
			pBindPointRequirements[0].bindPointType = VK_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_TYPE_MEMORY_ARM;
			pBindPointRequirements[0].numObjects = 1;
		}
	}
	else
	{
		*pBindPointRequirementCount = 1;
	}

	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetDataGraphPipelineSessionMemoryRequirementsARM(
    VkDevice                                    device,
    const VkDataGraphPipelineSessionMemoryRequirementsInfoARM* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
	ENTRY(vkGetDataGraphPipelineSessionMemoryRequirementsARM);
	CLOG("device=%p, pInfo=%p, pMemoryRequirements=%p", device, pInfo, pMemoryRequirements);
	cVkDevice* dev = device_cast(device);
	pMemoryRequirements->memoryRequirements.size = 1024; // TBD fake values for now
	pMemoryRequirements->memoryRequirements.alignment = 64;
	pMemoryRequirements->memoryRequirements.memoryTypeBits = dev->memoryTypeBits; // supports every memory type for now
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindDataGraphPipelineSessionMemoryARM(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindDataGraphPipelineSessionMemoryInfoARM* pBindInfos)
{
	ENTRY(vkBindDataGraphPipelineSessionMemoryARM);
	CLOG("device=%p, bindInfoCount=%u, pBindInfos=%p", device, bindInfoCount, pBindInfos);
	cVkDevice* dev = device_cast(device);
	for (uint32_t i = 0; i < bindInfoCount; i++)
	{
		cVkDataGraphPipelineSession* session = datagraphpipelinesession_cast(pBindInfos[i].session);
		switch (pBindInfos[i].bindPoint)
		{
			case VK_DATA_GRAPH_PIPELINE_SESSION_BIND_POINT_TRANSIENT_ARM:
				session->transientMemory = devicememory_cast(pBindInfos[i].memory);
				session->transientMemoryOffset = pBindInfos[i].memoryOffset;
				break;
			default:
				assert(false);
				break;
		}
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDataGraphPipelineSessionARM(
    VkDevice                                    device,
    VkDataGraphPipelineSessionARM               session,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyDataGraphPipelineSessionARM);
	CLOG("device=%p, session=" NHANDLE ", pAllocator=%p", device, session, pAllocator);
	destroy<cVkDataGraphPipelineSession, VkDataGraphPipelineSessionARM>(session, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchDataGraphARM(
    VkCommandBuffer                             commandBuffer,
    VkDataGraphPipelineSessionARM               session,
    const VkDataGraphPipelineDispatchInfoARM*   pInfo)
{
	ENTRY(vkCmdDispatchDataGraphARM);
	CMDLOG("commandBuffer=%p, session=" NHANDLE, commandBuffer, session);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdDispatchDataGraphARM, commandBuffer, MetricUnit(1));
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetDataGraphPipelinePropertiesARM(
    VkDevice                                    device,
    const VkDataGraphPipelineInfoARM*           pPipelineInfo,
    uint32_t                                    propertiesCount,
    VkDataGraphPipelinePropertyQueryResultARM*  pProperties)
{
	ENTRY(vkGetDataGraphPipelinePropertiesARM);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_calibrated_timestamps

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pTimeDomainCount,
    VkTimeDomainKHR*                            pTimeDomains)
{
	ENTRY(vkGetPhysicalDeviceCalibrateableTimeDomainsKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetCalibratedTimestampsKHR(
    VkDevice                                    device,
    uint32_t                                    timestampCount,
    const VkCalibratedTimestampInfoKHR*         pTimestampInfos,
    uint64_t*                                   pTimestamps,
    uint64_t*                                   pMaxDeviation)
{
	ENTRY(vkGetCalibratedTimestampsKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_line_rasterization

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStippleKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    lineStippleFactor,
    uint16_t                                    lineStipplePattern)
{
	ENTRY(vkCmdSetLineStippleKHR);
	TBD_UNSUPPORTED;
}

// VK_KHR_maintenance5

static void commonCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType)
{
	CMDLOG("commandBuffer=%p, buffer=" NHANDLE ", offset=%llu, size=%llu, indexType=%u",
	       commandBuffer, buffer, (unsigned long long)offset, (unsigned long long)size, indexType);

	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindIndexBuffer2, commandBuffer, MetricUnit(1));
	p->commands.back().bindings.push_back(buffer_cast(buffer));
}

static void commonGetImageSubresourceLayout2(VkDevice device, VkImage image, const VkImageSubresource2* pSubresource, VkSubresourceLayout2* pLayout)
{
	cVkDevice* cdevice = device_cast(device);
	cVkImage* cimage = image_cast(image);
	(void)cdevice;
	(void)pSubresource;

	if (!pLayout)
		return;

	pLayout->subresourceLayout.offset = cimage->memoryOffset;
	if (cimage->memory)
	{
		pLayout->subresourceLayout.size = cimage->memory->allocationSize;
	}
	else
	{
		pLayout->subresourceLayout.size = cimage->extent.width * cimage->extent.height * cimage->extent.depth * 4 * 4;
	}
	pLayout->subresourceLayout.rowPitch = 1;
	pLayout->subresourceLayout.arrayPitch = 1;
	pLayout->subresourceLayout.depthPitch = 1;
}

static cVkBuffer* find_buffer_by_device_address(cVkDevice* cdevice, VkDeviceAddress address)
{
	for (cVkBuffer& buffer : cdevice->buffers)
	{
		if (!buffer.memory || !buffer.memory->ptr)
		{
			continue;
		}
		VkDeviceAddress base = reinterpret_cast<VkDeviceAddress>(buffer.memory->ptr + buffer.memoryOffset);
		VkDeviceAddress end = base + buffer.size;
		if (address >= base && address < end)
		{
			return &buffer;
		}
	}
	return nullptr;
}

static void commonGetDeviceImageSubresourceLayout(VkDevice device, const VkDeviceImageSubresourceInfo* pInfo, VkSubresourceLayout2* pLayout)
{
	cVkDevice* cdevice = device_cast(device);
	(void)cdevice;

	if (!pLayout || !pInfo || !pInfo->pCreateInfo)
		return;

	const VkExtent3D& extent = pInfo->pCreateInfo->extent;
	pLayout->subresourceLayout.offset = 0;
	pLayout->subresourceLayout.size = extent.width * extent.height * extent.depth * 4 * 4;
	pLayout->subresourceLayout.rowPitch = 1;
	pLayout->subresourceLayout.arrayPitch = 1;
	pLayout->subresourceLayout.depthPitch = 1;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2KHR(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkIndexType                                 indexType)
{
	ENTRY(vkCmdBindIndexBuffer2KHR);
	commonCmdBindIndexBuffer2(commandBuffer, buffer, offset, size, indexType);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer3KHR(
    VkCommandBuffer                             commandBuffer,
    const VkBindIndexBuffer3InfoKHR*            pInfo)
{
	ENTRY(vkCmdBindIndexBuffer3KHR);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	(void)ccommandBuffer;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers3KHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBindVertexBuffer3InfoKHR*           pBindingInfos)
{
	ENTRY(vkCmdBindVertexBuffers3KHR);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	(void)firstBinding;
	(void)bindingCount;
	(void)pBindingInfos;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirect2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDrawIndirect2InfoKHR*               pInfo)
{
	ENTRY(vkCmdDrawIndirect2KHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndirect2KHR, commandBuffer, MetricUnit(1, pInfo ? pInfo->drawCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirect2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDrawIndirect2InfoKHR*               pInfo)
{
	ENTRY(vkCmdDrawIndexedIndirect2KHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndexedIndirect2KHR, commandBuffer, MetricUnit(1, pInfo ? pInfo->drawCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDispatchIndirect2InfoKHR*           pInfo)
{
	ENTRY(vkCmdDispatchIndirect2KHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDispatchIndirect2KHR, commandBuffer, MetricUnit(1));
	(void)pInfo;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyDeviceMemoryInfoKHR*            pCopyMemoryInfo)
{
	ENTRY(vkCmdCopyMemoryKHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyMemoryKHR, commandBuffer, MetricUnit(1, pCopyMemoryInfo ? pCopyMemoryInfo->regionCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyDeviceMemoryImageInfoKHR*       pCopyMemoryInfo)
{
	ENTRY(vkCmdCopyMemoryToImageKHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyMemoryToImageKHR, commandBuffer, MetricUnit(1, pCopyMemoryInfo ? pCopyMemoryInfo->regionCount : 0));
	if (pCopyMemoryInfo)
	{
		p->commands.back().bindings.push_back(image_cast(pCopyMemoryInfo->image));
	}
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToMemoryKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyDeviceMemoryImageInfoKHR*       pCopyMemoryInfo)
{
	ENTRY(vkCmdCopyImageToMemoryKHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyImageToMemoryKHR, commandBuffer, MetricUnit(1, pCopyMemoryInfo ? pCopyMemoryInfo->regionCount : 0));
	if (pCopyMemoryInfo)
	{
		p->commands.back().bindings.push_back(image_cast(pCopyMemoryInfo->image));
	}
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdUpdateMemoryKHR(
    VkCommandBuffer                             commandBuffer,
    const VkDeviceAddressRangeKHR*              pDstRange,
    VkAddressCommandFlagsKHR                    dstFlags,
    VkDeviceSize                                dataSize,
    const void*                                 pData)
{
	ENTRY(vkCmdUpdateMemoryKHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdUpdateMemoryKHR, commandBuffer, MetricUnit(1));
	(void)pDstRange;
	(void)dstFlags;
	(void)dataSize;
	(void)pData;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdFillMemoryKHR(
    VkCommandBuffer                             commandBuffer,
    const VkDeviceAddressRangeKHR*              pDstRange,
    VkAddressCommandFlagsKHR                    dstFlags,
    uint32_t                                    data)
{
	ENTRY(vkCmdFillMemoryKHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdFillMemoryKHR, commandBuffer, MetricUnit(1));
	(void)pDstRange;
	(void)dstFlags;
	(void)data;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResultsToMemoryKHR(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    const VkStridedDeviceAddressRangeKHR*       pDstRange,
    VkAddressCommandFlagsKHR                    dstFlags,
    VkQueryResultFlags                          queryResultFlags)
{
	ENTRY(vkCmdCopyQueryPoolResultsToMemoryKHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdCopyQueryPoolResultsToMemoryKHR, commandBuffer, MetricUnit(1, queryCount));
	p->commands.back().bindings.push_back(querypool_cast(queryPool));
	(void)firstQuery;
	(void)pDstRange;
	(void)dstFlags;
	(void)queryResultFlags;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCount2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDrawIndirectCount2InfoKHR*          pInfo)
{
	ENTRY(vkCmdDrawIndirectCount2KHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndirectCount2KHR, commandBuffer, MetricUnit(1, pInfo ? pInfo->maxDrawCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCount2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkDrawIndirectCount2InfoKHR*          pInfo)
{
	ENTRY(vkCmdDrawIndexedIndirectCount2KHR);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndexedIndirectCount2KHR, commandBuffer, MetricUnit(1, pInfo ? pInfo->maxDrawCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginConditionalRendering2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkConditionalRenderingBeginInfo2EXT*  pConditionalRenderingBegin)
{
	ENTRY(vkCmdBeginConditionalRendering2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginConditionalRendering2EXT, commandBuffer, MetricUnit(1));
	(void)pConditionalRenderingBegin;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindTransformFeedbackBuffers2EXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBindTransformFeedbackBuffer2InfoEXT* pBindingInfos)
{
	ENTRY(vkCmdBindTransformFeedbackBuffers2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBindTransformFeedbackBuffers2EXT, commandBuffer, MetricUnit(1, bindingCount));
	(void)firstBinding;
	(void)pBindingInfos;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginTransformFeedback2EXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterRange,
    uint32_t                                    counterRangeCount,
    const VkBindTransformFeedbackBuffer2InfoEXT* pCounterInfos)
{
	ENTRY(vkCmdBeginTransformFeedback2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdBeginTransformFeedback2EXT, commandBuffer, MetricUnit(1, counterRangeCount));
	(void)firstCounterRange;
	(void)pCounterInfos;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndTransformFeedback2EXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterRange,
    uint32_t                                    counterRangeCount,
    const VkBindTransformFeedbackBuffer2InfoEXT* pCounterInfos)
{
	ENTRY(vkCmdEndTransformFeedback2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdEndTransformFeedback2EXT, commandBuffer, MetricUnit(1, counterRangeCount));
	(void)firstCounterRange;
	(void)pCounterInfos;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectByteCount2EXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    const VkBindTransformFeedbackBuffer2InfoEXT* pCounterInfo,
    uint32_t                                    counterOffset,
    uint32_t                                    vertexStride)
{
	ENTRY(vkCmdDrawIndirectByteCount2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawIndirectByteCount2EXT, commandBuffer, MetricUnit(1));
	(void)instanceCount;
	(void)firstInstance;
	(void)pCounterInfo;
	(void)counterOffset;
	(void)vertexStride;
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirect2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkDrawIndirect2InfoKHR*               pInfo)
{
	ENTRY(vkCmdDrawMeshTasksIndirect2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawMeshTasksIndirect2EXT, commandBuffer, MetricUnit(1, pInfo ? pInfo->drawCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawMeshTasksIndirectCount2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkDrawIndirectCount2InfoKHR*          pInfo)
{
	ENTRY(vkCmdDrawMeshTasksIndirectCount2EXT);
	cVkCommandBuffer* p = commandbuffer_command(vkCmdDrawMeshTasksIndirectCount2EXT, commandBuffer, MetricUnit(1, pInfo ? pInfo->maxDrawCount : 0));
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateAccelerationStructure2KHR(
    VkDevice                                    device,
    const VkAccelerationStructureCreateInfo2KHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkAccelerationStructureKHR*                 pAccelerationStructure)
{
	ENTRY(vkCreateAccelerationStructure2KHR);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pAccelerationStructure=%p", device, pCreateInfo, pAllocator, pAccelerationStructure);

	cVkDevice* cdevice = device_cast(device);
	cVkAccelerationStructureKHR& acc = owner_create<cVkAccelerationStructureKHR, VkAccelerationStructureKHR>(cdevice->accelerationStructures, pAccelerationStructure, pAllocator);
	acc.flags = pCreateInfo->createFlags;
	acc.type = pCreateInfo->type;
	acc.memorySize = pCreateInfo->addressRange.size;
	cVkBuffer* backing = find_buffer_by_device_address(cdevice, pCreateInfo->addressRange.address);
	if (backing)
	{
		acc.buffer = backing;
		VkDeviceAddress base = reinterpret_cast<VkDeviceAddress>(backing->memory->ptr + backing->memoryOffset);
		acc.memoryOffset = pCreateInfo->addressRange.address - base;
	}
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularityKHR(
    VkDevice                                    device,
    const VkRenderingAreaInfoKHR*               pRenderingAreaInfo,
    VkExtent2D*                                 pGranularity)
{
	ENTRY(vkGetRenderingAreaGranularityKHR);
	cVkDevice* cdevice = device_cast(device);
	if (pGranularity)
	{
		pGranularity->width = 1;
		pGranularity->height = 1;
	}
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2KHR(
    VkDevice                                    device,
    VkImage                                     image,
    const VkImageSubresource2KHR*               pSubresource,
    VkSubresourceLayout2KHR*                    pLayout)
{
	ENTRY(vkGetImageSubresourceLayout2KHR);
	commonGetImageSubresourceLayout2(device, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayoutKHR(
    VkDevice                                    device,
    const VkDeviceImageSubresourceInfoKHR*      pInfo,
    VkSubresourceLayout2KHR*                    pLayout)
{
	ENTRY(vkGetDeviceImageSubresourceLayoutKHR);
	commonGetDeviceImageSubresourceLayout(device, pInfo, pLayout);
}

// VK_EXT_host_image_copy

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImageEXT(
    VkDevice                                    device,
    const VkCopyMemoryToImageInfoEXT*           pCopyMemoryToImageInfo)
{
	ENTRY(vkCopyMemoryToImageEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemoryEXT(
    VkDevice                                    device,
    const VkCopyImageToMemoryInfoEXT*           pCopyImageToMemoryInfo)
{
	ENTRY(vkCopyImageToMemoryEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImageEXT(
    VkDevice                                    device,
    const VkCopyImageToImageInfoEXT*            pCopyImageToImageInfo)
{
	ENTRY(vkCopyImageToImageEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayoutEXT(
    VkDevice                                    device,
    uint32_t                                    transitionCount,
    const VkHostImageLayoutTransitionInfoEXT*   pTransitions)
{
	ENTRY(vkTransitionImageLayoutEXT);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_video_encode_queue

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR* pQualityLevelInfo,
    VkVideoEncodeQualityLevelPropertiesKHR*     pQualityLevelProperties)
{
	ENTRY(vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEncodedVideoSessionParametersKHR(
    VkDevice                                    device,
    const VkVideoEncodeSessionParametersGetInfoKHR* pVideoSessionParametersInfo,
    VkVideoEncodeSessionParametersFeedbackInfoKHR* pFeedbackInfo,
    size_t*                                     pDataSize,
    void*                                       pData)
{
	ENTRY(vkGetEncodedVideoSessionParametersKHR);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEncodeVideoKHR(
    VkCommandBuffer                             commandBuffer,
    const VkVideoEncodeInfoKHR*                 pEncodeInfo)
{
	ENTRY(vkCmdEncodeVideoKHR);
	TBD_UNSUPPORTED;
}

// VK_KHR_maintenance6

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorSetsInfoKHR*          pBindDescriptorSetsInfo)
{
	ENTRY(vkCmdBindDescriptorSets2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkPushConstantsInfoKHR*               pPushConstantsInfo)
{
	ENTRY(vkCmdPushConstants2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetInfoKHR*           pPushDescriptorSetInfo)
{
	ENTRY(vkCmdPushDescriptorSet2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetWithTemplateInfoKHR* pPushDescriptorSetWithTemplateInfo)
{
	ENTRY(vkCmdPushDescriptorSetWithTemplate2KHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDescriptorBufferOffsets2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkSetDescriptorBufferOffsetsInfoEXT*  pSetDescriptorBufferOffsetsInfo)
{
	ENTRY(vkCmdSetDescriptorBufferOffsets2EXT);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorBufferEmbeddedSamplers2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo)
{
	ENTRY(vkCmdBindDescriptorBufferEmbeddedSamplers2EXT);
	TBD_UNSUPPORTED;
}

// VK_KHR_dynamic_rendering_local_read

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocationsKHR(
    VkCommandBuffer                             commandBuffer,
    const VkRenderingAttachmentLocationInfoKHR* pLocationInfo)
{
	ENTRY(vkCmdSetRenderingAttachmentLocationsKHR);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndicesKHR(
    VkCommandBuffer                             commandBuffer,
    const VkRenderingInputAttachmentIndexInfoKHR* pLocationInfo)
{
	ENTRY(vkCmdSetRenderingInputAttachmentIndicesKHR);
	TBD_UNSUPPORTED;
}

// VK_KHR_pipeline_binary

VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineBinariesKHR(VkDevice device, const VkPipelineBinaryCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineBinaryHandlesInfoKHR* pBinaries)
{
	ENTRY(vkCreatePipelineBinariesKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineBinaryKHR(VkDevice device, VkPipelineBinaryKHR pipelineBinary, const VkAllocationCallbacks* pAllocator)
{
	ENTRY(vkDestroyPipelineBinaryKHR);
	cVkDevice* cdevice = device_cast(device);
	cVkPipelineBinary* cpipelineBinary = pipelinebinary_cast(pipelineBinary);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineKeyKHR(VkDevice device, const VkPipelineCreateInfoKHR* pPipelineCreateInfo, VkPipelineBinaryKeyKHR* pPipelineKey)
{
	ENTRY(vkGetPipelineKeyKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineBinaryDataKHR(VkDevice device, const VkPipelineBinaryDataInfoKHR* pInfo, VkPipelineBinaryKeyKHR* pPipelineBinaryKey, size_t* pPipelineBinaryDataSize, void* pPipelineBinaryData)
{
	ENTRY(vkGetPipelineBinaryDataKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseCapturedPipelineDataKHR(VkDevice device, const VkReleaseCapturedPipelineDataInfoKHR* pInfo, const VkAllocationCallbacks* pAllocator)
{
	ENTRY(vkReleaseCapturedPipelineDataKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_EXT_directfb_surface

#ifdef VK_USE_PLATFORM_DIRECTFB_EXT
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDirectFBSurfaceEXT(VkInstance instance, const VkDirectFBSurfaceCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
	ENTRY(vkCreateDirectFBSurfaceEXT);
	cVkInstance* cinstance = instance_cast(instance);
	//NEED HANDLING: eg cVkTensorView& p = owner_create<cVkTensorView, VkTensorViewARM>(dev->tensorviews, pView, pAllocator);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_DIRECTFB_EXT

#ifdef VK_USE_PLATFORM_DIRECTFB_EXT
VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceDirectFBPresentationSupportEXT(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, IDirectFB* dfb)
{
	ENTRY(vkGetPhysicalDeviceDirectFBPresentationSupportEXT);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	TBD_UNSUPPORTED;
	return ??;
}
#endif // VK_USE_PLATFORM_DIRECTFB_EXT

// VK_EXT_device_generated_commands

VKAPI_ATTR void VKAPI_CALL vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo)
{
	ENTRY(vkCmdExecuteGeneratedCommandsEXT);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo, VkCommandBuffer stateCommandBuffer)
{
	ENTRY(vkCmdPreprocessGeneratedCommandsEXT);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	cVkCommandBuffer* cstateCommandBuffer = commandbuffer_cast(stateCommandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetGeneratedCommandsMemoryRequirementsEXT(VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoEXT* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
	ENTRY(vkGetGeneratedCommandsMemoryRequirementsEXT);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutEXT(VkDevice device, const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectCommandsLayoutEXT* pIndirectCommandsLayout)
{
	ENTRY(vkCreateIndirectCommandsLayoutEXT);
	cVkDevice* cdevice = device_cast(device);
	//NEED HANDLING: eg cVkTensorView& p = owner_create<cVkTensorView, VkTensorViewARM>(dev->tensorviews, pView, pAllocator);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutEXT(VkDevice device, VkIndirectCommandsLayoutEXT indirectCommandsLayout, const VkAllocationCallbacks* pAllocator)
{
	ENTRY(vkDestroyIndirectCommandsLayoutEXT);
	cVkDevice* cdevice = device_cast(device);
	//cVkIndirectCommandsLayoutEXT& cindirectCommandsLayout = _cast(indirectCommandsLayout);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectExecutionSetEXT(VkDevice device, const VkIndirectExecutionSetCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectExecutionSetEXT* pIndirectExecutionSet)
{
	ENTRY(vkCreateIndirectExecutionSetEXT);
	cVkDevice* cdevice = device_cast(device);
	//NEED HANDLING: eg cVkTensorView& p = owner_create<cVkTensorView, VkTensorViewARM>(dev->tensorviews, pView, pAllocator);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectExecutionSetEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, const VkAllocationCallbacks* pAllocator)
{
	ENTRY(vkDestroyIndirectExecutionSetEXT);
	cVkDevice* cdevice = device_cast(device);
	//cVkIndirectExecutionSetEXT& cindirectExecutionSet = name_cast(indirectExecutionSet);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetPipelineEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetPipelineEXT* pExecutionSetWrites)
{
	ENTRY(vkUpdateIndirectExecutionSetPipelineEXT);
	cVkDevice* cdevice = device_cast(device);
	//cVkIndirectExecutionSetEXT& cindirectExecutionSet = name_cast(indirectExecutionSet);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkUpdateIndirectExecutionSetShaderEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetShaderEXT* pExecutionSetWrites)
{
	ENTRY(vkUpdateIndirectExecutionSetShaderEXT);
	cVkDevice* cdevice = device_cast(device);
	//cVkIndirectExecutionSetEXT& cindirectExecutionSet = name_cast(indirectExecutionSet);
	TBD_UNSUPPORTED;
}

// Version 1.4

VKAPI_ATTR void VKAPI_CALL vkCmdSetLineStipple(VkCommandBuffer commandBuffer, uint32_t lineStippleFactor, uint16_t lineStipplePattern)
{
	ENTRY(vkCmdSetLineStipple);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, VkIndexType indexType)
{
	ENTRY(vkCmdBindIndexBuffer2);
	commonCmdBindIndexBuffer2(commandBuffer, buffer, offset, size, indexType);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImage(VkDevice device, const VkCopyMemoryToImageInfo* pCopyMemoryToImageInfo)
{
	ENTRY(vkCopyMemoryToImage);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemory(VkDevice device, const VkCopyImageToMemoryInfo* pCopyImageToMemoryInfo)
{
	ENTRY(vkCopyImageToMemory);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(VkDevice device, const VkCopyImageToImageInfo* pCopyImageToImageInfo)
{
	ENTRY(vkCopyImageToImage);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayout(VkDevice device, uint32_t transitionCount, const VkHostImageLayoutTransitionInfo* pTransitions)
{
	ENTRY(vkTransitionImageLayout);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2(VkDevice device, VkImage image, const VkImageSubresource2* pSubresource, VkSubresourceLayout2* pLayout)
{
	ENTRY(vkGetImageSubresourceLayout2);
	commonGetImageSubresourceLayout2(device, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayout(VkDevice device, const VkDeviceImageSubresourceInfo* pInfo, VkSubresourceLayout2* pLayout)
{
	ENTRY(vkGetDeviceImageSubresourceLayout);
	commonGetDeviceImageSubresourceLayout(device, pInfo, pLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(VkDevice device, const VkMemoryMapInfo* pMemoryMapInfo, void** ppData)
{
	ENTRY(vkMapMemory2);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2(VkDevice device, const VkMemoryUnmapInfo* pMemoryUnmapInfo)
{
	ENTRY(vkUnmapMemory2);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets2(VkCommandBuffer commandBuffer, const VkBindDescriptorSetsInfo* pBindDescriptorSetsInfo)
{
	ENTRY(vkCmdBindDescriptorSets2);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants2(VkCommandBuffer commandBuffer, const VkPushConstantsInfo* pPushConstantsInfo)
{
	ENTRY(vkCmdPushConstants2);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetInfo* pPushDescriptorSetInfo)
{
	ENTRY(vkCmdPushDescriptorSet2);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2(VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo* pPushDescriptorSetWithTemplateInfo)
{
	ENTRY(vkCmdPushDescriptorSetWithTemplate2);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingAttachmentLocations(VkCommandBuffer commandBuffer, const VkRenderingAttachmentLocationInfo* pLocationInfo)
{
	ENTRY(vkCmdSetRenderingAttachmentLocations);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRenderingInputAttachmentIndices(VkCommandBuffer commandBuffer, const VkRenderingInputAttachmentIndexInfo* pInputAttachmentIndexInfo)
{
	ENTRY(vkCmdSetRenderingInputAttachmentIndices);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkGetRenderingAreaGranularity(VkDevice device, const VkRenderingAreaInfo* pRenderingAreaInfo, VkExtent2D* pGranularity)
{
	ENTRY(vkGetRenderingAreaGranularity);
	cVkDevice* cdevice = device_cast(device);
	if (pGranularity)
	{
		pGranularity->width = 1;
		pGranularity->height = 1;
	}
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
{
	ENTRY(vkCmdPushDescriptorSet);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	cVkPipelineLayout* clayout = pipelinelayout_cast(layout);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData)
{
	ENTRY(vkCmdPushDescriptorSetWithTemplate);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	//cVkDescriptorUpdateTemplate& cdescriptorUpdateTemplate = name_cast(descriptorUpdateTemplate);
	cVkPipelineLayout* clayout = pipelinelayout_cast(layout);
	TBD_UNSUPPORTED;
}

// VK_KHR_external_fence_win32

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceWin32HandleKHR(VkDevice device, const VkFenceGetWin32HandleInfoKHR* pGetWin32HandleInfo, HANDLE* pHandle)
{
	ENTRY(vkGetFenceWin32HandleKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkImportFenceWin32HandleKHR(VkDevice device, const VkImportFenceWin32HandleInfoKHR* pImportFenceWin32HandleInfo)
{
	ENTRY(vkImportFenceWin32HandleKHR);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_WIN32_KHR

// VK_EXT_metal_surface

#ifdef VK_USE_PLATFORM_METAL_EXT
VKAPI_ATTR VkResult VKAPI_CALL vkCreateMetalSurfaceEXT(VkInstance instance, const VkMetalSurfaceCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
	ENTRY(vkCreateMetalSurfaceEXT);
	cVkInstance* cinstance = instance_cast(instance);
	//NEED HANDLING: eg cVkTensorView& p = owner_create<cVkTensorView, VkTensorViewARM>(dev->tensorviews, pView, pAllocator);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_METAL_EXT

// VK_EXT_full_screen_exclusive

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModes2EXT(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes)
{
	ENTRY(vkGetPhysicalDeviceSurfacePresentModes2EXT);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkGetDeviceGroupSurfacePresentModes2EXT(VkDevice device, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkDeviceGroupPresentModeFlagsKHR* pModes)
{
	ENTRY(vkGetDeviceGroupSurfacePresentModes2EXT);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain)
{
	ENTRY(vkAcquireFullScreenExclusiveModeEXT);
	cVkDevice* cdevice = device_cast(device);
	cVkSwapchainKHR* cswapchain = swapchain_cast(swapchain);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL vkReleaseFullScreenExclusiveModeEXT(VkDevice device, VkSwapchainKHR swapchain)
{
	ENTRY(vkReleaseFullScreenExclusiveModeEXT);
	cVkDevice* cdevice = device_cast(device);
	cVkSwapchainKHR* cswapchain = swapchain_cast(swapchain);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_WIN32_KHR

// VK_KHR_present_wait2

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForPresent2KHR(VkDevice device, VkSwapchainKHR swapchain, const VkPresentWait2InfoKHR* pPresentWait2Info)
{
	ENTRY(vkWaitForPresent2KHR);
	cVkDevice* cdevice = device_cast(device);
	cVkSwapchainKHR* cswapchain = swapchain_cast(swapchain);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_EXT_fragment_density_map_offset

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2EXT(VkCommandBuffer commandBuffer, const VkRenderingEndInfoEXT* pRenderingEndInfo)
{
	ENTRY(vkCmdEndRendering2EXT);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

// VK_EXT_metal_objects

#ifdef VK_USE_PLATFORM_METAL_EXT
VKAPI_ATTR void VKAPI_CALL vkExportMetalObjectsEXT(VkDevice device, VkExportMetalObjectsInfoEXT* pMetalObjectsInfo)
{
	ENTRY(vkExportMetalObjectsEXT);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
}
#endif // VK_USE_PLATFORM_METAL_EXT

// VK_EXT_depth_clamp_control

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthClampRangeEXT(VkCommandBuffer commandBuffer, VkDepthClampModeEXT depthClampMode, const VkDepthClampRangeEXT* pDepthClampRange)
{
	ENTRY(vkCmdSetDepthClampRangeEXT);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

// VK_EXT_external_memory_metal

#ifdef VK_USE_PLATFORM_METAL_EXT
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryMetalHandleEXT(VkDevice device, const VkMemoryGetMetalHandleInfoEXT* pGetMetalHandleInfo, void** pHandle)
{
	ENTRY(vkGetMemoryMetalHandleEXT);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_METAL_EXT

#ifdef VK_USE_PLATFORM_METAL_EXT
VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryMetalHandlePropertiesEXT(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, const void* pHandle, VkMemoryMetalHandlePropertiesEXT* pMemoryMetalHandleProperties)
{
	ENTRY(vkGetMemoryMetalHandlePropertiesEXT);
	cVkDevice* cdevice = device_cast(device);
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}
#endif // VK_USE_PLATFORM_METAL_EXT

// VK_EXT_descriptor_heap

VKAPI_ATTR VkDeviceSize VKAPI_CALL vkGetPhysicalDeviceDescriptorSizeEXT(
    VkPhysicalDevice                            physicalDevice,
    VkDescriptorType                            descriptorType)
{
	ENTRY(vkGetPhysicalDeviceDescriptorSizeEXT);
	CLOG("physicalDevice=%p, descriptorType=%u", physicalDevice, descriptorType);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	return sizeof(uint64_t);
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteSamplerDescriptorsEXT(
    VkDevice                                    device,
    uint32_t                                    samplerCount,
    const VkSamplerCreateInfo*                  pSamplers,
    const VkHostAddressRangeEXT*                pDescriptors)
{
	ENTRY(vkWriteSamplerDescriptorsEXT);
	CLOG("device=%p, samplerCount=%u, pSamplers=%p, pDescriptors=%p", device, samplerCount, pSamplers, pDescriptors);
	cVkDevice* cdevice = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWriteResourceDescriptorsEXT(
    VkDevice                                    device,
    uint32_t                                    resourceCount,
    const VkResourceDescriptorInfoEXT*          pResources,
    const VkHostAddressRangeEXT*                pDescriptors)
{
	ENTRY(vkWriteResourceDescriptorsEXT);
	CLOG("device=%p, resourceCount=%u, pResources=%p, pDescriptors=%p", device, resourceCount, pResources, pDescriptors);
	cVkDevice* cdevice = device_cast(device);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindSamplerHeapEXT(
    VkCommandBuffer                             commandBuffer,
    const VkBindHeapInfoEXT*                    pBindInfo)
{
	ENTRY(vkCmdBindSamplerHeapEXT);
	CMDLOG("commandBuffer=%p, pBindInfo=%p", commandBuffer, pBindInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBindResourceHeapEXT(
    VkCommandBuffer                             commandBuffer,
    const VkBindHeapInfoEXT*                    pBindInfo)
{
	ENTRY(vkCmdBindResourceHeapEXT);
	CMDLOG("commandBuffer=%p, pBindInfo=%p", commandBuffer, pBindInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDataEXT(
    VkCommandBuffer                             commandBuffer,
    const VkPushDataInfoEXT*                    pPushDataInfo)
{
	ENTRY(vkCmdPushDataEXT);
	CMDLOG("commandBuffer=%p, pPushDataInfo=%p", commandBuffer, pPushDataInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

// VK_EXT_descriptor_buffer_density_map + VK_EXT_descriptor_buffer

VKAPI_ATTR VkResult VKAPI_CALL vkGetImageOpaqueCaptureDataEXT(
    VkDevice                                    device,
    uint32_t                                    imageCount,
    const VkImage*                              pImages,
    VkHostAddressRangeEXT*                      pDatas)
{
	ENTRY(vkGetImageOpaqueCaptureDataEXT);
	CLOG("device=%p, imageCount=%u, pImages=%p, pDatas=%p", device, imageCount, pImages, pDatas);
	cVkDevice* cdevice = device_cast(device);
	if (pImages)
	{
		for (uint32_t i = 0; i < imageCount; i++)
		{
			cVkImage* cimage = image_cast(pImages[i]);
			if (pDatas)
			{
				pDatas[i] = {};
			}
		}
	}
	else if (pDatas)
	{
		for (uint32_t i = 0; i < imageCount; i++)
		{
			pDatas[i] = {};
		}
	}
	return VK_SUCCESS;
}

// VK_EXT_descriptor_buffer (custom border colors)

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterCustomBorderColorEXT(
    VkDevice                                    device,
    const VkSamplerCustomBorderColorCreateInfoEXT* pBorderColor,
    VkBool32                                    requestIndex,
    uint32_t*                                   pIndex)
{
	ENTRY(vkRegisterCustomBorderColorEXT);
	CLOG("device=%p, pBorderColor=%p, requestIndex=%u, pIndex=%p", device, pBorderColor, requestIndex, pIndex);
	cVkDevice* cdevice = device_cast(device);
	if (pIndex)
	{
		*pIndex = 0;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkUnregisterCustomBorderColorEXT(
    VkDevice                                    device,
    uint32_t                                    index)
{
	ENTRY(vkUnregisterCustomBorderColorEXT);
	CLOG("device=%p, index=%u", device, index);
	cVkDevice* cdevice = device_cast(device);
}

// VK_KHR_copy_memory_indirect

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryIndirectKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryIndirectInfoKHR*          pCopyMemoryIndirectInfo)
{
	ENTRY(vkCmdCopyMemoryIndirectKHR);
	CMDLOG("commandBuffer=%p, pCopyMemoryIndirectInfo=%p", commandBuffer, pCopyMemoryIndirectInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyMemoryToImageIndirectKHR(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToImageIndirectInfoKHR*   pCopyMemoryToImageIndirectInfo)
{
	ENTRY(vkCmdCopyMemoryToImageIndirectKHR);
	CMDLOG("commandBuffer=%p, pCopyMemoryToImageIndirectInfo=%p", commandBuffer, pCopyMemoryToImageIndirectInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

// VK_EXT_swapchain_present_mode_timing

VKAPI_ATTR VkResult VKAPI_CALL vkSetSwapchainPresentTimingQueueSizeEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t                                    size)
{
	ENTRY(vkSetSwapchainPresentTimingQueueSizeEXT);
	CLOG("device=%p, swapchain=" NHANDLE ", size=%u", device, swapchain, size);
	cVkDevice* cdevice = device_cast(device);
	cVkSwapchainKHR* cswapchain = swapchain_cast(swapchain);
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainTimingPropertiesEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimingPropertiesEXT*             pSwapchainTimingProperties,
    uint64_t*                                   pSwapchainTimingPropertiesCounter)
{
	ENTRY(vkGetSwapchainTimingPropertiesEXT);
	CLOG("device=%p, swapchain=" NHANDLE ", pSwapchainTimingProperties=%p, pSwapchainTimingPropertiesCounter=%p",
	     device, swapchain, pSwapchainTimingProperties, pSwapchainTimingPropertiesCounter);
	cVkDevice* cdevice = device_cast(device);
	cVkSwapchainKHR* cswapchain = swapchain_cast(swapchain);
	if (pSwapchainTimingProperties)
	{
		*pSwapchainTimingProperties = {};
	}
	if (pSwapchainTimingPropertiesCounter)
	{
		*pSwapchainTimingPropertiesCounter = 0;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainTimeDomainPropertiesEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSwapchainTimeDomainPropertiesEXT*         pSwapchainTimeDomainProperties,
    uint64_t*                                   pTimeDomainsCounter)
{
	ENTRY(vkGetSwapchainTimeDomainPropertiesEXT);
	CLOG("device=%p, swapchain=" NHANDLE ", pSwapchainTimeDomainProperties=%p, pTimeDomainsCounter=%p",
	     device, swapchain, pSwapchainTimeDomainProperties, pTimeDomainsCounter);
	cVkDevice* cdevice = device_cast(device);
	cVkSwapchainKHR* cswapchain = swapchain_cast(swapchain);
	if (pSwapchainTimeDomainProperties)
	{
		*pSwapchainTimeDomainProperties = {};
	}
	if (pTimeDomainsCounter)
	{
		*pTimeDomainsCounter = 0;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPastPresentationTimingEXT(
    VkDevice                                    device,
    const VkPastPresentationTimingInfoEXT*      pPastPresentationTimingInfo,
    VkPastPresentationTimingPropertiesEXT*      pPastPresentationTimingProperties)
{
	ENTRY(vkGetPastPresentationTimingEXT);
	CLOG("device=%p, pPastPresentationTimingInfo=%p, pPastPresentationTimingProperties=%p",
	     device, pPastPresentationTimingInfo, pPastPresentationTimingProperties);
	cVkDevice* cdevice = device_cast(device);
	if (pPastPresentationTimingProperties)
	{
		*pPastPresentationTimingProperties = {};
	}
	return VK_SUCCESS;
}

// VK_KHR_swapchain_maintenance1

VKAPI_ATTR VkResult VKAPI_CALL vkReleaseSwapchainImagesKHR(
    VkDevice                                    device,
    const VkReleaseSwapchainImagesInfoKHR*      pReleaseInfo)
{
	ENTRY(vkReleaseSwapchainImagesKHR);
	CLOG("device=%p, pReleaseInfo=%p", device, pReleaseInfo);
	cVkDevice* cdevice = device_cast(device);
	if (pReleaseInfo)
	{
		cVkSwapchainKHR* cswapchain = swapchain_cast(pReleaseInfo->swapchain);
	}
	TBD_UNSUPPORTED;
	return VK_SUCCESS;
}

// VK_KHR_dynamic_rendering

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkRenderingEndInfoKHR*                pRenderingEndInfo)
{
	ENTRY(vkCmdEndRendering2KHR);
	CMDLOG("commandBuffer=%p, pRenderingEndInfo=%p", commandBuffer, pRenderingEndInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

// VK_ARM_shader_instrumentation

#ifdef VK_ARM_SHADER_INSTRUMENTATION_SPEC_VERSION

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pDescriptionCount,
    VkShaderInstrumentationMetricDescriptionARM* pDescriptions)
{
	ENTRY(vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM);
	CLOG("physicalDevice=%p, pDescriptionCount=%p, pDescriptions=%p", physicalDevice, pDescriptionCount, pDescriptions);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	if (pDescriptionCount)
	{
		*pDescriptionCount = 0;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderInstrumentationARM(
    VkDevice                                    device,
    const VkShaderInstrumentationCreateInfoARM* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderInstrumentationARM*                 pInstrumentation)
{
	ENTRY(vkCreateShaderInstrumentationARM);
	CLOG("device=%p, pCreateInfo=%p, pAllocator=%p, pInstrumentation=%p", device, pCreateInfo, pAllocator, pInstrumentation);
	cVkDevice* cdevice = device_cast(device);
	owner_create<cVkShaderInstrumentationARM, VkShaderInstrumentationARM>(cdevice->shaderInstrumentations, pInstrumentation, pAllocator);
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyShaderInstrumentationARM(
    VkDevice                                    device,
    VkShaderInstrumentationARM                  instrumentation,
    const VkAllocationCallbacks*                pAllocator)
{
	ENTRY(vkDestroyShaderInstrumentationARM);
	CLOG("device=%p, instrumentation=" NHANDLE ", pAllocator=%p", device, instrumentation, pAllocator);
	cVkDevice* cdevice = device_cast(device);
	cVkShaderInstrumentationARM* cshaderInstrumentationARM = shaderinstrumentationarm_cast(instrumentation);
	destroy<cVkShaderInstrumentationARM, VkShaderInstrumentationARM>(instrumentation, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginShaderInstrumentationARM(
    VkCommandBuffer                             commandBuffer,
    VkShaderInstrumentationARM                  instrumentation)
{
	ENTRY(vkCmdBeginShaderInstrumentationARM);
	CMDLOG("commandBuffer=%p, instrumentation=" NHANDLE, commandBuffer, instrumentation);
	cVkCommandBuffer* p = commandbuffer_cast(commandBuffer);
	commandbuffer_command(vkCmdBeginShaderInstrumentationARM, commandBuffer, MetricUnit(1));
	cVkShaderInstrumentationARM* cshaderInstrumentationARM = shaderinstrumentationarm_cast(instrumentation);
	p->commands.back().bindings.push_back(cshaderInstrumentationARM);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndShaderInstrumentationARM(
    VkCommandBuffer                             commandBuffer)
{
	ENTRY(vkCmdEndShaderInstrumentationARM);
	CMDLOG("commandBuffer=%p", commandBuffer);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetShaderInstrumentationValuesARM(
    VkDevice                                    device,
    VkShaderInstrumentationARM                  instrumentation,
    uint32_t*                                   pMetricBlockCount,
    void*                                       pMetricValues,
    VkShaderInstrumentationValuesFlagsARM       flags)
{
	ENTRY(vkGetShaderInstrumentationValuesARM);
	CLOG("device=%p, instrumentation=" NHANDLE ", pMetricBlockCount=%p, pMetricValues=%p, flags=%u",
	     device, instrumentation, pMetricBlockCount, pMetricValues, flags);
	cVkDevice* cdevice = device_cast(device);
	cVkShaderInstrumentationARM* cshaderInstrumentationARM = shaderinstrumentationarm_cast(instrumentation);
	if (pMetricBlockCount)
	{
		*pMetricBlockCount = 0;
	}
	return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkClearShaderInstrumentationMetricsARM(
    VkDevice                                    device,
    VkShaderInstrumentationARM                  instrumentation)
{
	ENTRY(vkClearShaderInstrumentationMetricsARM);
	CLOG("device=%p, instrumentation=" NHANDLE, device, instrumentation);
	cVkDevice* cdevice = device_cast(device);
	cVkShaderInstrumentationARM* cshaderInstrumentationARM = shaderinstrumentationarm_cast(instrumentation);
}

#endif

// VK_EXT_device_fault / decompression

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDecompressMemoryInfoEXT*            pDecompressMemoryInfoEXT)
{
	ENTRY(vkCmdDecompressMemoryEXT);
	CMDLOG("commandBuffer=%p, pDecompressMemoryInfoEXT=%p", commandBuffer, pDecompressMemoryInfoEXT);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDecompressMemoryIndirectCountEXT(
    VkCommandBuffer                             commandBuffer,
    VkMemoryDecompressionMethodFlagsEXT         decompressionMethod,
    VkDeviceAddress                             indirectCommandsAddress,
    VkDeviceAddress                             indirectCommandsCountAddress,
    uint32_t                                    maxDecompressionCount,
    uint32_t                                    stride)
{
	ENTRY(vkCmdDecompressMemoryIndirectCountEXT);
	CMDLOG("commandBuffer=%p, decompressionMethod=%llu, indirectCommandsAddress=%llu, indirectCommandsCountAddress=%llu, maxDecompressionCount=%u, stride=%u",
	       commandBuffer, (unsigned long long)decompressionMethod, (unsigned long long)indirectCommandsAddress,
	       (unsigned long long)indirectCommandsCountAddress, maxDecompressionCount, stride);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}

// VK_ARM_performance_counter_region

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    uint32_t*                                   pCounterCount,
    VkPerformanceCounterARM*                    pCounters,
    VkPerformanceCounterDescriptionARM*         pCounterDescriptions)
{
	ENTRY(vkEnumeratePhysicalDeviceQueueFamilyPerformanceCountersByRegionARM);
	CLOG("physicalDevice=%p, queueFamilyIndex=%u, pCounterCount=%p, pCounters=%p, pCounterDescriptions=%p",
	     physicalDevice, queueFamilyIndex, pCounterCount, pCounters, pCounterDescriptions);
	cVkPhysicalDevice* cphysicalDevice = physicaldevice_cast(physicalDevice);
	if (pCounterCount)
	{
		*pCounterCount = 0;
	}
	return VK_SUCCESS;
}

// VK_EXT_image_processing

VKAPI_ATTR VkResult VKAPI_CALL vkGetTensorOpaqueCaptureDataARM(
    VkDevice                                    device,
    uint32_t                                    tensorCount,
    const VkTensorARM*                          pTensors,
    VkHostAddressRangeEXT*                      pDatas)
{
	ENTRY(vkGetTensorOpaqueCaptureDataARM);
	CLOG("device=%p, tensorCount=%u, pTensors=%p, pDatas=%p", device, tensorCount, pTensors, pDatas);
	cVkDevice* cdevice = device_cast(device);
	if (pTensors)
	{
		for (uint32_t i = 0; i < tensorCount; i++)
		{
			cVkTensor* ctensor = tensor_cast(pTensors[i]);
			if (pDatas)
			{
				pDatas[i] = {};
			}
		}
	}
	else if (pDatas)
	{
		for (uint32_t i = 0; i < tensorCount; i++)
		{
			pDatas[i] = {};
		}
	}
	return VK_SUCCESS;
}

// VK_EXT_custom_resolve

VKAPI_ATTR void VKAPI_CALL vkCmdBeginCustomResolveEXT(
    VkCommandBuffer                             commandBuffer,
    const VkBeginCustomResolveInfoEXT*          pBeginCustomResolveInfo)
{
	ENTRY(vkCmdBeginCustomResolveEXT);
	CMDLOG("commandBuffer=%p, pBeginCustomResolveInfo=%p", commandBuffer, pBeginCustomResolveInfo);
	cVkCommandBuffer* ccommandBuffer = commandbuffer_cast(commandBuffer);
	TBD_UNSUPPORTED;
}
