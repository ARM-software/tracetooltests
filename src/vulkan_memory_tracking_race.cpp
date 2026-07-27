// Exercise host memory tracking while an earlier queue submission is still writing.
// The second submission is correctly synchronized, but a capture layer which scans
// its read-only source before submitting can race the first queue.

#include "vulkan_common.h"

#include <cstdlib>

#include "vulkan_memory_tracking_race.inc"

constexpr VkDeviceSize kDefaultBufferSize = 64 * 1024 * 1024;
constexpr uint32_t kDefaultChunkCount = 1024;
constexpr uint32_t kRed = 0xff0000ff;
constexpr uint32_t kGreen = 0xff00ff00;
constexpr const char* kDefaultSyncType = "binary-semaphore";

enum class SyncType
{
	BinarySemaphore,
	TimelineSemaphore,
	PipelineBarrier,
	Event
};

struct BufferResource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
};

struct TestResources
{
	VkQueue writeQueue = VK_NULL_HANDLE;
	VkQueue readQueue = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer startCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer readCommandBuffer = VK_NULL_HANDLE;
	VkSemaphore writeComplete = VK_NULL_HANDLE;
	VkEvent writesComplete = VK_NULL_HANDLE;
	VkFence readComplete = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkShaderModule shader = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	BufferResource target;
	BufferResource readback;
};

static void show_usage()
{
	printf("-bs/--buffer-size N  Buffer size in bytes (default %lu)\n", (unsigned long)kDefaultBufferSize);
	printf("-cc/--chunk-count N  Dependent whole-buffer fill rounds; must be even (default %u)\n", kDefaultChunkCount);
	printf("-st/--sync-type TYPE  binary-semaphore, timeline-semaphore, pipeline-barrier, or event (default %s)\n",
	       kDefaultSyncType);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-bs", "--buffer-size"))
	{
		reqs.options["buffer_size"] = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-cc", "--chunk-count"))
	{
		reqs.options["chunk_count"] = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-st", "--sync-type"))
	{
		reqs.options["sync_type"] = std::string(get_string_arg(argv, ++i, argc));
		return true;
	}
	return false;
}

static bool parse_sync_type(const std::string& name, SyncType& sync_type)
{
	if (name == "binary-semaphore") sync_type = SyncType::BinarySemaphore;
	else if (name == "timeline-semaphore") sync_type = SyncType::TimelineSemaphore;
	else if (name == "pipeline-barrier") sync_type = SyncType::PipelineBarrier;
	else if (name == "event") sync_type = SyncType::Event;
	else return false;
	return true;
}

static bool command_line_requests_timeline(int argc, char** argv)
{
	for (int i = 1; i + 1 < argc; i++)
	{
		if ((strcmp(argv[i], "-st") == 0 || strcmp(argv[i], "--sync-type") == 0)
		    && strcmp(argv[i + 1], "timeline-semaphore") == 0) return true;
	}
	return false;
}

static BufferResource create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
	BufferResource resource;
	resource.size = size;

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &resource.buffer);
	check(result);
	assert(resource.buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name);

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &requirements);
	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(
		requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);
	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);
	return resource;
}

static void expose_and_clear_buffer(const vulkan_setup_t& vulkan, const BufferResource& resource)
{
	void* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, resource.size, 0, &mapped);
	check(result);
	assert(mapped != nullptr);
	memset(mapped, 0, static_cast<size_t>(resource.size));
	if (vulkan.has_explicit_host_updates) testFlushMemory(vulkan, resource.memory, 0, resource.size, true);
	vkUnmapMemory(vulkan.device, resource.memory);
}

