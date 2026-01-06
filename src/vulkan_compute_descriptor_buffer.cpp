// Compute unit test using VK_EXT_descriptor_buffer
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

struct pixel
{
	float r, g, b, a;
};

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
	p__loops = 1; // default to one loop
	VkPhysicalDeviceDescriptorBufferFeaturesEXT pddbf = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT, nullptr };
	pddbf.descriptorBuffer = VK_TRUE;
	vulkan_req_t req;
	req.options["width"] = 640;
	req.options["height"] = 480;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.bufferDeviceAddress = true;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.device_extensions.push_back("VK_EXT_descriptor_buffer");
	req.extension_features = (VkBaseInStructure*)&pddbf;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_descriptor_buffer", req);
	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	VkResult result;

	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutSizeEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorEXT);
	MAKEDEVICEPROCADDR(vulkan, vkGetDescriptorSetLayoutBindingOffsetEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBindDescriptorBuffersEXT);
	MAKEDEVICEPROCADDR(vulkan, vkCmdSetDescriptorBufferOffsetsEXT);

	// Create the descriptor buffer
	VkBuffer descriptor_buffer;
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	bufferCreateInfo.size = 1024; // just make it big enough
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	result = vkCreateBuffer(vulkan.device, &bufferCreateInfo, nullptr, &descriptor_buffer);
	check(result);
	VkMemoryRequirements memreq;
	vkGetBufferMemoryRequirements(vulkan.device, descriptor_buffer, &memreq);
	uint32_t memoryTypeIndex = get_device_memory_type(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VkMemoryAllocateFlagsInfo memext = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr };
	memext.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo pAllocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &memext };
	pAllocateMemInfo.memoryTypeIndex = memoryTypeIndex;
	pAllocateMemInfo.allocationSize = memreq.size;
	VkDeviceMemory memory = 0;
	result = vkAllocateMemory(vulkan.device, &pAllocateMemInfo, nullptr, &memory);
	check(result);
	assert(memory != 0);
	vkBindBufferMemory(vulkan.device, descriptor_buffer, memory, 0);
	VkBufferDeviceAddressInfo address_info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr };
	address_info.buffer = descriptor_buffer;
	VkDeviceAddress descriptor_buffer_address = vkGetBufferDeviceAddress(vulkan.device, &address_info);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
	descriptorSetLayoutBinding.binding = 0;
	descriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorSetLayoutBinding.descriptorCount = 1;
	descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	descriptorSetLayoutCreateInfo.bindingCount = 1;
	descriptorSetLayoutCreateInfo.pBindings = &descriptorSetLayoutBinding;
	descriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        result = vkCreateDescriptorSetLayout(vulkan.device, &descriptorSetLayoutCreateInfo, nullptr, &r.descriptorSetLayout);
	check(result);

	VkPhysicalDeviceDescriptorBufferPropertiesEXT pddbp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 pdp = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &pddbp };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &pdp);
	printf("storageBufferDescriptorSize = %d\n", (int)pddbp.storageBufferDescriptorSize);

	VkDeviceSize setsize = 0;
	pf_vkGetDescriptorSetLayoutSizeEXT(vulkan.device, r.descriptorSetLayout, &setsize);
	printf("Size of our descriptor set layout: %u\n", (unsigned)setsize);

	void* ptr = nullptr;
	vkMapMemory(vulkan.device, memory, 0, 1024, 0, &ptr);
	VkDescriptorAddressInfoEXT daie = { VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT, nullptr };
	address_info.buffer = r.buffer;
	VkDeviceAddress storage_buffer_address = vkGetBufferDeviceAddress(vulkan.device, &address_info);
	daie.address = storage_buffer_address;
	daie.range = r.buffer_size;
	VkDescriptorDataEXT dde;
	dde.pStorageBuffer = &daie;
	VkDescriptorGetInfoEXT dgi = { VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT, nullptr };
	dgi.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	dgi.data = dde;
	pf_vkGetDescriptorEXT(vulkan.device, &dgi, pddbp.storageBufferDescriptorSize, ptr);

	if (vulkan.has_trace_descriptor_buffer)
	{
		VkFlushRangesFlagsARM frf = { VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM, nullptr };
		frf.flags = VK_FLUSH_OPERATION_INFORMATIVE_BIT_ARM;
		VkMarkingTypeARM markingType = VK_MARKING_TYPE_DESCRIPTOR_BIT_ARM;
		VkMarkingSubTypeARM subType;
		subType.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		VkDeviceSize offset = 0;
		VkMarkedOffsetsARM offs = { VK_STRUCTURE_TYPE_MARKED_OFFSETS_ARM, &frf };
		offs.count = 1;
		offs.pMarkingTypes = &markingType;
		offs.pSubTypes = &subType;
		offs.pOffsets = &offset;
		VkMappedMemoryRange mmr = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, &offs };
		mmr.memory = memory;
		mmr.offset = 0;
		mmr.size = VK_WHOLE_SIZE;
		vkFlushMappedMemoryRanges(vulkan.device, 1, &mmr);
	}

	if (vulkan.has_explicit_host_updates) testFlushMemory(vulkan, memory, 0, 1024, vulkan.has_explicit_host_updates);
	vkUnmapMemory(vulkan.device, memory);

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);

	compute_create_pipeline(vulkan, r, req, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	for (int frame = 0; frame < p__loops; frame++)
	{
		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
		check(result);

		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);

		VkDescriptorBufferBindingInfoEXT dbbi = { VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT, nullptr };
		dbbi.address = descriptor_buffer_address;
		dbbi.usage = bufferCreateInfo.usage;
		pf_vkCmdBindDescriptorBuffersEXT(r.commandBuffer, 1, &dbbi);

		uint32_t bufferIndexUbo = 0;
		VkDeviceSize bufferOffset = 0;
		pf_vkCmdSetDescriptorBufferOffsetsEXT(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &bufferIndexUbo, &bufferOffset);

		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, req);
	}

	vkDestroyBuffer(vulkan.device, descriptor_buffer, nullptr);
	vkFreeMemory(vulkan.device, memory, nullptr);
	compute_done(vulkan, r, req);
	test_done(vulkan);
}
