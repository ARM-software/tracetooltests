// Minimal VK_ARM_shader_instrumentation compute test.

#include "vulkan_common.h"
#include "vulkan_compute_common.h"

#include <inttypes.h>

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

static void print_stage_flags(VkShaderStageFlags stages)
{
	if (stages & VK_SHADER_STAGE_COMPUTE_BIT) printf("compute");
	else printf("0x%x", stages);
}

static void print_metric_values(const std::vector<VkShaderInstrumentationMetricDescriptionARM>& descriptions,
                                const std::vector<uint64_t>& metric_data, uint32_t metric_block_count)
{
	const uint32_t metric_count = static_cast<uint32_t>(descriptions.size());
	const size_t metric_block_size = sizeof(VkShaderInstrumentationMetricDataHeaderARM) + metric_count * sizeof(uint64_t);
	const uint8_t* metric_bytes = reinterpret_cast<const uint8_t*>(metric_data.data());
	printf("Shader instrumentation metric blocks: %u\n", metric_block_count);
	for (uint32_t block = 0; block < metric_block_count; block++)
	{
		const uint8_t* ptr = metric_bytes + block * metric_block_size;
		const VkShaderInstrumentationMetricDataHeaderARM* header =
			reinterpret_cast<const VkShaderInstrumentationMetricDataHeaderARM*>(ptr);
		const uint64_t* values = reinterpret_cast<const uint64_t*>(ptr + sizeof(VkShaderInstrumentationMetricDataHeaderARM));
		printf("Metric block %u: resultIndex=%u resultSubIndex=%u stages=", block, header->resultIndex, header->resultSubIndex);
		print_stage_flags(header->stages);
		printf(" basicBlockIndex=%u\n", header->basicBlockIndex);
		for (uint32_t i = 0; i < metric_count; i++)
		{
			printf("\t%s: %" PRIu64 "\n", descriptions[i].name, values[i]);
		}
	}
}