static void create_compute_resources(const vulkan_setup_t& vulkan, TestResources& resources)
{
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutBinding bindings[2] = { binding, binding };
	bindings[1].binding = 1;
	VkDescriptorSetLayoutCreateInfo set_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	set_layout_info.bindingCount = 2;
	set_layout_info.pBindings = bindings;
	check(vkCreateDescriptorSetLayout(vulkan.device, &set_layout_info, nullptr, &resources.descriptorSetLayout));

	VkPushConstantRange push_range = {};
	push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_range.size = sizeof(uint32_t) * 3;
	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &resources.descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_range;
	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &resources.pipelineLayout));

	VkShaderModuleCreateInfo shader_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	shader_info.codeSize = vulkan_memory_tracking_race_spv_len;
	shader_info.pCode = reinterpret_cast<const uint32_t*>(vulkan_memory_tracking_race_spv);
	check(vkCreateShaderModule(vulkan.device, &shader_info, nullptr, &resources.shader));
	VkPipelineShaderStageCreateInfo stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage_info.module = resources.shader;
	stage_info.pName = "main";
	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stage = stage_info;
	pipeline_info.layout = resources.pipelineLayout;
	check(vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline));

	VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	check(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &resources.descriptorPool));
	VkDescriptorSetAllocateInfo set_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	set_info.descriptorPool = resources.descriptorPool;
	set_info.descriptorSetCount = 1;
	set_info.pSetLayouts = &resources.descriptorSetLayout;
	check(vkAllocateDescriptorSets(vulkan.device, &set_info, &resources.descriptorSet));
	VkDescriptorBufferInfo buffer_infos[2] = {
		{ resources.target.buffer, 0, resources.target.size },
		{ resources.readback.buffer, 0, resources.readback.size }
	};
	VkWriteDescriptorSet writes[2] = {
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr },
		{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr }
	};
	for (uint32_t i = 0; i < 2; i++)
	{
		writes[i].dstSet = resources.descriptorSet;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &buffer_infos[i];
	}
	vkUpdateDescriptorSets(vulkan.device, 2, writes, 0, nullptr);
}

static void record_write_commands(const TestResources& resources, uint32_t chunk_count, SyncType sync_type)
{
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkResult result = vkBeginCommandBuffer(resources.startCommandBuffer, &begin_info);
	check(result);

	// Red is the intermediate value before the long sequence of green copies.
	vkCmdFillBuffer(resources.startCommandBuffer, resources.target.buffer, 0, VK_WHOLE_SIZE, kRed);
	for (uint32_t chunk = 0; chunk < chunk_count; chunk++)
	{
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(resources.startCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		                     1, &barrier, 0, nullptr, 0, nullptr);
		const uint32_t value = (chunk + 1 == chunk_count || (chunk & 1) != 0) ? kGreen : kRed;
		vkCmdFillBuffer(resources.startCommandBuffer, resources.target.buffer, 0, VK_WHOLE_SIZE, value);
	}
	if (sync_type == SyncType::Event)
	{
		vkCmdSetEvent(resources.startCommandBuffer, resources.writesComplete, VK_PIPELINE_STAGE_TRANSFER_BIT);
	}

	result = vkEndCommandBuffer(resources.startCommandBuffer);
	check(result);
}

static void record_read_commands(const TestResources& resources, SyncType sync_type)
{
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkResult result = vkBeginCommandBuffer(resources.readCommandBuffer, &begin_info);
	check(result);
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	if (sync_type == SyncType::PipelineBarrier)
	{
		vkCmdPipelineBarrier(resources.readCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
		                     0, nullptr, 0, nullptr);
	}
	else if (sync_type == SyncType::Event)
	{
		vkCmdWaitEvents(resources.readCommandBuffer, 1, &resources.writesComplete, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 1, &barrier, 0, nullptr, 0, nullptr);
	}

	const VkDeviceSize word_count = resources.target.size / sizeof(uint32_t);
	assert(word_count <= UINT32_MAX);
	uint32_t parameters[3] = { static_cast<uint32_t>(word_count), 1, 1 };
	vkCmdBindPipeline(resources.readCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.pipeline);
	vkCmdBindDescriptorSets(resources.readCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.pipelineLayout, 0, 1,
	                        &resources.descriptorSet, 0, nullptr);
	vkCmdPushConstants(resources.readCommandBuffer, resources.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), parameters);
	vkCmdDispatch(resources.readCommandBuffer, 1, 1, 1);
	result = vkEndCommandBuffer(resources.readCommandBuffer);
	check(result);
}

