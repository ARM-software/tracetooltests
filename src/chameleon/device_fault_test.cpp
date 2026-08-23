#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <vector>

#include <vulkan/vulkan.h>

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

struct test_device
{
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	PFN_vkQueueSubmit2KHR queueSubmit2KHR = nullptr;
	PFN_vkGetDeviceFaultInfoEXT getDeviceFaultInfoEXT = nullptr;
	PFN_vkGetDeviceFaultReportsKHR getDeviceFaultReportsKHR = nullptr;
	PFN_vkGetDeviceFaultDebugInfoKHR getDeviceFaultDebugInfoKHR = nullptr;
};

static uint32_t find_queue_family(VkPhysicalDevice physical_device)
{
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
	assert(count > 0);
	std::vector<VkQueueFamilyProperties> properties(count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.data());
	for (uint32_t i = 0; i < count; i++)
	{
		if (properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) return i;
	}
	assert(false);
	return 0;
}

static test_device create_test_device(VkPhysicalDevice physical_device, uint32_t queue_family, VkBool32 vendor_binary)
{
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
		nullptr,
		VK_TRUE,
	};
	VkPhysicalDeviceSynchronization2Features synchronization2 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
		&timeline_semaphore,
		VK_TRUE,
	};
	VkPhysicalDeviceShaderAbortFeaturesKHR shader_abort = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_FEATURES_KHR,
		&synchronization2,
		VK_TRUE,
	};
	VkPhysicalDeviceShaderConstantDataFeaturesKHR shader_constant_data = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CONSTANT_DATA_FEATURES_KHR,
		&shader_abort,
		VK_TRUE,
	};
	VkPhysicalDeviceFaultFeaturesKHR fault_khr = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR,
		&shader_constant_data,
		VK_TRUE,
		vendor_binary,
		VK_FALSE,
		VK_FALSE,
	};
	VkPhysicalDeviceFaultFeaturesEXT fault_ext = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
		&fault_khr,
		VK_TRUE,
		vendor_binary,
	};

	const float priority = 1.0f;
	VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr };
	queue_info.queueFamilyIndex = queue_family;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &priority;
	const char* extensions[] = {
		VK_EXT_DEVICE_FAULT_EXTENSION_NAME,
		VK_KHR_DEVICE_FAULT_EXTENSION_NAME,
		VK_KHR_SHADER_CONSTANT_DATA_EXTENSION_NAME,
		VK_KHR_SHADER_ABORT_EXTENSION_NAME,
		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
	};
	VkDeviceCreateInfo device_info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &fault_ext };
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.enabledExtensionCount = sizeof(extensions) / sizeof(extensions[0]);
	device_info.ppEnabledExtensionNames = extensions;

	test_device result;
	assert(vkCreateDevice(physical_device, &device_info, nullptr, &result.device) == VK_SUCCESS);
	vkGetDeviceQueue(result.device, queue_family, 0, &result.queue);
	assert(result.queue != VK_NULL_HANDLE);
	result.queueSubmit2KHR = reinterpret_cast<PFN_vkQueueSubmit2KHR>(vkGetDeviceProcAddr(result.device, "vkQueueSubmit2KHR"));
	result.getDeviceFaultInfoEXT = reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(vkGetDeviceProcAddr(result.device, "vkGetDeviceFaultInfoEXT"));
	result.getDeviceFaultReportsKHR = reinterpret_cast<PFN_vkGetDeviceFaultReportsKHR>(vkGetDeviceProcAddr(result.device, "vkGetDeviceFaultReportsKHR"));
	result.getDeviceFaultDebugInfoKHR = reinterpret_cast<PFN_vkGetDeviceFaultDebugInfoKHR>(vkGetDeviceProcAddr(result.device, "vkGetDeviceFaultDebugInfoKHR"));
	assert(result.queueSubmit2KHR);
	assert(result.getDeviceFaultInfoEXT);
	assert(result.getDeviceFaultReportsKHR);
	assert(result.getDeviceFaultDebugInfoKHR);
	return result;
}

static void test_empty_fault_state(const test_device& test)
{
	uint32_t fault_count = UINT32_MAX;
	const auto start = std::chrono::steady_clock::now();
	assert(test.getDeviceFaultReportsKHR(test.device, 20000000, &fault_count, nullptr) == VK_TIMEOUT);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	assert(elapsed >= std::chrono::milliseconds(10));
	assert(fault_count == 0);
}

