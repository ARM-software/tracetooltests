#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_EXT_debug_marker");
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_debug_marker", reqs);
	VkResult result;

	// Test VK_EXT_debug_marker
	PFN_vkDebugMarkerSetObjectTagEXT ppDebugMarkerSetObjectTagEXT = (PFN_vkDebugMarkerSetObjectTagEXT)vkGetDeviceProcAddr(vulkan.device, "vkDebugMarkerSetObjectTagEXT");
	PFN_vkDebugMarkerSetObjectNameEXT ppDebugMarkerSetObjectNameEXT = (PFN_vkDebugMarkerSetObjectNameEXT)vkGetDeviceProcAddr(vulkan.device, "vkDebugMarkerSetObjectNameEXT");
	PFN_vkCmdDebugMarkerBeginEXT ppCmdDebugMarkerBeginEXT = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(vulkan.device, "vkCmdDebugMarkerBeginEXT");
	PFN_vkCmdDebugMarkerEndEXT ppCmdDebugMarkerEndEXT = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(vulkan.device, "vkCmdDebugMarkerEndEXT");
	PFN_vkCmdDebugMarkerInsertEXT ppCmdDebugMarkerInsertEXT = (PFN_vkCmdDebugMarkerInsertEXT)vkGetDeviceProcAddr(vulkan.device, "vkCmdDebugMarkerInsertEXT");

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	std::vector<char> payload(60);
	memset(payload.data(), 'a', payload.size());
	VkDebugMarkerObjectNameInfoEXT device_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, (uint64_t)vulkan.device, "Our device" };
	VkDebugMarkerObjectNameInfoEXT instance_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT, (uint64_t)vulkan.instance, "Our instance" };
	VkDebugMarkerObjectNameInfoEXT physical_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT, (uint64_t)vulkan.physical, "Our physical device" };
	VkDebugMarkerObjectNameInfoEXT queue_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT, (uint64_t)queue, "Our queue" };
	VkDebugMarkerObjectTagInfoEXT device_tag = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_TAG_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT, (uint64_t)vulkan.device, 42, payload.size(), payload.data() };
	VkDebugMarkerMarkerInfoEXT begin_marker = { VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT, nullptr, "begin marker", { 1.0f, 1.0f, 1.0f, 1.0f } };
	VkDebugMarkerMarkerInfoEXT insert_marker = { VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT, nullptr, "insert marker", { 1.0f, 1.0f, 1.0f, 1.0f } };

	ppDebugMarkerSetObjectTagEXT(vulkan.device, &device_tag);

	ppDebugMarkerSetObjectNameEXT(vulkan.device, &device_name);
	ppDebugMarkerSetObjectNameEXT(vulkan.device, &instance_name);
	ppDebugMarkerSetObjectNameEXT(vulkan.device, &physical_name);
	ppDebugMarkerSetObjectNameEXT(vulkan.device, &queue_name);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(result);
	VkDebugMarkerObjectNameInfoEXT fence_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT, (uint64_t)fence, "Our fence" };
	ppDebugMarkerSetObjectNameEXT(vulkan.device, &fence_name);

	VkCommandPoolCreateInfo command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_create_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, NULL, &command_pool);
	check(result);
	VkDebugMarkerObjectNameInfoEXT pool_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT, (uint64_t)command_pool, "Our cmd pool" };
	ppDebugMarkerSetObjectNameEXT(vulkan.device, &pool_name);

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer);
	check(result);
	VkDebugMarkerObjectNameInfoEXT cmdbuf_name = { VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, nullptr, VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT, (uint64_t)command_buffer, "Our commandbuffer" };
	ppDebugMarkerSetObjectNameEXT(vulkan.device, &cmdbuf_name);

	VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
	check(result);
	ppCmdDebugMarkerBeginEXT(command_buffer, &begin_marker);
	ppCmdDebugMarkerInsertEXT(command_buffer, &insert_marker);
	ppCmdDebugMarkerEndEXT(command_buffer);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);

	// Cleanup...
	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	test_done(vulkan);
	return 0;
}
