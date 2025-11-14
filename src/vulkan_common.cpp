#include "vulkan_common.h"
#include "external/json.hpp"
#include <fstream>
#include <spirv/unified1/spirv.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

static VkPhysicalDeviceMemoryProperties memory_properties = {};
static int no_explicit = 0;

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

void test_done(vulkan_setup_t& vulkan, bool shared_instance)
{
	bench_done(vulkan.bench);
	vkDestroyDevice(vulkan.device, nullptr);
	vulkan.device = VK_NULL_HANDLE;

	if (!shared_instance)
	{
		vkDestroyInstance(vulkan.instance, nullptr);
		vulkan.instance = VK_NULL_HANDLE;
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
	case VK_API_VERSION_1_4: return 4;
	}
	return -1;
}

static void print_usage(const vulkan_req_t& reqs)
{
	printf("Usage:\n");
	printf("-h/--help              This help\n");
	printf("-N/--gpu-native        Use the native GPU (default), fails if not available\n");
	printf("-L/--gpu-simulated     Use a software rasterizer as your GPU, fails if not available\n");
	printf("-v/--validation        Enable validation layer\n");
	printf("-d/--debug level N     Set debug level [0,1,2,3] (default %d)\n", p__debug_level);
	printf("-G/--garbage-pointers  Set ignored pointers to garbage values instead of null\n");
	printf("-V/--vulkan-variant N  Set Vulkan variant (default %d)\n", apiversion2variant(reqs.apiVersion));
	if (reqs.minApiVersion <= VK_API_VERSION_1_0) printf("\t0 - Vulkan 1.0\n");
	if (reqs.minApiVersion <= VK_API_VERSION_1_1 && reqs.maxApiVersion >= VK_API_VERSION_1_1) printf("\t1 - Vulkan 1.1\n");
	if (reqs.minApiVersion <= VK_API_VERSION_1_2 && reqs.maxApiVersion >= VK_API_VERSION_1_2) printf("\t2 - Vulkan 1.2\n");
	if (reqs.minApiVersion <= VK_API_VERSION_1_3 && reqs.maxApiVersion >= VK_API_VERSION_1_3) printf("\t3 - Vulkan 1.3\n");
	if (reqs.minApiVersion <= VK_API_VERSION_1_4 && reqs.maxApiVersion >= VK_API_VERSION_1_4) printf("\t4 - Vulkan 1.4\n");
	printf("-neu/--no-explicit     Do not use the explicit host updates extension (default %d)\n", no_explicit);
	if (reqs.usage) reqs.usage();
	exit(1);
}

bool enable_frame_boundary(vulkan_req_t& reqs)
{
	if (reqs.options.count("frame_boundary") > 0) // this will fail badly if we run it twice
	{
		printf("enable_frame_boundary already enabled!\n");
		return true;
	}
	reqs.options["frame_boundary"] = true;
	reqs.device_extensions.push_back("VK_EXT_frame_boundary");
	// yes, this is a memory leak... fix later...
	VkPhysicalDeviceFrameBoundaryFeaturesEXT* fbfeats = (VkPhysicalDeviceFrameBoundaryFeaturesEXT*)malloc(sizeof(VkPhysicalDeviceFrameBoundaryFeaturesEXT));
	fbfeats->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAME_BOUNDARY_FEATURES_EXT;
	fbfeats->pNext = reqs.extension_features; // chain them if there are any existing ones
	reqs.extension_features = (VkBaseInStructure*)fbfeats;
	printf("Enabling frame boundary extension\n");
	return true;
}

