#include "vulkan_common.h"
#include "vulkan_raytracing_common.h"

#include <glm/glm.hpp>

// Shaders from external/vulkan-demos/shaders/glsl/raytracingcallable
// glslangValidator -V raygen.rgen -o raygen.rgen.spv
// xxd -i -n vulkan_raytracing_2_raygen_spv raygen.rgen.spv > vulkan_raytracing_2_raygen.rgen.inc
#include "vulkan_raytracing_2_raygen.rgen.inc"
// glslangValidator -V miss.rmiss -o miss.rmiss.spv
// xxd -i -n vulkan_raytracing_2_miss_spv miss.rmiss.spv > vulkan_raytracing_2_miss.rmiss.inc
#include "vulkan_raytracing_2_miss.rmiss.inc"
// glslangValidator -V closesthit.rchit -o closesthit.rchit.spv
// xxd -i -n vulkan_raytracing_2_closesthit_spv closesthit.rchit.spv > vulkan_raytracing_2_closesthit.rchit.inc
#include "vulkan_raytracing_2_closesthit.rchit.inc"
// glslangValidator -V callable1.rcall -o callable1.rcall.spv
// xxd -i -n vulkan_raytracing_2_callable1_spv callable1.rcall.spv > vulkan_raytracing_2_callable1.rcall.inc
#include "vulkan_raytracing_2_callable1.rcall.inc"
// glslangValidator -V callable2.rcall -o callable2.rcall.spv
// xxd -i -n vulkan_raytracing_2_callable2_spv callable2.rcall.spv > vulkan_raytracing_2_callable2.rcall.inc
#include "vulkan_raytracing_2_callable2.rcall.inc"
// glslangValidator -V callable3.rcall -o callable3.rcall.spv
// xxd -i -n vulkan_raytracing_2_callable3_spv callable3.rcall.spv > vulkan_raytracing_2_callable3.rcall.inc
#include "vulkan_raytracing_2_callable3.rcall.inc"

struct CameraData
{
	alignas(16) glm::mat4 viewInverse;
	alignas(16) glm::mat4 projInverse;
};

struct Resources
{
	ray_tracing_common::Context context;
	ray_tracing_common::SimpleAS accel;

	VkImage storage_image{VK_NULL_HANDLE};
	VkDeviceMemory storage_memory{VK_NULL_HANDLE};
	VkImageView storage_view{VK_NULL_HANDLE};
	VkFormat storage_format{VK_FORMAT_R8G8B8A8_UNORM};

	acceleration_structures::Buffer ubo;

	VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
	VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
	VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
	VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};

	VkPipelineShaderStageCreateInfo shader_stages[6]{};
	VkRayTracingShaderGroupCreateInfoKHR shader_groups[6]{};

	acceleration_structures::Buffer sbt_buffer;
	VkStridedDeviceAddressRegionKHR raygen_region{};
	VkStridedDeviceAddressRegionKHR miss_region{};
	VkStridedDeviceAddressRegionKHR hit_region{};
	VkStridedDeviceAddressRegionKHR callable_region{};
};

static void show_usage()
{
	printf("Minimal ray tracing callable pipeline test based on raytracingcallable\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return false;
}

static void create_storage_image(const vulkan_setup_t& vulkan, Resources& resources, uint32_t width, uint32_t height)
{
	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = resources.storage_format;
	image_info.extent.width = width;
	image_info.extent.height = height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	check(vkCreateImage(vulkan.device, &image_info, nullptr, &resources.storage_image));

	VkMemoryRequirements mem_reqs{};
	vkGetImageMemoryRequirements(vulkan.device, resources.storage_image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	check(vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &resources.storage_memory));
	check(vkBindImageMemory(vulkan.device, resources.storage_image, resources.storage_memory, 0));

	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr};
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = resources.storage_format;
	view_info.image = resources.storage_image;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;
	check(vkCreateImageView(vulkan.device, &view_info, nullptr, &resources.storage_view));

	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(resources.context.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.context.command_buffer, &begin_info));

	VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr};
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.image = resources.storage_image;
	barrier.subresourceRange = view_info.subresourceRange;

	vkCmdPipelineBarrier(resources.context.command_buffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&barrier);

	check(vkEndCommandBuffer(resources.context.command_buffer));
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &resources.context.command_buffer;
	check(vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.context.queue));
}

