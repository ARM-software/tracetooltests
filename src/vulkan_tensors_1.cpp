#include "vulkan_common.h"
#include <inttypes.h>

static vulkan_req_t reqs;

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceTensorFeaturesARM tensor_features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TENSOR_FEATURES_ARM, nullptr };
	tensor_features.tensors = VK_TRUE;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.device_extensions.push_back("VK_ARM_tensors");
	reqs.extension_features = (VkBaseInStructure*)&tensor_features;
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_tensors_1", reqs);
	VkResult r;

	bench_start_iteration(vulkan.bench);

	MAKEDEVICEPROCADDR(vulkan, vkCreateTensorARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetTensorMemoryRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetDeviceTensorMemoryRequirementsARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateTensorViewARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyTensorViewARM);
	MAKEDEVICEPROCADDR(vulkan, vkBindTensorMemoryARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetPhysicalDeviceExternalTensorPropertiesARM);
	//TBD
	//MAKEDEVICEPROCADDR(vulkan, vkCmdCopyTensorARM);

	int64_t dimension = 64;
	VkTensorDescriptionARM td = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
	td.tiling = VK_TENSOR_TILING_LINEAR_ARM;
	td.format = VK_FORMAT_R8_UINT;
	td.dimensionCount = 1;
	td.pDimensions = &dimension;
	td.pStrides = nullptr;
	td.usage = VK_TENSOR_USAGE_TRANSFER_SRC_BIT_ARM | VK_TENSOR_USAGE_TRANSFER_DST_BIT_ARM;
	VkTensorCreateInfoARM tci = { VK_STRUCTURE_TYPE_TENSOR_CREATE_INFO_ARM, nullptr };
	tci.flags = 0;
	tci.pDescription = &td;
	tci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	tci.queueFamilyIndexCount = 0;
	tci.pQueueFamilyIndices = nullptr;
	VkTensorARM tensor = VK_NULL_HANDLE;
	r = pf_vkCreateTensorARM(vulkan.device, &tci, nullptr, &tensor);
	check(r);

	test_set_name(vulkan, VK_OBJECT_TYPE_TENSOR_ARM, (uint64_t)tensor, "Our tensor object");

	VkPhysicalDeviceExternalTensorInfoARM pdeti = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_TENSOR_INFO_ARM, nullptr };
	pdeti.flags = 0;
	pdeti.pDescription = &td;
	pdeti.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	VkExternalTensorPropertiesARM etp = { VK_STRUCTURE_TYPE_EXTERNAL_TENSOR_PROPERTIES_ARM, nullptr };
	pf_vkGetPhysicalDeviceExternalTensorPropertiesARM(vulkan.physical, &pdeti, &etp);

	VkTensorViewCreateInfoARM tvci = { VK_STRUCTURE_TYPE_TENSOR_VIEW_CREATE_INFO_ARM, nullptr };
	tvci.flags = 0;
	tvci.tensor = tensor;
	tvci.format = VK_FORMAT_R8_UINT;
	VkTensorViewARM tensor_view = VK_NULL_HANDLE;
	r = pf_vkCreateTensorViewARM(vulkan.device, &tvci, nullptr, &tensor_view);
	check(r);

	test_set_name(vulkan, VK_OBJECT_TYPE_TENSOR_VIEW_ARM, (uint64_t)tensor, "Our tensor view object");

	VkMemoryRequirements2 memreq_dev = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };
	VkMemoryRequirements2 memreq = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr };

	VkDeviceTensorMemoryRequirementsARM dtmr = { VK_STRUCTURE_TYPE_DEVICE_TENSOR_MEMORY_REQUIREMENTS_ARM, nullptr };
	dtmr.pCreateInfo = &tci;
	pf_vkGetDeviceTensorMemoryRequirementsARM(vulkan.device, &dtmr, &memreq_dev);

	VkTensorMemoryRequirementsInfoARM tmri = { VK_STRUCTURE_TYPE_TENSOR_MEMORY_REQUIREMENTS_INFO_ARM, nullptr };
	tmri.tensor = tensor;
	pf_vkGetTensorMemoryRequirementsARM(vulkan.device, &tmri, &memreq);

	// Should have same result
	assert(memreq.memoryRequirements.memoryTypeBits == memreq_dev.memoryRequirements.memoryTypeBits);
	assert(memreq.memoryRequirements.alignment == memreq_dev.memoryRequirements.alignment);
	assert(memreq.memoryRequirements.size == memreq_dev.memoryRequirements.size);

	const uint32_t memoryTypeIndex = get_device_memory_type(memreq.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	const uint32_t align_mod = memreq.memoryRequirements.size % memreq.memoryRequirements.alignment;
	const uint32_t aligned_size = (align_mod == 0) ? memreq.memoryRequirements.size : (memreq.memoryRequirements.size + memreq.memoryRequirements.alignment - align_mod);

	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = aligned_size;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	r = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(r);
	assert(memory != VK_NULL_HANDLE);

	VkBindTensorMemoryInfoARM btmi = { VK_STRUCTURE_TYPE_BIND_TENSOR_MEMORY_INFO_ARM, nullptr };
	btmi.tensor = tensor;
	btmi.memory = memory;
	btmi.memoryOffset = 0;
	r = pf_vkBindTensorMemoryARM(vulkan.device, 1, &btmi);

	pf_vkDestroyTensorViewARM(vulkan.device, VK_NULL_HANDLE, nullptr);
	pf_vkDestroyTensorViewARM(vulkan.device, tensor_view, nullptr);
	pf_vkDestroyTensorARM(vulkan.device, tensor, nullptr);
	pf_vkDestroyTensorARM(vulkan.device, VK_NULL_HANDLE, nullptr);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