static bool check_bench(vulkan_setup_t& vulkan, vulkan_req_t& reqs, const char* testname)
{
	const char* enable_json = getenv("BENCHMARKING_ENABLE_JSON");
	const char* enable_path = getenv("BENCHMARKING_ENABLE_FILE");
	char* content = nullptr;

	if (enable_path && enable_json) fprintf(stderr, "Both BENCHMARKING_ENABLE_JSON and BENCHMARKING_ENABLE_PATH are set -- this is an error!\n");

	if (enable_path)
	{
		printf("Reading benchmarking enable file: %s\n", enable_path);
		uint32_t size = 0;
		content = load_blob(enable_path, &size);
	}
	else if (enable_json)
	{
		printf("Reading benchmarking enable file directly from the environment variable\n");
		content = strdup(enable_json);
	}
	else return false;

	nlohmann::json data = nlohmann::json::parse(content);
	if (!data.count("target")) { printf("No app name in benchmarking enable file - skipping!\n"); return false; }
	if (data.value("target", "no target") != testname) { printf("Name in benchmarking enable file is not ours - skipping\n"); return false; }

	if (data.count("capabilities"))
	{
		nlohmann::json caps = data.at("capabilities");

		reqs.fence_delay = caps.value("gpu_delay_reuse", 0);
		if (caps.count("frameless") && caps.value("frameless", true) == false) enable_frame_boundary(reqs);
		p__loops = caps.value("loops", p__loops);
		// TBD: gpu_no_coherent
	}

	if (data.count("settings"))
	{
		nlohmann::json settings = data.at("settings");

		if (settings.count("vulkan_variant"))
		{
			std::string api = settings.value("vulkan_variant", "1.1");
			if (api == "1.0") reqs.apiVersion = VK_API_VERSION_1_0;
			else if (api == "1.1") reqs.apiVersion = VK_API_VERSION_1_1;
			else if (api == "1.2") reqs.apiVersion = VK_API_VERSION_1_2;
			else if (api == "1.3") reqs.apiVersion = VK_API_VERSION_1_3;
			else { printf("Bad vulkan_variant: %s\n", api.c_str()); return false; }
		}

		if (settings.count("queue_count")) reqs.queues = settings.value("queue_count", 1);
	}
	bench_init(vulkan.bench, testname, content, data.value("results", "results.json").c_str());

	return true;
}

