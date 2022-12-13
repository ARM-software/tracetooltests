#include "vulkan_common.h"

#include <unordered_set>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

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

void testFreeMemory(vulkan_setup_t vulkan, VkDeviceMemory memory)
{
	if (vulkan.vkGetDeviceTracingObjectProperty)
	{
		uint64_t allocations = vulkan.vkGetDeviceTracingObjectProperty(vulkan.device, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)memory, VK_TRACING_OBJECT_PROPERTY_ALLOCATIONS_COUNT_TRACETOOLTEST);
		assert(allocations == 0);
	}
	vkFreeMemory(vulkan.device, memory, nullptr);
}

void test_done(vulkan_setup_t vulkan, bool shared_instance)
{
	vkDestroyDevice(vulkan.device, nullptr);
	if (!shared_instance)
	{
		vkDestroyInstance(vulkan.instance, nullptr);
	}
}

static void print_usage(TOOLSTEST_CALLBACK_USAGE usage)
{
	printf("Usage:\n");
	printf("-h/--help              This help\n");
	printf("-g/--gpu level N       Select GPU (default %d)\n", gpu());
	printf("-v/--validation        Enable validation layer\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	if (usage) usage();
	exit(1);
}

vulkan_setup_t test_init(int argc, char** argv, const std::string& testname, vulkan_req_t& reqs)
{
	const char* wsi = getenv("TOOLSTEST_WINSYS");
	vulkan_setup_t vulkan;
	std::unordered_set<std::string> required(reqs.extensions.begin(), reqs.extensions.end()); // temp copy
	bool has_tooling_checksum = false;
	bool has_tooling_obj_property = false;
	bool has_tooling_benchmarking = false;

	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			print_usage(reqs.usage);
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			p__debug_level = get_arg(argv, ++i, argc);
		}
		else if (match(argv[i], "-v", "--validation"))
		{
			p__validation = true;
		}
		else if (match(argv[i], "-g", "--gpu"))
		{
			select_gpu(get_arg(argv, ++i, argc));
		}
		else
		{
			if (!reqs.cmdopt || !reqs.cmdopt(i, argc, argv, reqs))
			{
				ELOG("Unrecognized cmd line parameter: %s", argv[i]);
				print_usage(reqs.usage);
			}
		}
	}

	// Create instance
	if (reqs.instance == VK_NULL_HANDLE)
	{
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
		std::vector<VkExtensionProperties> supported_instance_extensions(propertyCount);
		result = vkEnumerateInstanceExtensionProperties(nullptr, &propertyCount, supported_instance_extensions.data());
		assert(result == VK_SUCCESS);
		for (const VkExtensionProperties& s : supported_instance_extensions)
		{
			if (strcmp(s.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0) enabledExtensions.push_back(s.extensionName);
			else if (strcmp(s.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) enabledExtensions.push_back(s.extensionName);
			else if (strcmp(s.extensionName, VK_TRACETOOLTEST_BENCHMARKING_EXTENSION_NAME) == 0)
			{
				enabledExtensions.push_back(s.extensionName);
				has_tooling_benchmarking = true;
			}
		}
		if (wsi && strcmp(wsi, "headless") == 0)
		{
			enabledExtensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
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
		const char *validationLayerNames[] = { "VK_LAYER_LUNARG_standard_validation" };
		if (p__validation)
		{
			pCreateInfo.enabledLayerCount = 1;
			pCreateInfo.ppEnabledLayerNames = validationLayerNames;
		}
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
	}
	else
	{
		vulkan.instance = reqs.instance;
	}

	// Create logical device
	uint32_t num_devices = 0;
	VkResult result = vkEnumeratePhysicalDevices(vulkan.instance, &num_devices, nullptr);
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
			exit(77);
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
		exit(77);
	}

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		VkBenchmarkingTRACETOOLTEST benchmarking = { VK_STRUCTURE_TYPE_BENCHMARKING_TRACETOOLTEST, nullptr };
		VkPhysicalDeviceVulkan13Features feat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
		if (has_tooling_benchmarking) feat13.pNext = &benchmarking;
		VkPhysicalDeviceVulkan12Features feat12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &feat13 };
		VkPhysicalDeviceVulkan11Features feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &feat12 };
		VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &feat11 };
		vkGetPhysicalDeviceFeatures2(vulkan.physical, &feat2);
		if (reqs.samplerAnisotropy) assert(feat2.features.samplerAnisotropy);
		if (has_tooling_benchmarking)
		{
			printf("Benchmarking mode requested:\n");
			printf("\tfixedTimeStep = %u\n", benchmarking.fixedTimeStep);
			printf("\tdisablePerformanceAdaptation = %s\n", benchmarking.disablePerformanceAdaptation ? "true" : "false");
			printf("\tdisableVendorAdaptation = %s\n", benchmarking.disableVendorAdaptation ? "true" : "false");
			printf("\tdisableLoadingFrames = %s\n", benchmarking.disableVendorAdaptation ? "true" : "false");
			printf("\tvisualSettings = %u\n", benchmarking.visualSettings);
			printf("\tscenario = %u\n", benchmarking.scenario);
			printf("\tloopTime = %u\n", benchmarking.loopTime);
		}
	}
	else // vulkan 1.0 mode
	{
		VkPhysicalDeviceFeatures feat = {};
		vkGetPhysicalDeviceFeatures(vulkan.physical, &feat);
	}

	uint32_t layer_count = 0;
	vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
	std::vector<VkLayerProperties> layer_info(layer_count);
	vkEnumerateInstanceLayerProperties(&layer_count, layer_info.data());

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
	if (reqs.samplerAnisotropy) features.samplerAnisotropy = VK_TRUE;
	deviceInfo.pEnabledFeatures = &features;

	std::vector<const char*> enabledExtensions;
	uint32_t propertyCount = 0;
	result = vkEnumerateDeviceExtensionProperties(vulkan.physical, nullptr, &propertyCount, nullptr);
	assert(result == VK_SUCCESS);
	std::vector<VkExtensionProperties> supported_device_extensions(propertyCount);
	result = vkEnumerateDeviceExtensionProperties(vulkan.physical, nullptr, &propertyCount, supported_device_extensions.data());
	assert(result == VK_SUCCESS);

	for (const VkExtensionProperties& s : supported_device_extensions)
	{
		// These are fake extensions used for testing, see README.md for documentation
		if (strcmp(s.extensionName, VK_TRACETOOLTEST_CHECKSUM_VALIDATION_EXTENSION_NAME) == 0)
		{
			enabledExtensions.push_back(s.extensionName);
			has_tooling_checksum = true;
		}
		else if (strcmp(s.extensionName, VK_TRACETOOLTEST_OBJECT_PROPERTY_EXTENSION_NAME) == 0)
		{
			enabledExtensions.push_back(s.extensionName);
			has_tooling_obj_property = true;
		}

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
		exit(77);
	}
	deviceInfo.enabledExtensionCount = enabledExtensions.size();
	if (enabledExtensions.size() > 0)
	{
		deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}

	result = vkCreateDevice(vulkan.physical, &deviceInfo, NULL, &vulkan.device);
	check(result);
	test_set_name(vulkan.device, VK_OBJECT_TYPE_DEVICE, (uint64_t)vulkan.device, "Our device");

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		VkPhysicalDeviceMemoryProperties2 mprops = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, nullptr };
		vkGetPhysicalDeviceMemoryProperties2(vulkan.physical, &mprops);
		memory_properties = mprops.memoryProperties; // struct copy
	}
	else
	{
		vkGetPhysicalDeviceMemoryProperties(vulkan.physical, &memory_properties);
	}

	if (has_tooling_checksum)
	{
		vulkan.vkAssertBuffer = (PFN_vkAssertBufferTRACETOOLTEST)vkGetDeviceProcAddr(vulkan.device, "vkAssertBufferTRACETOOLTEST");
	}
	if (has_tooling_obj_property)
	{
		vulkan.vkGetDeviceTracingObjectProperty = (PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST)vkGetDeviceProcAddr(vulkan.device, "vkGetDeviceTracingObjectPropertyTRACETOOLTEST");
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

void test_save_image(const vulkan_setup_t& vulkan, const char* filename, VkDeviceMemory memory, uint32_t offset, uint32_t size, uint32_t width, uint32_t height)
{
	float* ptr = nullptr;
	VkResult result = vkMapMemory(vulkan.device, memory, offset, size, 0, (void**)&ptr);
	check(result);
	assert(ptr != nullptr);
	std::vector<unsigned char> image;
	image.reserve(width * height * 4);
	for (unsigned i = 0; i < width * height * 4; i++)
	{
		image.push_back((unsigned char)(255.0f * ptr[i]));
	}
	int r = stbi_write_png(filename, width, height, 4, image.data(), 0);
	assert(r != 0);
	vkUnmapMemory(vulkan.device, memory);
}
