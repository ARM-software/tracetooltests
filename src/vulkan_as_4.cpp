#include "vulkan_common.h"
#include "vulkan_as_4.inc"
#include <cstdint>

using Buffer = acceleration_structures::Buffer;
using AccelerationStructure = acceleration_structures::AccelerationStructure;

struct Vertex
{
	float pos[3];
};

static std::vector<Vertex> vertices = {
	{{1.0f, 1.0f, 0.0f}},
	{{-1.0f, 1.0f, 0.0f}},
	{{0.0f, -1.0f, 0.0f}}
};

static std::vector<uint32_t> indices = {0, 1, 2};
static uint32_t index_count = static_cast<uint32_t>(indices.size());

struct Resources
{
	acceleration_structures::functions functions;

	AccelerationStructure blas;
	Buffer blas_buffer;

	AccelerationStructure tlas;
	Buffer tlas_buffer;
	Buffer instance_buffer;

	Buffer vertex_buffer;
	Buffer index_buffer;

	VkDescriptorPool descriptor_pool {VK_NULL_HANDLE};
	VkDescriptorSetLayout descriptor_set_layout { VK_NULL_HANDLE };
	VkDescriptorUpdateTemplate descriptor_update_template { VK_NULL_HANDLE };
	VkDescriptorSet descriptor_set { VK_NULL_HANDLE };

	VkShaderModule compute_shader_module{ VK_NULL_HANDLE };
	VkPipeline compute_pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout compute_pipeline_layout { VK_NULL_HANDLE };

	VkQueue queue{ VK_NULL_HANDLE };
	VkCommandPool command_pool{ VK_NULL_HANDLE};
	VkCommandBuffer command_buffer{ VK_NULL_HANDLE };
};

static void show_usage()
{
	printf("Test the binding of the acceleration structure to compute pipeline\n");
}

static bool test_cmdopt(int &i, int argc, char **argv, vulkan_req_t &reqs)
{
    return false;
}

void prepare_test_resources(const vulkan_setup_t & vulkan, Resources & resources)
{
	resources.functions = acceleration_structures::query_acceleration_structure_functions(vulkan.device);

	VkCommandPoolCreateInfo command_pool_create_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr};
	command_pool_create_info.queueFamilyIndex = 0; // TODO Make sure that this points to compute
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkResult command_pool_create_result = vkCreateCommandPool(vulkan.device, &command_pool_create_info, nullptr, &resources.command_pool);
	check(command_pool_create_result);

	vkGetDeviceQueue(vulkan.device, 0, 0, &resources.queue);

	resources.vertex_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		vertices.size() * sizeof(Vertex),
		vertices.data(),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);
	resources.vertex_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.vertex_buffer.handle);

	resources.index_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		indices.size() * sizeof(uint32_t),
		indices.data(),
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	);
	resources.index_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.index_buffer.handle);
}

void free_test_resources(const vulkan_setup_t & vulkan, Resources & resources)
{
	vkDestroyCommandPool(vulkan.device, resources.command_pool, nullptr);

	vkDestroyPipeline(vulkan.device, resources.compute_pipeline, nullptr);
	vkDestroyPipelineLayout(vulkan.device, resources.compute_pipeline_layout, nullptr);
	vkDestroyShaderModule(vulkan.device, resources.compute_shader_module, nullptr);

	vkDestroyDescriptorUpdateTemplate(vulkan.device, resources.descriptor_update_template, nullptr);
	vkDestroyDescriptorSetLayout(vulkan.device, resources.descriptor_set_layout, nullptr);
	vkFreeDescriptorSets(vulkan.device, resources.descriptor_pool, 1, &resources.descriptor_set);
	vkDestroyDescriptorPool(vulkan.device, resources.descriptor_pool, nullptr);

	resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.blas.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.blas_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.blas_buffer.handle, nullptr);

	resources.functions.vkDestroyAccelerationStructureKHR(vulkan.device, resources.tlas.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.tlas_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.tlas_buffer.handle, nullptr);
	vkFreeMemory(vulkan.device, resources.instance_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.instance_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, resources.index_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.index_buffer.handle, nullptr);

	vkFreeMemory(vulkan.device, resources.vertex_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, resources.vertex_buffer.handle, nullptr);
}