vulkan_setup_t test_init(int argc, char** argv, const std::string& testname, vulkan_req_t& reqs)
{
	const char* wsi = getenv("TOOLSTEST_WINSYS");
	vulkan_setup_t vulkan;
	bool has_debug_utils = false;
	bool req_maintenance_6 = false;
	bool force_native_gpu = false;
	bool use_simulated_gpu = false;

	std::string api;
	switch (reqs.apiVersion)
	{
	case VK_API_VERSION_1_0: api = "1.0"; break;
	case VK_API_VERSION_1_1: api = "1.1"; break;
	case VK_API_VERSION_1_2: api = "1.2"; break;
	case VK_API_VERSION_1_3: api = "1.3"; break;
	default: api = "(unrecognized version)"; break;
	}

	// Parse bench enable file, if any
	check_bench(vulkan, reqs, testname.c_str());
	vulkan.bench.backend_name = "Vulkan " + api;

	for (int i = 1; i < argc; i++)
	{
		int old = i;
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
		else if (match(argv[i], "-N", "--gpu-native"))
		{
			force_native_gpu = true;
		}
		else if (match(argv[i], "-L", "--gpu-simulated"))
		{
			use_simulated_gpu = true;
		}
		else if (match(argv[i], "-G", "--garbage-pointers"))
		{
			vulkan.garbage_pointers = true;
		}
		else if (match(argv[i], "-neu", "--no-explicit"))
		{
			no_explicit = 1;
		}
		else if (match(argv[i], "-V", "--vulkan-variant")) // overrides version req from test itself
		{
			int vulkan_variant = get_arg(argv, ++i, argc);

			if (apiversion2variant(reqs.minApiVersion) > vulkan_variant)
			{
				ELOG("Given Vulkan version too low for this test!");
				print_usage(reqs);
			}

			if (apiversion2variant(reqs.maxApiVersion) < vulkan_variant)
			{
				ELOG("Given Vulkan version too high for this test!");
				print_usage(reqs);
			}

			if (vulkan_variant == 0) reqs.apiVersion = VK_API_VERSION_1_0;
			else if (vulkan_variant == 1) reqs.apiVersion = VK_API_VERSION_1_1;
			else if (vulkan_variant == 2) reqs.apiVersion = VK_API_VERSION_1_2;
			else if (vulkan_variant == 3) reqs.apiVersion = VK_API_VERSION_1_3;
			else if (vulkan_variant == 4) reqs.apiVersion = VK_API_VERSION_1_4;
			if (vulkan_variant < 0 || vulkan_variant > 4) print_usage(reqs);
		}
		else
		{
			if (!reqs.cmdopt || !reqs.cmdopt(i, argc, argv, reqs))
			{
				ELOG("Unrecognized or invalid cmd line parameter: %s", argv[i]);
				print_usage(reqs);
			}
			continue; // do not run reqs.cmdopt twice
		}
		if (reqs.cmdopt) reqs.cmdopt(old, argc, argv, reqs); // allow specializations to handle the options as well
	}

	if (force_native_gpu && use_simulated_gpu)
	{
		ELOG("You cannot combine --gpu-native and --gpu-simulated, choose one!\n");
		print_usage(reqs);
	}

	std::unordered_set<std::string> instance_required(reqs.instance_extensions.begin(), reqs.instance_extensions.end()); // temp copy
	std::unordered_set<std::string> device_required(reqs.device_extensions.begin(), reqs.device_extensions.end()); // temp copy
	vulkan.instance_extensions.insert(reqs.instance_extensions.begin(), reqs.instance_extensions.end()); // permanent copy
	vulkan.device_extensions.insert(reqs.device_extensions.begin(), reqs.device_extensions.end()); // permanent copy

	// Create instance
	if (reqs.instance == VK_NULL_HANDLE)
	{
		VkInstanceCreateInfo pCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
		VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
		app.pApplicationName = testname.c_str();
		app.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
		app.pEngineName = "testEngine";
		app.engineVersion = VK_MAKE_VERSION( 1, 0, 0 );
		app.apiVersion = reqs.apiVersion;
		vulkan.apiVersion = reqs.apiVersion;
		pCreateInfo.pApplicationInfo = &app;

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
				vulkan.instance_extensions.insert(s.extensionName);
			}

			for (const auto& str : reqs.instance_extensions) if (str == s.extensionName)
			{
				enabledExtensions.push_back(str.c_str());
				instance_required.erase(str);
			}
		}
		if (instance_required.size() > 0)
		{
			printf("Missing required Vulkan instance extensions:\n");
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

	// Set up the feature pnext chain
	reqs.reqfeat14.pNext = reqs.extension_features;
	if (reqs.apiVersion < VK_API_VERSION_1_4) reqs.reqfeat13.pNext = reqs.extension_features;
	if (reqs.apiVersion < VK_API_VERSION_1_3) reqs.reqfeat12.pNext = reqs.extension_features;
	if (reqs.apiVersion < VK_API_VERSION_1_2) reqs.reqfeat11.pNext = reqs.extension_features;

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
	int selected_gpu = -1;
	for (unsigned i = 0; i < physical_devices.size(); i++)
	{
		VkPhysicalDeviceProperties device_properties = {};
		vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
		if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && force_native_gpu) continue; // skip it
		else if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && use_simulated_gpu) { selected_gpu = i; break; } // pick first simulated GPU
		else if (device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && !use_simulated_gpu) { selected_gpu = i; break; } // pick first native GPU
		else selected_gpu = i;
	}
	if (selected_gpu == -1)
	{
		printf("No GPU of the desired type found\n");
		exit(77);
	}
	printf("Found %d physical devices (selecting %d)\n", (int)num_devices, selected_gpu);
	for (unsigned i = 0; i < physical_devices.size(); i++)
	{
		VkPhysicalDeviceProperties device_properties = {};
		vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
		printf("\t%u : %s (Vulkan %d.%d.%d)\n", i, device_properties.deviceName, VK_VERSION_MAJOR(device_properties.apiVersion),
		       VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));
		if (i == (unsigned)selected_gpu && device_properties.apiVersion < reqs.apiVersion)
		{
			printf("Selected GPU does support required Vulkan version %d.%d.%d\n", VK_VERSION_MAJOR(reqs.apiVersion),
			       VK_VERSION_MINOR(reqs.apiVersion), VK_VERSION_PATCH(reqs.apiVersion));
			exit(77);
		}
	}
	if (selected_gpu >= (int)num_devices)
	{
		printf("Selected GPU %d does not exist!\n", selected_gpu);
		exit(-1);
	}
	vulkan.physical = physical_devices.at(selected_gpu);

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
		exit(77);
	}

	// Get physical device capabilities
	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		vkGetPhysicalDeviceFeatures2(vulkan.physical, &vulkan.hasfeat2);
		if (reqs.samplerAnisotropy && !vulkan.hasfeat2.features.samplerAnisotropy) { printf("Sampler anisotropy required but not supported!\n"); exit(77); }
		if (reqs.reqfeat12.bufferDeviceAddress && !vulkan.hasfeat12.bufferDeviceAddress) { printf("Buffer device address extension feature required but not supported!\n"); exit(77); }
		if (reqs.bufferDeviceAddress && !vulkan.hasfeat12.bufferDeviceAddress) { printf("Buffer device address required but not supported!\n"); exit(77); }
		if (vulkan.hasfeat13.synchronization2 == VK_TRUE) reqs.reqfeat13.synchronization2 = VK_TRUE;
	}
	else // vulkan 1.0 mode
	{
		assert(!reqs.bufferDeviceAddress);
		vkGetPhysicalDeviceFeatures(vulkan.physical, &vulkan.hasfeat2.features);
		if (reqs.samplerAnisotropy) assert(vulkan.hasfeat2.features.samplerAnisotropy);
	}

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 1)
	{
		VkPhysicalDeviceProperties2 properties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, nullptr };
		vulkan.device_ray_tracing_pipeline_properties = VkPhysicalDeviceRayTracingPipelinePropertiesKHR{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR, nullptr};
		properties.pNext = &vulkan.device_ray_tracing_pipeline_properties;
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
	std::vector<float> queuePriorities(reqs.queues);
	std::fill(queuePriorities.begin(), queuePriorities.end(), 1.0f);
	queueCreateInfo.pQueuePriorities = queuePriorities.data();
	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceInfo.enabledLayerCount = 0;
	deviceInfo.ppEnabledLayerNames = nullptr;
	if (reqs.samplerAnisotropy) reqs.reqfeat2.features.samplerAnisotropy = VK_TRUE;
	if (reqs.bufferDeviceAddress)
	{
		reqs.reqfeat12.bufferDeviceAddress = VK_TRUE;
	}

	if (VK_VERSION_MAJOR(reqs.apiVersion) >= 1 && VK_VERSION_MINOR(reqs.apiVersion) >= 2)
	{
		deviceInfo.pNext = &reqs.reqfeat2;
	}
	else // Vulkan 1.1 or below
	{
		deviceInfo.pEnabledFeatures = &reqs.reqfeat2.features;
		deviceInfo.pNext = reqs.extension_features;
	}

	std::vector<const char*> enabledExtensions;
	uint32_t propertyCount = 0;
	result = vkEnumerateDeviceExtensionProperties(vulkan.physical, nullptr, &propertyCount, nullptr);
	assert(result == VK_SUCCESS);
	std::vector<VkExtensionProperties> supported_device_extensions(propertyCount);
	result = vkEnumerateDeviceExtensionProperties(vulkan.physical, nullptr, &propertyCount, supported_device_extensions.data());
	assert(result == VK_SUCCESS);

	VkPhysicalDeviceExplicitHostUpdatesFeaturesARM explicit_updates_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXPLICIT_HOST_UPDATES_FEATURES_ARM, nullptr };

	for (const VkExtensionProperties& s : supported_device_extensions)
	{
		// The following two are fake extensions used for testing, see README.md for documentation
		if (strcmp(s.extensionName, VK_ARM_TRACE_HELPERS_EXTENSION_NAME) == 0)
		{
			enabledExtensions.push_back(s.extensionName);
			vulkan.has_trace_helpers = true;
			vulkan.device_extensions.insert(s.extensionName);
		}
		else if (strcmp(s.extensionName, VK_TRACETOOLTEST_TRACE_HELPERS2_EXTENSION_NAME) == 0)
		{
			enabledExtensions.push_back(s.extensionName);
			vulkan.has_trace_helpers2 = true;
			vulkan.device_extensions.insert(s.extensionName);
		}
		else if (strcmp(s.extensionName, VK_ARM_TRACE_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0)
		{
			enabledExtensions.push_back(s.extensionName);
			vulkan.has_trace_descriptor_buffer = true;
			vulkan.device_extensions.insert(s.extensionName);
		}
		else if (strcmp(s.extensionName, VK_ARM_EXPLICIT_HOST_UPDATES_EXTENSION_NAME) == 0 && no_explicit == 0)
		{
			enabledExtensions.push_back(s.extensionName);
			vulkan.has_explicit_host_updates = true;
			vulkan.device_extensions.insert(s.extensionName);
			explicit_updates_features.explicitHostUpdates = VK_TRUE;
			explicit_updates_features.pNext = (void*)deviceInfo.pNext;
			deviceInfo.pNext = &explicit_updates_features;
		}

		for (const auto& str : reqs.device_extensions) if (str == s.extensionName)
		{
			enabledExtensions.push_back(str.c_str());
			device_required.erase(str);

			if (strcmp(s.extensionName, VK_KHR_MAINTENANCE_6_EXTENSION_NAME) == 0)
			{
				req_maintenance_6 = true;
			}
		}
	}
	if (enabledExtensions.size() > 0) printf("Required Vulkan device extensions:\n");
	for (auto str : enabledExtensions) printf("\t%s\n", str);
	if (device_required.size() > 0)
	{
		printf("Missing required Vulkan device extensions:\n");
		for (auto str : device_required) printf("\t%s\n", str.c_str());
		exit(77);
	}
	deviceInfo.enabledExtensionCount = enabledExtensions.size();
	if (enabledExtensions.size() > 0)
	{
		deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();
	}

	// Logging
	const VkBaseOutStructure* pNext = (const VkBaseOutStructure*)deviceInfo.pNext;
	if (pNext) DLOG("Our vkCreateDevice pNext chain contains:");
	else DLOG("Our vkCreateDevice pNext chain is empty");
	while (pNext)
	{
		DLOG("\t%d", (int)pNext->sType);
		pNext = pNext->pNext;
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

	if (has_debug_utils)
	{
		vulkan.vkSetDebugUtilsObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(vulkan.device, "vkSetDebugUtilsObjectNameEXT");
		vulkan.vkCmdInsertDebugUtilsLabel = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetDeviceProcAddr(vulkan.device, "vkCmdInsertDebugUtilsLabelEXT");

		if (vulkan.vkSetDebugUtilsObjectName && vulkan.vkCmdInsertDebugUtilsLabel) ILOG("Debug utils enabled");
	}

	if (reqs.bufferDeviceAddress)
	{
		vulkan.vkGetBufferDeviceAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddress>(vkGetDeviceProcAddr(vulkan.device, "vkGetBufferDeviceAddress"));
		assert(vulkan.vkGetBufferDeviceAddress);
	}

	if (vulkan.has_trace_helpers)
	{
		vulkan.vkCmdUpdateBuffer2 = reinterpret_cast<PFN_vkCmdUpdateBuffer2ARM>(vkGetDeviceProcAddr(vulkan.device, "vkCmdUpdateBuffer2ARM"));
	}
	if (vulkan.has_trace_helpers && p__sanity > 0)
	{
		vulkan.vkAssertBuffer = (PFN_vkAssertBufferARM)vkGetDeviceProcAddr(vulkan.device, "vkAssertBufferARM");
	}

	if (vulkan.has_trace_helpers2)
	{
		vulkan.vkUpdateBuffer = reinterpret_cast<PFN_vkUpdateBufferTRACETOOLTEST>(vkGetDeviceProcAddr(vulkan.device, "vkUpdateBufferTRACETOOLTEST"));
		vulkan.vkUpdateImage = reinterpret_cast<PFN_vkUpdateImageTRACETOOLTEST>(vkGetDeviceProcAddr(vulkan.device, "vkUpdateBufferTRACETOOLTEST"));
		vulkan.vkThreadBarrier = reinterpret_cast<PFN_vkThreadBarrierTRACETOOLTEST>(vkGetDeviceProcAddr(vulkan.device, "vkThreadBarrierTRACETOOLTEST"));
	}

	if (req_maintenance_6)
	{
		vulkan.vkCmdPushConstants2 = reinterpret_cast<PFN_vkCmdPushConstants2KHR>(vkGetDeviceProcAddr(vulkan.device, "vkCmdPushConstants2KHR"));
	}

	return vulkan;
}

acceleration_structures::functions acceleration_structures::query_acceleration_structure_functions(const vulkan_setup_t& vulkan)
{
	acceleration_structures::functions functions{};
	functions.vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCreateAccelerationStructureKHR"));
	assert(functions.vkCreateAccelerationStructureKHR);

	functions.vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetAccelerationStructureBuildSizesKHR"));
	assert(functions.vkGetAccelerationStructureBuildSizesKHR);

	functions.vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCmdBuildAccelerationStructuresKHR"));
	assert(functions.vkCmdBuildAccelerationStructuresKHR);

	functions.vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(vulkan.device, "vkBuildAccelerationStructuresKHR"));
	assert(functions.vkBuildAccelerationStructuresKHR);

	functions.vkCmdWriteAccelerationStructuresPropertiesKHR = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
	assert(functions.vkCmdWriteAccelerationStructuresPropertiesKHR);

	functions.vkWriteAccelerationStructuresPropertiesKHR = reinterpret_cast<PFN_vkWriteAccelerationStructuresPropertiesKHR>(vkGetDeviceProcAddr(vulkan.device, "vkWriteAccelerationStructuresPropertiesKHR"));
	assert(functions.vkCmdWriteAccelerationStructuresPropertiesKHR);

	functions.vkCopyAccelerationStructureKHR = reinterpret_cast<PFN_vkCopyAccelerationStructureKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCopyAccelerationStructureKHR"));
	assert(functions.vkCopyAccelerationStructureKHR);

	functions.vkCmdCopyAccelerationStructureKHR = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCmdCopyAccelerationStructureKHR"));
	assert(functions.vkCmdCopyAccelerationStructureKHR);

	functions.vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(vulkan.device, "vkBuildAccelerationStructuresKHR"));
	assert(functions.vkBuildAccelerationStructuresKHR);

	functions.vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetAccelerationStructureDeviceAddressKHR"));
	assert(functions.vkGetAccelerationStructureDeviceAddressKHR);

	functions.vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(vulkan.device, "vkDestroyAccelerationStructureKHR"));
	assert(functions.vkDestroyAccelerationStructureKHR);

	// Query those functions only for the tests that want to use ray tracing pipeline extension
	if(vulkan.device_extensions.count(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) != 0)
	{
		functions.vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCreateRayTracingPipelinesKHR"));
		assert(functions.vkCreateRayTracingPipelinesKHR);

		functions.vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(vulkan.device, "vkCmdTraceRaysKHR"));
		assert(functions.vkCmdTraceRaysKHR);

		functions.vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetRayTracingShaderGroupHandlesKHR"));
		assert(functions.vkGetRayTracingShaderGroupHandlesKHR);
	}

	return functions;
}