static void test_fault_payloads(const test_device& test, VkPhysicalDevice physical_device)
{
	VkDeviceFaultCountsEXT counts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT, nullptr };
	assert(test.getDeviceFaultInfoEXT(test.device, &counts, nullptr) == VK_SUCCESS);
	assert(counts.addressInfoCount == 1);
	assert(counts.vendorInfoCount == 1);
	assert(counts.vendorBinarySize == sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneEXT));
	std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
	std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
	std::vector<uint8_t> vendor_binary(counts.vendorBinarySize);

	VkDeviceFaultCountsEXT null_counts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT, nullptr };
	null_counts.addressInfoCount = 1;
	null_counts.vendorInfoCount = 1;
	null_counts.vendorBinarySize = sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneEXT);
	VkDeviceFaultInfoEXT null_info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, nullptr };
	assert(test.getDeviceFaultInfoEXT(test.device, &null_counts, &null_info) == VK_INCOMPLETE);
	assert(null_counts.addressInfoCount == 0);
	assert(null_counts.vendorInfoCount == 0);
	assert(null_counts.vendorBinarySize == 0);

	VkDeviceFaultCountsEXT partial_counts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT, nullptr };
	partial_counts.addressInfoCount = 1;
	partial_counts.vendorInfoCount = 1;
	partial_counts.vendorBinarySize = sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneEXT) - 1;
	std::vector<uint8_t> partial_binary(partial_counts.vendorBinarySize, 0x5a);
	VkDeviceFaultInfoEXT partial_info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, nullptr };
	partial_info.pAddressInfos = addresses.data();
	partial_info.pVendorInfos = vendors.data();
	partial_info.pVendorBinaryData = partial_binary.data();
	assert(test.getDeviceFaultInfoEXT(test.device, &partial_counts, &partial_info) == VK_INCOMPLETE);
	assert(partial_counts.addressInfoCount == 1);
	assert(partial_counts.vendorInfoCount == 1);
	assert(partial_counts.vendorBinarySize == 0);
	for (uint8_t byte : partial_binary) assert(byte == 0x5a);

	counts.addressInfoCount = addresses.size();
	counts.vendorInfoCount = vendors.size();
	counts.vendorBinarySize = vendor_binary.size();
	VkDeviceFaultInfoEXT info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, nullptr };
	info.pAddressInfos = addresses.data();
	info.pVendorInfos = vendors.data();
	info.pVendorBinaryData = vendor_binary.data();
	assert(test.getDeviceFaultInfoEXT(test.device, &counts, &info) == VK_SUCCESS);
	assert(strcmp(info.description, "Chameleon injected device loss") == 0);
	assert(addresses[0].addressType == VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT);
	assert(strcmp(vendors[0].description, "Chameleon mock device fault") == 0);
	const VkDeviceFaultVendorBinaryHeaderVersionOneEXT* header =
		reinterpret_cast<const VkDeviceFaultVendorBinaryHeaderVersionOneEXT*>(vendor_binary.data());
	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties(physical_device, &properties);
	assert(header->headerSize == sizeof(*header));
	assert(header->headerVersion == VK_DEVICE_FAULT_VENDOR_BINARY_HEADER_VERSION_ONE_EXT);
	assert(header->vendorID == properties.vendorID);
	assert(header->deviceID == properties.deviceID);
	assert(header->driverVersion == properties.driverVersion);
	assert(memcmp(header->pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0);
	assert(header->apiVersion == 0 || header->apiVersion == VK_API_VERSION_1_3);

	counts.addressInfoCount = 0;
	counts.vendorInfoCount = 0;
	counts.vendorBinarySize = 0;
	assert(test.getDeviceFaultInfoEXT(test.device, &counts, &info) == VK_INCOMPLETE);

	uint32_t fault_count = 0;
	assert(test.getDeviceFaultReportsKHR(test.device, 0, &fault_count, nullptr) == VK_SUCCESS);
	assert(fault_count == 1);
	VkDeviceFaultInfoKHR fault_info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR, nullptr };
	assert(test.getDeviceFaultReportsKHR(test.device, 0, &fault_count, &fault_info) == VK_SUCCESS);
	assert(fault_count == 1);
	assert(fault_info.flags & VK_DEVICE_FAULT_FLAG_DEVICE_LOST_KHR);
	assert(fault_info.flags & VK_DEVICE_FAULT_FLAG_VENDOR_KHR);
	assert(strcmp(fault_info.description, "Chameleon injected device loss") == 0);
	fault_count = UINT32_MAX;
	assert(test.getDeviceFaultReportsKHR(test.device, 0, &fault_count, nullptr) == VK_TIMEOUT);
	assert(fault_count == 0);

	VkDeviceFaultShaderAbortMessageInfoKHR message_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR,
		nullptr,
	};
	VkDeviceFaultDebugInfoKHR debug_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR,
		&message_info,
	};
	assert(test.getDeviceFaultDebugInfoKHR(test.device, &debug_info) == VK_SUCCESS);
	assert(debug_info.vendorBinarySize == sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneKHR));
	assert(message_info.messageDataSize == 16);
	std::vector<uint8_t> short_debug_binary(debug_info.vendorBinarySize - 1, 0x5a);
	VkDeviceFaultDebugInfoKHR short_debug_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR,
		nullptr,
		static_cast<uint32_t>(short_debug_binary.size()),
		short_debug_binary.data(),
	};
	assert(test.getDeviceFaultDebugInfoKHR(test.device, &short_debug_info) == VK_ERROR_NOT_ENOUGH_SPACE_KHR);
	assert(short_debug_info.vendorBinarySize == 0);
	for (uint8_t byte : short_debug_binary) assert(byte == 0x5a);
	std::vector<uint8_t> debug_binary(debug_info.vendorBinarySize);
	std::vector<uint8_t> message_data(message_info.messageDataSize);
	debug_info.pVendorBinaryData = debug_binary.data();
	message_info.pMessageData = message_data.data();
	assert(test.getDeviceFaultDebugInfoKHR(test.device, &debug_info) == VK_SUCCESS);
	uint64_t payload_size = 0;
	uint32_t payload = 0;
	memcpy(&payload_size, message_data.data(), sizeof(payload_size));
	memcpy(&payload, message_data.data() + sizeof(payload_size), sizeof(payload));
	assert(payload_size == sizeof(payload));
	assert(payload == 0xdeadbeef);
}

