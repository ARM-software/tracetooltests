#include "vulkan_common.h"

#include <algorithm>
#include <cstring>

#include "vulkan_device_fault.inc"

#pragma GCC diagnostic ignored "-Wunused-function"

static constexpr uint32_t ABORT_MAGIC = 0xdeadbeef;
static bool compile_only = false;

struct device_fault_functions
{
	PFN_vkGetDeviceFaultReportsKHR vkGetDeviceFaultReportsKHR = nullptr;
	PFN_vkGetDeviceFaultDebugInfoKHR vkGetDeviceFaultDebugInfoKHR = nullptr;
};

static device_fault_functions query_functions(const vulkan_setup_t& vulkan)
{
	device_fault_functions functions;
	functions.vkGetDeviceFaultReportsKHR =
		reinterpret_cast<PFN_vkGetDeviceFaultReportsKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetDeviceFaultReportsKHR"));
	functions.vkGetDeviceFaultDebugInfoKHR =
		reinterpret_cast<PFN_vkGetDeviceFaultDebugInfoKHR>(vkGetDeviceProcAddr(vulkan.device, "vkGetDeviceFaultDebugInfoKHR"));
	assert(functions.vkGetDeviceFaultReportsKHR);
	assert(functions.vkGetDeviceFaultDebugInfoKHR);
	return functions;
}

static void show_usage()
{
	printf("--compile-only         Create the abort shader pipeline and query empty fault state without dispatching it\n");
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
	shader_info.codeSize = vulkan_device_fault_spirv_len;
	shader_info.pCode = vulkan_device_fault_spirv;

	VkShaderModule shader = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &shader_info, nullptr, &shader);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)shader, "device_fault_abort_shader");
	return shader;
}

static VkPipelineLayout create_pipeline_layout(const vulkan_setup_t& vulkan)
{
	VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkResult result = vkCreatePipelineLayout(vulkan.device, &layout_info, nullptr, &layout);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)layout, "device_fault_pipeline_layout");
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
	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline, "device_fault_abort_pipeline");
	return pipeline;
}

static VkResult submit_abort_dispatch(const vulkan_setup_t& vulkan, VkPipeline pipeline, VkCommandPool& pool, VkCommandBuffer& command_buffer)
{
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkResult result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &pool);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)pool, "device_fault_command_pool");

	VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_info.commandPool = pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "device_fault_command_buffer");

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdDispatch(command_buffer, 1, 1, 1);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	test_marker_mention(vulkan, "Dispatching shader abort", VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer);

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

static bool message_contains_magic(const std::vector<uint8_t>& message_data)
{
	size_t offset = 0;
	while (offset + sizeof(uint64_t) <= message_data.size())
	{
		uint64_t payload_size = 0;
		std::memcpy(&payload_size, message_data.data() + offset, sizeof(payload_size));
		offset += sizeof(payload_size);

		if (payload_size > message_data.size() - offset)
		{
			printf("Malformed shader abort message: payload size %llu exceeds remaining %zu bytes\n",
			       (unsigned long long)payload_size, message_data.size() - offset);
			return false;
		}
		if (payload_size >= sizeof(ABORT_MAGIC))
		{
			uint32_t payload = 0;
			std::memcpy(&payload, message_data.data() + offset, sizeof(payload));
			if (payload == ABORT_MAGIC)
			{
				return true;
			}
		}

		offset += payload_size;
		offset = (offset + 7) & ~size_t(7);
	}
	return false;
}

static void query_fault_reports(const vulkan_setup_t& vulkan, const device_fault_functions& functions)
{
	uint32_t fault_count = 0;
	VkResult result = functions.vkGetDeviceFaultReportsKHR(vulkan.device, 0, &fault_count, nullptr);
	check(result);
	assert(fault_count > 0);

	std::vector<VkDeviceFaultInfoKHR> fault_infos(fault_count);
	for (VkDeviceFaultInfoKHR& info : fault_infos)
	{
		info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;
	}

	result = functions.vkGetDeviceFaultReportsKHR(vulkan.device, 0, &fault_count, fault_infos.data());
	check(result);
	assert(fault_count > 0);

	for (uint32_t i = 0; i < fault_count; i++)
	{
		const VkDeviceFaultInfoKHR& info = fault_infos.at(i);
		printf("Fault %u: flags=0x%x group=%llu description=\"%s\"\n",
		       i, info.flags, (unsigned long long)info.groupId, info.description);
		assert(info.flags & VK_DEVICE_FAULT_FLAG_DEVICE_LOST_KHR);
	}
}