acceleration_structures::Buffer acceleration_structures::prepare_buffer(const vulkan_setup_t &vulkan, VkDeviceSize size, void *data, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties)
{
	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.usage = usage;
	create_info.size = size;
	if (vulkan.garbage_pointers) create_info.pQueueFamilyIndices = (const uint32_t*)0xdeadbeef;
	acceleration_structures::Buffer buffer { };

	check(vkCreateBuffer(vulkan.device, &create_info, nullptr, &buffer.handle));

	VkMemoryRequirements memory_requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, buffer.handle, &memory_requirements);
	VkMemoryAllocateInfo memory_allocate_info{};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, memory_properties);

	VkMemoryAllocateFlagsInfoKHR allocation_flags_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR, nullptr };
	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		allocation_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		memory_allocate_info.pNext = &allocation_flags_info;
	}

	check(vkAllocateMemory(vulkan.device, &memory_allocate_info, nullptr, &buffer.memory));

	if (data)
	{
		void *mapped;
		check(vkMapMemory(vulkan.device, buffer.memory, 0, size, 0, &mapped));
		memcpy(mapped, data, size);
		testFlushMemory(vulkan, buffer.memory, 0, size, vulkan.has_explicit_host_updates);
		vkUnmapMemory(vulkan.device, buffer.memory);
	}
	check(vkBindBufferMemory(vulkan.device, buffer.handle, buffer.memory, 0));
	return buffer;
}

