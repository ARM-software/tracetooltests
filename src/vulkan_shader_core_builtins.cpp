#include "vulkan_common.h"

#include <cinttypes>

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.device_extensions.push_back(VK_ARM_SHADER_CORE_BUILTINS_EXTENSION_NAME);

	VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM shader_core_builtins_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_FEATURES_ARM, nullptr, VK_TRUE
	};
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&shader_core_builtins_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_shader_core_builtins", reqs);
	assert(vulkan.device_extensions.count(VK_ARM_SHADER_CORE_BUILTINS_EXTENSION_NAME) == 1);

	bench_start_iteration(vulkan.bench);

	VkPhysicalDeviceShaderCoreBuiltinsFeaturesARM queried_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_FEATURES_ARM, nullptr
	};
	VkPhysicalDeviceFeatures2 features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &queried_features
	};
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &features);

	printf("VK_ARM_shader_core_builtins features:\n");
	printf("\tshaderCoreBuiltins: %u\n", queried_features.shaderCoreBuiltins);
	assert(queried_features.shaderCoreBuiltins == VK_TRUE);

	VkPhysicalDeviceShaderCoreBuiltinsPropertiesARM properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_BUILTINS_PROPERTIES_ARM, nullptr
	};
	VkPhysicalDeviceProperties2 properties2 = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &properties
	};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties2);

	printf("VK_ARM_shader_core_builtins properties:\n");
	printf("\tshaderCoreMask: %" PRIu64 "\n", properties.shaderCoreMask);
	printf("\tshaderCoreCount: %u\n", properties.shaderCoreCount);
	printf("\tshaderWarpsPerCore: %u\n", properties.shaderWarpsPerCore);
	assert(properties.shaderCoreMask != 0);
	assert(properties.shaderCoreCount != 0);
	assert(properties.shaderWarpsPerCore != 0);
	test_marker_mention(vulkan, "Queried VK_ARM_shader_core_builtins", VK_OBJECT_TYPE_PHYSICAL_DEVICE, (uint64_t)vulkan.physical);

	bench_stop_iteration(vulkan.bench);
	test_done(vulkan);
	return 0;
}