static void create_ubo(const vulkan_setup_t& vulkan, Resources& resources)
{
	CameraData camera{};
	camera.viewInverse = glm::mat4(1.0f);
	camera.projInverse = glm::mat4(1.0f);

	resources.ubo = acceleration_structures::prepare_buffer(
		vulkan,
		sizeof(CameraData),
		&camera,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

static void create_descriptor_set(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkDescriptorSetLayoutBinding bindings[3]{};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	layout_info.bindingCount = 3;
	layout_info.pBindings = bindings;
	check(vkCreateDescriptorSetLayout(vulkan.device, &layout_info, nullptr, &resources.descriptor_set_layout));

	VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &resources.descriptor_set_layout;
	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_info, nullptr, &resources.pipeline_layout));

	VkDescriptorPoolSize pool_sizes[3]{};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[0].descriptorCount = 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[1].descriptorCount = 1;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[2].descriptorCount = 1;

	VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 3;
	pool_info.pPoolSizes = pool_sizes;
	check(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &resources.descriptor_pool));

	VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
	alloc_info.descriptorPool = resources.descriptor_pool;
	alloc_info.descriptorSetCount = 1;
	alloc_info.pSetLayouts = &resources.descriptor_set_layout;
	check(vkAllocateDescriptorSets(vulkan.device, &alloc_info, &resources.descriptor_set));

	VkWriteDescriptorSetAccelerationStructureKHR as_info{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr};
	as_info.accelerationStructureCount = 1;
	as_info.pAccelerationStructures = &resources.accel.tlas.handle;

	VkDescriptorImageInfo image_info{};
	image_info.imageView = resources.storage_view;
	image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkDescriptorBufferInfo buffer_info{};
	buffer_info.buffer = resources.ubo.handle;
	buffer_info.offset = 0;
	buffer_info.range = sizeof(CameraData);

	VkWriteDescriptorSet writes[3]{};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].pNext = &as_info;
	writes[0].dstSet = resources.descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = resources.descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &image_info;

	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = resources.descriptor_set;
	writes[2].dstBinding = 2;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[2].pBufferInfo = &buffer_info;

	vkUpdateDescriptorSets(vulkan.device, 3, writes, 0, nullptr);
}

