#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.extensions.push_back("VK_EXT_tooling_info");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_tool_1", reqs);
	VkResult result;

	// Test VK_EXT_tooling_info
	std::vector<VkPhysicalDeviceToolPropertiesEXT> tools;
	uint32_t toolCount = 0;
	PFN_vkGetPhysicalDeviceToolPropertiesEXT ppGetPhysicalDeviceToolPropertiesEXT = (PFN_vkGetPhysicalDeviceToolPropertiesEXT)vkGetInstanceProcAddr(vulkan.instance, "vkGetPhysicalDeviceToolPropertiesEXT");
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
