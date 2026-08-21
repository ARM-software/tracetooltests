#include "vulkan_common.h"

alignas(uint32_t)
#include "vulkan_device_fault_ext_comp.inc"

#pragma GCC diagnostic ignored "-Wunused-function"

static bool compile_only = false;

static PFN_vkGetDeviceFaultInfoEXT query_function(const vulkan_setup_t& vulkan)
{
	PFN_vkGetDeviceFaultInfoEXT function =
		reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(vkGetDeviceProcAddr(vulkan.device, "vkGetDeviceFaultInfoEXT"));
	assert(function);
	return function;
}

static void show_usage()
{
	printf("--compile-only         Create the invalid-address shader pipeline without dispatching it\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)argc;
	(void)reqs;
	if (match(argv[i], "-co", "--compile-only"))
	{
		compile_only = true;
		return true;
	}
	return false;
}

static VkShaderModule create_shader_module(const vulkan_setup_t& vulkan)
{
	VkShaderModuleCreateInfo shader_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	shader_info.codeSize = vulkan_device_fault_ext_comp_spirv_len;
	shader_info.pCode = reinterpret_cast<const uint32_t*>(vulkan_device_fault_ext_comp_spirv);

	VkShaderModule shader = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &shader_info, nullptr, &shader);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shader, "device_fault_ext_shader");
	return shader;
}

static VkPipelineLayout create_pipeline_layout(const vulkan_setup_t& vulkan)
{
	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	VkPushConstantRange push_constant_range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkDeviceAddress) };
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &push_constant_range;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkResult result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &layout);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)layout, "device_fault_ext_pipeline_layout");
	return layout;
}

static VkPipeline create_pipeline(const vulkan_setup_t& vulkan, VkPipelineLayout layout, VkShaderModule shader)
{
	VkPipelineShaderStageCreateInfo stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_info.module = shader;
	stage_info.pName = "main";

	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stage = stage_info;
	pipeline_info.layout = layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "device_fault_ext_pipeline");
	return pipeline;
}

static VkResult submit_fault_dispatch(const vulkan_setup_t& vulkan, VkPipeline pipeline, VkPipelineLayout layout,
	                                  VkDeviceAddress address, VkCommandPool& pool, VkCommandBuffer& command_buffer)
{
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkResult result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &pool);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)pool, "device_fault_ext_command_pool");

	VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_info.commandPool = pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "device_fault_ext_command_buffer");

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdPushConstants(command_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(address), &address);
	vkCmdDispatch(command_buffer, 1, 1, 1);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	test_marker_mention(vulkan, "Dispatching invalid buffer device address write", VK_OBJECT_TYPE_COMMAND_BUFFER,
	                    (uint64_t)command_buffer);

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	if (result == VK_SUCCESS)
	{
		result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	}
	vkDestroyFence(vulkan.device, fence, nullptr);
	return result;
}

struct fault_buffer
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress address = 0;
};

static fault_buffer create_fault_buffer(const vulkan_setup_t& vulkan)
{
	fault_buffer resource;
	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = sizeof(uint32_t);
	buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &resource.buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, "device_fault_ext_buffer");

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &requirements);
	VkMemoryAllocateFlagsInfo flags = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		nullptr,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
		0,
	};
	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);

	VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	address_info.buffer = resource.buffer;
	resource.address = vkGetBufferDeviceAddress(vulkan.device, &address_info);
	assert(resource.address != 0);
	return resource;
}

static void query_fault_info(PFN_vkGetDeviceFaultInfoEXT get_device_fault_info, const vulkan_setup_t& vulkan)
{
	VkDeviceFaultCountsEXT counts = { VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT, nullptr };
	VkResult result = get_device_fault_info(vulkan.device, &counts, nullptr);
	check(result);

	printf("EXT fault counts: addresses=%u vendor=%u vendorBinarySize=%llu\n", counts.addressInfoCount,
	       counts.vendorInfoCount, (unsigned long long)counts.vendorBinarySize);
	std::vector<VkDeviceFaultAddressInfoEXT> address_infos(counts.addressInfoCount);
	std::vector<VkDeviceFaultVendorInfoEXT> vendor_infos(counts.vendorInfoCount);
	std::vector<uint8_t> vendor_binary(counts.vendorBinarySize);

	VkDeviceFaultInfoEXT info = { VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT, nullptr };
	info.pAddressInfos = address_infos.data();
	info.pVendorInfos = vendor_infos.data();
	info.pVendorBinaryData = vendor_binary.data();
	result = get_device_fault_info(vulkan.device, &counts, &info);
	check(result);

	printf("EXT fault description: \"%s\"\n", info.description);
	assert(counts.addressInfoCount == address_infos.size());
	assert(counts.vendorInfoCount == vendor_infos.size());
	assert(counts.vendorBinarySize == vendor_binary.size());

	for (uint32_t i = 0; i < counts.addressInfoCount; i++)
	{
		const VkDeviceFaultAddressInfoEXT& address = address_infos.at(i);
		printf("Address %u: type=%u reported=0x%llx precision=0x%llx\n", i, address.addressType,
		       (unsigned long long)address.reportedAddress, (unsigned long long)address.addressPrecision);
	}
	for (uint32_t i = 0; i < counts.vendorInfoCount; i++)
	{
		const VkDeviceFaultVendorInfoEXT& vendor = vendor_infos.at(i);
		printf("Vendor %u: code=%llu data=%llu description=\"%s\"\n", i,
		       (unsigned long long)vendor.vendorFaultCode, (unsigned long long)vendor.vendorFaultData,
		       vendor.description);
	}
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceFaultFeaturesEXT fault_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
		nullptr,
		VK_TRUE,
		VK_FALSE,
	};

	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.required_queue_flags = VK_QUEUE_COMPUTE_BIT;
	reqs.bufferDeviceAddress = true;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.device_extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&fault_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_device_fault_ext", reqs);
	PFN_vkGetDeviceFaultInfoEXT get_device_fault_info = query_function(vulkan);

	bench_start_iteration(vulkan.bench);

	VkShaderModule shader = create_shader_module(vulkan);
	VkPipelineLayout layout = create_pipeline_layout(vulkan);
	VkPipeline pipeline = create_pipeline(vulkan, layout, shader);
	fault_buffer buffer = create_fault_buffer(vulkan);
	const VkDeviceAddress invalid_address = buffer.address;
	vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
	vkFreeMemory(vulkan.device, buffer.memory, nullptr);
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;

	if (compile_only)
	{
		printf("VK_EXT_device_fault information cannot be queried before device loss\n");
	}
	else
	{
		const VkResult fault_result =
			submit_fault_dispatch(vulkan, pipeline, layout, invalid_address, pool, command_buffer);
		printf("Invalid address dispatch returned %d\n", fault_result);
		if (get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
		{
			assert(fault_result == VK_ERROR_DEVICE_LOST);
			query_fault_info(get_device_fault_info, vulkan);
		}
	}

	bench_stop_iteration(vulkan.bench);

	if (pool != VK_NULL_HANDLE)
	{
		vkFreeCommandBuffers(vulkan.device, pool, 1, &command_buffer);
		vkDestroyCommandPool(vulkan.device, pool, nullptr);
	}
	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, layout, nullptr);
	vkDestroyShaderModule(vulkan.device, shader, nullptr);

	test_done(vulkan);
	return 0;
}
