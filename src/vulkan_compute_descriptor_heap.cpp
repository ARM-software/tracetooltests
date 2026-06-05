// Compute unit test using VK_EXT_descriptor_heap.
// Based on vulkan_compute_descriptor_buffer.cpp.

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_compute_1.comp -o vulkan_compute_1.spirv
//   xxd -i vulkan_compute_1.spirv > vulkan_compute_1.inc
#include "vulkan_compute_1.inc"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

struct pixel
{
	float r, g, b, a;
};

struct heap_buffer
{
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDeviceAddress device_address = 0;
	void* mapped = nullptr;
	VkDeviceSize size = 0;
};

struct descriptor_heap_functions
{
	PFN_vkWriteSamplerDescriptorsEXT writeSamplerDescriptors = nullptr;
	PFN_vkWriteResourceDescriptorsEXT writeResourceDescriptors = nullptr;
	PFN_vkCmdBindSamplerHeapEXT cmdBindSamplerHeap = nullptr;
	PFN_vkCmdBindResourceHeapEXT cmdBindResourceHeap = nullptr;
};

static void show_usage()
{
	compute_usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return compute_cmdopt(i, argc, argv, reqs);
}

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment)
{
	if (alignment == 0) return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static VkDeviceAddress align_up_address(VkDeviceAddress value, VkDeviceSize alignment)
{
	if (alignment == 0) return value;
	return ((value + alignment - 1) / alignment) * alignment;
}

static descriptor_heap_functions load_descriptor_heap_functions(const vulkan_setup_t& vulkan)
{
	descriptor_heap_functions funcs{};
	funcs.writeSamplerDescriptors = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkWriteSamplerDescriptorsEXT"));
	funcs.writeResourceDescriptors = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkWriteResourceDescriptorsEXT"));
	funcs.cmdBindSamplerHeap = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkCmdBindSamplerHeapEXT"));
	funcs.cmdBindResourceHeap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
		vkGetDeviceProcAddr(vulkan.device, "vkCmdBindResourceHeapEXT"));
	assert(funcs.writeSamplerDescriptors);
	assert(funcs.writeResourceDescriptors);
	assert(funcs.cmdBindSamplerHeap);
	assert(funcs.cmdBindResourceHeap);
	return funcs;
}

static VkPhysicalDeviceDescriptorHeapPropertiesEXT get_descriptor_heap_properties(VkPhysicalDevice physical)
{
	VkPhysicalDeviceDescriptorHeapPropertiesEXT props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT, nullptr};
	VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &props};
	vkGetPhysicalDeviceProperties2(physical, &props2);
	return props;
}

static heap_buffer create_heap_buffer(const vulkan_setup_t& vulkan, VkDeviceSize size, const char* name)
{
	heap_buffer out{};
	out.size = size;

	VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr};
	buffer_info.size = size;
	buffer_info.usage = VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(vulkan.device, &buffer_info, nullptr, &out.buffer);
	check(result);

	VkMemoryRequirements memreq{};
	vkGetBufferMemoryRequirements(vulkan.device, out.buffer, &memreq);

	VkMemoryAllocateFlagsInfo flags_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr};
	flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &flags_info};
	alloc_info.allocationSize = memreq.size;
	alloc_info.memoryTypeIndex = get_device_memory_type(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = vkAllocateMemory(vulkan.device, &alloc_info, nullptr, &out.memory);
	check(result);

	result = vkBindBufferMemory(vulkan.device, out.buffer, out.memory, 0);
	check(result);
	result = vkMapMemory(vulkan.device, out.memory, 0, memreq.size, 0, &out.mapped);
	check(result);
	assert(out.mapped != nullptr);
	std::memset(out.mapped, 0, static_cast<size_t>(memreq.size));

	VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
	address_info.buffer = out.buffer;
	out.device_address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	assert(out.device_address != 0);

	test_set_name(vulkan, VK_OBJECT_TYPE_BUFFER, (uint64_t)out.buffer, name);
	return out;
}

static void destroy_heap_buffer(const vulkan_setup_t& vulkan, heap_buffer& buffer)
{
	if (buffer.mapped != nullptr)
	{
		vkUnmapMemory(vulkan.device, buffer.memory);
		buffer.mapped = nullptr;
	}
	if (buffer.buffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(vulkan.device, buffer.buffer, nullptr);
		buffer.buffer = VK_NULL_HANDLE;
	}
	if (buffer.memory != VK_NULL_HANDLE)
	{
		testFreeMemory(vulkan, buffer.memory);
		buffer.memory = VK_NULL_HANDLE;
	}
}

