#include "vulkan_common.h"

static VkBool32 callback(
    VkDebugReportFlagsEXT                       flags,
    VkDebugReportObjectTypeEXT                  objectType,
    uint64_t                                    object,
    size_t                                      location,
    int32_t                                     messageCode,
    const char*                                 pLayerPrefix,
    const char*                                 pMessage,
    void*                                       pUserData)
{
	assert((uint64_t)pUserData == 0xdeadbeef);
	printf("output from %s: %s\n", pLayerPrefix, pMessage);
	return VK_TRUE;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_EXT_debug_report");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_debug_report", reqs);

	// Test VK_EXT_debug_report

	MAKEINSTANCEPROCADDR(vulkan, vkCreateDebugReportCallbackEXT);
	MAKEINSTANCEPROCADDR(vulkan, vkDestroyDebugReportCallbackEXT);
	MAKEINSTANCEPROCADDR(vulkan, vkDebugReportMessageEXT);

	VkDebugReportCallbackCreateInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT, nullptr };
	info.flags = VK_DEBUG_REPORT_DEBUG_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
	info.pfnCallback = callback;
	info.pUserData = (void*)0xdeadbeef;

	VkDebugReportCallbackEXT cb = VK_NULL_HANDLE;
	pf_vkCreateDebugReportCallbackEXT(vulkan.instance, &info, nullptr, &cb);
	pf_vkDebugReportMessageEXT(vulkan.instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT, (uint64_t)vulkan.instance, 0, 0, "vulkan_debug_report", "instance test");
	pf_vkDebugReportMessageEXT(vulkan.instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, (uint64_t)vulkan.device, 0, 0, "vulkan_debug_report", "device test");
	pf_vkDestroyDebugReportCallbackEXT(vulkan.instance, cb, nullptr);

	test_done(vulkan);
	return 0;
}