void prepare_acceleration_structures(const vulkan_setup_t & vulkan, Resources & resources)
{
	const uint32_t primitive_count = 1;

	// Build bottom level acceleration structure
	VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry.geometry.triangles.vertexData = resources.vertex_buffer.address;
	geometry.geometry.triangles.maxVertex = 3;
	geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	geometry.geometry.triangles.indexData = resources.index_buffer.address;

	VkAccelerationStructureBuildGeometryInfoKHR build_geometry_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build_geometry_info.geometryCount = 1;
	build_geometry_info.pGeometries = &geometry;

	VkAccelerationStructureBuildSizesInfoKHR build_size_info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	resources.functions.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&build_geometry_info,
		&primitive_count,
		&build_size_info
	);

	resources.blas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		build_size_info.accelerationStructureSize,
		nullptr,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
	);

	VkAccelerationStructureCreateInfoKHR create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	create_info.buffer = resources.blas_buffer.handle;
	create_info.size = build_size_info.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &create_info, nullptr, &resources.blas.handle));

	Buffer scratch_bottom = acceleration_structures::prepare_buffer(
			vulkan,
			build_size_info.buildScratchSize,
			nullptr,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
	scratch_bottom.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch_bottom.handle);

	build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build_geometry_info.dstAccelerationStructure = resources.blas.handle;
	build_geometry_info.scratchData.deviceAddress = scratch_bottom.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR *build_range_infos;
	VkAccelerationStructureBuildRangeInfoKHR build_range_info;
	build_range_info.primitiveCount = primitive_count;
	build_range_info.primitiveOffset = 0;
	build_range_info.firstVertex = 0;
	build_range_info.transformOffset = 0;
	build_range_infos = &build_range_info;

	VkCommandBufferAllocateInfo command_buffer_allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr};
	command_buffer_allocate_info.commandPool = resources.command_pool;
	command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_allocate_info.commandBufferCount = 1;

	check(vkAllocateCommandBuffers(vulkan.device, &command_buffer_allocate_info, &resources.command_buffer));
	VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

	resources.functions.vkCmdBuildAccelerationStructuresKHR(resources.command_buffer, 1, &build_geometry_info, &build_range_infos);

	check(vkEndCommandBuffer(resources.command_buffer));
	VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &resources.command_buffer;
	check(vkQueueSubmit(resources.queue, 1, &submitInfo, nullptr));
	check(vkQueueWaitIdle(resources.queue));

	// Build top level acceleration structure
	vkFreeMemory(vulkan.device, scratch_bottom.memory, nullptr);
	vkDestroyBuffer(vulkan.device, scratch_bottom.handle, nullptr);

	static VkTransformMatrixKHR identity = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
	};

	VkAccelerationStructureInstanceKHR instance;
	instance.transform = identity;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = 0;
	instance.accelerationStructureReference = resources.blas.address.deviceAddress;

	resources.instance_buffer = acceleration_structures::prepare_buffer(
			vulkan,
			sizeof(VkAccelerationStructureInstanceKHR),
			&instance,
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

	resources.instance_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, resources.instance_buffer.handle);

	VkAccelerationStructureGeometryInstancesDataKHR instance_data;
	instance_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instance_data.pNext = nullptr;
	instance_data.arrayOfPointers = VK_FALSE;
	instance_data.data = resources.instance_buffer.address;

	VkAccelerationStructureBuildRangeInfoKHR as_range_info{};
	as_range_info.primitiveOffset = 0;
	as_range_info.primitiveCount = 1;
	as_range_info.firstVertex = 0;
	as_range_info.transformOffset = 0;

	VkAccelerationStructureGeometryKHR as_geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, nullptr};
	as_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	as_geometry.geometry.instances = instance_data;

	VkAccelerationStructureBuildGeometryInfoKHR as_build_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, nullptr};
	as_build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	as_build_info.geometryCount = 1;
	as_build_info.pGeometries = &as_geometry;
	as_build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	as_build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	as_build_info.srcAccelerationStructure = VK_NULL_HANDLE;

	VkAccelerationStructureBuildSizesInfoKHR as_build_size_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR, nullptr};
	resources.functions.vkGetAccelerationStructureBuildSizesKHR(
		vulkan.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_HOST_OR_DEVICE_KHR,
		&as_build_info,
		&as_range_info.primitiveCount,
		&as_build_size_info
	);

	resources.tlas_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		as_build_size_info.accelerationStructureSize,
		nullptr,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
	);

	VkAccelerationStructureCreateInfoKHR as_create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, nullptr};
	as_create_info.type = as_build_info.type;
	as_create_info.size = as_build_size_info.accelerationStructureSize;
	as_create_info.buffer = resources.tlas_buffer.handle;
	as_create_info.offset = 0;

	check(resources.functions.vkCreateAccelerationStructureKHR(vulkan.device, &as_create_info, nullptr, &resources.tlas.handle));

	as_build_info.dstAccelerationStructure = resources.tlas.handle;

	Buffer scratch_buffer = acceleration_structures::prepare_buffer(
		vulkan,
		as_build_size_info.buildScratchSize,
		nullptr,
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR
	);
	scratch_buffer.address.deviceAddress = acceleration_structures::get_buffer_device_address(vulkan, scratch_buffer.handle);
	as_build_info.scratchData.deviceAddress = scratch_buffer.address.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR * as_range_infos = &as_range_info;

	check(vkResetCommandBuffer(resources.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

	resources.functions.vkCmdBuildAccelerationStructuresKHR(resources.command_buffer, 1, &as_build_info, &as_range_infos);

	check(vkEndCommandBuffer(resources.command_buffer));
	check(vkQueueSubmit(resources.queue, 1, &submitInfo, nullptr));
	check(vkQueueWaitIdle(resources.queue));

	vkFreeMemory(vulkan.device, scratch_buffer.memory, nullptr);
	vkDestroyBuffer(vulkan.device, scratch_buffer.handle, nullptr);
}

void prepare_descriptor_set(const vulkan_setup_t &vulkan, Resources & resources)
{
	VkDescriptorSetLayoutBinding as_binding{};
	as_binding.binding = 0;
	as_binding.descriptorCount = 1;
	as_binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	as_binding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	descriptor_set_layout_create_info.bindingCount = 1;
	descriptor_set_layout_create_info.pBindings = &as_binding;

	check(vkCreateDescriptorSetLayout(vulkan.device, &descriptor_set_layout_create_info, nullptr, &resources.descriptor_set_layout));

	VkDescriptorPoolSize as_pool_size;
	as_pool_size.type = VkDescriptorType::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	as_pool_size.descriptorCount = 1;

	VkDescriptorPoolCreateInfo descriptor_pool_create_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr};
	descriptor_pool_create_info.flags = VkDescriptorPoolCreateFlagBits::VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descriptor_pool_create_info.maxSets = 1;
	descriptor_pool_create_info.poolSizeCount = 1;
	descriptor_pool_create_info.pPoolSizes = &as_pool_size;

	check(vkCreateDescriptorPool(vulkan.device, &descriptor_pool_create_info, nullptr, &resources.descriptor_pool));

	VkDescriptorSetAllocateInfo allocate_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr};
	allocate_info.descriptorPool = resources.descriptor_pool;
	allocate_info.descriptorSetCount = 1;
	allocate_info.pSetLayouts = &resources.descriptor_set_layout;

	check(vkAllocateDescriptorSets(vulkan.device, &allocate_info, &resources.descriptor_set));
}