static void test_disabled_vendor_binary(VkPhysicalDevice physical_device, uint32_t queue_family)
{
	setenv("CHAMELEON_DEVICE_LOST_AT_SUBMIT", "0", 1);
	test_device test = create_test_device(physical_device, queue_family, VK_FALSE);
	VkSubmitInfo empty_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	assert(vkQueueSubmit(test.queue, 1, &empty_submit, VK_NULL_HANDLE) == VK_ERROR_DEVICE_LOST);

	VkDeviceFaultCountsEXT counts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT, nullptr };
	assert(test.getDeviceFaultInfoEXT(test.device, &counts, nullptr) == VK_SUCCESS);
	assert(counts.addressInfoCount == 1);
	assert(counts.vendorInfoCount == 1);
	assert(counts.vendorBinarySize == 0);

	std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
	std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
	uint8_t ext_binary[8];
	memset(ext_binary, 0x5a, sizeof(ext_binary));
	counts.vendorBinarySize = sizeof(ext_binary);
	VkDeviceFaultInfoEXT info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, nullptr };
	info.pAddressInfos = addresses.data();
	info.pVendorInfos = vendors.data();
	info.pVendorBinaryData = ext_binary;
	assert(test.getDeviceFaultInfoEXT(test.device, &counts, &info) == VK_SUCCESS);
	assert(counts.vendorBinarySize == 0);
	for (uint8_t byte : ext_binary) assert(byte == 0x5a);

	uint8_t khr_binary[8];
	memset(khr_binary, 0x5a, sizeof(khr_binary));
	VkDeviceFaultDebugInfoKHR debug_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR,
		nullptr,
		sizeof(khr_binary),
		khr_binary,
	};
	assert(test.getDeviceFaultDebugInfoKHR(test.device, &debug_info) == VK_SUCCESS);
	assert(debug_info.vendorBinarySize == 0);
	for (uint8_t byte : khr_binary) assert(byte == 0x5a);
	vkDestroyDevice(test.device, nullptr);
}

