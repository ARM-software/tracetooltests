// Test that a host wait for GPU work does not return before the queued work is complete.
// The first submission runs ordinary compute work. The second submission waits on it,
// copies a green source buffer to a target buffer, then signals a VkEvent.

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include "vulkan_compute_1.inc"

constexpr uint32_t kDefaultWidth = 1024;
constexpr uint32_t kDefaultHeight = 1024;
constexpr uint32_t kDefaultPayloadBytes = 1024 * 1024;
constexpr uint64_t kEventTimeoutSeconds = 5;

struct Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

constexpr Color kGreen = { 0, 255, 0, 255 };
constexpr Color kRed = { 255, 0, 0, 255 };
constexpr Color kBlack = { 0, 0, 0, 255 };

struct BufferResource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
};

struct ImageResource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
	bool initialized = false;
};

struct WaitResources
{
	compute_resources compute;
	VkCommandBuffer copyCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer frameBoundaryCommandBuffer = VK_NULL_HANDLE;
	BufferResource source;
	BufferResource target;
	ImageResource frameImage;
	VkEvent completionEvent = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	uint64_t frameID = 0;
};

enum class WaitMode
{
	None,
	CompletionEvent
};

static void show_usage()
{
	compute_usage();
	printf("-pb/--payload-bytes N  Bytes copied from source to target (default %u)\n", kDefaultPayloadBytes);
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (compute_cmdopt(i, argc, argv, reqs)) return true;
	if (match(argv[i], "-pb", "--payload-bytes"))
	{
		reqs.options["payload_bytes"] = get_arg(argv, ++i, argc);
		return true;
	}
	return false;
}

static BufferResource create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, const char* name)
{
	BufferResource out;
	out.size = size;

	VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &out.buffer);
	check(result);
	assert(out.buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)out.buffer, name);

	VkMemoryRequirements memory_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, out.buffer, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = memory_requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(
		memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &out.memory);
	check(result);
	assert(out.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, out.buffer, out.memory, 0);
	check(result);
	return out;
}

static void destroy_buffer(const vulkan_setup_t& vulkan, BufferResource& buffer)
{
	if (buffer.buffer) vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
	if (buffer.memory) testFreeMemory(vulkan, buffer.memory);
	buffer = {};
}

static ImageResource create_frame_image(const vulkan_setup_t& vulkan, VkDeviceSize payload_bytes)
{
	const uint64_t pixels = payload_bytes / sizeof(Color);
	assert(payload_bytes % sizeof(Color) == 0);
	assert(pixels > 0);

	uint32_t divisor = 1;
	for (uint32_t candidate = 1; static_cast<uint64_t>(candidate) * candidate <= pixels; candidate++)
	{
		if (pixels % candidate == 0) divisor = candidate;
	}

	ImageResource out;
	out.width = static_cast<uint32_t>(pixels / divisor);
	out.height = divisor;
	assert(out.width > 0);
	assert(out.height > 0);
	assert(out.width <= vulkan.device_properties.limits.maxImageDimension2D);
	assert(out.height <= vulkan.device_properties.limits.maxImageDimension2D);

	VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent = { out.width, out.height, 1 };
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(vulkan.device, &image_info, nullptr, &out.image);
	check(result);
	assert(out.image != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)out.image, "compute_wait_frame_image");

	VkMemoryRequirements memory_requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, out.image, &memory_requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = memory_requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, 0);

	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &out.memory);
	check(result);
	assert(out.memory != VK_NULL_HANDLE);

	result = vkBindImageMemory(vulkan.device, out.image, out.memory, 0);
	check(result);
	return out;
}

static void destroy_image(const vulkan_setup_t& vulkan, ImageResource& image)
{
	if (image.image) vkDestroyImage(vulkan.device, image.image, nullptr);
	if (image.memory) testFreeMemory(vulkan, image.memory);
	image = {};
}