void prepare_compute_pipeline(const vulkan_setup_t & vulkan, Resources & resources)
{
	VkPipelineLayoutCreateInfo pipeline_layout_create_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr};
	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts = &resources.descriptor_set_layout;
	pipeline_layout_create_info.pushConstantRangeCount = 0;
	pipeline_layout_create_info.pPushConstantRanges = nullptr;

	check(vkCreatePipelineLayout(vulkan.device, &pipeline_layout_create_info, nullptr, &resources.compute_pipeline_layout));

	std::vector<uint32_t> code(vulkan_as_4_spirv_len);
	memcpy(code.data(), vulkan_as_4_spirv, vulkan_as_4_spirv_len);

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pCode = code.data();
	createInfo.codeSize = vulkan_as_4_spirv_len;
 	check(vkCreateShaderModule(vulkan.device, &createInfo, NULL, &resources.compute_shader_module));

	VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
	shaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStageCreateInfo.module = resources.compute_shader_module;
	shaderStageCreateInfo.pName = "main";

	VkComputePipelineCreateInfo compute_pipeline_create_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr};
	compute_pipeline_create_info.stage = shaderStageCreateInfo;
	compute_pipeline_create_info.layout = resources.compute_pipeline_layout;
	compute_pipeline_create_info.basePipelineHandle = 0;
	compute_pipeline_create_info.basePipelineIndex = -1;

	check(vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info, nullptr, &resources.compute_pipeline));
}