VkDeviceAddress acceleration_structures::get_buffer_device_address(const vulkan_setup_t &vulkan, VkBuffer buffer)
{
	VkBufferDeviceAddressInfo buffer_device_adress_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	buffer_device_adress_info.buffer = buffer;
	return vulkan.vkGetBufferDeviceAddress(vulkan.device, &buffer_device_adress_info);
}

VkPipelineShaderStageCreateInfo acceleration_structures::prepare_shader_stage_create_info(const vulkan_setup_t &vulkan, const uint8_t *spirv, uint32_t spirv_length, VkShaderStageFlagBits stage)
{
	std::vector<uint32_t> code(spirv_length);
	memcpy(code.data(), spirv, spirv_length);

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	createInfo.pCode = code.data();
	createInfo.codeSize = spirv_length;
	VkShaderModule shader_module;
 	check(vkCreateShaderModule(vulkan.device, &createInfo, NULL, &shader_module));

	VkPipelineShaderStageCreateInfo shader_stage_create_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	shader_stage_create_info.stage = stage;
	shader_stage_create_info.module = shader_module;
	shader_stage_create_info.pName = "main";

	return shader_stage_create_info;
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

/// Takes an RGBA8888 image and saves it to disk as PNG
void test_save_image(const vulkan_setup_t& vulkan, const char* filename, VkDeviceMemory memory, uint32_t offset, uint32_t width, uint32_t height)
{
	float* ptr = nullptr;
	const uint32_t size = width * height * 4;
	VkResult result = vkMapMemory(vulkan.device, memory, offset, size * sizeof(float), 0, (void**)&ptr);
	check(result);
	assert(ptr != nullptr);
	std::vector<unsigned char> image;
	image.reserve(size);
	for (unsigned i = 0; i < size; i++)
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
		VkDeviceMemory mem = memory.at(dedicated ? i : 0);
		VkResult result = vkMapMemory(vulkan.device, mem, offset, aligned_size, 0, (void**)&data);
		assert(result == VK_SUCCESS);
		memset(data, pattern ? i : 0, aligned_size);
		// Explicit notification
		if (vulkan.has_explicit_host_updates)
		{
			VkFlushRangesFlagsARM frf = { VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM, nullptr };
			frf.flags = VK_FLUSH_OPERATION_INFORMATIVE_BIT_ARM;
			VkMappedMemoryRange mmr = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, &frf };
			mmr.memory = mem;
			mmr.offset = 0;
			mmr.size = VK_WHOLE_SIZE;
			vkFlushMappedMemoryRanges(vulkan.device, 1, &mmr);
		}
		vkUnmapMemory(vulkan.device, mem);
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

void testFlushMemory(const vulkan_setup_t& vulkan, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, bool extra)
{
	VkFlushRangesFlagsARM frf = { VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM, nullptr };
	frf.flags = VK_FLUSH_OPERATION_INFORMATIVE_BIT_ARM;
	VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr };
	range.memory = memory;
	range.size = size;
	range.offset = offset;
	if (vulkan.has_explicit_host_updates && extra) range.pNext = &frf;
	VkResult result = vkFlushMappedMemoryRanges(vulkan.device, 1, &range);
	check(result);
}

void testCopyBuffer(const vulkan_setup_t& vulkan, VkQueue queue, VkBuffer target, VkBuffer origin, VkDeviceSize size)
{
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkFence fence;

	VkCommandPoolCreateInfo command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, NULL, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer);
	check(result);

	VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(result);

	VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
	check(result);

	testCmdCopyBuffer(vulkan, command_buffer, { origin }, { target }, size);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
}

void testQueueBuffer(const vulkan_setup_t& vulkan, VkQueue queue, const std::vector<VkBuffer>& buffers)
{
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkFence fence;

	VkCommandPoolCreateInfo command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, NULL, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer);
	check(result);

	VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(result);

	VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
	check(result);

	for (VkBuffer buffer : buffers)
	{
		VkMemoryBarrier memory_barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
		memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		VkBufferMemoryBarrier buffer_barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
		buffer_barrier.buffer = buffer;
		buffer_barrier.offset = 0;
		buffer_barrier.size = VK_WHOLE_SIZE;
		buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		buffer_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &memory_barrier, 1, &buffer_barrier, 0, NULL);
	}

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
}
