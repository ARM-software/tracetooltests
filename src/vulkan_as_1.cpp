#include "vulkan_common.h"

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.device_extensions.push_back("VK_KHR_acceleration_structure");
	reqs.apiVersion = VK_API_VERSION_1_2;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_1", reqs);
	VkResult result;

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
	testFreeMemory(vulkan, memory);

	test_done(vulkan);
	return 0;
}
