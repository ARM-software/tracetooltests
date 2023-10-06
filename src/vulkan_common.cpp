#include "vulkan_common.h"

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

void test_set_name(const vulkan_setup_t& vulkan, VkObjectType type, uint64_t handle, const char* name)
{
	VkDebugUtilsObjectNameInfoEXT info = {};
	info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	info.objectType = type;
	info.objectHandle = handle;
	info.pObjectName = name;
	if (vulkan.vkSetDebugUtilsObjectName) vulkan.vkSetDebugUtilsObjectName(vulkan.device, &info);
}

void testFreeMemory(const vulkan_setup_t& vulkan, VkDeviceMemory memory)
{
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

static int apiversion2variant(uint32_t apiversion)
{
	switch(apiversion)
	{
	case VK_API_VERSION_1_0: return 0;
	case VK_API_VERSION_1_1: return 1;
	case VK_API_VERSION_1_2: return 2;
	case VK_API_VERSION_1_3: return 3;
	}
	return -1;
}

static void print_usage(const vulkan_req_t& reqs)
{
	printf("Usage:\n");
	printf("-h/--help              This help\n");
	printf("-g/--gpu level N       Select GPU (default %d)\n", gpu());
	printf("-v/--validation        Enable validation layer\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	printf("-V/--vulkan-variant N  Set Vulkan variant (default %d)\n", apiversion2variant(reqs.apiVersion));
	printf("\t0 - Vulkan 1.0\n");
	printf("\t1 - Vulkan 1.1\n");
	printf("\t2 - Vulkan 1.2\n");
	printf("\t3 - Vulkan 1.3\n");
	if (reqs.usage) reqs.usage();
	exit(1);
}

vulkan_setup_t test_init(int argc, char** argv, const std::string& testname, vulkan_req_t& reqs)
{
	const char* wsi = getenv("TOOLSTEST_WINSYS");
	vulkan_setup_t vulkan;
	std::unordered_set<std::string> instance_required(reqs.instance_extensions.begin(), reqs.instance_extensions.end()); // temp copy
	std::unordered_set<std::string> device_required(reqs.device_extensions.begin(), reqs.device_extensions.end()); // temp copy
	bool has_tooling_checksum = false;
	bool has_tooling_obj_property = false;
	bool has_tooling_benchmarking = false;
	bool has_debug_utils = false;
	bool has_frame_end = false;

	vulkan.instance_extensions.insert(reqs.instance_extensions.begin(), reqs.instance_extensions.end()); // permanent copy
	vulkan.device_extensions.insert(reqs.device_extensions.begin(), reqs.device_extensions.end()); // permanent copy

	for (int i = 1; i < argc; i++)
	{
		if (match(argv[i], "-h", "--help"))
		{
			print_usage(reqs);
		}
		else if (match(argv[i], "-d", "--debug"))
		{
			p__debug_level = get_arg(argv, ++i, argc);
			if (p__debug_level > 3) print_usage(reqs);
		}
		else if (match(argv[i], "-v", "--validation"))
		{
			p__validation = true;
		}
		else if (match(argv[i], "-g", "--gpu"))
		{
			select_gpu(get_arg(argv, ++i, argc));
		}
		else if (match(argv[i], "-V", "--vulkan-variant")) // overrides version req from test itself
		{
			int vulkan_variant = get_arg(argv, ++i, argc);
			if (vulkan_variant == 0) reqs.apiVersion = VK_API_VERSION_1_0;
			else if (vulkan_variant == 1) reqs.apiVersion = VK_API_VERSION_1_1;
			else if (vulkan_variant == 2) reqs.apiVersion = VK_API_VERSION_1_2;
			else if (vulkan_variant == 3) reqs.apiVersion = VK_API_VERSION_1_3;
			if (vulkan_variant < 0 || vulkan_variant > 3) print_usage(reqs);
		}
		else
		{
			if (!reqs.cmdopt || !reqs.cmdopt(i, argc, argv, reqs))
			{
				ELOG("Unrecognized cmd line parameter: %s", argv[i]);
				print_usage(reqs);
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
		vulkan.apiVersion = reqs.apiVersion;
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
			if (strcmp(s.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
			{
				enabledExtensions.push_back(s.extensionName);
				has_debug_utils = true;
			}
			else if (strcmp(s.extensionName, VK_TRACETOOLTEST_BENCHMARKING_EXTENSION_NAME) == 0)
			{
				enabledExtensions.push_back(s.extensionName);
				has_tooling_benchmarking = true;
			}

			for (const auto& str : reqs.instance_extensions) if (str == s.extensionName)
			{
				enabledExtensions.push_back(str.c_str());
				instance_required.erase(str);
			}
		}
		if (instance_required.size() > 0)
		{
			printf("Missing required instance extensions:\n");
			for (auto str : instance_required) printf("\t%s\n", str.c_str());
			exit(77);
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
		const char *validationLayerNames[] = { "VK_LAYER_KHRONOS_validation" };
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

		VkDebugUtilsMessengerCreateInfoEXT messext = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, nullptr };
		if (has_debug_utils)
		{
			messext.flags = 0;
			messext.pfnUserCallback = messenger_callback;
			messext.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			messext.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			pCreateInfo.pNext = &messext;
		}

		result = vkCreateInstance(&pCreateInfo, NULL, &vulkan.instance);
		check(result);
	}
	else
	{
		vulkan.instance = reqs.instance;
	}

	// Create logical device
	VkPhysicalDeviceVulkan13Features reqfeat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, reqs.extension_features };
	VkPhysicalDeviceVulkan12Features reqfeat12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &reqfeat13 };
	if (reqs.apiVersion < VK_API_VERSION_1_3) reqfeat12.pNext = reqs.extension_features;
	VkPhysicalDeviceVulkan11Features reqfeat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &reqfeat12 };
	if (reqs.apiVersion < VK_API_VERSION_1_2) reqfeat11.pNext = reqs.extension_features;
	VkPhysicalDeviceFeatures2 reqfeat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &reqfeat11 };
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
		printf("\t%u : %s (Vulkan %d.%d.%d)\n", i, device_properties.deviceName, VK_VERSION_MAJOR(device_properties.apiVersion),
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
		printf("Vulkan implementation does not have sufficient queues (only %d, need %u) for this test\n", familyprops[0].queueCount, reqs.queues);
		exit(77);
	}

	if (reqs.bufferDeviceAddress && reqs.apiVersion < VK_API_VERSION_1_2)
	{
		printf("Buffer device address feature requires at least Vulkan 1.2 - set the Vulkan version with the -V parameter\n");
		exit(78);
	}

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		VkBenchmarkingTRACETOOLTEST benchmarking = { VK_STRUCTURE_TYPE_BENCHMARKING_TRACETOOLTEST, nullptr };
		VkPhysicalDeviceVulkan13Features feat13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, nullptr };
		if (has_tooling_benchmarking) feat13.pNext = &benchmarking;
		VkPhysicalDeviceVulkan12Features feat12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &feat13 };
		VkPhysicalDeviceVulkan11Features feat11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &feat12 };
		VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &feat11 };
		vkGetPhysicalDeviceFeatures2(vulkan.physical, &feat2);
		if (reqs.samplerAnisotropy) assert(feat2.features.samplerAnisotropy);
		if (reqs.bufferDeviceAddress) assert(feat12.bufferDeviceAddress);
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
		if (feat13.synchronization2 == VK_TRUE) reqfeat13.synchronization2 = VK_TRUE;
	}
	else // vulkan 1.0 mode
	{
		assert(!reqs.bufferDeviceAddress);
		VkPhysicalDeviceFeatures feat = {};
		vkGetPhysicalDeviceFeatures(vulkan.physical, &feat);
		if (reqs.samplerAnisotropy) assert(feat.samplerAnisotropy);
	}

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		VkPhysicalDeviceProperties2 properties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, nullptr };
		vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);
		vulkan.device_properties = properties.properties;
	}
	else vkGetPhysicalDeviceProperties(vulkan.physical, &vulkan.device_properties); // 1.0 version

	uint32_t layer_count = 0;
	vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
	std::vector<VkLayerProperties> layer_info(layer_count);
	vkEnumerateInstanceLayerProperties(&layer_count, layer_info.data());

	VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr };
	queueCreateInfo.queueFamilyIndex = 0; // just grab first one
	queueCreateInfo.queueCount = reqs.queues;
	float queuePriorities[] = { 1.0f, 0.5f };
	queueCreateInfo.pQueuePriorities = queuePriorities;
	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceInfo.enabledLayerCount = 0;
	deviceInfo.ppEnabledLayerNames = nullptr;
	if (reqs.samplerAnisotropy) reqfeat2.features.samplerAnisotropy = VK_TRUE;
	if (reqs.bufferDeviceAddress)
	{
		reqfeat12.bufferDeviceAddress = VK_TRUE;
	}
	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 2)
	{
		deviceInfo.pNext = &reqfeat2;
	}
	else
	{
		deviceInfo.pEnabledFeatures = &reqfeat2.features;
	}

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
		else if (strcmp(s.extensionName, VK_TRACETOOLTEST_FRAME_END_EXTENSION_NAME) == 0)
		{
			has_frame_end = true;
		}

		for (const auto& str : reqs.device_extensions) if (str == s.extensionName)
		{
			enabledExtensions.push_back(str.c_str());
			device_required.erase(str);
		}
	}
	if (enabledExtensions.size() > 0) printf("Required device extensions:\n");
	for (auto str : enabledExtensions) printf("\t%s\n", str);
	if (device_required.size() > 0)
	{
		printf("Missing required device extensions:\n");
		for (auto str : device_required) printf("\t%s\n", str.c_str());
		exit(77);
	}
	deviceInfo.enabledExtensionCount = enabledExtensions.size();
	if (enabledExtensions.size() > 0)
	{
		deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}

	result = vkCreateDevice(vulkan.physical, &deviceInfo, NULL, &vulkan.device);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_DEVICE, (uint64_t)vulkan.device, "Our device");

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
	if (has_debug_utils)
	{
		vulkan.vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(vulkan.device, "vkSetDebugUtilsObjectNameEXT");
		vulkan.vkCmdInsertDebugUtilsLabel = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(vulkan.device, "vkCmdInsertDebugUtilsLabelEXT");
		
		if (vulkan.vkSetDebugUtilsObjectName && vulkan.vkCmdInsertDebugUtilsLabel) ILOG("Debug utils enabled");
	}
	if (has_tooling_obj_property)
	{
		vulkan.vkGetDeviceTracingObjectProperty = (PFN_vkGetDeviceTracingObjectPropertyTRACETOOLTEST)vkGetDeviceProcAddr(vulkan.device, "vkGetDeviceTracingObjectPropertyTRACETOOLTEST");
	}
	if (has_frame_end)
	{
		vulkan.vkFrameEnd = (PFN_vkFrameEndTRACETOOLTEST)vkGetDeviceProcAddr(vulkan.device, "vkFrameEndTRACETOOLTEST");
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

void testCmdCopyBuffer(const vulkan_setup_t& vulkan, VkCommandBuffer cmdbuf, const std::vector<VkBuffer>& origin, const std::vector<VkBuffer>& target, VkDeviceSize size)
{
	// TBD if (vulkan.device_extensions.count("VK_KHR_copy_commands2") && VK_KHR_synchronization2)
	if (vulkan.apiVersion < VK_API_VERSION_1_3)
	{
		for (unsigned i = 0; i < origin.size(); i++)
		{
			VkBufferCopy region;
			region.srcOffset = 0;
			region.dstOffset = 0;
			region.size = size;
			vkCmdCopyBuffer(cmdbuf, origin.at(i), target.at(i), 1, &region);
		}
		VkMemoryBarrier memory_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
		memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
	}
	else // vulkan 1.3+
	{
		for (unsigned i = 0; i < origin.size(); i++)
		{
			VkCopyBufferInfo2 info = { VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, nullptr };
			info.srcBuffer = origin.at(i);
			info.dstBuffer = target.at(i);
			info.regionCount = 1;
			VkBufferCopy2 region = { VK_STRUCTURE_TYPE_BUFFER_COPY_2, nullptr };
			region.srcOffset = 0;
			region.dstOffset = 0;
			region.size = size;
			info.pRegions = &region;
			vkCmdCopyBuffer2(cmdbuf, &info);
		}
		VkMemoryBarrier2 memory_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, nullptr };
		memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		memory_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		VkDependencyInfo info = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr };
		info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		info.memoryBarrierCount = 1;
		info.pMemoryBarriers = &memory_barrier;
		vkCmdPipelineBarrier2(cmdbuf, &info);
	}
}

