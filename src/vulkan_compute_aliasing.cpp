// Minimal compute unit test
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

static void show_usage()
{
	compute_usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return compute_cmdopt(i, argc, argv, reqs);
}

static VkDeviceSize aligned_requirement_size(const VkMemoryRequirements& requirements)
{
	const VkDeviceSize align_mod = requirements.size % requirements.alignment;
	return (align_mod == 0) ? requirements.size : (requirements.size + requirements.alignment - align_mod);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.options["width"] = 640;
	reqs.options["height"] = 480;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	enable_frame_boundary(reqs); // need this here
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_aliasing", reqs);
	compute_resources r = compute_init(vulkan, reqs);
	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int workgroup_size = std::get<int>(reqs.options.at("wg_size"));
	VkResult result;

	// Replace the default compute resources with aliased buffer/image resources.
	vkDestroyImage(vulkan.device, r.image, nullptr);
	r.image = VK_NULL_HANDLE;
	vkDestroyBuffer(vulkan.device, r.buffer, nullptr);
	r.buffer = VK_NULL_HANDLE;
	testFreeMemory(vulkan, r.memory);
	r.memory = VK_NULL_HANDLE;

	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = r.buffer_size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &r.buffer);
	check(result);

	const uint32_t queueFamilyIndex = 0;
	VkImageCreateInfo imageCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	imageCreateInfo.flags = 0;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCreateInfo.extent.width = width;
	imageCreateInfo.extent.height = height;
	imageCreateInfo.extent.depth = 1;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.queueFamilyIndexCount = 1;
	imageCreateInfo.pQueueFamilyIndices = &queueFamilyIndex;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(vulkan.device, &imageCreateInfo, nullptr, &r.image);
	check(result);

	VkMemoryRequirements buffer_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, r.buffer, &buffer_requirements);
	VkMemoryRequirements image_requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, r.image, &image_requirements);
	const uint32_t merged_memory_type_bits = buffer_requirements.memoryTypeBits & image_requirements.memoryTypeBits;
	assert(merged_memory_type_bits != 0);
	const uint32_t memory_type_index = get_device_memory_type(merged_memory_type_bits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	const VkDeviceSize buffer_allocation_size = aligned_requirement_size(buffer_requirements);
	const VkDeviceSize image_allocation_size = aligned_requirement_size(image_requirements);

	VkMemoryAllocateInfo allocationInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocationInfo.memoryTypeIndex = memory_type_index;
	allocationInfo.allocationSize = std::max(buffer_allocation_size, image_allocation_size);
	result = vkAllocateMemory(vulkan.device, &allocationInfo, nullptr, &r.memory);
	check(result);
	assert(r.memory != VK_NULL_HANDLE);

	if (vulkan.apiVersion >= VK_API_VERSION_1_1)
	{
		VkBindBufferMemoryInfo bindBufferInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr };
		bindBufferInfo.buffer = r.buffer;
		bindBufferInfo.memory = r.memory;
		bindBufferInfo.memoryOffset = 0;
		result = vkBindBufferMemory2(vulkan.device, 1, &bindBufferInfo);
		check(result);

		VkBindImageMemoryInfo bindImageInfo = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, nullptr };
		bindImageInfo.image = r.image;
		bindImageInfo.memory = r.memory;
		bindImageInfo.memoryOffset = 0; // aliased with buffer
		result = vkBindImageMemory2(vulkan.device, 1, &bindImageInfo);
		check(result);
	}
	else
	{
		result = vkBindBufferMemory(vulkan.device, r.buffer, r.memory, 0);
		check(result);
		result = vkBindImageMemory(vulkan.device, r.image, r.memory, 0);
		check(result);
	}

	result = vkResetCommandBuffer(r.commandBufferFrameBoundary, 0);
	check(result);
	VkCommandBufferBeginInfo fbBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	fbBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(r.commandBufferFrameBoundary, &fbBeginInfo);
	check(result);
	VkImageMemoryBarrier image_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	image_barrier.srcAccessMask = VK_ACCESS_NONE;
	image_barrier.dstAccessMask = VK_ACCESS_NONE;
	image_barrier.image = r.image;
	image_barrier.subresourceRange.baseMipLevel = 0;
	image_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	image_barrier.subresourceRange.baseArrayLayer = 0;
	image_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	vkCmdPipelineBarrier(r.commandBufferFrameBoundary, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &image_barrier);
	result = vkEndCommandBuffer(r.commandBufferFrameBoundary);
	check(result);
	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &r.commandBufferFrameBoundary;
	result = vkQueueSubmit(r.queue, 1, &submitInfo, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(r.queue);
	check(result);
	result = vkResetCommandBuffer(r.commandBufferFrameBoundary, 0);
	check(result);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
	descriptorSetLayoutBinding.binding = 0;
	descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorSetLayoutBinding.descriptorCount = 1;
	descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	descriptorSetLayoutCreateInfo.bindingCount = 1;
	descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
        result = vkCreateDescriptorSetLayout(vulkan.device, &descriptorSetLayoutCreateInfo, nullptr, &r.descriptorSetLayout);
	check(result);

	VkDescriptorPoolSize descriptorPoolSize = {};
	descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorPoolSize.descriptorCount = 1;
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	descriptorPoolCreateInfo.maxSets = 1;
	descriptorPoolCreateInfo.poolSizeCount = 1;
	descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
	result = vkCreateDescriptorPool(vulkan.device, &descriptorPoolCreateInfo, nullptr, &r.descriptorPool);
	check(result);

	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	descriptorSetAllocateInfo.descriptorPool = r.descriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.pSetLayouts = &r.descriptorSetLayout;
	result = vkAllocateDescriptorSets(vulkan.device, &descriptorSetAllocateInfo, &r.descriptorSet);
	check(result);

	VkDescriptorBufferInfo descriptorBufferInfo = {};
	descriptorBufferInfo.buffer = r.buffer;
	descriptorBufferInfo.offset = 0;
	descriptorBufferInfo.range = r.buffer_size;
	VkWriteDescriptorSet writeDescriptorSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	writeDescriptorSet.dstSet = r.descriptorSet;
	writeDescriptorSet.dstBinding = 0;
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writeDescriptorSet.pBufferInfo = &descriptorBufferInfo;
	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, NULL);

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);

	compute_create_pipeline(vulkan, r, reqs);

	VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
	check(result);
	vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
	vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, NULL);
	vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
	result = vkEndCommandBuffer(r.commandBuffer);
	check(result);

	compute_submit(vulkan, r, reqs);
	compute_done(vulkan, r, reqs);
	test_done(vulkan);
}