static void fill_buffer(const vulkan_setup_t& vulkan, const BufferResource& buffer, Color color)
{
	assert(buffer.size % sizeof(Color) == 0);
	Color* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, buffer.memory, 0, buffer.size, 0, (void**)&mapped);
	check(result);
	assert(mapped != nullptr);
	const size_t pixels = static_cast<size_t>(buffer.size / sizeof(Color));
	for (size_t pixel = 0; pixel < pixels; pixel++)
	{
		mapped[pixel] = color;
	}
	vkUnmapMemory(vulkan.device, buffer.memory);
}

static std::vector<Color> read_buffer(const vulkan_setup_t& vulkan, const BufferResource& buffer)
{
	assert(buffer.size % sizeof(Color) == 0);
	Color* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, buffer.memory, 0, buffer.size, 0, (void**)&mapped);
	check(result);
	assert(mapped != nullptr);
	const size_t pixels = static_cast<size_t>(buffer.size / sizeof(Color));
	std::vector<Color> data(mapped, mapped + pixels);
	vkUnmapMemory(vulkan.device, buffer.memory);
	return data;
}

static bool same_color(Color actual, Color expected)
{
	return actual.r == expected.r && actual.g == expected.g && actual.b == expected.b && actual.a == expected.a;
}

static bool verify_color(const vulkan_setup_t& vulkan, const BufferResource& buffer, Color expected, const char* label)
{
	const std::vector<Color> data = read_buffer(vulkan, buffer);
	for (size_t pixel = 0; pixel < data.size(); pixel++)
	{
		if (!same_color(data[pixel], expected))
		{
			printf("%s: pixel %zu was (%u,%u,%u,%u), expected (%u,%u,%u,%u)\n", label, pixel, data[pixel].r, data[pixel].g,
			       data[pixel].b, data[pixel].a, expected.r, expected.g, expected.b, expected.a);
			return false;
		}
	}
	return true;
}

static void setup_compute_pipeline(vulkan_setup_t& vulkan, vulkan_req_t& reqs, compute_resources& compute)
{
	VkResult result;

	VkDescriptorSetLayoutBinding descriptor_binding = {};
	descriptor_binding.binding = 0;
	descriptor_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptor_binding.descriptorCount = 1;
	descriptor_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	layout_info.bindingCount = 1;
	layout_info.pBindings = &descriptor_binding;
	result = vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &compute.descriptorSetLayout);
	check(result);

	VkDescriptorPoolSize pool_size = {};
	pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_size.descriptorCount = 1;
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;
	result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &compute.descriptorPool);
	check(result);

	VkDescriptorSetAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	allocate_info.descriptorPool = compute.descriptorPool;
	allocate_info.descriptorSetCount = 1;
	allocate_info.pSetLayouts = &compute.descriptorSetLayout;
	result = vkAllocateDescriptorSets(vulkan.device, &allocate_info, &compute.descriptorSet);
	check(result);

	VkDescriptorBufferInfo buffer_info = {};
	buffer_info.buffer = compute.buffer;
	buffer_info.offset = 0;
	buffer_info.range = compute.buffer_size;

	VkWriteDescriptorSet write_info = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	write_info.dstSet = compute.descriptorSet;
	write_info.dstBinding = 0;
	write_info.descriptorCount = 1;
	write_info.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write_info.pBufferInfo = &buffer_info;
	vkUpdateDescriptorSets(vulkan.device, 1, &write_info, 0, nullptr);

	compute.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	compute_create_pipeline(vulkan, compute, reqs);
}

static void record_compute_command_buffer(const vulkan_setup_t& vulkan, const vulkan_req_t& reqs, compute_resources& compute)
{
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int workgroup_size = std::get<int>(reqs.options.at("wg_size"));
	assert(width > 0);
	assert(height > 0);
	assert(workgroup_size > 0);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	VkResult result = vkBeginCommandBuffer(compute.commandBuffer, &begin_info);
	check(result);
	vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
	vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0,
	                        nullptr);
	vkCmdDispatch(compute.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
	result = vkEndCommandBuffer(compute.commandBuffer);
	check(result);
}

