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
	reqs.instance_extensions.push_back("VK_EXT_debug_report");
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
	pf_vkDebugReportMessageEXT(vulkan.instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT, (uint64_t)vulkan.physical, 0, 0, "vulkan_debug_report", "physical device test");

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = 0;
	pAllocateMemInfo.allocationSize = 1024;
	VkDeviceMemory memory = 0;
	VkResult result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != 0);
	pf_vkDebugReportMessageEXT(vulkan.instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, (uint64_t)memory, 0, 0, "vulkan_debug_report", "memory test");
	vkFreeMemory(vulkan.device, memory, nullptr);

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	pf_vkDebugReportMessageEXT(vulkan.instance, VK_DEBUG_REPORT_DEBUG_BIT_EXT, VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, (uint64_t)queue, 0, 0, "vulkan_debug_report", "queue test");

	pf_vkDestroyDebugReportCallbackEXT(vulkan.instance, cb, nullptr);
	test_done(vulkan);
	return 0;
}