void testBindBufferMemory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, VkDeviceMemory memory, VkDeviceSize stride, const char* name)
{
	// TBD if (vulkan.device_extensions.count("VK_KHR_bind_memory2"))
	if (vulkan.apiVersion < VK_API_VERSION_1_1)
	{
		VkDeviceSize offset = 0;
		for (const auto& v : buffers)
		{
			VkResult result = vkBindBufferMemory(vulkan.device, v, memory, offset);
			check(result);
			offset += stride;
		}
	}
	else // vulkan 1.3+
	{
		std::vector<VkBindBufferMemoryInfo> info(buffers.size());
		VkDeviceSize offset = 0;
		for (unsigned i = 0; i < buffers.size(); i++)
		{
			VkBindBufferMemoryInfo& v = info.at(i);
			v.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
			v.pNext = nullptr;
			v.buffer = buffers.at(i);
			v.memory = memory;
			v.memoryOffset = offset;
			offset += stride;
		}
		VkResult result = vkBindBufferMemory2(vulkan.device, buffers.size(), info.data());
		check(result);
	}
	for (unsigned i = 0; i < buffers.size() && name; i++)
	{
		VkDeviceSize offset = 0;
		std::string bufname = std::string(name) + "_" + std::to_string(i) + "_offset=" + std::to_string(offset);
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffers.at(i), bufname.c_str());
		offset += stride;
	}
}