static void query_shader_abort_debug_info(const vulkan_setup_t& vulkan, const device_fault_functions& functions)
{
	VkDeviceFaultShaderAbortMessageInfoKHR message_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR,
		nullptr,
		0,
		nullptr,
	};
	VkDeviceFaultDebugInfoKHR debug_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR,
		&message_info,
		0,
		nullptr,
	};

	VkResult result = functions.vkGetDeviceFaultDebugInfoKHR(vulkan.device, &debug_info);
	check(result);
	assert(message_info.messageDataSize > 0);

	std::vector<uint8_t> message_data(message_info.messageDataSize);
	message_info.pMessageData = message_data.data();
	result = functions.vkGetDeviceFaultDebugInfoKHR(vulkan.device, &debug_info);
	check(result);

	printf("Retrieved %llu bytes of shader abort messages\n", (unsigned long long)message_info.messageDataSize);
	assert(message_contains_magic(message_data));
}

static void query_empty_fault_state(const vulkan_setup_t& vulkan, const device_fault_functions& functions)
{
	VkDeviceFaultShaderAbortMessageInfoKHR message_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR,
		nullptr,
		0,
		nullptr,
	};
	VkDeviceFaultDebugInfoKHR debug_info = {
		VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR,
		&message_info,
		0,
		nullptr,
	};
	VkResult result = functions.vkGetDeviceFaultDebugInfoKHR(vulkan.device, &debug_info);
	check(result);
	assert(message_info.messageDataSize == 0);

	uint32_t fault_count = UINT32_MAX;
	result = functions.vkGetDeviceFaultReportsKHR(vulkan.device, 0, &fault_count, nullptr);
	if (result == VK_TIMEOUT)
	{
		printf("No device fault report available before dispatch\n");
		return;
	}
	check(result);
	assert(fault_count == 0);
}

int main(int argc, char** argv)
{
	VkPhysicalDeviceShaderAbortFeaturesKHR shader_abort_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_FEATURES_KHR,
		nullptr,
		VK_TRUE,
	};
	VkPhysicalDeviceShaderConstantDataFeaturesKHR shader_constant_data_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CONSTANT_DATA_FEATURES_KHR,
		&shader_abort_features,
		VK_TRUE,
	};
	VkPhysicalDeviceFaultFeaturesKHR fault_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR,
		&shader_constant_data_features,
		VK_TRUE,
		VK_FALSE,
		VK_FALSE,
		VK_FALSE,
	};

	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.required_queue_flags = VK_QUEUE_COMPUTE_BIT;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	reqs.device_extensions.push_back(VK_KHR_DEVICE_FAULT_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_ABORT_EXTENSION_NAME);
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&fault_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_device_fault", reqs);
	const device_fault_functions functions = query_functions(vulkan);

	VkPhysicalDeviceFaultPropertiesKHR fault_properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_PROPERTIES_KHR, nullptr };
	VkPhysicalDeviceShaderAbortPropertiesKHR shader_abort_properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_PROPERTIES_KHR,
		&fault_properties,
	};
	VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &shader_abort_properties };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);
	printf("maxDeviceFaultCount=%u maxShaderAbortMessageSize=%llu\n",
	       fault_properties.maxDeviceFaultCount,
	       (unsigned long long)shader_abort_properties.maxShaderAbortMessageSize);
	assert(fault_properties.maxDeviceFaultCount > 0);

	bench_start_iteration(vulkan.bench);

	VkShaderModule shader = create_shader_module(vulkan);
	VkPipelineLayout layout = create_pipeline_layout(vulkan);
	VkPipeline pipeline = create_pipeline(vulkan, layout, shader);
	VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;

	if (compile_only)
	{
		query_empty_fault_state(vulkan, functions);
	}
	else
	{
		const VkResult abort_result = submit_abort_dispatch(vulkan, pipeline, pool, command_buffer);
		assert(abort_result == VK_ERROR_DEVICE_LOST);

		query_fault_reports(vulkan, functions);
		query_shader_abort_debug_info(vulkan, functions);
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
