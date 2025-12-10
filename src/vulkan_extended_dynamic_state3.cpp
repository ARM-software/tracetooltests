#include "vulkan_common.h"

#include <array>

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
	reqs.apiVersion = VK_API_VERSION_1_3;
	reqs.device_extensions.push_back("VK_EXT_extended_dynamic_state3");
	reqs.device_extensions.push_back("VK_EXT_depth_clip_control");
	reqs.device_extensions.push_back("VK_EXT_depth_clip_enable");
	reqs.device_extensions.push_back("VK_EXT_transform_feedback");
	reqs.device_extensions.push_back("VK_EXT_blend_operation_advanced");
	reqs.device_extensions.push_back("VK_EXT_sample_locations");
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;

	VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dyn3Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, nullptr };
	dyn3Features.extendedDynamicState3TessellationDomainOrigin = VK_TRUE;
	dyn3Features.extendedDynamicState3DepthClampEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3PolygonMode = VK_TRUE;
	dyn3Features.extendedDynamicState3RasterizationSamples = VK_TRUE;
	dyn3Features.extendedDynamicState3SampleMask = VK_TRUE;
	dyn3Features.extendedDynamicState3AlphaToCoverageEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3AlphaToOneEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3LogicOpEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3ColorBlendEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3ColorBlendEquation = VK_TRUE;
	dyn3Features.extendedDynamicState3ColorWriteMask = VK_TRUE;
	dyn3Features.extendedDynamicState3RasterizationStream = VK_TRUE;
	dyn3Features.extendedDynamicState3ConservativeRasterizationMode = VK_TRUE;
	dyn3Features.extendedDynamicState3ExtraPrimitiveOverestimationSize = VK_TRUE;
	dyn3Features.extendedDynamicState3DepthClipEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3SampleLocationsEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3ColorBlendAdvanced = VK_TRUE;
	dyn3Features.extendedDynamicState3ProvokingVertexMode = VK_TRUE;
	dyn3Features.extendedDynamicState3LineRasterizationMode = VK_TRUE;
	dyn3Features.extendedDynamicState3LineStippleEnable = VK_TRUE;
	dyn3Features.extendedDynamicState3DepthClipNegativeOneToOne = VK_TRUE;

	VkPhysicalDeviceTransformFeedbackFeaturesEXT tfFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT, nullptr };
	tfFeatures.transformFeedback = VK_TRUE;

	VkPhysicalDeviceDepthClipEnableFeaturesEXT depthClipEnableFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT, nullptr };
	depthClipEnableFeatures.depthClipEnable = VK_TRUE;

	VkPhysicalDeviceDepthClipControlFeaturesEXT depthClipFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT, nullptr };
	depthClipFeatures.depthClipControl = VK_TRUE;

	VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT blendFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BLEND_OPERATION_ADVANCED_FEATURES_EXT, nullptr };
	blendFeatures.advancedBlendCoherentOperations = VK_FALSE;

	dyn3Features.pNext = &tfFeatures;
	tfFeatures.pNext = &depthClipEnableFeatures;
	depthClipEnableFeatures.pNext = &depthClipFeatures;
	depthClipFeatures.pNext = &blendFeatures;

	reqs.extension_features = reinterpret_cast<VkBaseInStructure*>(&dyn3Features);

	auto vk = test_init(argc, argv, "vulkan_extended_dynamic_state3", reqs);

	MAKEDEVICEPROCADDR(vk, vkCmdSetDepthClampEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetPolygonModeEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetRasterizationSamplesEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetSampleMaskEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetAlphaToCoverageEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetAlphaToOneEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetLogicOpEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetColorBlendEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetColorBlendEquationEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetColorWriteMaskEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetTessellationDomainOriginEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetRasterizationStreamEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetConservativeRasterizationModeEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetExtraPrimitiveOverestimationSizeEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetDepthClipEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetSampleLocationsEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetColorBlendAdvancedEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetProvokingVertexModeEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetLineRasterizationModeEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetLineStippleEnableEXT);
	MAKEDEVICEPROCADDR(vk, vkCmdSetDepthClipNegativeOneToOneEXT);

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

	VkPipelineDynamicStateCreateInfo dsci{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr };
	std::array<VkDynamicState, 21> dynamics = {
		VK_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT,
		VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT,
		VK_DYNAMIC_STATE_POLYGON_MODE_EXT,
		VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,
		VK_DYNAMIC_STATE_SAMPLE_MASK_EXT,
		VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT,
		VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT,
		VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT,
		VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
		VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
		VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
		VK_DYNAMIC_STATE_RASTERIZATION_STREAM_EXT,
		VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT,
		VK_DYNAMIC_STATE_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE_EXT,
		VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT,
		VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT,
		VK_DYNAMIC_STATE_COLOR_BLEND_ADVANCED_EXT,
		VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT,
		VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT,
		VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT,
		VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT
	};
	dsci.dynamicStateCount = static_cast<uint32_t>(dynamics.size());
	dsci.pDynamicStates = dynamics.data();

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
	gpci.stageCount = 2;
	gpci.pStages = stages;
	gpci.pVertexInputState = &visci;
	gpci.pInputAssemblyState = &iasci;
	gpci.pViewportState = &vsci;
	gpci.pRasterizationState = &rsci;
	gpci.pMultisampleState = &msci;
	gpci.pColorBlendState = &cbci;
	gpci.pDynamicState = &dsci;
	gpci.layout = layout;
	gpci.renderPass = renderPass;
	gpci.subpass = 0;
	VkPipeline pipeline = VK_NULL_HANDLE;
	result = vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline);
	check(result);

	VkCommandPool cmdpool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo cpci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	cpci.queueFamilyIndex = 0;
	cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	result = vkCreateCommandPool(vk.device, &cpci, nullptr, &cmdpool);
	check(result);

	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo cbai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	cbai.commandPool = cmdpool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;
	result = vkAllocateCommandBuffers(vk.device, &cbai, &cmd);
	check(result);

	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = vkBeginCommandBuffer(cmd, &beginInfo);
	check(result);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	VkSampleMask sampleMask = 0x1;
	VkColorBlendEquationEXT blendEq{};
	blendEq.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendEq.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendEq.colorBlendOp = VK_BLEND_OP_ADD;
	blendEq.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendEq.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendEq.alphaBlendOp = VK_BLEND_OP_ADD;
	VkColorBlendAdvancedEXT blendAdv{};
	blendAdv.advancedBlendOp = VK_BLEND_OP_OVERLAY_EXT;
	blendAdv.srcPremultiplied = VK_FALSE;
	blendAdv.dstPremultiplied = VK_FALSE;
	blendAdv.blendOverlap = VK_BLEND_OVERLAP_UNCORRELATED_EXT;
	blendAdv.clampResults = VK_FALSE;
	VkColorComponentFlags colorMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	pf_vkCmdSetTessellationDomainOriginEXT(cmd, VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT);
	pf_vkCmdSetDepthClampEnableEXT(cmd, VK_FALSE);
	pf_vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_FILL);
	pf_vkCmdSetRasterizationSamplesEXT(cmd, VK_SAMPLE_COUNT_1_BIT);
	pf_vkCmdSetSampleMaskEXT(cmd, VK_SAMPLE_COUNT_1_BIT, &sampleMask);
	pf_vkCmdSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);
	pf_vkCmdSetAlphaToOneEnableEXT(cmd, VK_FALSE);
	pf_vkCmdSetLogicOpEnableEXT(cmd, VK_FALSE);
	VkBool32 blendEnable = VK_FALSE;
	pf_vkCmdSetColorBlendEnableEXT(cmd, 0, 1, &blendEnable);
	pf_vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &blendEq);
	pf_vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &colorMask);
	VkPhysicalDeviceTransformFeedbackPropertiesEXT tfProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT, nullptr };
	VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &tfProps };
	vkGetPhysicalDeviceProperties2(vk.physical, &props2);
	if (tfProps.maxTransformFeedbackStreams > 0)
	{
		pf_vkCmdSetRasterizationStreamEXT(cmd, 0);
	}
	pf_vkCmdSetConservativeRasterizationModeEXT(cmd, VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT);
	pf_vkCmdSetExtraPrimitiveOverestimationSizeEXT(cmd, 0.0f);
	pf_vkCmdSetDepthClipEnableEXT(cmd, VK_TRUE);
	pf_vkCmdSetSampleLocationsEnableEXT(cmd, VK_FALSE);
	pf_vkCmdSetColorBlendAdvancedEXT(cmd, 0, 1, &blendAdv);
	pf_vkCmdSetProvokingVertexModeEXT(cmd, VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT);
	pf_vkCmdSetLineRasterizationModeEXT(cmd, VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT);
	pf_vkCmdSetLineStippleEnableEXT(cmd, VK_FALSE);
	pf_vkCmdSetDepthClipNegativeOneToOneEXT(cmd, VK_FALSE);

	result = vkEndCommandBuffer(cmd);
	check(result);

	bench_stop_iteration(vk.bench);

	vkDestroyPipeline(vk.device, pipeline, nullptr);
	vkDestroyRenderPass(vk.device, renderPass, nullptr);
	vkDestroyPipelineLayout(vk.device, layout, nullptr);
	vkDestroyDescriptorSetLayout(vk.device, dsl, nullptr);
	vkDestroyShaderModule(vk.device, vert, nullptr);
	vkDestroyShaderModule(vk.device, frag, nullptr);
	vkFreeCommandBuffers(vk.device, cmdpool, 1, &cmd);
	vkDestroyCommandPool(vk.device, cmdpool, nullptr);

	test_done(vk);
	return 0;
}
