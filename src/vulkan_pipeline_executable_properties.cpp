#include "vulkan_common.h"

#include <array>
#include <vector>

#include "vulkan_graphics_1_vert.inc"
#include "vulkan_graphics_1_frag.inc"

static void show_usage()
{
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	(void)i;
	(void)argc;
	(void)argv;
	(void)reqs;
	return false;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs{};
	reqs.apiVersion = VK_API_VERSION_1_2;
	reqs.device_extensions.push_back("VK_KHR_pipeline_executable_properties");
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR execFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR, nullptr };
	execFeatures.pipelineExecutableInfo = VK_TRUE;
	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&execFeatures);

	auto vk = test_init(argc, argv, "vulkan_pipeline_executable_properties", reqs);

	MAKEDEVICEPROCADDR(vk, vkGetPipelineExecutablePropertiesKHR);
	MAKEDEVICEPROCADDR(vk, vkGetPipelineExecutableStatisticsKHR);
	MAKEDEVICEPROCADDR(vk, vkGetPipelineExecutableInternalRepresentationsKHR);

	bench_start_iteration(vk.bench);

	VkShaderModuleCreateInfo smci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
	smci.codeSize = vulkan_graphics_1_vert_spirv_len;
	smci.pCode = reinterpret_cast<const uint32_t*>(vulkan_graphics_1_vert_spirv);
	VkShaderModule vert = VK_NULL_HANDLE;
	VkResult result = vkCreateShaderModule(vk.device, &smci, nullptr, &vert);
	check(result);

	smci.codeSize = vulkan_graphics_1_frag_spirv_len;
	smci.pCode = reinterpret_cast<const uint32_t*>(vulkan_graphics_1_frag_spirv);
	VkShaderModule frag = VK_NULL_HANDLE;
	result = vkCreateShaderModule(vk.device, &smci, nullptr, &frag);
	check(result);

	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vert;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";

	VkVertexInputBindingDescription binding{};
	binding.binding = 0;
	binding.stride = 32;
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	std::array<VkVertexInputAttributeDescription, 3> attrs{};
	attrs[0].location = 0;
	attrs[0].binding = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].offset = 0;
	attrs[1].location = 1;
	attrs[1].binding = 0;
	attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[1].offset = 16;
	attrs[2].location = 2;
	attrs[2].binding = 0;
	attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[2].offset = 28;
	VkPipelineVertexInputStateCreateInfo visci{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };
	visci.vertexBindingDescriptionCount = 1;
	visci.pVertexBindingDescriptions = &binding;
	visci.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
	visci.pVertexAttributeDescriptions = attrs.data();

	VkPipelineInputAssemblyStateCreateInfo iasci{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
	iasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{};
	viewport.width = 16.0f;
	viewport.height = 16.0f;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor{};
	scissor.extent = { 16, 16 };
	VkPipelineViewportStateCreateInfo vsci{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	vsci.viewportCount = 1;
	vsci.pViewports = &viewport;
	vsci.scissorCount = 1;
	vsci.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rsci{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr };
	rsci.polygonMode = VK_POLYGON_MODE_FILL;
	rsci.cullMode = VK_CULL_MODE_NONE;
	rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rsci.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo msci{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };
	msci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cbci{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };
	cbci.attachmentCount = 1;
	cbci.pAttachments = &cba;

	VkDescriptorSetLayoutBinding bindings[2] = {};
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo dslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
	dslci.bindingCount = 2;
	dslci.pBindings = bindings;
	VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
	result = vkCreateDescriptorSetLayout(vk.device, &dslci, nullptr, &dsl);
	check(result);

	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &dsl;
	result = vkCreatePipelineLayout(vk.device, &plci, nullptr, &layout);
	check(result);

	VkAttachmentDescription attachment{};
	attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkAttachmentReference ref{};
	ref.attachment = 0;
	ref.layout = VK_IMAGE_LAYOUT_GENERAL;
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &ref;
	VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };
	rpci.attachmentCount = 1;
	rpci.pAttachments = &attachment;
	rpci.subpassCount = 1;
	rpci.pSubpasses = &subpass;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	result = vkCreateRenderPass(vk.device, &rpci, nullptr, &renderPass);
	check(result);

	VkGraphicsPipelineCreateInfo gpci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };
	gpci.flags = VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR | VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
	gpci.stageCount = 2;
	gpci.pStages = stages;
	gpci.pVertexInputState = &visci;
	gpci.pInputAssemblyState = &iasci;
	gpci.pViewportState = &vsci;
	gpci.pRasterizationState = &rsci;
	gpci.pMultisampleState = &msci;
	gpci.pColorBlendState = &cbci;
	gpci.layout = layout;
	gpci.renderPass = renderPass;
	gpci.subpass = 0;
	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline);
	check(result);

	VkPipelineInfoKHR pipelineInfo{ VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, nullptr };
	pipelineInfo.pipeline = pipeline;

	uint32_t propCount = 0;
	result = pf_vkGetPipelineExecutablePropertiesKHR(vk.device, &pipelineInfo, &propCount, nullptr);
	check(result);
	std::vector<VkPipelineExecutablePropertiesKHR> props(propCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR, nullptr });
	if (propCount > 0)
	{
		result = pf_vkGetPipelineExecutablePropertiesKHR(vk.device, &pipelineInfo, &propCount, props.data());
		check(result);

		for (uint32_t i = 0; i < propCount; ++i)
		{
			printf("Executable %u: %s - %s\n", i, props[i].name, props[i].description);
		}

		VkPipelineExecutableInfoKHR execInfo{ VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR, nullptr };
		execInfo.pipeline = pipeline;
		execInfo.executableIndex = 0;

		uint32_t statCount = 0;
		result = pf_vkGetPipelineExecutableStatisticsKHR(vk.device, &execInfo, &statCount, nullptr);
		check(result);
		std::vector<VkPipelineExecutableStatisticKHR> stats(statCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR, nullptr });
		if (statCount > 0)
		{
			result = pf_vkGetPipelineExecutableStatisticsKHR(vk.device, &execInfo, &statCount, stats.data());
			check(result);
			for (uint32_t i = 0; i < statCount; ++i)
			{
				printf("Statistic %u: %s - %s\n", i, stats[i].name, stats[i].description);
			}
		}

		uint32_t irCount = 0;
		result = pf_vkGetPipelineExecutableInternalRepresentationsKHR(vk.device, &execInfo, &irCount, nullptr);
		check(result);
		std::vector<VkPipelineExecutableInternalRepresentationKHR> irs(irCount, { VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR, nullptr });
		for (auto& ir : irs)
		{
			ir.pData = nullptr;
			ir.dataSize = 0;
		}
		if (irCount > 0)
		{
			result = pf_vkGetPipelineExecutableInternalRepresentationsKHR(vk.device, &execInfo, &irCount, irs.data());
			check(result);
			for (uint32_t i = 0; i < irCount; ++i)
			{
				printf("InternalRep %u: %s - %s\n", i, irs[i].name, irs[i].description);
			}
		}
	}

	bench_stop_iteration(vk.bench);

	vkDestroyPipeline(vk.device, pipeline, nullptr);
	vkDestroyRenderPass(vk.device, renderPass, nullptr);
	vkDestroyPipelineLayout(vk.device, layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);
	vkDestroyShaderModule(vk.device, vert, nullptr);
	vkDestroyShaderModule(vk.device, frag, nullptr);

	test_done(vk);
	return 0;
}
