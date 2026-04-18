#include "vulkan_common.h"

#include <array>
#include <cstring>
#include <vector>

namespace
{

constexpr uint32_t kWidth = 4;
constexpr uint32_t kHeight = 4;
constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkDeviceSize kImageBytes = kWidth * kHeight * 4;

struct BufferResource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ImageResource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

VkSampleCountFlagBits pick_multisample_count(const vulkan_setup_t& vulkan)
{
	VkImageFormatProperties properties = {};
	VkResult result = vkGetPhysicalDeviceImageFormatProperties(vulkan.physical, kFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
	                                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &properties);
	if (result != VK_SUCCESS) return VK_SAMPLE_COUNT_1_BIT;
	if (properties.sampleCounts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
	if (properties.sampleCounts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
	return VK_SAMPLE_COUNT_1_BIT;
}

BufferResource create_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, const char* name)
{
	BufferResource resource;

	VkBufferCreateInfo create_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	create_info.size = size;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &create_info, nullptr, &resource.buffer);
	check(result);
	assert(resource.buffer != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)resource.buffer, name);

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, resource.buffer, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, properties);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindBufferMemory(vulkan.device, resource.buffer, resource.memory, 0);
	check(result);
	return resource;
}

ImageResource create_image(const vulkan_setup_t& vulkan, VkImageUsageFlags usage, VkSampleCountFlagBits samples, const char* name)
{
	ImageResource resource;

	VkImageCreateInfo create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	create_info.imageType = VK_IMAGE_TYPE_2D;
	create_info.format = kFormat;
	create_info.extent = { kWidth, kHeight, 1 };
	create_info.mipLevels = 1;
	create_info.arrayLayers = 1;
	create_info.samples = samples;
	create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(vulkan.device, &create_info, nullptr, &resource.image);
	check(result);
	assert(resource.image != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_IMAGE, (uint64_t)resource.image, name);

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, resource.image, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, 0);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);
	assert(resource.memory != VK_NULL_HANDLE);

	result = vkBindImageMemory(vulkan.device, resource.image, resource.memory, 0);
	check(result);
	return resource;
}

