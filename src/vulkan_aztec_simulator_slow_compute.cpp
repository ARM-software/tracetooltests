// Isolated repro for a slow SPIR-V simulator run seen in Kishonti Aztec Ruins.
//
// The source shader is:
//   gfxbench50/shaders/scene5/deferred_irradiance_volumes/m_envprobe_generate_sh_compute.shader

#include "vulkan_common.h"

#include <array>
#include <vector>

#include "vulkan_aztec_simulator_slow_compute.inc"

static constexpr uint32_t k_envprobe_count = 32;
static constexpr uint32_t k_envmap_width = 96;
static constexpr uint32_t k_envmap_height = 512;
static constexpr uint32_t k_uniform_vec4_count = 32;
static constexpr uint32_t k_sh_vec4_count = 512;
static constexpr VkDeviceSize k_uniform_buffer_size = k_uniform_vec4_count * sizeof(uint32_t) * 4;
static constexpr VkDeviceSize k_output_buffer_size = k_sh_vec4_count * sizeof(float) * 4;

struct image_resource
{
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
};

static VkShaderModule create_shader_module(const vulkan_setup_t& vulkan)
{
	const std::vector<uint32_t> code = copy_shader(vulkan_aztec_simulator_slow_compute_spv, vulkan_aztec_simulator_slow_compute_spv_len);
	VkShaderModuleCreateInfo create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	create_info.codeSize = code.size() * sizeof(uint32_t);
	create_info.pCode = code.data();

	VkShaderModule module = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &module);
	check(result);
	return module;
}

static image_resource create_sampled_image(const vulkan_setup_t& vulkan)
{
	image_resource resource;

	VkImageCreateInfo image_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	image_info.extent = { k_envmap_width, k_envmap_height, 1 };
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(vulkan.device, &image_info, nullptr, &resource.image);
	check(result);

	VkMemoryRequirements requirements = {};
	vkGetImageMemoryRequirements(vulkan.device, resource.image, &requirements);

	VkMemoryAllocateInfo allocate_info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = get_device_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &resource.memory);
	check(result);

	result = vkBindImageMemory(vulkan.device, resource.image, resource.memory, 0);
	check(result);

	VkImageViewCreateInfo view_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
	view_info.image = resource.image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = image_info.format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	result = vkCreateImageView(vulkan.device, &view_info, nullptr, &resource.view);
	check(result);

	VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr };
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 1.0f;
	result = vkCreateSampler(vulkan.device, &sampler_info, nullptr, &resource.sampler);
	check(result);

	return resource;
}

static std::vector<float> make_envmap_data()
{
	std::vector<float> data(k_envmap_width * k_envmap_height * 4);
	for (uint32_t y = 0; y < k_envmap_height; y++)
	{
		for (uint32_t x = 0; x < k_envmap_width; x++)
		{
			const uint32_t offset = (y * k_envmap_width + x) * 4;
			data[offset + 0] = static_cast<float>((x % 17) + 1) / 17.0f;
			data[offset + 1] = static_cast<float>((y % 31) + 1) / 31.0f;
			data[offset + 2] = static_cast<float>(((x + y) % 23) + 1) / 23.0f;
			data[offset + 3] = 1.0f;
		}
	}
	return data;
}