static void create_descriptor_heap_pipeline(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs, uint32_t resource_heap_offset)
{
	VkShaderModuleCreateInfo create_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr};
	create_info.pCode = r.code.data();
	create_info.codeSize = r.code.size() * sizeof(uint32_t);
	VkResult result = vkCreateShaderModule(vulkan.device, &create_info, nullptr, &r.computeShaderModule);
	check(result);

	std::array<VkSpecializationMapEntry, 5> smentries{};
	for (uint32_t i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	const int width = std::get<int>(reqs.options.at("width"));
	const int height = std::get<int>(reqs.options.at("height"));
	const int wg_size = std::get<int>(reqs.options.at("wg_size"));
	std::array<int32_t, 5> sdata = {wg_size, wg_size, 1, width, height};

	VkSpecializationInfo spec_info{};
	spec_info.mapEntryCount = static_cast<uint32_t>(smentries.size());
	spec_info.pMapEntries = smentries.data();
	spec_info.dataSize = sdata.size() * sizeof(int32_t);
	spec_info.pData = sdata.data();

	VkDescriptorSetAndBindingMappingEXT storage_buffer_mapping{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT, nullptr};
	storage_buffer_mapping.descriptorSet = 0;
	storage_buffer_mapping.firstBinding = 0;
	storage_buffer_mapping.bindingCount = 1;
	storage_buffer_mapping.resourceMask = VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT;
	storage_buffer_mapping.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
	storage_buffer_mapping.sourceData.constantOffset.heapOffset = resource_heap_offset;
	storage_buffer_mapping.sourceData.constantOffset.heapArrayStride = 0;
	storage_buffer_mapping.sourceData.constantOffset.pEmbeddedSampler = nullptr;
	storage_buffer_mapping.sourceData.constantOffset.samplerHeapOffset = 0;
	storage_buffer_mapping.sourceData.constantOffset.samplerHeapArrayStride = 0;

	VkShaderDescriptorSetAndBindingMappingInfoEXT mapping_info{VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT, nullptr};
	mapping_info.mappingCount = 1;
	mapping_info.pMappings = &storage_buffer_mapping;

	VkPipelineShaderStageCreateInfo shader_stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, &mapping_info};
	shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shader_stage.module = r.computeShaderModule;
	shader_stage.pName = "main";
	shader_stage.pSpecializationInfo = &spec_info;

	VkPipelineCreateFlags2CreateInfo flags2{VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO, nullptr};
	flags2.flags = VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
	VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, &flags2};
	pipeline_info.stage = shader_stage;
	pipeline_info.layout = VK_NULL_HANDLE;

	if (reqs.options.count("pipelinecache"))
	{
		char* blob = nullptr;
		VkPipelineCacheCreateInfo cache_info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, nullptr};
		if (reqs.options.count("cachefile") && exists_blob(std::get<std::string>(reqs.options.at("cachefile"))))
		{
			uint32_t size = 0;
			blob = load_blob(std::get<std::string>(reqs.options.at("cachefile")), &size);
			cache_info.initialDataSize = size;
			cache_info.pInitialData = blob;
		}
		result = vkCreatePipelineCache(vulkan.device, &cache_info, nullptr, &r.cache);
		check(result);
		free(blob);
	}

	result = vkCreateComputePipelines(vulkan.device, r.cache, 1, &pipeline_info, nullptr, &r.pipeline);
	check(result);
}

