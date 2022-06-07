#include "vulkan_common.h"

#include <unordered_set>

static VkPhysicalDeviceMemoryProperties memory_properties = {};
static int selected_gpu = 0;

#if defined(ANDROID)
int STOI(const std::string& value)
{
    int out;
    std::istringstream(value) >> out;
    return out;
}
#endif

static int get_env_int(const char* name, int fallback)
{
	int v = fallback;
	const char* tmpstr = getenv(name);
	if (tmpstr)
	{
		v = atoi(tmpstr);
	}
	return v;
}

int repeats()
{
	return get_env_int("TOOLSTEST_TIMES", 10);
}

void select_gpu(int chosen_gpu)
{
	selected_gpu = chosen_gpu;
}

static int gpu()
{
	return get_env_int("TOOLSTEST_GPU", selected_gpu);
}

static VkBool32 messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData)
{
	if (!is_debug() && (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT || messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)) return VK_TRUE;
	fprintf(stderr, "messenger (s%d, t%d): %s\n", (int)messageSeverity, (int)messageTypes, pCallbackData->pMessage);
	return VK_TRUE;
}

static VkBool32 report_callback(
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char*                                 pLayerPrefix,
    const char*                                 pMessage,
    void*                                       pUserData)
{
	if (((flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) || (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)) && !is_debug()) return VK_TRUE;
	fprintf(stderr, "report: %s\n", pMessage);
	return VK_TRUE;
}

void test_set_name(VkDevice device, VkObjectType type, uint64_t handle, const char* name)
{
	VkDebugUtilsObjectNameInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectType = type;
	info.objectHandle = handle;
	info.pObjectName = name;
	(void)info;
	//vkSetDebugUtilsObjectNameEXT(device, &info);
}

void test_done(vulkan_setup_t vulkan)
{
	vkDestroyDevice(vulkan.device, nullptr);
	vkDestroyInstance(vulkan.instance, nullptr);
}

vulkan_setup_t test_init(const std::string& testname, const vulkan_req_t& reqs)
{
	const char* wsi = getenv("TOOLSTEST_WINSYS");
	vulkan_setup_t vulkan;
	std::unordered_set<std::string> required(reqs.extensions.begin(), reqs.extensions.end()); // temp copy
	bool has_tooling_checksum = false;

	// Create instance
	VkInstanceCreateInfo pCreateInfo = {};
	VkApplicationInfo app = {};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = testname.c_str();
	app.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
	app.pEngineName = "testEngine";
	app.engineVersion = VK_MAKE_VERSION( 1, 0, 0 );
	app.apiVersion = reqs.apiVersion;
	pCreateInfo.pApplicationInfo = &app;
	pCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

	std::vector<const char*> enabledExtensions;
	uint32_t propertyCount = 0;
	VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, nullptr);
	assert(result == VK_SUCCESS);
	std::vector<VkExtensionProperties> supported_dev_extensions(propertyCount);
	result = vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, supported_dev_extensions.data());
	assert(result == VK_SUCCESS);
	for (const VkExtensionProperties& s : supported_dev_extensions)
	{
		if (strcmp(s.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0) enabledExtensions.push_back(s.extensionName);
	}
	if (wsi && strcmp(wsi, "headless") == 0)
	{
		enabledExtensions.push_back("VK_EXT_headless_surface");
	}
#ifdef VK_USE_PLATFORM_XCB_KHR
	else
	{
		enabledExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
	}
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
	enabledExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
#ifdef VALIDATION
	enabledExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
	const char *validationLayerNames[] = { "VK_LAYER_LUNARG_standard_validation" };
	pCreateInfo.enabledLayerCount = 1;
	pCreateInfo.ppEnabledLayerNames = validationLayerNames;
#endif
	enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	enabledExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
	if (enabledExtensions.size() > 0)
	{
		pCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}
	pCreateInfo.enabledExtensionCount = enabledExtensions.size();

	VkDebugReportCallbackCreateInfoEXT debugcallbackext = {};
	debugcallbackext.pNext = nullptr;
	debugcallbackext.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	debugcallbackext.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT
				| VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
	debugcallbackext.pfnCallback = report_callback;
	debugcallbackext.pUserData = nullptr;

	VkDebugUtilsMessengerCreateInfoEXT messext = {};
	messext.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	messext.pNext = &debugcallbackext;
	messext.flags = 0;
	messext.pfnUserCallback = messenger_callback;
	messext.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	messext.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	pCreateInfo.pNext = &messext;

	result = vkCreateInstance(&pCreateInfo, NULL, &vulkan.instance);
	check(result);

	// Create logical device
	uint32_t num_devices = 0;
	result = vkEnumeratePhysicalDevices(vulkan.instance, &num_devices, nullptr);
	check(result);
	assert(result == VK_SUCCESS);
	assert(num_devices > 0);
	std::vector<VkPhysicalDevice> physical_devices(num_devices);
	result = vkEnumeratePhysicalDevices(vulkan.instance, &num_devices, physical_devices.data());
	check(result);
	assert(result == VK_SUCCESS);
	assert(num_devices == physical_devices.size());
	printf("Found %d physical devices (selecting %d)!\n", (int)num_devices, gpu());
	for (unsigned i = 0; i < physical_devices.size(); i++)
	{
		VkPhysicalDeviceProperties device_properties = {};
		vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
		printf("\t%d : %s (Vulkan %d.%d.%d)\n", i, device_properties.deviceName, VK_VERSION_MAJOR(device_properties.apiVersion),
		       VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));
		if (i == (unsigned)gpu() && device_properties.apiVersion < reqs.apiVersion)
		{
			printf("Selected GPU %d does support required Vulkan version %d.%d.%d\n", gpu(), VK_VERSION_MAJOR(reqs.apiVersion),
			       VK_VERSION_MINOR(reqs.apiVersion), VK_VERSION_PATCH(reqs.apiVersion));
			exit(-1);
		}
	}
	if (gpu() >= (int)num_devices)
	{
		printf("Selected GPU %d does not exist!\n", gpu());
		exit(-1);
	}
	vulkan.physical = physical_devices[gpu()];

	uint32_t family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan.physical, &family_count, nullptr);
	std::vector<VkQueueFamilyProperties> familyprops(family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(vulkan.physical, &family_count, familyprops.data());
	if (familyprops[0].queueCount < reqs.queues)
	{
		printf("Vulkan implementation does not have sufficient queues (only %d, need %d) for this test\n", familyprops[0].queueCount, reqs.queues);
		exit(-1);
	}

	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = 0; // just grab first one
	queueCreateInfo.queueCount = reqs.queues;
	float queuePriorities[] = { 1.0f, 0.5f };
	queueCreateInfo.pQueuePriorities = queuePriorities;
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceInfo.enabledLayerCount = 0;
	deviceInfo.ppEnabledLayerNames = nullptr;
	VkPhysicalDeviceFeatures features = {};
	deviceInfo.pEnabledFeatures = &features;

	enabledExtensions.clear();
	result = vkEnumerateDeviceExtensionProperties(vulkan.physical, nullptr, &propertyCount, nullptr);
	assert(result == VK_SUCCESS);
	std::vector<VkExtensionProperties> supported_extensions(propertyCount);
	result = vkEnumerateDeviceExtensionProperties(vulkan.physical, nullptr, &propertyCount, supported_extensions.data());
	assert(result == VK_SUCCESS);

	for (const VkExtensionProperties& s : supported_extensions)
	{
		if (strcmp(s.extensionName, "VK_TRACETOOLTEST_checksum_validation") == 0) { enabledExtensions.push_back(s.extensionName); has_tooling_checksum = true; }
		for (const auto& str : reqs.extensions) if (str == s.extensionName)
		{
			enabledExtensions.push_back(str.c_str());
			required.erase(str);
		}
	}
	if (enabledExtensions.size() > 0) printf("Required device extensions:\n");
	for (auto str : enabledExtensions) printf("\t%s\n", str);
	if (required.size() > 0)
	{
		printf("Missing required device extensions:\n");
		for (auto str : required) printf("\t%s\n", str.c_str());
		exit(-1);
	}
	deviceInfo.enabledExtensionCount = enabledExtensions.size();
	if (enabledExtensions.size() > 0)
	{
		deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}

	result = vkCreateDevice(vulkan.physical, &deviceInfo, NULL, &vulkan.device);
	check(result);
	test_set_name(vulkan.device, VK_OBJECT_TYPE_DEVICE, (uint64_t)vulkan.device, "Our device");

	vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &memory_properties);

	if (has_tooling_checksum)
	{
		vulkan.vkAssertBuffer = (PFN_vkAssertBufferTRACETOOLTEST)vkGetDeviceProcAddr(vulkan.device, "vkAssertBufferTRACETOOLTEST");
	}

	return vulkan;
}