static void upload_sampled_image(const vulkan_setup_t& vulkan, VkQueue queue, VkCommandPool command_pool, VkCommandBuffer command_buffer,
                                 const image_resource& image)
{
	std::vector<float> pixels = make_envmap_data();
	acceleration_structures::Buffer staging = acceleration_structures::prepare_buffer(
	    vulkan,
	    pixels.size() * sizeof(float),
	    pixels.data(),
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);

	VkImageMemoryBarrier to_transfer = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
	to_transfer.srcAccessMask = 0;
	to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_transfer.image = image.image;
	to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	to_transfer.subresourceRange.levelCount = 1;
	to_transfer.subresourceRange.layerCount = 1;
	vkCmdPipelineBarrier(
	    command_buffer,
	    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	    VK_PIPELINE_STAGE_TRANSFER_BIT,
	    0,
	    0,
	    nullptr,
	    0,
	    nullptr,
	    1,
	    &to_transfer);

	VkBufferImageCopy copy = {};
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent = { k_envmap_width, k_envmap_height, 1 };
	vkCmdCopyBufferToImage(command_buffer, staging.handle, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	VkImageMemoryBarrier to_sample = to_transfer;
	to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier(
	    command_buffer,
	    VK_PIPELINE_STAGE_TRANSFER_BIT,
	    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	    0,
	    0,
	    nullptr,
	    0,
	    nullptr,
	    1,
	    &to_sample);

	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
	check(result);
	result = vkQueueWaitIdle(queue);
	check(result);

	vkResetCommandBuffer(command_buffer, 0);
	vkDestroyBuffer(vulkan.device, staging.handle, nullptr);
	testFreeMemory(vulkan, staging.memory);
}

static void destroy_image(const vulkan_setup_t& vulkan, image_resource& image)
{
	vkDestroySampler(vulkan.device, image.sampler, nullptr);
	vkDestroyImageView(vulkan.device, image.view, nullptr);
	vkDestroyImage(vulkan.device, image.image, nullptr);
	testFreeMemory(vulkan, image.memory);
}

int main(int argc, char** argv)
{
	vulkan_req_t req;
	req.minApiVersion = VK_API_VERSION_1_0;
	req.apiVersion = VK_API_VERSION_1_0;
	req.required_queue_flags = VK_QUEUE_COMPUTE_BIT;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_aztec_simulator_slow_compute", req);

	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(vulkan.device, vulkan.queue_family_index, 0, &queue);

	VkCommandPoolCreateInfo command_pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = vulkan.queue_family_index;
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

	image_resource envmap = create_sampled_image(vulkan);
	upload_sampled_image(vulkan, queue, command_pool, command_buffer, envmap);

	std::array<std::array<uint32_t, 4>, k_uniform_vec4_count> envprobe_indices = {};
	for (uint32_t i = 0; i < k_envprobe_count; i++)
	{
		envprobe_indices[i][0] = i;
	}
	std::array<std::array<float, 4>, k_sh_vec4_count> sh_output = {};

	acceleration_structures::Buffer uniform_buffer = acceleration_structures::prepare_buffer(
	    vulkan,
	    k_uniform_buffer_size,
	    envprobe_indices.data(),
	    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	acceleration_structures::Buffer output_buffer = acceleration_structures::prepare_buffer(
	    vulkan,
	    k_output_buffer_size,
	    sh_output.data(),
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	std::array<VkDescriptorSetLayoutBinding, 2> set0_bindings = {};
	set0_bindings[0].binding = 0;
	set0_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	set0_bindings[0].descriptorCount = 1;
	set0_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	set0_bindings[1].binding = 1;
	set0_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	set0_bindings[1].descriptorCount = 1;
	set0_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo set0_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	set0_layout_info.bindingCount = static_cast<uint32_t>(set0_bindings.size());
	set0_layout_info.pBindings = set0_bindings.data();
	VkDescriptorSetLayout set0_layout = VK_NULL_HANDLE;
	result = vkCreateDescriptorSetLayout(vulkan.device, &set0_layout_info, nullptr, &set0_layout);
	check(result);

	VkDescriptorSetLayoutBinding set1_binding = {};
	set1_binding.binding = 0;
	set1_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	set1_binding.descriptorCount = 1;
	set1_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo set1_layout_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	set1_layout_info.bindingCount = 1;
	set1_layout_info.pBindings = &set1_binding;
	VkDescriptorSetLayout set1_layout = VK_NULL_HANDLE;
	result = vkCreateDescriptorSetLayout(vulkan.device, &set1_layout_info, nullptr, &set1_layout);
	check(result);

	std::array<VkDescriptorPoolSize, 3> pool_sizes = {};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[0].descriptorCount = 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	pool_sizes[1].descriptorCount = 1;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[2].descriptorCount = 1;
	VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	pool_info.maxSets = 2;
	pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
	pool_info.pPoolSizes = pool_sizes.data();
	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
	result = vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &descriptor_pool);
	check(result);

	std::array<VkDescriptorSetLayout, 2> set_layouts = { set0_layout, set1_layout };
	std::array<VkDescriptorSet, 2> descriptor_sets = {};
	VkDescriptorSetAllocateInfo allocate_sets = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
	allocate_sets.descriptorPool = descriptor_pool;
	allocate_sets.descriptorSetCount = static_cast<uint32_t>(descriptor_sets.size());
	allocate_sets.pSetLayouts = set_layouts.data();
	result = vkAllocateDescriptorSets(vulkan.device, &allocate_sets, descriptor_sets.data());
	check(result);

	VkDescriptorBufferInfo uniform_info = {};
	uniform_info.buffer = uniform_buffer.handle;
	uniform_info.range = k_uniform_buffer_size;

	VkDescriptorBufferInfo output_info = {};
	output_info.buffer = output_buffer.handle;
	output_info.range = k_output_buffer_size;

	VkDescriptorImageInfo image_info = {};
	image_info.sampler = envmap.sampler;
	image_info.imageView = envmap.view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	std::array<VkWriteDescriptorSet, 3> writes = {};
	writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	writes[0].dstSet = descriptor_sets[0];
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo = &uniform_info;
	writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	writes[1].dstSet = descriptor_sets[0];
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[1].pBufferInfo = &output_info;
	writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
	writes[2].dstSet = descriptor_sets[1];
	writes[2].dstBinding = 0;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[2].pImageInfo = &image_info;
	vkUpdateDescriptorSets(vulkan.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	VkPipelineLayoutCreateInfo pipeline_layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
	pipeline_layout_info.pSetLayouts = set_layouts.data();
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &pipeline_layout);
	check(result);

	VkShaderModule shader = create_shader_module(vulkan);
	VkComputePipelineCreateInfo pipeline_info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	pipeline_info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = shader;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = pipeline_layout;
	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
	check(result);

	bench_start_scene(vulkan.bench, "aztec_simulator_slow_compute");
	bench_start_iteration(vulkan.bench);

	VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check(result);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, static_cast<uint32_t>(descriptor_sets.size()), descriptor_sets.data(), 0, nullptr);
	vkCmdDispatch(command_buffer, 1, k_envprobe_count, 1);
	result = vkEndCommandBuffer(command_buffer);
	check(result);

	VkFence fence = VK_NULL_HANDLE;
	VkFenceCreateInfo fence_info = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	result = vkCreateFence(vulkan.device, &fence_info, nullptr, &fence);
	check(result);

	VkSubmitInfo submit_info = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;
	result = vkQueueSubmit(queue, 1, &submit_info, fence);
	check(result);
	result = vkWaitForFences(vulkan.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);

	bench_stop_iteration(vulkan.bench);
	bench_stop_scene(vulkan.bench);

	vkDestroyFence(vulkan.device, fence, nullptr);
	vkDestroyPipeline(vulkan.device, pipeline, nullptr);
	vkDestroyShaderModule(vulkan.device, shader, nullptr);
	vkDestroyPipelineLayout(vulkan.device, pipeline_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, set1_layout, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, set0_layout, nullptr);
	vkDestroyBuffer(vulkan.device, output_buffer.handle, nullptr);
	testFreeMemory(vulkan, output_buffer.memory);
	vkDestroyBuffer(vulkan.device, uniform_buffer.handle, nullptr);
	testFreeMemory(vulkan, uniform_buffer.memory);
	destroy_image(vulkan, envmap);
	vkFreeCommandBuffers(vulkan.device, command_pool, 1, &command_buffer);
	vkDestroyCommandPool(vulkan.device, command_pool, nullptr);

	test_done(vulkan);
	return 0;
}
