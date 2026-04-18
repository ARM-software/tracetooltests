#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_0;
	reqs.minApiVersion = VK_API_VERSION_1_0;
	reqs.maxApiVersion = VK_API_VERSION_1_0;
	reqs.instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

	VkPhysicalDeviceFeatures2KHR requested_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR, nullptr };
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&requested_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_get_physical_device_properties2", reqs);
	assert(vulkan.instance_extensions.count(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 1);

	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceFeatures2KHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceProperties2KHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceFormatProperties2KHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceImageFormatProperties2KHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceQueueFamilyProperties2KHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceMemoryProperties2KHR);
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceSparseImageFormatProperties2KHR);

	bench_start_iteration(vulkan.bench);

	VkPhysicalDeviceFeatures2KHR features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR, nullptr };
	pf_vkGetPhysicalDeviceFeatures2KHR(vulkan.physical, &features);

	VkPhysicalDeviceProperties2KHR properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR, nullptr };
	pf_vkGetPhysicalDeviceProperties2KHR(vulkan.physical, &properties);
	assert(properties.properties.apiVersion >= VK_API_VERSION_1_0);

	VkFormatProperties2KHR format_properties = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2_KHR, nullptr };
	pf_vkGetPhysicalDeviceFormatProperties2KHR(vulkan.physical, VK_FORMAT_R8G8B8A8_UNORM, &format_properties);

	VkPhysicalDeviceImageFormatInfo2KHR image_format_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2_KHR, nullptr,
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0
	};
	VkImageFormatProperties2KHR image_format_properties = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2_KHR, nullptr };
	VkResult result = pf_vkGetPhysicalDeviceImageFormatProperties2KHR(vulkan.physical, &image_format_info, &image_format_properties);
	check(result);

	uint32_t queue_family_count = 0;
	pf_vkGetPhysicalDeviceQueueFamilyProperties2KHR(vulkan.physical, &queue_family_count, nullptr);
	assert(queue_family_count > 0);
	std::vector<VkQueueFamilyProperties2KHR> queue_family_properties(queue_family_count);
	for (auto& family : queue_family_properties) family = { VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2_KHR, nullptr };
	pf_vkGetPhysicalDeviceQueueFamilyProperties2KHR(vulkan.physical, &queue_family_count, queue_family_properties.data());
	assert(queue_family_count == queue_family_properties.size());
	assert(queue_family_properties[0].queueFamilyProperties.queueCount > 0);

	VkPhysicalDeviceMemoryProperties2KHR memory_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2_KHR, nullptr };
	pf_vkGetPhysicalDeviceMemoryProperties2KHR(vulkan.physical, &memory_properties);
	assert(memory_properties.memoryProperties.memoryHeapCount > 0);

	VkPhysicalDeviceSparseImageFormatInfo2KHR sparse_image_info = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SPARSE_IMAGE_FORMAT_INFO_2_KHR, nullptr,
		VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_TILING_OPTIMAL
	};
	uint32_t sparse_property_count = 0;
	pf_vkGetPhysicalDeviceSparseImageFormatProperties2KHR(vulkan.physical, &sparse_image_info, &sparse_property_count, nullptr);
	std::vector<VkSparseImageFormatProperties2KHR> sparse_properties(sparse_property_count);
	for (auto& property : sparse_properties) property = { VK_STRUCTURE_TYPE_SPARSE_IMAGE_FORMAT_PROPERTIES_2_KHR, nullptr };
	if (sparse_property_count > 0)
	{
		pf_vkGetPhysicalDeviceSparseImageFormatProperties2KHR(vulkan.physical, &sparse_image_info, &sparse_property_count, sparse_properties.data());
		assert(sparse_property_count == sparse_properties.size());
	}

	test_marker_mention(vulkan, "Queried VK_KHR_get_physical_device_properties2 entry points", VK_OBJECT_TYPE_PHYSICAL_DEVICE, (uint64_t)vulkan.physical);

	bench_stop_iteration(vulkan.bench);
	test_done(vulkan);
	return 0;
}