void bind_top_level_acceleration_structure_to_compute(const vulkan_setup_t & vulkan, Resources & resources)
{
	VkDescriptorUpdateTemplateEntry descriptor_update_template_entry{};
	descriptor_update_template_entry.dstBinding = 0;
	descriptor_update_template_entry.dstArrayElement = 0;
	descriptor_update_template_entry.descriptorCount = 1;
	descriptor_update_template_entry.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	descriptor_update_template_entry.offset = 0;
	descriptor_update_template_entry.stride = 0;

	VkDescriptorUpdateTemplateCreateInfo descriptor_update_template_create_info{VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO_KHR, nullptr};
	descriptor_update_template_create_info.descriptorUpdateEntryCount = 1;
	descriptor_update_template_create_info.pDescriptorUpdateEntries = &descriptor_update_template_entry;
	descriptor_update_template_create_info.pipelineLayout = resources.compute_pipeline_layout;
	descriptor_update_template_create_info.descriptorSetLayout = resources.descriptor_set_layout;
	descriptor_update_template_create_info.templateType = VkDescriptorUpdateTemplateType::VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
	descriptor_update_template_create_info.pipelineBindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_COMPUTE;
	descriptor_update_template_create_info.set = 0;

	check(vkCreateDescriptorUpdateTemplate(vulkan.device, &descriptor_update_template_create_info, nullptr, &resources.descriptor_update_template));

	vkUpdateDescriptorSetWithTemplate(vulkan.device, resources.descriptor_set, resources.descriptor_update_template, &resources.tlas.handle);

	VkCommandBufferBeginInfo command_buffer_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
	check(vkResetCommandBuffer(resources.command_buffer, 0));
	check(vkBeginCommandBuffer(resources.command_buffer, &command_buffer_begin_info));

	vkCmdBindPipeline(resources.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.compute_pipeline);
	vkCmdBindDescriptorSets(resources.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, resources.compute_pipeline_layout, 0, 1, &resources.descriptor_set, 0, nullptr);
	vkCmdDispatch(resources.command_buffer, 1, 1, 1);

	check(vkEndCommandBuffer(resources.command_buffer));

	VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &resources.command_buffer;

	check(vkQueueSubmit(resources.queue, 1, &submitInfo, nullptr));
	check(vkQueueWaitIdle(resources.queue));
}

int main(int argc, char** argv)
{
    vulkan_req_t reqs;
	VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr, VK_TRUE};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accfeats = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, &ray_query_features, VK_TRUE };
	reqs.device_extensions.push_back("VK_KHR_acceleration_structure");
	reqs.device_extensions.push_back("VK_KHR_deferred_host_operations");
	reqs.device_extensions.push_back("VK_KHR_ray_query");
	reqs.bufferDeviceAddress = true;
	reqs.extension_features = (VkBaseInStructure*)&accfeats;
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.queues = 1;
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_as_4", reqs);

	Resources resources{};

	prepare_test_resources(vulkan, resources);
	prepare_acceleration_structures(vulkan, resources);
	prepare_descriptor_set(vulkan, resources);
	prepare_compute_pipeline(vulkan, resources);

	bind_top_level_acceleration_structure_to_compute(vulkan, resources);

	free_test_resources(vulkan, resources);

    test_done(vulkan);
    return 0;
}