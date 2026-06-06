#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_ARM_SHADER_CORE_PROPERTIES_EXTENSION_NAME);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_shader_core_properties", reqs);
	assert(vulkan.device_extensions.count(VK_ARM_SHADER_CORE_PROPERTIES_EXTENSION_NAME) == 1);

	bench_start_iteration(vulkan.bench);

	VkPhysicalDeviceShaderCorePropertiesARM shader_core_properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_ARM, nullptr
	};
	VkPhysicalDeviceProperties2 properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &shader_core_properties
	};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);

	assert(shader_core_properties.sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_ARM);
	printf("VK_ARM_shader_core_properties:\n");
	printf("\tpixelRate: %u\n", shader_core_properties.pixelRate);
	printf("\ttexelRate: %u\n", shader_core_properties.texelRate);
	printf("\tfmaRate: %u\n", shader_core_properties.fmaRate);
	assert(shader_core_properties.pixelRate != 0);
	assert(shader_core_properties.texelRate != 0);
	assert(shader_core_properties.fmaRate != 0);
	test_marker_mention(vulkan, "Queried VK_ARM_shader_core_properties", VK_OBJECT_TYPE_PHYSICAL_DEVICE, (uint64_t)vulkan.physical);

	bench_stop_iteration(vulkan.bench);
	test_done(vulkan);
	return 0;
}