static void test_thresholds(VkPhysicalDevice physical_device, uint32_t queue_family)
{
	unsetenv("CHAMELEON_DEVICE_LOST_AT_SUBMIT");
	test_device disabled = create_test_device(physical_device, queue_family, VK_TRUE);
	test_empty_fault_state(disabled);
	VkSubmitInfo empty_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	assert(vkQueueSubmit(disabled.queue, 1, &empty_submit, VK_NULL_HANDLE) == VK_SUCCESS);
	vkDestroyDevice(disabled.device, nullptr);

	setenv("CHAMELEON_DEVICE_LOST_AT_SUBMIT", "0", 1);
	test_device immediate = create_test_device(physical_device, queue_family, VK_TRUE);
	assert(vkQueueSubmit(immediate.queue, 1, &empty_submit, VK_NULL_HANDLE) == VK_ERROR_DEVICE_LOST);
	vkDestroyDevice(immediate.device, nullptr);

	setenv("CHAMELEON_DEVICE_LOST_AT_SUBMIT", "2", 1);
	test_device delayed = create_test_device(physical_device, queue_family, VK_TRUE);
	test_empty_fault_state(delayed);
	assert(vkQueueSubmit(delayed.queue, 1, &empty_submit, VK_NULL_HANDLE) == VK_SUCCESS);
	VkSubmitInfo2 empty_submit2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2, nullptr };
	assert(vkQueueSubmit2(delayed.queue, 1, &empty_submit2, VK_NULL_HANDLE) == VK_SUCCESS);

	VkSemaphoreTypeCreateInfo type_info = {
		VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		nullptr,
		VK_SEMAPHORE_TYPE_TIMELINE,
		0,
	};
	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type_info };
	VkSemaphore semaphore = VK_NULL_HANDLE;
	assert(vkCreateSemaphore(delayed.device, &semaphore_info, nullptr, &semaphore) == VK_SUCCESS);
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	assert(vkCreateFence(delayed.device, &fence_info, nullptr, &fence) == VK_SUCCESS);
	VkSemaphoreSubmitInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, nullptr };
	signal_info.semaphore = semaphore;
	signal_info.value = 1;
	signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	empty_submit2.signalSemaphoreInfoCount = 1;
	empty_submit2.pSignalSemaphoreInfos = &signal_info;
	assert(delayed.queueSubmit2KHR(delayed.queue, 1, &empty_submit2, fence) == VK_ERROR_DEVICE_LOST);
	assert(vkQueueSubmit(delayed.queue, 1, &empty_submit, VK_NULL_HANDLE) == VK_ERROR_DEVICE_LOST);
	assert(vkGetFenceStatus(delayed.device, fence) == VK_NOT_READY);
	uint64_t semaphore_value = UINT64_MAX;
	assert(vkGetSemaphoreCounterValue(delayed.device, semaphore, &semaphore_value) == VK_SUCCESS);
	assert(semaphore_value == 0);
	test_fault_payloads(delayed, physical_device);
	vkDestroyFence(delayed.device, fence, nullptr);
	vkDestroySemaphore(delayed.device, semaphore, nullptr);
	vkDestroyDevice(delayed.device, nullptr);

	setenv("CHAMELEON_DEVICE_LOST_AT_SUBMIT", "-1", 1);
	test_device negative = create_test_device(physical_device, queue_family, VK_TRUE);
	assert(vkQueueSubmit(negative.queue, 1, &empty_submit, VK_NULL_HANDLE) == VK_SUCCESS);
	vkDestroyDevice(negative.device, nullptr);

	test_disabled_vendor_binary(physical_device, queue_family);
}

int main()
{
	VkApplicationInfo application_info = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	application_info.pApplicationName = "chameleon_device_fault_test";
	application_info.apiVersion = VK_API_VERSION_1_3;
	VkInstanceCreateInfo instance_info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	instance_info.pApplicationInfo = &application_info;
	VkInstance instance = VK_NULL_HANDLE;
	assert(vkCreateInstance(&instance_info, nullptr, &instance) == VK_SUCCESS);
	uint32_t physical_device_count = 1;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	assert(vkEnumeratePhysicalDevices(instance, &physical_device_count, &physical_device) == VK_SUCCESS);
	assert(physical_device_count == 1);
	assert(physical_device != VK_NULL_HANDLE);
	test_thresholds(physical_device, find_queue_family(physical_device));
	vkDestroyInstance(instance, nullptr);
	return 0;
}