static void record_copy_command_buffer(VkCommandBuffer command_buffer, const BufferResource& source, const BufferResource& target,
                                       VkEvent completion_event)
{
	assert(source.size == target.size);
	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkBufferMemoryBarrier source_barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
	source_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	source_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	source_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	source_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	source_barrier.buffer = source.buffer;
	source_barrier.offset = 0;
	source_barrier.size = VK_WHOLE_SIZE;

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &source_barrier, 0,
	                     nullptr);

	VkBufferCopy copy_region = { 0, 0, source.size };
	vkCmdCopyBuffer(command_buffer, source.buffer, target.buffer, 1, &copy_region);
	vkCmdSetEvent(command_buffer, completion_event, VK_PIPELINE_STAGE_TRANSFER_BIT);

	result = vkEndCommandBuffer(command_buffer);
	check(result);
}

static void submit_frame_boundary(vulkan_setup_t& vulkan, WaitResources& resources)
{
	VkResult result = vkResetCommandBuffer(resources.frameBoundaryCommandBuffer, 0);
	check(result);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(resources.frameBoundaryCommandBuffer, &begin_info);
	check(result);

	VkBufferMemoryBarrier target_barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
	target_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	target_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	target_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	target_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	target_barrier.buffer = resources.target.buffer;
	target_barrier.offset = 0;
	target_barrier.size = VK_WHOLE_SIZE;

	VkImageMemoryBarrier image_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	image_barrier.srcAccessMask = resources.frameImage.initialized ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_NONE;
	image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_barrier.oldLayout = resources.frameImage.initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_barrier.image = resources.frameImage.image;
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.subresourceRange.baseMipLevel = 0;
	image_barrier.subresourceRange.levelCount = 1;
	image_barrier.subresourceRange.baseArrayLayer = 0;
	image_barrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(resources.frameBoundaryCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1,
	                     &target_barrier, 1, &image_barrier);

	VkImageSubresourceLayers image_layers = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	VkBufferImageCopy copy_region = {};
	copy_region.bufferOffset = 0;
	copy_region.bufferRowLength = resources.frameImage.width;
	copy_region.bufferImageHeight = resources.frameImage.height;
	copy_region.imageSubresource = image_layers;
	copy_region.imageOffset = { 0, 0, 0 };
	copy_region.imageExtent = { resources.frameImage.width, resources.frameImage.height, 1 };
	vkCmdCopyBufferToImage(resources.frameBoundaryCommandBuffer, resources.target.buffer, resources.frameImage.image, VK_IMAGE_LAYOUT_GENERAL, 1,
	                       &copy_region);

	VkImageMemoryBarrier readable_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	readable_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	readable_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	readable_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	readable_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	readable_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	readable_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	readable_barrier.image = resources.frameImage.image;
	readable_barrier.subresourceRange = image_barrier.subresourceRange;
	vkCmdPipelineBarrier(resources.frameBoundaryCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
	                     0, nullptr, 1, &readable_barrier);

	result = vkEndCommandBuffer(resources.frameBoundaryCommandBuffer);
	check(result);

	VkFrameBoundaryEXT frame_boundary = { VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, nullptr };
	frame_boundary.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
	frame_boundary.frameID = resources.frameID++;
	frame_boundary.imageCount = 1;
	frame_boundary.pImages = &resources.frameImage.image;
	frame_boundary.bufferCount = 0;
	frame_boundary.pBuffers = nullptr;

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &frame_boundary };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &resources.frameBoundaryCommandBuffer;
	result = vkQueueSubmit(resources.compute.queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	vkDestroyFence(vulkan.device, fence, nullptr);
	resources.frameImage.initialized = true;
}

static bool wait_for_completion_event(const vulkan_setup_t& vulkan, VkEvent event)
{
	const auto start = std::chrono::steady_clock::now();
	while (true)
	{
		VkResult result = vkGetEventStatus(vulkan.device, event);
		if (result == VK_EVENT_SET) return true;
		if (result != VK_EVENT_RESET)
		{
			check(result);
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
		if (seconds >= static_cast<int64_t>(kEventTimeoutSeconds))
		{
			printf("Timed out waiting for completion event after %lu seconds\n", (unsigned long)kEventTimeoutSeconds);
			return false;
		}
		usleep(100);
	}
}

static VkSemaphore create_timeline_semaphore(const vulkan_setup_t& vulkan)
{
	VkSemaphoreTypeCreateInfo type_info = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr };
	type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	type_info.initialValue = 0;

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type_info };
	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkResult result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &semaphore);
	check(result);
	assert(semaphore != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore, "compute_wait_timeline_gate");
	return semaphore;
}

