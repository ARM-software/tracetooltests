#include "vulkan_common.h"

static constexpr uint32_t kWidth = 16;
static constexpr uint32_t kHeight = 16;
static constexpr uint32_t kPixelSize = 4;
static constexpr VkDeviceSize kReadbackSize = kWidth * kHeight * kPixelSize;

struct image_resource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
};

struct buffer_resource
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

static void create_color_image(const vulkan_setup_t &vulkan, image_resource *target)
{
	VkImageCreateInfo image_info{};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.extent = {kWidth, kHeight, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;

	VkResult result = vkCreateImage(vulkan.device, &image_info, nullptr, &target->image);
	check(result);

	VkMemoryRequirements memory_requirements{};
	vkGetImageMemoryRequirements(vulkan.device, target->image, &memory_requirements);

	VkMemoryAllocateInfo memory_info{};
	memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_info.allocationSize = memory_requirements.size;
	memory_info.memoryTypeIndex = get_device_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	result = vkAllocateMemory(vulkan.device, &memory_info, nullptr, &target->memory);
	check(result);

	VkBindImageMemoryInfo bind_image_info{};
	bind_image_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
	bind_image_info.image = target->image;
	bind_image_info.memory = target->memory;

	result = vkBindImageMemory2(vulkan.device, 1, &bind_image_info);
	check(result);

	VkImageViewCreateInfo view_info{};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = target->image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = image_info.format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;

	result = vkCreateImageView(vulkan.device, &view_info, nullptr, &target->view);
	check(result);
}

static void create_readback_buffer(const vulkan_setup_t &vulkan, buffer_resource *readback)
{
	VkBufferCreateInfo buffer_info{};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = kReadbackSize;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &readback->buffer);
	check(result);

	VkMemoryRequirements memory_requirements{};
	vkGetBufferMemoryRequirements(vulkan.device, readback->buffer, &memory_requirements);

	VkMemoryAllocateInfo memory_info{};
	memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_info.allocationSize = memory_requirements.size;
	memory_info.memoryTypeIndex = get_device_memory_type(
		memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	result = vkAllocateMemory(vulkan.device, &memory_info, nullptr, &readback->memory);
	check(result);

	result = vkBindBufferMemory(vulkan.device, readback->buffer, readback->memory, 0);
	check(result);
}

int main(int argc, char **argv)
{
	vulkan_req_t reqs{};
	reqs.minApiVersion = VK_API_VERSION_1_3;
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.reqfeat13.dynamicRendering = VK_TRUE;
	reqs.device_extensions.push_back(VK_ARM_RENDER_PASS_STRIPED_EXTENSION_NAME);

	VkPhysicalDeviceRenderPassStripedFeaturesARM striped_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_FEATURES_ARM, nullptr, VK_TRUE};
	reqs.extension_features = reinterpret_cast<VkBaseInStructure *>(&striped_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "render_pass_striped", reqs);

	VkPhysicalDeviceRenderPassStripedPropertiesARM striped_properties{};
	striped_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RENDER_PASS_STRIPED_PROPERTIES_ARM;

	VkPhysicalDeviceProperties2 properties{};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &striped_properties;
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties);
	assert(striped_properties.maxRenderPassStripes >= 1);

	image_resource target{};
	create_color_image(vulkan, &target);

	buffer_resource readback{};
	create_readback_buffer(vulkan, &readback);

	VkCommandPoolCreateInfo pool_info{};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = vulkan.queue_family_index;

	VkCommandPool command_pool = VK_NULL_HANDLE;
	VkResult result = vkCreateCommandPool(vulkan.device, &pool_info, nullptr, &command_pool);
	check(result);

	VkCommandBufferAllocateInfo alloc_info{};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer = VK_NULL_HANDLE;
	result = vkAllocateCommandBuffers(vulkan.device, &alloc_info, &command_buffer);
	check(result);
	test_set_name(vulkan, VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer, "render_pass_striped_command_buffer");

	VkCommandBufferBeginInfo begin_info{};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	barrier.srcAccessMask = 0;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	barrier.image = target.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.layerCount = 1;

	VkDependencyInfo dep{};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(command_buffer, &dep);

	VkRenderingAttachmentInfo color_attachment{};
	color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color_attachment.imageView = target.view;
	color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.clearValue.color = {{0.0f, 0.2f, 0.8f, 1.0f}};

	VkRenderPassStripeInfoARM stripe_info{};
	stripe_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_STRIPE_INFO_ARM;
	stripe_info.stripeArea = {{0, 0}, {kWidth, kHeight}};

	VkRenderPassStripeBeginInfoARM stripe_begin{};
	stripe_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_STRIPE_BEGIN_INFO_ARM;
	stripe_begin.stripeInfoCount = 1;
	stripe_begin.pStripeInfos = &stripe_info;

	VkRenderingInfo rendering_info{};
	rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering_info.pNext = &stripe_begin;
	rendering_info.renderArea = {{0, 0}, {kWidth, kHeight}};
	rendering_info.layerCount = 1;
	rendering_info.colorAttachmentCount = 1;
	rendering_info.pColorAttachments = &color_attachment;

	assert(striped_properties.maxRenderPassStripes >= 1);
	assert(stripe_info.stripeArea.extent.width == rendering_info.renderArea.extent.width);
	assert(stripe_info.stripeArea.extent.height == rendering_info.renderArea.extent.height);

	vkCmdBeginRendering(command_buffer, &rendering_info);
	vkCmdEndRendering(command_buffer);

	VkImageMemoryBarrier2 readback_image_barrier{};
	readback_image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	readback_image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	readback_image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	readback_image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	readback_image_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	readback_image_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	readback_image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	readback_image_barrier.image = target.image;
	readback_image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	readback_image_barrier.subresourceRange.levelCount = 1;
	readback_image_barrier.subresourceRange.layerCount = 1;

	VkDependencyInfo readback_image_dep{};
	readback_image_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	readback_image_dep.imageMemoryBarrierCount = 1;
	readback_image_dep.pImageMemoryBarriers = &readback_image_barrier;
	vkCmdPipelineBarrier2(command_buffer, &readback_image_dep);

	VkBufferImageCopy copy_region{};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.mipLevel = 0;
	copy_region.imageSubresource.baseArrayLayer = 0;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent = {kWidth, kHeight, 1};

	vkCmdCopyImageToBuffer(command_buffer, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy_region);

	VkBufferMemoryBarrier2 readback_buffer_barrier{};
	readback_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	readback_buffer_barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	readback_buffer_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	readback_buffer_barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	readback_buffer_barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
	readback_buffer_barrier.buffer = readback.buffer;
	readback_buffer_barrier.offset = 0;
	readback_buffer_barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfo readback_buffer_dep{};
	readback_buffer_dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	readback_buffer_dep.bufferMemoryBarrierCount = 1;
	readback_buffer_dep.pBufferMemoryBarriers = &readback_buffer_barrier;
	vkCmdPipelineBarrier2(command_buffer, &readback_buffer_dep);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSemaphoreCreateInfo semaphore_info{};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore stripe_semaphore = VK_NULL_HANDLE;
	result = vkCreateSemaphore(vulkan.device, &semaphore_info, nullptr, &stripe_semaphore);
	check(result);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	VkSemaphoreSubmitInfo stripe_semaphore_info{};
	stripe_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	stripe_semaphore_info.semaphore = stripe_semaphore;
	stripe_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkRenderPassStripeSubmitInfoARM stripe_submit{};
	stripe_submit.sType = VK_STRUCTURE_TYPE_RENDER_PASS_STRIPE_SUBMIT_INFO_ARM;
	stripe_submit.stripeSemaphoreInfoCount = 1;
	stripe_submit.pStripeSemaphoreInfos = &stripe_semaphore_info;

	VkCommandBufferSubmitInfo command_buffer_info{};
	command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	command_buffer_info.pNext = &stripe_submit;
	command_buffer_info.commandBuffer = command_buffer;

	VkSubmitInfo2 submit_info{};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit_info.commandBufferInfoCount = 1;
	submit_info.pCommandBufferInfos = &command_buffer_info;

	test_marker_mention(vulkan, "Submitting VK_ARM_render_pass_striped command buffer", VK_OBJECT_TYPE_COMMAND_BUFFER, (uint64_t)command_buffer);
	result = vkQueueSubmit2(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);

	result = vkQueueWaitIdle(queue);
	check(result);

	if (vulkan.vkAssertBuffer)
	{
		uint32_t readback_crc = 0;
		const VkUpdateBufferInfoARM assert_info{
			VK_STRUCTURE_TYPE_UPDATE_BUFFER_INFO_ARM, nullptr, readback.buffer, 0, kReadbackSize, nullptr};

		result = vulkan.vkAssertBuffer(vulkan.device, &assert_info, &readback_crc, "render pass striped clear readback buffer");
		check(result);
		(void)readback_crc;
	}

	vkDestroySemaphore(vulkan.device, stripe_semaphore, nullptr);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);
	vkDestroyImageView(vulkan.device, target.view, nullptr);
	vkDestroyImage(vulkan.device, target.image, nullptr);
	vkFreeMemory(vulkan.device, target.memory, nullptr);
	vkDestroyBuffer(vulkan.device, readback.buffer, nullptr);
	vkFreeMemory(vulkan.device, readback.memory, nullptr);

	test_done(vulkan);
	return 0;
}
