#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accfeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr, VK_TRUE };
	reqs.device_extensions.push_back("VK_KHR_acceleration_structure");
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.extension_features = (VkBaseInStructure*)&accfeats;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.bufferDeviceAddress = true;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_1", reqs);
	VkResult result;
	VkQueue queue;

	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	VkPhysicalDeviceAccelerationStructureFeaturesKHR accel = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr };
	VkPhysicalDeviceFeatures2 feat2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &accel };
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &feat2);
	printf("Acceleration structure features:\n");
	printf("\taccelerationStructure = %s\n", accel.accelerationStructure ? "true" : "false");
	printf("\taccelerationStructureCaptureReplay = %s\n", accel.accelerationStructureCaptureReplay ? "true" : "false");
	printf("\taccelerationStructureIndirectBuild = %s\n", accel.accelerationStructureIndirectBuild ? "true" : "false");
	printf("\taccelerationStructureHostCommands = %s\n", accel.accelerationStructureHostCommands ? "true" : "false");
	printf("\tdescriptorBindingAccelerationStructureUpdateAfterBind = %s\n", accel.descriptorBindingAccelerationStructureUpdateAfterBind ? "true" : "false");

	VkBuffer buffer;
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024 * 1024;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &buffer);
	check(result);

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(vulkan.device, buffer, &req);
	uint32_t memoryTypeIndex = get_device_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr };
	allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &allocFlagsInfo };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = req.size;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != 0);

	vkBindBufferMemory(vulkan.device, buffer, memory, 0);

	MAKEDEVICEPROCADDR(vulkan, vkCreateAccelerationStructureKHR);
	MAKEDEVICEPROCADDR(vulkan, vkGetAccelerationStructureDeviceAddressKHR);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyAccelerationStructureKHR);

	VkAccelerationStructureCreateInfoKHR asinfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr };
	asinfo.createFlags = 0;
	asinfo.buffer = buffer;
	asinfo.offset = 0; // "offset must be a multiple of 256 bytes"
	asinfo.size = 0;
	asinfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	asinfo.deviceAddress = 0;
	VkAccelerationStructureKHR as;
	result = pf_vkCreateAccelerationStructureKHR(vulkan.device, &asinfo, nullptr, &as);
	check(result);

	VkAccelerationStructureDeviceAddressInfoKHR dai = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, nullptr, as };
	VkDeviceAddress addr = pf_vkGetAccelerationStructureDeviceAddressKHR(vulkan.device, &dai);
	(void)addr; // do nothing with it

	// just submit it somewhere
	testQueueBuffer(vulkan, queue, { buffer });

	pf_vkDestroyAccelerationStructureKHR(vulkan.device, as, nullptr);
	vkDestroyBuffer(vulkan.device, buffer, nullptr);
	testFreeMemory(vulkan, memory);

	test_done(vulkan);
	return 0;
}