int main(int argc, char** argv)
{
	p__loops = 1; // default to one loop

	VkPhysicalDeviceShaderInstrumentationFeaturesARM shader_instrumentation_features = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INSTRUMENTATION_FEATURES_ARM, nullptr, VK_TRUE
	};

	vulkan_req_t req;
	req.apiVersion = VK_API_VERSION_1_4;
	req.minApiVersion = VK_API_VERSION_1_4;
	req.options["width"] = 640;
	req.options["height"] = 480;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.device_extensions.push_back(VK_ARM_SHADER_INSTRUMENTATION_EXTENSION_NAME);
	req.extension_features = reinterpret_cast<VkBaseInStructure*>(&shader_instrumentation_features);

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_shader_instrumentation", req);
	compute_resources r = compute_init(vulkan, req);
	const int width = std::get<int>(req.options.at("width"));
	const int height = std::get<int>(req.options.at("height"));
	const int workgroup_size = std::get<int>(req.options.at("wg_size"));
	VkResult result;

	MAKEINSTANCEPROCADDR(vulkan, vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM);
	MAKEDEVICEPROCADDR(vulkan, vkCreateShaderInstrumentationARM);
	MAKEDEVICEPROCADDR(vulkan, vkDestroyShaderInstrumentationARM);
	MAKEDEVICEPROCADDR(vulkan, vkCmdBeginShaderInstrumentationARM);
	MAKEDEVICEPROCADDR(vulkan, vkCmdEndShaderInstrumentationARM);
	MAKEDEVICEPROCADDR(vulkan, vkGetShaderInstrumentationValuesARM);
	MAKEDEVICEPROCADDR(vulkan, vkClearShaderInstrumentationMetricsARM);

	VkPhysicalDeviceShaderInstrumentationPropertiesARM properties = {
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INSTRUMENTATION_PROPERTIES_ARM, nullptr
	};
	VkPhysicalDeviceProperties2 properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &properties };
	vkGetPhysicalDeviceProperties2(vulkan.physical, &properties2);
	printf("VK_ARM_shader_instrumentation properties:\n");
	printf("\tnumMetrics: %u\n", properties.numMetrics);
	printf("\tperBasicBlockGranularity: %s\n", properties.perBasicBlockGranularity ? "true" : "false");

	uint32_t metric_description_count = 0;
	result = pf_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(vulkan.physical, &metric_description_count, nullptr);
	check(result);
	assert(metric_description_count == properties.numMetrics);
	std::vector<VkShaderInstrumentationMetricDescriptionARM> metric_descriptions(metric_description_count);
	for (VkShaderInstrumentationMetricDescriptionARM& description : metric_descriptions)
	{
		description = { VK_STRUCTURE_TYPE_SHADER_INSTRUMENTATION_METRIC_DESCRIPTION_ARM, nullptr };
	}
	result = pf_vkEnumeratePhysicalDeviceShaderInstrumentationMetricsARM(vulkan.physical, &metric_description_count, metric_descriptions.data());
	check(result);
	printf("Shader instrumentation metric descriptions:\n");
	for (uint32_t i = 0; i < metric_description_count; i++)
	{
		printf("\t%u: %s - %s\n", i, metric_descriptions[i].name, metric_descriptions[i].description);
	}

	VkShaderInstrumentationCreateInfoARM instrumentation_info = {
		VK_STRUCTURE_TYPE_SHADER_INSTRUMENTATION_CREATE_INFO_ARM, nullptr
	};
	VkShaderInstrumentationARM instrumentation = VK_NULL_HANDLE;
	result = pf_vkCreateShaderInstrumentationARM(vulkan.device, &instrumentation_info, nullptr, &instrumentation);
	check(result);
	assert(instrumentation != VK_NULL_HANDLE);
	test_set_name(vulkan, VK_OBJECT_TYPE_SHADER_INSTRUMENTATION_ARM, (uint64_t)instrumentation, "Shader instrumentation");
	test_marker_mention(vulkan, "Created VK_ARM_shader_instrumentation object", VK_OBJECT_TYPE_SHADER_INSTRUMENTATION_ARM, (uint64_t)instrumentation);
	pf_vkClearShaderInstrumentationMetricsARM(vulkan.device, instrumentation);

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
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
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
	vkUpdateDescriptorSets(vulkan.device, 1, &writeDescriptorSet, 0, nullptr);

	r.code = copy_shader(vulkan_compute_1_spirv, vulkan_compute_1_spirv_len);
	compute_create_pipeline(vulkan, r, req, VK_PIPELINE_CREATE_2_INSTRUMENT_SHADERS_BIT_ARM);

	for (unsigned frame = 0; frame < p__loops; frame++)
	{
		test_marker(vulkan, "Frame " + std::to_string(frame));
		VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		result = vkBeginCommandBuffer(r.commandBuffer, &beginInfo);
		check(result);
		pf_vkCmdBeginShaderInstrumentationARM(r.commandBuffer, instrumentation);
		vkCmdBindPipeline(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
		vkCmdBindDescriptorSets(r.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipelineLayout, 0, 1, &r.descriptorSet, 0, nullptr);
		vkCmdDispatch(r.commandBuffer, (uint32_t)ceil(width / float(workgroup_size)), (uint32_t)ceil(height / float(workgroup_size)), 1);
		pf_vkCmdEndShaderInstrumentationARM(r.commandBuffer);
		result = vkEndCommandBuffer(r.commandBuffer);
		check(result);

		compute_submit(vulkan, r, req);

		const size_t metric_block_size =
			sizeof(VkShaderInstrumentationMetricDataHeaderARM) + metric_descriptions.size() * sizeof(uint64_t);
		uint32_t metric_block_count = 1;
		std::vector<uint64_t> metric_values;
		while (true)
		{
			metric_values.resize((metric_block_count * metric_block_size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
			result = pf_vkGetShaderInstrumentationValuesARM(vulkan.device, instrumentation, &metric_block_count, metric_values.data(), 0);
			if (result != VK_INCOMPLETE) break;
			assert(metric_block_count > 0);
			metric_block_count *= 2;
		}
		check(result);
		if (metric_block_count == 0)
		{
			metric_values.clear();
		}
		else
		{
			metric_values.resize((metric_block_count * metric_block_size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
		}
		print_metric_values(metric_descriptions, metric_values, metric_block_count);
	}

	pf_vkDestroyShaderInstrumentationARM(vulkan.device, instrumentation, nullptr);
	compute_done(vulkan, r, req);
	test_done(vulkan);
}