static void create_pipeline(const vulkan_setup_t& vulkan, Resources& resources)
{
	resources.shader_stages[0] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_raytracing_2_raygen_spv, vulkan_raytracing_2_raygen_spv_len, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
	resources.shader_stages[1] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_raytracing_2_miss_spv, vulkan_raytracing_2_miss_spv_len, VK_SHADER_STAGE_MISS_BIT_KHR);
	resources.shader_stages[2] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_raytracing_2_closesthit_spv, vulkan_raytracing_2_closesthit_spv_len, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
	resources.shader_stages[3] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_raytracing_2_callable1_spv, vulkan_raytracing_2_callable1_spv_len, VK_SHADER_STAGE_CALLABLE_BIT_KHR);
	resources.shader_stages[4] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_raytracing_2_callable2_spv, vulkan_raytracing_2_callable2_spv_len, VK_SHADER_STAGE_CALLABLE_BIT_KHR);
	resources.shader_stages[5] = acceleration_structures::prepare_shader_stage_create_info(
		vulkan, vulkan_raytracing_2_callable3_spv, vulkan_raytracing_2_callable3_spv_len, VK_SHADER_STAGE_CALLABLE_BIT_KHR);

	resources.shader_groups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	resources.shader_groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	resources.shader_groups[0].generalShader = 0;
	resources.shader_groups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_groups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

	resources.shader_groups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	resources.shader_groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	resources.shader_groups[1].generalShader = 1;
	resources.shader_groups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_groups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

	resources.shader_groups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	resources.shader_groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	resources.shader_groups[2].generalShader = VK_SHADER_UNUSED_KHR;
	resources.shader_groups[2].closestHitShader = 2;
	resources.shader_groups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
	resources.shader_groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

	for (uint32_t i = 0; i < 3; ++i)
	{
		const uint32_t group_index = 3 + i;
		resources.shader_groups[group_index].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		resources.shader_groups[group_index].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		resources.shader_groups[group_index].generalShader = group_index;
		resources.shader_groups[group_index].closestHitShader = VK_SHADER_UNUSED_KHR;
		resources.shader_groups[group_index].anyHitShader = VK_SHADER_UNUSED_KHR;
		resources.shader_groups[group_index].intersectionShader = VK_SHADER_UNUSED_KHR;
	}

	VkRayTracingPipelineCreateInfoKHR pipeline_info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR, nullptr};
	pipeline_info.stageCount = 6;
	pipeline_info.pStages = resources.shader_stages;
	pipeline_info.groupCount = 6;
	pipeline_info.pGroups = resources.shader_groups;
	pipeline_info.maxPipelineRayRecursionDepth = 1;
	pipeline_info.layout = resources.pipeline_layout;

	check(resources.context.functions.vkCreateRayTracingPipelinesKHR(
		vulkan.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &resources.pipeline));
}

static void create_sbt(const vulkan_setup_t& vulkan, Resources& resources)
{
	const uint32_t handle_size = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleSize;
	const uint32_t handle_alignment = vulkan.device_ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
	const uint32_t base_alignment = vulkan.device_ray_tracing_pipeline_properties.shaderGroupBaseAlignment;
	const uint32_t callable_count = 3;
	const uint32_t group_count = 3 + callable_count;

	const uint32_t entry_size = static_cast<uint32_t>(aligned_size(handle_size, handle_alignment));
	const uint32_t raygen_offset = 0;
	const uint32_t miss_offset = static_cast<uint32_t>(aligned_size(raygen_offset + entry_size, base_alignment));
	const uint32_t hit_offset = static_cast<uint32_t>(aligned_size(miss_offset + entry_size, base_alignment));
	const uint32_t callable_offset = static_cast<uint32_t>(aligned_size(hit_offset + entry_size, base_alignment));
	const uint32_t sbt_size = callable_offset + entry_size * callable_count;

	std::vector<uint8_t> handle_storage(group_count * handle_size);
	check(resources.context.functions.vkGetRayTracingShaderGroupHandlesKHR(
		vulkan.device, resources.pipeline, 0, group_count, handle_storage.size(), handle_storage.data()));

	resources.sbt_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		sbt_size,
		nullptr,
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	uint8_t* mapped = nullptr;
	check(vkMapMemory(vulkan.device, resources.sbt_buffer.memory, 0, sbt_size, 0, reinterpret_cast<void**>(&mapped)));
	memcpy(mapped + raygen_offset, handle_storage.data() + handle_size * 0, handle_size);
	memcpy(mapped + miss_offset, handle_storage.data() + handle_size * 1, handle_size);
	memcpy(mapped + hit_offset, handle_storage.data() + handle_size * 2, handle_size);
	for (uint32_t i = 0; i < callable_count; ++i)
	{
		const uint32_t dst_offset = callable_offset + entry_size * i;
		memcpy(mapped + dst_offset, handle_storage.data() + handle_size * (3 + i), handle_size);
	}
	if (vulkan.has_explicit_host_updates) testFlushMemory(vulkan, resources.sbt_buffer.memory, 0, sbt_size, true);
	vkUnmapMemory(vulkan.device, resources.sbt_buffer.memory);

	const VkDeviceAddress sbt_address = acceleration_structures::get_buffer_device_address(vulkan, resources.sbt_buffer.handle);
	resources.raygen_region.deviceAddress = sbt_address + raygen_offset;
	resources.raygen_region.stride = entry_size;
	resources.raygen_region.size = entry_size;

	resources.miss_region.deviceAddress = sbt_address + miss_offset;
	resources.miss_region.stride = entry_size;
	resources.miss_region.size = entry_size;

	resources.hit_region.deviceAddress = sbt_address + hit_offset;
	resources.hit_region.stride = entry_size;
	resources.hit_region.size = entry_size;

	resources.callable_region.deviceAddress = sbt_address + callable_offset;
	resources.callable_region.stride = entry_size;
	resources.callable_region.size = entry_size * callable_count;
}