int main(int argc, char** argv)
{
	p__loops = 1;

	VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR, nullptr};
	maintenance5_features.maintenance5 = VK_TRUE;
	VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptor_heap_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT, &maintenance5_features};
	descriptor_heap_features.descriptorHeap = VK_TRUE;

	vulkan_req_t req{};
	req.options["width"] = 640;
	req.options["height"] = 480;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.bufferDeviceAddress = true;
	req.apiVersion = VK_API_VERSION_1_3;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.device_extensions.push_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
	req.device_extensions.push_back(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&descriptor_heap_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_compute_descriptor_heap", req);
	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	VkResult result;

	descriptor_heap_functions funcs = load_descriptor_heap_functions(vulkan);
	VkPhysicalDeviceDescriptorHeapPropertiesEXT desc_props = get_descriptor_heap_properties(vulkan.physical);
	assert(desc_props.bufferDescriptorSize > 0);
	assert(desc_props.samplerDescriptorSize > 0);

	const VkDeviceSize resource_descriptor_offset = 0;
	const VkDeviceSize resource_reserved_offset = align_up(desc_props.bufferDescriptorSize,
	                                                       std::max(desc_props.bufferDescriptorAlignment, desc_props.resourceHeapAlignment));
	const VkDeviceSize resource_heap_size = resource_reserved_offset + desc_props.minResourceHeapReservedRange;
	assert(resource_heap_size <= desc_props.maxResourceHeapSize);

	const VkDeviceSize sampler_descriptor_offset = 0;
	const VkDeviceSize sampler_reserved_offset = align_up(desc_props.samplerDescriptorSize,
	                                                      std::max(desc_props.samplerDescriptorAlignment, desc_props.samplerHeapAlignment));
	const VkDeviceSize sampler_heap_size = sampler_reserved_offset + desc_props.minSamplerHeapReservedRange;
	assert(sampler_heap_size <= desc_props.maxSamplerHeapSize);

	heap_buffer resource_heap = create_heap_buffer(
		vulkan,
		resource_heap_size + std::max(desc_props.resourceHeapAlignment, VkDeviceSize(1)) - 1,
		"compute_descriptor_heap_resource_heap");
	heap_buffer sampler_heap = create_heap_buffer(
		vulkan,
		sampler_heap_size + std::max(desc_props.samplerHeapAlignment, VkDeviceSize(1)) - 1,
		"compute_descriptor_heap_sampler_heap");

	const VkDeviceAddress resource_heap_base = align_up_address(resource_heap.device_address, desc_props.resourceHeapAlignment);
	const VkDeviceAddress sampler_heap_base = align_up_address(sampler_heap.device_address, desc_props.samplerHeapAlignment);
	const VkDeviceSize resource_heap_map_offset = resource_heap_base - resource_heap.device_address;
	const VkDeviceSize sampler_heap_map_offset = sampler_heap_base - sampler_heap.device_address;
	assert(resource_heap_map_offset + resource_heap_size <= resource_heap.size);
	assert(sampler_heap_map_offset + sampler_heap_size <= sampler_heap.size);
	assert(resource_descriptor_offset <= std::numeric_limits<uint32_t>::max());

	VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
	address_info.buffer = r.buffer;
	VkDeviceAddress storage_buffer_address = vulkan.vkGetBufferDeviceAddress(vulkan.device, &address_info);
	assert(storage_buffer_address != 0);

	VkPhysicalDeviceProperties device_props{};
	vkGetPhysicalDeviceProperties(vulkan.physical, &device_props);
	assert((storage_buffer_address % device_props.limits.minStorageBufferOffsetAlignment) == 0);

	VkDeviceAddressRangeEXT storage_buffer_range{};
	storage_buffer_range.address = storage_buffer_address;
	storage_buffer_range.size = r.buffer_size;
	VkResourceDescriptorInfoEXT storage_buffer_descriptor{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT, nullptr};
	storage_buffer_descriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	storage_buffer_descriptor.data.pAddressRange = &storage_buffer_range;
	VkHostAddressRangeEXT resource_descriptor_range{};
	resource_descriptor_range.address = static_cast<uint8_t*>(resource_heap.mapped) + resource_heap_map_offset + resource_descriptor_offset;
	resource_descriptor_range.size = static_cast<size_t>(desc_props.bufferDescriptorSize);
	result = funcs.writeResourceDescriptors(vulkan.device, 1, &storage_buffer_descriptor, &resource_descriptor_range);
	check(result);

	VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr};
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 1.0f;
	VkHostAddressRangeEXT sampler_descriptor_range{};
	sampler_descriptor_range.address = static_cast<uint8_t*>(sampler_heap.mapped) + sampler_heap_map_offset + sampler_descriptor_offset;
	sampler_descriptor_range.size = static_cast<size_t>(desc_props.samplerDescriptorSize);
	result = funcs.writeSamplerDescriptors(vulkan.device, 1, &sampler_info, &sampler_descriptor_range);
	check(result);

	testFlushMemoryDescriptors(vulkan, resource_heap.memory, 0, resource_heap.size,
	                           {resource_heap_map_offset + resource_descriptor_offset},
	                           {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
	testFlushMemoryDescriptors(vulkan, sampler_heap.memory, 0, sampler_heap.size,
	                           {sampler_heap_map_offset + sampler_descriptor_offset},
	                           {VK_DESCRIPTOR_TYPE_SAMPLER});

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	create_descriptor_heap_pipeline(vulkan, r, req, static_cast<uint32_t>(resource_descriptor_offset));

	test_set_name(vulkan, VK_OBJECT_TYPE_PIPELINE, (uint64_t)r.pipeline, "compute_descriptor_heap_pipeline");
	test_marker_mention(vulkan, "Compute descriptor heap resources are ready", VK_OBJECT_TYPE_BUFFER, (uint64_t)resource_heap.buffer);

	for (unsigned frame = 0; frame < p__loops; frame++)
	{
		VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr};
		begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBuffer, &begin_info);
		check(result);

		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);

		VkBindHeapInfoEXT sampler_bind_info{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr};
		sampler_bind_info.heapRange.address = sampler_heap_base;
		sampler_bind_info.heapRange.size = sampler_heap_size;
		sampler_bind_info.reservedRangeOffset = sampler_reserved_offset;
		sampler_bind_info.reservedRangeSize = desc_props.minSamplerHeapReservedRange;
		funcs.cmdBindSamplerHeap(r.commandBuffer, &sampler_bind_info);

		VkBindHeapInfoEXT resource_bind_info{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT, nullptr};
		resource_bind_info.heapRange.address = resource_heap_base;
		resource_bind_info.heapRange.size = resource_heap_size;
		resource_bind_info.reservedRangeOffset = resource_reserved_offset;
		resource_bind_info.reservedRangeSize = desc_props.minResourceHeapReservedRange;
		funcs.cmdBindResourceHeap(r.commandBuffer, &resource_bind_info);

		vkCmdDispatch(r.commandBuffer, static_cast<uint32_t>(std::ceil(width / float(workgroup_size))),
		              static_cast<uint32_t>(std::ceil(height / float(workgroup_size))), 1);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, req);
	}

	destroy_heap_buffer(vulkan, sampler_heap);
	destroy_heap_buffer(vulkan, resource_heap);
	compute_done(vulkan, r, req);
	test_done(vulkan);
	return 0;
}