uint32_t get_device_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
	{
		if (type_filter & (1 << i) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}
	assert(false);
	return 0xffff; // satisfy compiler
}

const char* errorString(const VkResult errorCode)
{
	switch (errorCode)
	{
#define STR(r) case VK_ ##r: return #r
	STR(NOT_READY);
	STR(TIMEOUT);
	STR(EVENT_SET);
	STR(EVENT_RESET);
	STR(INCOMPLETE);
	STR(ERROR_OUT_OF_HOST_MEMORY);
	STR(ERROR_OUT_OF_DEVICE_MEMORY);
	STR(ERROR_INITIALIZATION_FAILED);
	STR(ERROR_DEVICE_LOST);
	STR(ERROR_MEMORY_MAP_FAILED);
	STR(ERROR_LAYER_NOT_PRESENT);
	STR(ERROR_EXTENSION_NOT_PRESENT);
	STR(ERROR_FEATURE_NOT_PRESENT);
	STR(ERROR_INCOMPATIBLE_DRIVER);
	STR(ERROR_TOO_MANY_OBJECTS);
	STR(ERROR_FORMAT_NOT_SUPPORTED);
	STR(ERROR_FRAGMENTED_POOL);
	STR(ERROR_OUT_OF_POOL_MEMORY);
	STR(ERROR_INVALID_EXTERNAL_HANDLE);
	STR(ERROR_SURFACE_LOST_KHR);
	STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
	STR(SUBOPTIMAL_KHR);
	STR(ERROR_OUT_OF_DATE_KHR);
	STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
	STR(ERROR_VALIDATION_FAILED_EXT);
	STR(ERROR_INVALID_SHADER_NV);
	STR(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
	STR(ERROR_FRAGMENTATION_EXT);
	STR(ERROR_NOT_PERMITTED_EXT);
	STR(ERROR_INVALID_DEVICE_ADDRESS_EXT);
#undef STR
	default:
		return "UNKNOWN_ERROR";
	}
}

void check_retval(VkResult stored_retval, VkResult retval)
{
	if (stored_retval == VK_SUCCESS && retval != VK_SUCCESS)
	{
		const char* err = errorString(retval);
		FELOG("TOOLSTEST ERROR: Returncode does not match stored value, got error: %s (code %u)", err, (unsigned)retval);
		assert(false);
	}
}
