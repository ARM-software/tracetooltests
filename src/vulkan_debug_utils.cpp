#include "vulkan_common.h"

static VkBool32 callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	assert((uint64_t)pUserData == 0xdeadbeef);
	const char* id = pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "vulkan_debug_utils";
	printf("output from %s: %s\n", id, pCallbackData->pMessage);
	return VK_TRUE;
}

static void submit_message(PFN_vkSubmitDebugUtilsMessageEXT submit, VkInstance instance, VkObjectType object_type, uint64_t object_handle, const char* id_name, const char* message)
{
	VkDebugUtilsObjectNameInfoEXT object_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr };
	object_info.objectType = object_type;
	object_info.objectHandle = object_handle;
	object_info.pObjectName = nullptr;

	VkDebugUtilsMessengerCallbackDataEXT data = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT, nullptr };
	data.pMessageIdName = id_name;
	data.pMessage = message;
	data.objectCount = 1;
	data.pObjects = &object_info;

	submit(instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &data);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_debug_utils", reqs);

	// Test VK_EXT_debug_utils

	MAKEINSTANCEPROCADDR(vulkan, vkCreateDebugUtilsMessengerEXT);
	MAKEINSTANCEPROCADDR(vulkan, vkDestroyDebugUtilsMessengerEXT);
	MAKEINSTANCEPROCADDR(vulkan, vkSubmitDebugUtilsMessageEXT);
	MAKEDEVICEPROCADDR(vulkan, vkSetDebugUtilsObjectNameEXT);
	MAKEDEVICEPROCADDR(vulkan, vkSetDebugUtilsObjectTagEXT);
	MAKEDEVICEPROCADDR(vulkan, vkQueueBeginDebugUtilsLabelEXT);
	MAKEDEVICEPROCADDR(vulkan, vkQueueEndDebugUtilsLabelEXT);
	MAKEDEVICEPROCADDR(vulkan, vkQueueInsertDebugUtilsLabelEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginDebugUtilsLabelEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndDebugUtilsLabelEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdInsertDebugUtilsLabelEXT);

	VkDebugUtilsMessengerCreateInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, nullptr };
	info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	info.pfnUserCallback = callback;
	info.pUserData = (void*)0xdeadbeef;

	VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
	VkResult result = pf_vkCreateDebugUtilsMessengerEXT(vulkan.instance, &info, nullptr, &messenger);
	check(result);
	submit_message(pf_vkSubmitDebugUtilsMessageEXT, vulkan.instance, VK_OBJECT_TYPE_INSTANCE, (uint64_t)vulkan.instance, "vulkan_debug_utils", "instance test");
	submit_message(pf_vkSubmitDebugUtilsMessageEXT, vulkan.instance, VK_OBJECT_TYPE_DEVICE, (uint64_t)vulkan.device, "vulkan_debug_utils", "device test");
	submit_message(pf_vkSubmitDebugUtilsMessageEXT, vulkan.instance, VK_OBJECT_TYPE_PHYSICAL_DEVICE, (uint64_t)vulkan.physical, "vulkan_debug_utils", "physical device test");

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = 0;
	pAllocateMemInfo.allocationSize = 1024;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != 0);
	VkDebugUtilsObjectNameInfoEXT name_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr };
	name_info.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
	name_info.objectHandle = (uint64_t)memory;
	name_info.pObjectName = "debug utils memory";
	result = pf_vkSetDebugUtilsObjectNameEXT(vulkan.device, &name_info);
	check(result);

	const uint64_t tag_value = 0xfeedf00d;
	VkDebugUtilsObjectTagInfoEXT tag_info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_TAG_INFO_EXT, nullptr };
	tag_info.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
	tag_info.objectHandle = (uint64_t)memory;
	tag_info.tagName = 1;
	tag_info.tagSize = sizeof(tag_value);
	tag_info.pTag = &tag_value;
	result = pf_vkSetDebugUtilsObjectTagEXT(vulkan.device, &tag_info);
	check(result);
	submit_message(pf_vkSubmitDebugUtilsMessageEXT, vulkan.instance, VK_OBJECT_TYPE_DEVICE_MEMORY, (uint64_t)memory, "vulkan_debug_utils", "memory test");
	vkFreeMemory(vulkan.device, memory, nullptr);

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	name_info.objectType = VK_OBJECT_TYPE_QUEUE;
	name_info.objectHandle = (uint64_t)queue;
	name_info.pObjectName = "debug utils queue";
	result = pf_vkSetDebugUtilsObjectNameEXT(vulkan.device, &name_info);
	check(result);
	submit_message(pf_vkSubmitDebugUtilsMessageEXT, vulkan.instance, VK_OBJECT_TYPE_QUEUE, (uint64_t)queue, "vulkan_debug_utils", "queue test");

	VkDebugUtilsLabelEXT queue_label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr };
	queue_label.pLabelName = "queue label";
	queue_label.color[0] = 0.0f;
	queue_label.color[1] = 0.6f;
	queue_label.color[2] = 1.0f;
	queue_label.color[3] = 1.0f;
	pf_vkQueueBeginDebugUtilsLabelEXT(queue, &queue_label);
	pf_vkQueueInsertDebugUtilsLabelEXT(queue, &queue_label);
	pf_vkQueueEndDebugUtilsLabelEXT(queue);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo cmd_alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cmd_alloc.commandPool = command_pool;
	cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmd_alloc.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &cmd_alloc, &command_buffer);
	check(result);

	VkCommandBufferBeginInfo cmd_begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	result = vkBeginCommandBuffer(command_buffer, &cmd_begin);
	check(result);

	VkDebugUtilsLabelEXT cmd_label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr };
	cmd_label.pLabelName = "command label";
	cmd_label.color[0] = 1.0f;
	cmd_label.color[1] = 0.3f;
	cmd_label.color[2] = 0.0f;
	cmd_label.color[3] = 1.0f;
	pf_vkCmdBeginDebugUtilsLabelEXT(command_buffer, &cmd_label);
	pf_vkCmdInsertDebugUtilsLabelEXT(command_buffer, &cmd_label);
	pf_vkCmdEndDebugUtilsLabelEXT(command_buffer);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	pf_vkDestroyDebugUtilsMessengerEXT(vulkan.instance, messenger, nullptr);
	test_done(vulkan);
	return 0;
}