static void signal_timeline_semaphore(const vulkan_setup_t& vulkan, VkSemaphore semaphore, uint64_t value)
{
	VkSemaphoreSignalInfo signal_info = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, nullptr };
	signal_info.semaphore = semaphore;
	signal_info.value = value;
	VkResult result = vkSignalSemaphore(vulkan.device, &signal_info);
	check(result);
}

static bool run_wait_case(const vulkan_req_t& reqs, vulkan_setup_t& vulkan, WaitResources& resources, WaitMode wait_mode,
                          bool use_timeline_gate, Color expected, const char* label)
{
	fill_buffer(vulkan, resources.source, kGreen);
	fill_buffer(vulkan, resources.target, kBlack);

	VkResult result = vkResetEvent(vulkan.device, resources.completionEvent);
	check(result);
	result = vkResetFences(vulkan.device, 1, &resources.fence);
	check(result);

	VkSemaphoreCreateInfo semaphore_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	VkSemaphore semaphore = VK_NULL_HANDLE;
	result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &semaphore);
	check(result);
	assert(semaphore != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore, "compute_wait_semaphore");
	VkSemaphore timeline_semaphore = use_timeline_gate ? create_timeline_semaphore(vulkan) : VK_NULL_HANDLE;

	VkSubmitInfo compute_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	compute_submit.commandBufferCount = 1;
	compute_submit.pCommandBuffers = &resources.compute.commandBuffer;
	compute_submit.signalSemaphoreCount = 1;
	compute_submit.pSignalSemaphores = &semaphore;

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkSemaphore wait_semaphores[] = { semaphore, timeline_semaphore };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
	uint64_t timeline_values[] = { 0, 1 };
	VkTimelineSemaphoreSubmitInfo timeline_submit = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr };
	timeline_submit.waitSemaphoreValueCount = 2;
	timeline_submit.pWaitSemaphoreValues = timeline_values;

	VkSubmitInfo copy_submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	copy_submit.waitSemaphoreCount = use_timeline_gate ? 2 : 1;
	copy_submit.pWaitSemaphores = use_timeline_gate ? wait_semaphores : &semaphore;
	copy_submit.pWaitDstStageMask = use_timeline_gate ? wait_stages : &wait_stage;
	copy_submit.commandBufferCount = 1;
	copy_submit.pCommandBuffers = &resources.copyCommandBuffer;
	if (use_timeline_gate)
	{
		copy_submit.pNext = &timeline_submit;
	}

	test_marker(vulkan, label);
	result = vkQueueSubmit(resources.compute.queue, 1, &compute_submit, VK_NULL_HANDLE);
	check(result);
	result = vkQueueSubmit(resources.compute.queue, 1, &copy_submit, resources.fence);
	check(result);

	bool success = true;
	if (wait_mode == WaitMode::CompletionEvent)
	{
		success = wait_for_completion_event(vulkan, resources.completionEvent);
	}

	fill_buffer(vulkan, resources.source, kRed);
	if (use_timeline_gate)
	{
		signal_timeline_semaphore(vulkan, timeline_semaphore, 1);
	}

	result = vkWaitForFences(vulkan.device, 1, &resources.fence, VK_TRUE, UINT64_MAX);
	check(result);

	if (reqs.options.count("frame_boundary")) submit_frame_boundary(vulkan, resources);
	if (success) success = verify_color(vulkan, resources.target, expected, label);
	if (timeline_semaphore) vkDestroySemaphore(vulkan.device, timeline_semaphore, nullptr);
	vkDestroySemaphore(vulkan.device, semaphore, nullptr);
	return success;
}