static bool verify_readback(const vulkan_setup_t& vulkan, const BufferResource& readback)
{
	if (vulkan.vkAssertBuffer && get_env_int("TOOLSTEST_NULL_RUN", 0) == 0)
	{
		uint32_t checksum = 0;
		const VkUpdateBufferInfoARM assert_info = {
			VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback.buffer, 0, readback.size, nullptr
		};
		VkResult result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &checksum,
		                                               "memory tracking race final green readback");
		check(result);
	}

	if (get_env_int("TOOLSTEST_NULL_RUN", 0) > 0) return true;
	uint32_t* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, readback.memory, 0, readback.size, 0, (void**)&mapped);
	check(result);
	assert(mapped != nullptr);
	bool success = true;
	const size_t words = static_cast<size_t>(readback.size / sizeof(uint32_t));
	for (size_t i = 0; i < words; i++)
	{
		if (mapped[i] == kGreen) continue;
		printf("Readback word %zu was 0x%08x, expected 0x%08x\n", i, mapped[i], kGreen);
		success = false;
		break;
	}
	vkUnmapMemory(vulkan.device, readback.memory);
	return success;
}

static void destroy_buffer(const vulkan_setup_t& vulkan, BufferResource& resource)
{
	if (resource.buffer) vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.queues = 1;
	reqs.required_queue_flags = VK_QUEUE_TRANSFER_BIT;
	reqs.options["buffer_size"] = static_cast<int>(kDefaultBufferSize);
	reqs.options["chunk_count"] = static_cast<int>(kDefaultChunkCount);
	reqs.options["sync_type"] = std::string(kDefaultSyncType);
	if (command_line_requests_timeline(argc, argv))
	{
		reqs.apiVersion = VK_API_VERSION_1_2;
		reqs.minApiVersion = VK_API_VERSION_1_2;
		reqs.reqfeat12.timelineSemaphore = VK_TRUE;
	}
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_memory_tracking_race", reqs);
	SyncType sync_type = SyncType::BinarySemaphore;
	const std::string sync_type_name = std::get<std::string>(reqs.options.at("sync_type"));
	if (!parse_sync_type(sync_type_name, sync_type))
	{
		printf("Invalid sync type '%s'\n", sync_type_name.c_str());
		test_done(vulkan);
		return 1;
	}
	TestResources resources;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &resources.writeQueue);
	resources.readQueue = resources.writeQueue;
	assert(resources.writeQueue != VK_NULL_HANDLE);
	assert(resources.readQueue != VK_NULL_HANDLE);

	const int requested_size = std::get<int>(reqs.options.at("buffer_size"));
	const int requested_chunks = std::get<int>(reqs.options.at("chunk_count"));
	if (requested_size <= 0 || requested_size % static_cast<int>(sizeof(uint32_t)) != 0 || requested_chunks <= 0
	    || (requested_chunks & 1) != 0)
	{
		printf("Buffer size must be a positive multiple of four and chunk count must be positive and even\n");
		test_done(vulkan);
		return 1;
	}

	resources.target = create_buffer(vulkan, requested_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	                                 "memory_tracking_race_target");
	resources.readback = create_buffer(vulkan, requested_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "memory_tracking_race_readback");
	expose_and_clear_buffer(vulkan, resources.target);
	expose_and_clear_buffer(vulkan, resources.readback);

	VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	pool_info.queueFamilyIndex = vulkan.queue_family_index;
	VkResult result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &resources.commandPool);
	check(result);
	VkCommandBuffer command_buffers[2] = {};
	VkCommandBufferAllocateInfo command_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_info.commandPool = resources.commandPool;
	command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_info.commandBufferCount = 2;
	result = vkAllocateCommandBuffers(vulkan.device, &command_info, command_buffers);
	check(result);
	resources.startCommandBuffer = command_buffers[0];
	resources.readCommandBuffer = command_buffers[1];
	create_compute_resources(vulkan, resources);

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	VkSemaphoreTypeCreateInfo semaphore_type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr };
	if (sync_type == SyncType::TimelineSemaphore)
	{
		semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		semaphore_type_info.initialValue = 0;
		semaphore_info.pNext = &semaphore_type_info;
	}
	if (sync_type == SyncType::BinarySemaphore || sync_type == SyncType::TimelineSemaphore)
	{
		result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &resources.writeComplete);
		check(result);
	}
	VkEventCreateInfo event_info = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, nullptr };
	if (sync_type == SyncType::Event)
	{
		result = vkCreateEvent(vulkan.device, &event_info, nullptr, &resources.writesComplete);
		check(result);
	}
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &resources.readComplete);
	check(result);

	record_write_commands(resources, static_cast<uint32_t>(requested_chunks), sync_type);
	record_read_commands(resources, sync_type);

	bench_start_iteration(vulkan.bench);
	VkSubmitInfo start_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	start_submit.commandBufferCount = 1;
	start_submit.pCommandBuffers = &resources.startCommandBuffer;
	uint64_t timeline_value = 1;
	VkTimelineSemaphoreSubmitInfo write_timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr };
	if (sync_type == SyncType::BinarySemaphore || sync_type == SyncType::TimelineSemaphore)
	{
		start_submit.signalSemaphoreCount = 1;
		start_submit.pSignalSemaphores = &resources.writeComplete;
	}
	if (sync_type == SyncType::TimelineSemaphore)
	{
		write_timeline_info.signalSemaphoreValueCount = 1;
		write_timeline_info.pSignalSemaphoreValues = &timeline_value;
		start_submit.pNext = &write_timeline_info;
	}
	result = vkQueueSubmit(resources.writeQueue, 1, &start_submit, VK_NULL_HANDLE);
	check(result);
	bool success = true;
	if (success)
	{
		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		VkSubmitInfo read_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		VkTimelineSemaphoreSubmitInfo read_timeline_info = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr };
		if (sync_type == SyncType::BinarySemaphore || sync_type == SyncType::TimelineSemaphore)
		{
			read_submit.waitSemaphoreCount = 1;
			read_submit.pWaitSemaphores = &resources.writeComplete;
			read_submit.pWaitDstStageMask = &wait_stage;
		}
		if (sync_type == SyncType::TimelineSemaphore)
		{
			read_timeline_info.waitSemaphoreValueCount = 1;
			read_timeline_info.pWaitSemaphoreValues = &timeline_value;
			read_submit.pNext = &read_timeline_info;
		}
		read_submit.commandBufferCount = 1;
		read_submit.pCommandBuffers = &resources.readCommandBuffer;
		result = vkQueueSubmit(resources.readQueue, 1, &read_submit, resources.readComplete);
		check(result);
		result = vkWaitForFences(vulkan.device, 1, &resources.readComplete, VK_TRUE, UINT64_MAX);
		check(result);
		success = verify_readback(vulkan, resources.readback);
	}
	bench_stop_iteration(vulkan.bench);

	vkDeviceWaitIdle(vulkan.device);
	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, resources.shader, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipelineLayout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptorSetLayout, nullptr);
	vkDestroyFence(vulkan.device, resources.readComplete, nullptr);
	if (resources.writesComplete) vkDestroyEvent(vulkan.device, resources.writesComplete, nullptr);
	if (resources.writeComplete) vkDestroySemaphore(vulkan.device, resources.writeComplete, nullptr);
	vkDestroyCommandPool(vulkan.device, resources.commandPool, nullptr);
	destroy_buffer(vulkan, resources.readback);
	destroy_buffer(vulkan, resources.target);
	test_done(vulkan);
	return success ? 0 : 1;
}
