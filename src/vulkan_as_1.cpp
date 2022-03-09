#include "vulkan_common.h"

void usage()
{
}

int main()
{
	vulkan_setup_t vulkan = test_init("vulkan_as_1", { "VK_KHR_acceleration_structure" });
	VkResult result;

	VkBuffer buffer;
	VkBufferCreateInfo bufferCreateInfo = {};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = 1024 * 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
	check(result);

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &req);
	uint32_t memoryTypeIndex = get_device_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkMemoryAllocateInfo pAllocateMemInfo = {};
	pAllocateMemInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = req.size;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != 0);

	vkBindBufferMemory(vulkan.device, buffer, memory, 0);

	auto ttCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(vulkan.device, "vkCreateAccelerationStructureKHR");
	assert(ttCreateAccelerationStructureKHR);
	auto ttGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(vulkan.device, "vkGetAccelerationStructureDeviceAddressKHR");
	assert(ttGetAccelerationStructureDeviceAddressKHR);
	auto ttDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(vulkan.device, "vkDestroyAccelerationStructureKHR");
	assert(ttDestroyAccelerationStructureKHR);

	VkAccelerationStructureCreateInfoKHR asinfo = {};
	asinfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	asinfo.createFlags = 0;
	asinfo.buffer = buffer;
	asinfo.offset = 0; // "offset must be a multiple of 256 bytes"
	asinfo.size = 0;
	asinfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	asinfo.deviceAddress = 0;
	VkAccelerationStructureKHR as;
	result = ttCreateAccelerationStructureKHR(vulkan.device, &asinfo, nullptr, &as);
	check(result);

	VkAccelerationStructureDeviceAddressInfoKHR dai = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, as };
	VkDeviceAddress addr = ttGetAccelerationStructureDeviceAddressKHR(vulkan.device, &dai);
	(void)addr; // do nothing with it

	ttDestroyAccelerationStructureKHR(vulkan.device, as, nullptr);
	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);

	test_done(vulkan);
	return 0;
}
