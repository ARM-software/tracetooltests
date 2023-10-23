#include "vulkan_common.h"
#include <inttypes.h>

int main(int argc, char** argv)
{
	VkInstanceCreateInfo pCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	VkApplicationInfo appinfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	appinfo.apiVersion = VK_API_VERSION_1_1;
	pCreateInfo.pApplicationInfo = &appinfo;
	VkInstance instance;
	VkResult result = vkCreateInstance(&pCreateInfo, NULL, &instance);
	check(result);

	// Create logical device
	uint32_t num_devices = 0;
	result = vkEnumeratePhysicalDevices(instance, &num_devices, nullptr);
	check(result);
	assert(num_devices > 0);
	std::vector<VkPhysicalDevice> physical_devices(num_devices);
	result = vkEnumeratePhysicalDevices(instance, &num_devices, physical_devices.data());
	check(result);
	for (unsigned i = 0; i < physical_devices.size(); i++)
	{
		VkPhysicalDeviceProperties2 devprops { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, nullptr };
		vkGetPhysicalDeviceProperties2(physical_devices[i], &devprops);

		uint32_t family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties2(physical_devices[i], &family_count, nullptr);
		std::vector<VkQueueFamilyProperties2> familyprops(family_count);
		for (unsigned j = 0; j < family_count; j++) familyprops[j].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
		vkGetPhysicalDeviceQueueFamilyProperties2(physical_devices[i], &family_count, familyprops.data());

		VkPhysicalDeviceProperties2 properties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, nullptr };
		vkGetPhysicalDeviceProperties2(physical_devices[i], &properties);

		uint32_t propertyCount = 0;
		result = vkEnumerateDeviceExtensionProperties(physical_devices[i], nullptr, &propertyCount, nullptr);
		assert(result == VK_SUCCESS);
		std::vector<VkExtensionProperties> supported_device_extensions(propertyCount);
		result = vkEnumerateDeviceExtensionProperties(physical_devices[i], nullptr, &propertyCount, supported_device_extensions.data());
		assert(result == VK_SUCCESS);

		VkPhysicalDeviceMemoryProperties2 mprops { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, nullptr };
		vkGetPhysicalDeviceMemoryProperties2(physical_devices[i], &mprops);

		printf("\t%u : %s (Vulkan %d.%d.%d) with %u queues\n", i, devprops.properties.deviceName, VK_VERSION_MAJOR(devprops.properties.apiVersion),
		       VK_VERSION_MINOR(devprops.properties.apiVersion), VK_VERSION_PATCH(devprops.properties.apiVersion),
		       familyprops[0].queueFamilyProperties.queueCount);
	}

	vkDestroyInstance(instance, nullptr);

	return 0;
}