static void trace(const vulkan_setup_t& vulkan, Resources& resources)
{
	VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(resources.context.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.context.command_buffer, &begin_info));

	vkCmdBindPipeline(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources.pipeline);
	vkCmdBindDescriptorSets(resources.context.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources.pipeline_layout, 0, 1, &resources.descriptor_set, 0, nullptr);

	resources.context.functions.vkCmdTraceRaysKHR(
		resources.context.command_buffer,
		&resources.raygen_region,
		&resources.miss_region,
		&resources.hit_region,
		&resources.callable_region,
		1, 1, 1);

	check(vkEndCommandBuffer(resources.context.command_buffer));
	VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &resources.context.command_buffer;
	check(vkQueueSubmit(resources.context.queue, 1, &submit_info, VK_NULL_HANDLE));
	check(vkQueueWaitIdle(resources.context.queue));
}

static void cleanup(const vulkan_setup_t& vulkan, Resources& resources)
{
	vkDestroyPipeline(vulkan.device, resources.pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptor_set_layout, nullptr);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptor_pool, nullptr);

	for (auto& stage : resources.shader_stages)
	{
		if (stage.module != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(vulkan.device, stage.module, nullptr);
			stage.module = VK_NULL_HANDLE;
		}
	}

	vkDestroyImageView(vulkan.device, resources.storage_view, nullptr);
	vkDestroyImage(vulkan.device, resources.storage_image, nullptr);
	vkFreeMemory(vulkan.device, resources.storage_memory, nullptr);

	vkFreeMemory(vulkan.device, resources.ubo.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.ubo.handle, nullptr);

	vkFreeMemory(vulkan.device, resources.sbt_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.sbt_buffer.handle, nullptr);

	ray_tracing_common::destroy_simple_triangle_as(vulkan, resources.context, resources.accel);
	ray_tracing_common::destroy_context(vulkan, resources.context);
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_tracing_features};

	ray_tracing_features.rayTracingPipeline = VK_TRUE;
	acceleration_features.accelerationStructure = VK_TRUE;

	reqs.device_extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
	reqs.device_extensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	reqs.bufferDeviceAddress = true;
	reqs.reqfeat12.descriptorIndexing = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&acceleration_features);
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_raytracing_2", reqs);
	assert(vulkan.hasfeat12.bufferDeviceAddress && "Buffer device address required");

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as_supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &rt_supported};
	VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &as_supported};
	vkGetPhysicalDeviceFeatures2(vulkan.physical, &supported);
	assert(rt_supported.rayTracingPipeline && "Ray tracing pipeline feature required");
	assert(as_supported.accelerationStructure && "Acceleration structure feature required");

	Resources resources{};
	ray_tracing_common::init_context(vulkan, resources.context);
	ray_tracing_common::build_simple_triangle_as(vulkan, resources.context, resources.accel);

	create_storage_image(vulkan, resources, 1, 1);
	create_ubo(vulkan, resources);
	create_descriptor_set(vulkan, resources);
	create_pipeline(vulkan, resources);
	create_sbt(vulkan, resources);
	trace(vulkan, resources);

	cleanup(vulkan, resources);
	test_done(vulkan);
	return 0;
}
