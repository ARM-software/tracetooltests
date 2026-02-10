#include "vulkan_common.h"
#include <inttypes.h>

static vulkan_req_t reqs;
static bool aliasing = false;

static void show_usage()
{
	printf("-A / --alias           Add image/tensor aliasing\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-A", "--alias"))
	{
		aliasing = true;
		return true;
	}
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
	MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceExternalTensorPropertiesARM);
	MAKEDEVICEPROCADDR(vulkan, vkCmdCopyTensorARM);

	VkQueue queue;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);

	std::vector<int64_t> dimensions { 64, 64 };
	VkTensorDescriptionARM td = { VK_STRUCTURE_TYPE_TENSOR_DESCRIPTION_ARM, nullptr };
	td.tiling = VK_TENSOR_TILING_LINEAR_ARM;
	td.format = VK_FORMAT_R8_UINT;
	td.dimensionCount = dimensions.size();
	td.pDimensions = dimensions.data();
	td.pStrides = nullptr;
	td.usage = VK_TENSOR_USAGE_TRANSFER_SRC_BIT_ARM | VK_TENSOR_USAGE_TRANSFER_DST_BIT_ARM | VK_TENSOR_USAGE_SHADER_BIT_ARM;
	if (aliasing) td.usage |= VK_TENSOR_USAGE_IMAGE_ALIASING_BIT_ARM;
	VkTensorCreateInfoARM tci = { VK_STRUCTURE_TYPE_TENSOR_CREATE_INFO_ARM, nullptr };
	tci.flags = 0;
	tci.pDescription = &td;
	tci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	tci.queueFamilyIndexCount = 0;
	tci.pQueueFamilyIndices = nullptr;
	VkTensorARM tensor = VK_NULL_HANDLE;
	VkTensorARM target = VK_NULL_HANDLE;
	r = pf_vkCreateTensorARM(vulkan.device, &tci, nullptr, &tensor);
	check(r);
	r = pf_vkCreateTensorARM(vulkan.device, &tci, nullptr, &target);
	check(r);

	test_set_name(vulkan, VK_OBJECT_TYPE_TENSOR_ARM, (uint64_t)tensor, "Our tensor object");

	VkPhysicalDeviceExternalTensorInfoARM pdeti = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_TENSOR_INFO_ARM, nullptr };
	pdeti.flags = 0;
	pdeti.pDescription = &td;
	pdeti.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
	VkExternalTensorPropertiesARM etp = { VK_STRUCTURE_TYPE_EXTERNAL_TENSOR_PROPERTIES_ARM, nullptr };
	pf_vkGetPhysicalDeviceExternalTensorPropertiesARM(vulkan.physical, &pdeti, &etp);

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
	pAllocateMemInfo.allocationSize = aligned_size * 2;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	r = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(r);
	assert(memory != VK_NULL_HANDLE);

	if (aliasing)
	{
		VkImageCreateInfo imageCreateInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
		imageCreateInfo.flags = 0;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_R8_UINT;
		imageCreateInfo.extent.width = 64;
		imageCreateInfo.extent.height = 64;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR; // do note VUID-VkImageCreateInfo-tiling-09711
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.queueFamilyIndexCount = 0;
		imageCreateInfo.pQueueFamilyIndices = nullptr;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImage image;
		r = vkCreateImage(vulkan.device, &imageCreateInfo, nullptr, &image);
		assert(r == VK_SUCCESS);
		vkBindImageMemory(vulkan.device, image, memory, 0);
	}

	VkBindTensorMemoryInfoARM btmi = { VK_STRUCTURE_TYPE_BIND_TENSOR_MEMORY_INFO_ARM, nullptr };
	btmi.tensor = tensor;
	btmi.memory = memory;
	btmi.memoryOffset = 0;
	r = pf_vkBindTensorMemoryARM(vulkan.device, 1, &btmi); // potentially aliasing the image
	check(r);
	btmi.tensor = target;
	btmi.memoryOffset = aligned_size;
	r = pf_vkBindTensorMemoryARM(vulkan.device, 1, &btmi); // not aliasing any image
	check(r);

	VkTensorViewCreateInfoARM tvci = { VK_STRUCTURE_TYPE_TENSOR_VIEW_CREATE_INFO_ARM, nullptr };
	tvci.flags = 0;
	tvci.tensor = tensor;
	tvci.format = VK_FORMAT_R8_UINT;
	VkTensorViewARM tensor_view = VK_NULL_HANDLE;
	r = pf_vkCreateTensorViewARM(vulkan.device, &tvci, nullptr, &tensor_view);
	check(r);

	test_set_name(vulkan, VK_OBJECT_TYPE_TENSOR_VIEW_ARM, (uint64_t)tensor_view, "Our tensor view object");

	VkCommandPool command_pool;
	VkCommandPoolCreateInfo command_pool_create_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, NULL, &command_pool);
	check(result);

	VkCommandBuffer command_buffer;
	VkCommandBufferAllocateInfo command_buffer_allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_allocate_info.commandPool = command_pool;
	command_buffer_allocate_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &command_buffer);
	check(result);

	VkFence fence;
	VkFenceCreateInfo fence_create_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	result = vkCreateFence(vulkan.device, &fence_create_info, NULL, &fence);
	check(result);

	VkCommandBufferBeginInfo command_buffer_begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	command_buffer_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info);
	check(result);

	VkTensorCopyARM tc = { VK_STRUCTURE_TYPE_TENSOR_COPY_ARM, nullptr };
	tc.dimensionCount = dimensions.size();
	tc.pSrcOffset = nullptr;
	tc.pDstOffset = nullptr;
	tc.pExtent = nullptr;
	VkCopyTensorInfoARM cti = { VK_STRUCTURE_TYPE_COPY_TENSOR_INFO_ARM, nullptr };
	cti.srcTensor = tensor;
	cti.dstTensor = target;
	cti.regionCount = 1;
	cti.pRegions = &tc;
	pf_vkCmdCopyTensorARM(command_buffer, &cti);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);

	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	pf_vkDestroyTensorViewARM(vulkan.device, VK_NULL_HANDLE, nullptr);
	pf_vkDestroyTensorViewARM(vulkan.device, tensor_view, nullptr);
	pf_vkDestroyTensorARM(vulkan.device, VK_NULL_HANDLE, nullptr);
	pf_vkDestroyTensorARM(vulkan.device, tensor, nullptr);
	pf_vkDestroyTensorARM(vulkan.device, target, nullptr);
	testFreeMemory(vulkan, memory);

	bench_stop_iteration(vulkan.bench);

	test_done(vulkan);

	return 0;
}