static bool setup_wait_resources(vulkan_setup_t& vulkan, vulkan_req_t& reqs, WaitResources& resources)
{
	resources.compute = compute_init(vulkan, reqs);
	setup_compute_pipeline(vulkan, reqs, resources.compute);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue == resources.compute.queue);
	assert(resources.compute.queue != VK_NULL_HANDLE);

	const int payload_bytes = std::get<int>(reqs.options.at("payload_bytes"));
	if (payload_bytes <= 0 || payload_bytes % static_cast<int>(sizeof(Color)) != 0)
	{
		printf("Payload size must be a positive multiple of %zu bytes\n", sizeof(Color));
		return false;
	}

	resources.source = create_buffer(vulkan, payload_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "compute_wait_source");
	resources.target = create_buffer(vulkan, payload_bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "compute_wait_target");
	resources.frameImage = create_frame_image(vulkan, payload_bytes);

	VkEventCreateInfo event_info = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, nullptr };
	VkResult result = vkCreateEvent(vulkan.device, &event_info, nullptr, &resources.completionEvent);
	check(result);
	assert(resources.completionEvent != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_EVENT, (uint64_t)resources.completionEvent, "compute_wait_completion_event");

	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &resources.fence);
	check(result);
	assert(resources.fence != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_FENCE, (uint64_t)resources.fence, "compute_wait_fence");

	std::array<VkCommandBuffer, 2> command_buffers = {};
	VkCommandBufferAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	allocate_info.commandPool = resources.compute.commandPool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = command_buffers.size();
	result = vkAllocateCommandBuffers(vulkan.device, &allocate_info, command_buffers.data());
	check(result);
	resources.copyCommandBuffer = command_buffers[0];
	resources.frameBoundaryCommandBuffer = command_buffers[1];
	assert(resources.copyCommandBuffer != VK_NULL_HANDLE);
	assert(resources.frameBoundaryCommandBuffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)resources.copyCommandBuffer, "compute_wait_copy_command_buffer");
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)resources.frameBoundaryCommandBuffer, "compute_wait_frame_boundary_command_buffer");

	record_compute_command_buffer(vulkan, reqs, resources.compute);
	record_copy_command_buffer(resources.copyCommandBuffer, resources.source, resources.target, resources.completionEvent);
	return true;
}

static void destroy_wait_resources(vulkan_setup_t& vulkan, vulkan_req_t& reqs, WaitResources& resources)
{
	if (resources.fence) vkDestroyFence(vulkan.device, resources.fence, nullptr);
	if (resources.completionEvent) vkDestroyEvent(vulkan.device, resources.completionEvent, nullptr);
	destroy_image(vulkan, resources.frameImage);
	destroy_buffer(vulkan, resources.target);
	destroy_buffer(vulkan, resources.source);
	compute_done(vulkan, resources.compute, reqs);
	resources = {};
}

int main(int argc, char** argv)
{
	p__loops = 1;
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.minApiVersion = VK_API_VERSION_1_2;
	reqs.reqfeat12.timelineSemaphore = VK_TRUE;
	reqs.options["width"] = static_cast<int>(kDefaultWidth);
	reqs.options["height"] = static_cast<int>(kDefaultHeight);
	reqs.options["payload_bytes"] = static_cast<int>(kDefaultPayloadBytes);
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_wait", reqs);
	assert(vulkan.hasfeat12.timelineSemaphore == VK_TRUE);
	if (getenv("TOOLSTEST_NULL_RUN"))
	{
		printf("Skipping execution-sensitive wait verification in null-run mode.\n");
		test_done(vulkan);
		return 77;
	}

	WaitResources resources;
	bool success = setup_wait_resources(vulkan, reqs, resources);

	bench_start_iteration(vulkan.bench);
	if (success)
	{
		// Canary: prove that changing the source before the copy reaches the GPU produces the failing red result.
		success = run_wait_case(reqs, vulkan, resources, WaitMode::None, true, kRed, "canary_without_completion_wait");
	}
	if (success)
	{
		success = run_wait_case(reqs, vulkan, resources, WaitMode::CompletionEvent, false, kGreen, "completion_event_wait");
	}
	bench_stop_iteration(vulkan.bench);

	destroy_wait_resources(vulkan, reqs, resources);
	test_done(vulkan);
	return success ? 0 : 1;
}