uint32_t testAllocateBufferMemory(const vulkan_setup_t& vulkan, const std::vector<VkBuffer>& buffers, std::vector<VkDeviceMemory>& memory, bool deviceaddress, bool dedicated, bool pattern, const char* name)
{
	// Allocate
	const unsigned count = dedicated ? buffers.size() : 1;
	uint32_t aligned_size = 0;
	for (unsigned i = 0; i < count; i++)
	{
		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements(vulkan.device, buffers.at(i), &memory_requirements);
		const uint32_t memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		const uint32_t align_mod = memory_requirements.size % memory_requirements.alignment;
		const uint32_t new_aligned_size = (align_mod == 0) ? memory_requirements.size : (memory_requirements.size + memory_requirements.alignment - align_mod);
		assert(i == 0 || new_aligned_size == aligned_size);
		aligned_size = new_aligned_size;

		VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };
		VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
		if (vulkan.apiVersion >= VK_API_VERSION_1_1)
		{
			pAllocateMemInfo.pNext = &flaginfo;
		}
		pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
		pAllocateMemInfo.allocationSize = dedicated ? aligned_size : aligned_size * buffers.size();
		if (deviceaddress)
		{
			if (!dedicated) printf("We're binding multiple bufferdeviceaddress buffers to a single device memory here in violation of VUID-VkBufferDeviceAddressInfo-buffer-02600\n");
			flaginfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		}
		memory.push_back(VK_NULL_HANDLE);
		VkResult result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory.back());
		assert(result == VK_SUCCESS);
		assert(memory.back() != VK_NULL_HANDLE);
	}
	// Bind
	if (vulkan.apiVersion < VK_API_VERSION_1_1)
	{
		VkDeviceSize offset = 0;
		for (unsigned i = 0; i < buffers.size(); i++)
		{
			const unsigned memidx = dedicated ? i : 0;
			VkResult result = vkBindBufferMemory(vulkan.device, buffers[i], memory[memidx], dedicated ? 0 : offset);
			check(result);
			offset += aligned_size;
		}
	}
	else // vulkan 1.3+
	{
		std::vector<VkBindBufferMemoryInfo> info(buffers.size());
		VkDeviceSize offset = 0;
		for (unsigned i = 0; i < buffers.size(); i++)
		{
			VkBindBufferMemoryInfo& v = info.at(i);
			v.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
			v.pNext = nullptr;
			v.buffer = buffers.at(i);
			v.memory = dedicated ? memory[i] : memory[0];
			v.memoryOffset = dedicated ? 0 : offset;
			offset += aligned_size;
		}
		VkResult result = vkBindBufferMemory2(vulkan.device, buffers.size(), info.data());
		check(result);
	}
	// Fill
	for (unsigned i = 0; i < buffers.size(); i++)
	{
		const VkDeviceSize offset = dedicated ? 0 : i * aligned_size;
		uint8_t* data = nullptr;
		VkResult result = vkMapMemory(vulkan.device, memory[dedicated ? i : 0], offset, aligned_size, 0, (void**)&data);
		assert(result == VK_SUCCESS);
		memset(data, pattern ? i : 0, aligned_size);
		vkUnmapMemory(vulkan.device, memory[dedicated ? i : 0]);
	}
	// Label
	for (unsigned i = 0; i < buffers.size() && name; i++)
	{
		VkDeviceSize offset = 0;
		std::string bufname = std::string(name) + "_" + std::to_string(i) + "_offset=" + std::to_string(offset);
		test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffers.at(i), bufname.c_str());
		offset += aligned_size;
	}
	return aligned_size;
}
