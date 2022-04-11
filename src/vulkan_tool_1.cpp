#include "vulkan_common.h"

void usage()
{
}

int main()
{
	vulkan_setup_t vulkan = test_init("vulkan_tool_1", { "VK_EXT_tooling_info" });
	VkResult result;
	std::vector<VkPhysicalDeviceToolPropertiesEXT> tools;
	uint32_t toolCount = 0;
	PFN_vkGetPhysicalDeviceToolPropertiesEXT ppGetPhysicalDeviceToolPropertiesEXT = (PFN_vkGetPhysicalDeviceToolPropertiesEXT)vkGetDeviceProcAddr(vulkan.device, "vkGetPhysicalDeviceToolPropertiesEXT");
	assert(ppGetPhysicalDeviceToolPropertiesEXT);
	result = ppGetPhysicalDeviceToolPropertiesEXT(vulkan.physical, &toolCount, NULL);
	assert(result == VK_SUCCESS);
	tools.resize(toolCount);
	printf("%u tools in use:\n", toolCount); // should be 1 for most runs
	result = ppGetPhysicalDeviceToolPropertiesEXT(vulkan.physical, &toolCount, tools.data());
	assert(result == VK_SUCCESS);
	for (VkPhysicalDeviceToolPropertiesEXT &tool : tools)
	{
		printf("\t%s %s\n", tool.name, tool.version);
	}
	test_done(vulkan);
	return 0;
}
