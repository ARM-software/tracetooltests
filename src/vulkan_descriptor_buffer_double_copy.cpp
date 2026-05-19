// Descriptor-buffer test where one vkGetDescriptorEXT result is copied to two slots.

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

#include "vulkan_compute_1.inc"

static void show_usage()
{
	compute_usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return compute_cmdopt(i, argc, argv, reqs);
}

int main(int argc, char** argv)
{
	p__loops = 1;

	VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
		nullptr
	};
	descriptor_buffer_features.descriptorBuffer = VK_TRUE;

	vulkan_req_t req;
	req.options["width"] = 640;
	req.options["height"] = 480;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.bufferDeviceAddress = true;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.device_extensions.push_back("VK_EXT_descriptor_buffer");
	req.extension_features = (VkBaseInStructure*)&descriptor_buffer_features;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_descriptor_buffer_double_copy", req);
	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));

	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutSizeEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutBindingOffsetEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBindDescriptorBuffersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetDescriptorBufferOffsetsEXT);

	VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT,
		nullptr
	};
	VkPhysicalDeviceProperties2 device_properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		&descriptor_buffer_properties
	};
	vkGetPhysicalDeviceProperties2(vulkan.physical, &device_properties);
	assert(descriptor_buffer_properties.storageBufferDescriptorSize > 0);

	VkDescriptorSetLayoutBinding layout_bindings[2] = {};
	for (uint32_t i = 0; i < 2; i++)
	{
		layout_bindings[i].binding = i;
		layout_bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		layout_bindings[i].descriptorCount = 1;
		layout_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		nullptr
	};
	descriptor_set_layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	descriptor_set_layout_info.bindingCount = 2;
	descriptor_set_layout_info.pBindings = layout_bindings;
	VkResult result = vkCreateDescriptorSetLayout(vulkan.device, &descriptor_set_layout_info, nullptr, &r.descriptorSetLayout);
	check(result);

	VkDeviceSize descriptor_set_size = 0;
	pf_vkGetDescriptorSetLayoutSizeEXT(vulkan.device, r.descriptorSetLayout, &descriptor_set_size);
	assert(descriptor_set_size > 0);

	VkDeviceSize binding_offsets[2] = {};
	pf_vkGetDescriptorSetLayoutBindingOffsetEXT(vulkan.device, r.descriptorSetLayout, 0, &binding_offsets[0]);
	pf_vkGetDescriptorSetLayoutBindingOffsetEXT(vulkan.device, r.descriptorSetLayout, 1, &binding_offsets[1]);
	assert(binding_offsets[0] != binding_offsets[1]);

	VkBuffer descriptor_buffer = VK_NULL_HANDLE;
	VkBufferCreateInfo buffer_info = {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		nullptr
	};
	buffer_info.size = descriptor_set_size;
	buffer_info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
		| VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
		| VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		| VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &descriptor_buffer);
	check(result);

	VkMemoryRequirements memory_requirements = {};
	vkGetBufferMemoryRequirements(vulkan.device, descriptor_buffer, &memory_requirements);
	uint32_t memory_type_index = get_device_memory_type(memory_requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkMemoryAllocateFlagsInfo allocate_flags = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		nullptr
	};
	allocate_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo allocate_info = {
		VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		&allocate_flags
	};
	allocate_info.memoryTypeIndex = memory_type_index;
	allocate_info.allocationSize = memory_requirements.size;
	VkDeviceMemory descriptor_memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(vulkan.device, &allocate_info, nullptr, &descriptor_memory);
	check(result);
	result = vkBindBufferMemory(vulkan.device, descriptor_buffer, descriptor_memory, 0);
	check(result);

	VkBufferDeviceAddressInfo address_info = {
		VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		nullptr
	};
	address_info.buffer = descriptor_buffer;
	VkDeviceAddress descriptor_buffer_address = vkGetBufferDeviceAddress(vulkan.device, &address_info);

	address_info.buffer = r.buffer;
	VkDeviceAddress storage_buffer_address = vkGetBufferDeviceAddress(vulkan.device, &address_info);
	VkDescriptorAddressInfoEXT storage_buffer_descriptor = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
		nullptr
	};
	storage_buffer_descriptor.address = storage_buffer_address;
	storage_buffer_descriptor.range = r.buffer_size;
	storage_buffer_descriptor.format = VK_FORMAT_UNDEFINED;

	VkDescriptorDataEXT descriptor_data = {};
	descriptor_data.pStorageBuffer = &storage_buffer_descriptor;
	VkDescriptorGetInfoEXT descriptor_get_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
		nullptr
	};
	descriptor_get_info.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptor_get_info.data = descriptor_data;

	std::vector<uint8_t> descriptor_bytes(descriptor_buffer_properties.storageBufferDescriptorSize);
	pf_vkGetDescriptorEXT(vulkan.device, &descriptor_get_info, descriptor_bytes.size(), descriptor_bytes.data());

	void* mapped = nullptr;
	result = vkMapMemory(vulkan.device, descriptor_memory, 0, memory_requirements.size, 0, &mapped);
	check(result);
	memset(mapped, 0, descriptor_set_size);
	memcpy((uint8_t*)mapped + binding_offsets[0], descriptor_bytes.data(), descriptor_bytes.size());
	memcpy((uint8_t*)mapped + binding_offsets[1], descriptor_bytes.data(), descriptor_bytes.size());

	if (vulkan.has_trace_helpers)
	{
		VkFlushRangesFlagsARM flush_flags = {
			VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM,
			nullptr
		};
		flush_flags.flags = VK_FLUSH_OPERATION_INFORMATIVE_BIT_ARM;
		VkMarkingTypeARM marking_types[2] = {
			VK_MARKING_TYPE_DESCRIPTOR_ARM,
			VK_MARKING_TYPE_DESCRIPTOR_ARM
		};
		VkMarkingSubTypeARM sub_types[2] = {};
		sub_types[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		sub_types[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		VkMarkedOffsetsARM marked_offsets = {
			VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM,
			&flush_flags
		};
		marked_offsets.count = 2;
		marked_offsets.pMarkingTypes = marking_types;
		marked_offsets.pSubTypes = sub_types;
		marked_offsets.pOffsets = binding_offsets;
		VkMappedMemoryRange range = {
			VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			&marked_offsets
		};
		range.memory = descriptor_memory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		vkFlushMappedMemoryRanges(vulkan.device, 1, &range);
	}
	else if (vulkan.has_explicit_host_updates)
	{
		testFlushMemory(vulkan, descriptor_memory, 0, memory_requirements.size, true);
	}
	vkUnmapMemory(vulkan.device, descriptor_memory);

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	compute_create_pipeline(vulkan, r, req, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	for (unsigned frame = 0; frame < p__loops; frame++)
	{
		VkCommandBufferBeginInfo begin_info = {
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			nullptr
		};
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBuffer, &begin_info);
		check(result);
		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);

		VkDescriptorBufferBindingInfoEXT binding_info = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
			nullptr
		};
		binding_info.address = descriptor_buffer_address;
		binding_info.usage = buffer_info.usage;
		pf_vkCmdBindDescriptorBuffersEXT(r.commandBuffer, 1, &binding_info);

		uint32_t buffer_index = 0;
		VkDeviceSize buffer_offset = 0;
		pf_vkCmdSetDescriptorBufferOffsetsEXT(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			r.pipelineLayout, 0, 1, &buffer_index, &buffer_offset);

		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)),
			(uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, req);
	}

	vkDestroyBuffer(vulkan.device, descriptor_buffer, nullptr);
	vkFreeMemory(vulkan.device, descriptor_memory, nullptr);
	compute_done(vulkan, r, req);
	test_done(vulkan);
}