void transition_image(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                      VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
                      VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask)
{
	VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	barrier.srcAccessMask = src_access_mask;
	barrier.dstAccessMask = dst_access_mask;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(command_buffer, src_stage_mask, dst_stage_mask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void write_buffer(const vulkan_setup_t& vulkan, const BufferResource& resource, const std::vector<uint8_t>& data)
{
	assert(data.size() == kImageBytes);
	uint8_t* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, kImageBytes, 0, (void**)&mapped);
	check(result);
	std::memcpy(mapped, data.data(), data.size());
	vkUnmapMemory(vulkan.device, resource.memory);
}

std::vector<uint8_t> read_buffer(const vulkan_setup_t& vulkan, const BufferResource& resource)
{
	uint8_t* mapped = nullptr;
	VkResult result = vkMapMemory(vulkan.device, resource.memory, 0, kImageBytes, 0, (void**)&mapped);
	check(result);
	std::vector<uint8_t> data(mapped, mapped + kImageBytes);
	vkUnmapMemory(vulkan.device, resource.memory);
	return data;
}

std::vector<uint8_t> make_pattern_data()
{
	std::vector<uint8_t> data(kImageBytes);
	for (uint32_t y = 0; y < kHeight; y++)
	{
		for (uint32_t x = 0; x < kWidth; x++)
		{
			const size_t idx = (y * kWidth + x) * 4;
			data[idx + 0] = static_cast<uint8_t>(x * 17 + y * 5);
			data[idx + 1] = static_cast<uint8_t>(255 - x * 13 - y * 7);
			data[idx + 2] = static_cast<uint8_t>(40 + x * 9 + y * 11);
			data[idx + 3] = 255;
		}
	}
	return data;
}

std::vector<uint8_t> make_solid_data(const std::array<uint8_t, 4>& color)
{
	std::vector<uint8_t> data(kImageBytes);
	for (size_t i = 0; i < data.size(); i += 4)
	{
		data[i + 0] = color[0];
		data[i + 1] = color[1];
		data[i + 2] = color[2];
		data[i + 3] = color[3];
	}
	return data;
}

void destroy_buffer(const vulkan_setup_t& vulkan, BufferResource& resource)
{
	if (resource.buffer) vkDestroyBuffer(vulkan.device, resource.buffer, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

void destroy_image(const vulkan_setup_t& vulkan, ImageResource& resource)
{
	if (resource.image) vkDestroyImage(vulkan.device, resource.image, nullptr);
	if (resource.memory) testFreeMemory(vulkan, resource.memory);
	resource = {};
}

}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.maxApiVersion = VK_API_VERSION_1_2;
	reqs.device_extensions.push_back(VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_copy_commands2", reqs);
	assert(vulkan.device_extensions.count(VK_KHR_COPY_COMMANDS_2_EXTENSION_NAME) == 1);

	VkFormatProperties format_properties = {};
	vkGetPhysicalDeviceFormatProperties(vulkan.physical, kFormat, &format_properties);
	if ((format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0 ||
	    (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0)
	{
		printf("VK_FORMAT_R8G8B8A8_UNORM does not support image blit on this device.\n");
		test_done(vulkan);
		return 77;
	}

	const VkSampleCountFlagBits msaa_samples = pick_multisample_count(vulkan);
	if (msaa_samples == VK_SAMPLE_COUNT_1_BIT)
	{
		printf("No multisampled transfer-capable color sample count is available on this device.\n");
		test_done(vulkan);
		return 77;
	}

	MAKEDEVICEPROCADDR(vulkan, vkCmdCopyBuffer2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdCopyImage2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdCopyBufferToImage2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdCopyImageToBuffer2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBlitImage2KHR);
	MAKEDEVICEPROCADDR(vulkan, vkCmdResolveImage2KHR);

	BufferResource source_buffer = create_buffer(vulkan, kImageBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	                                             "copy_commands2_source");
	BufferResource copied_buffer = create_buffer(vulkan, kImageBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	                                             "copy_commands2_buffer_dst");
	BufferResource image_readback_buffer = create_buffer(vulkan, kImageBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	                                                     "copy_commands2_image_readback");
	BufferResource resolve_readback_buffer = create_buffer(vulkan, kImageBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	                                                       "copy_commands2_resolve_readback");

	ImageResource upload_image = create_image(vulkan, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                          VK_SAMPLE_COUNT_1_BIT, "copy_commands2_upload");
	ImageResource copied_image = create_image(vulkan, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                          VK_SAMPLE_COUNT_1_BIT, "copy_commands2_copy");
	ImageResource blit_image = create_image(vulkan, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                        VK_SAMPLE_COUNT_1_BIT, "copy_commands2_blit");
	ImageResource msaa_image = create_image(vulkan, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                        msaa_samples, "copy_commands2_msaa");
	ImageResource resolve_image = create_image(vulkan, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	                                           VK_SAMPLE_COUNT_1_BIT, "copy_commands2_resolve");

	const std::vector<uint8_t> source_data = make_pattern_data();
	const std::array<uint8_t, 4> resolve_color = { 19, 87, 143, 255 };
	const std::vector<uint8_t> expected_resolve = make_solid_data(resolve_color);
	write_buffer(vulkan, source_buffer, source_data);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, 0, 0, &queue);
	assert(queue != VK_NULL_HANDLE);

	VkCommandPoolCreateInfo command_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_info.queueFamilyIndex = 0;
	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkResult result = vkCreateCommandPool(vulkan.device, &command_pool_info, nullptr, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo command_buffer_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	command_buffer_info.commandPool = command_pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;
	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &command_buffer_info, &command_buffer);
	check(result);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkMemoryBarrier host_to_transfer = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
	host_to_transfer.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	host_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &host_to_transfer, 0, nullptr, 0, nullptr);

	transition_image(command_buffer, upload_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
	                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	transition_image(command_buffer, copied_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
	                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	transition_image(command_buffer, blit_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
	                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	transition_image(command_buffer, msaa_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
	                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	transition_image(command_buffer, resolve_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
	                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkBufferCopy2KHR buffer_region = { VK_STRUCTURE_TYPE_BUFFER_COPY_2_KHR, nullptr, 0, 0, kImageBytes };
	VkCopyBufferInfo2KHR copy_buffer_info = {
		VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2_KHR, nullptr, source_buffer.buffer, copied_buffer.buffer, 1, &buffer_region
	};
	pf_vkCmdCopyBuffer2KHR(command_buffer, &copy_buffer_info);

	VkBufferImageCopy2KHR buffer_image_region = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2_KHR, nullptr };
	buffer_image_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	buffer_image_region.imageSubresource.mipLevel = 0;
	buffer_image_region.imageSubresource.baseArrayLayer = 0;
	buffer_image_region.imageSubresource.layerCount = 1;
	buffer_image_region.imageExtent = { kWidth, kHeight, 1 };
	VkCopyBufferToImageInfo2KHR copy_buffer_to_image_info = {
		VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2_KHR, nullptr,
		source_buffer.buffer, upload_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_region
	};
	pf_vkCmdCopyBufferToImage2KHR(command_buffer, &copy_buffer_to_image_info);

	transition_image(command_buffer, upload_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkImageSubresourceLayers layers = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	VkImageCopy2KHR image_region = { VK_STRUCTURE_TYPE_IMAGE_COPY_2_KHR, nullptr };
	image_region.srcSubresource = layers;
	image_region.dstSubresource = layers;
	image_region.extent = { kWidth, kHeight, 1 };
	VkCopyImageInfo2KHR copy_image_info = {
		VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2_KHR, nullptr,
		upload_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		copied_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &image_region
	};
	pf_vkCmdCopyImage2KHR(command_buffer, &copy_image_info);

	transition_image(command_buffer, copied_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkOffset3D zero_offset = { 0, 0, 0 };
	VkOffset3D max_offset = { (int32_t)kWidth, (int32_t)kHeight, 1 };
	VkImageBlit2KHR blit_region = { VK_STRUCTURE_TYPE_IMAGE_BLIT_2_KHR, nullptr };
	blit_region.srcSubresource = layers;
	blit_region.dstSubresource = layers;
	blit_region.srcOffsets[0] = zero_offset;
	blit_region.srcOffsets[1] = max_offset;
	blit_region.dstOffsets[0] = zero_offset;
	blit_region.dstOffsets[1] = max_offset;
	VkBlitImageInfo2KHR blit_info = {
		VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2_KHR, nullptr,
		copied_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		blit_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit_region, VK_FILTER_NEAREST
	};
	pf_vkCmdBlitImage2KHR(command_buffer, &blit_info);

	transition_image(command_buffer, blit_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkCopyImageToBufferInfo2KHR copy_image_to_buffer_info = {
		VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2_KHR, nullptr,
		blit_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_readback_buffer.buffer, 1, &buffer_image_region
	};
	pf_vkCmdCopyImageToBuffer2KHR(command_buffer, &copy_image_to_buffer_info);

	VkClearColorValue clear_color = { { resolve_color[0] / 255.0f, resolve_color[1] / 255.0f, resolve_color[2] / 255.0f, 1.0f } };
	VkImageSubresourceRange clear_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vkCmdClearColorImage(command_buffer, msaa_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &clear_range);

	transition_image(command_buffer, msaa_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkImageResolve2KHR resolve_region = { VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR, nullptr };
	resolve_region.srcSubresource = layers;
	resolve_region.dstSubresource = layers;
	resolve_region.extent = { kWidth, kHeight, 1 };
	VkResolveImageInfo2KHR resolve_info = {
		VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2_KHR, nullptr,
		msaa_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		resolve_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &resolve_region
	};
	pf_vkCmdResolveImage2KHR(command_buffer, &resolve_info);

	transition_image(command_buffer, resolve_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	VkCopyImageToBufferInfo2KHR copy_resolve_to_buffer_info = {
		VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2_KHR, nullptr,
		resolve_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resolve_readback_buffer.buffer, 1, &buffer_image_region
	};
	pf_vkCmdCopyImageToBuffer2KHR(command_buffer, &copy_resolve_to_buffer_info);

	VkMemoryBarrier transfer_to_host = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
	transfer_to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transfer_to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &transfer_to_host, 0, nullptr, 0, nullptr);

	result = vkEndCommandBuffer(command_buffer);
	check(result);
	test_marker_mention(vulkan, "Recorded VK_KHR_copy_commands2 commands", VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer);

	bench_start_iteration(vulkan.bench);
	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);
	bench_stop_iteration(vulkan.bench);

	assert(read_buffer(vulkan, copied_buffer) == source_data);
	assert(read_buffer(vulkan, image_readback_buffer) == source_data);
	assert(read_buffer(vulkan, resolve_readback_buffer) == expected_resolve);

	destroy_image(vulkan, resolve_image);
	destroy_image(vulkan, msaa_image);
	destroy_image(vulkan, blit_image);
	destroy_image(vulkan, copied_image);
	destroy_image(vulkan, upload_image);
	destroy_buffer(vulkan, resolve_readback_buffer);
	destroy_buffer(vulkan, image_readback_buffer);
	destroy_buffer(vulkan, copied_buffer);
	destroy_buffer(vulkan, source_buffer);

	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	test_done(vulkan);
	return 0;
}
