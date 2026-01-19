// Unit test to try out vulkan graphic with variations

#include <cstring>
#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our vert shader, generated with:
//   glslangValidator -V vulkan_descriptor_buffer_mutable_type.vert -o vulkan_descriptor_buffer_mutable_type_vert.spirv
//   xxd -i vulkan_descriptor_buffer_mutable_type_vert.spirv > vulkan_descriptor_buffer_mutable_type_vert.inc

#include "vulkan_descriptor_buffer_mutable_type_vert.inc"
#include "vulkan_descriptor_buffer_mutable_type_frag.inc"
#include "vulkan_descriptor_buffer_mutable_type_comp.inc"


// contains image data
//   xxd -i girl.jpg > girl.inc
#include "asset/image/girl.inc"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <chrono>

static void show_usage()
{
	usage();
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	return parseCmdopt(i, argc, argv, reqs);
}

using namespace tracetooltests;

struct Vertex
{
	glm::vec3 pos;   // 12
	glm::vec3 nrm;   // 12
	glm::vec3 tan;   // 12
	glm::vec2 uv;    // 8
	uint32_t padding;
};

static const std::vector<Vertex> kCubeVerts =
{
	// +X
	{{+0.5f,-0.5f,-0.5f}, {+1,0,0}, {0,1,0}, {0,0}},
	{{+0.5f,-0.5f,+0.5f}, {+1,0,0}, {0,1,0}, {1,0}},
	{{+0.5f,+0.5f,+0.5f}, {+1,0,0}, {0,1,0}, {1,1}},
	{{+0.5f,+0.5f,-0.5f}, {+1,0,0}, {0,1,0}, {0,1}},
	// -X
	{{-0.5f,-0.5f,+0.5f}, {-1,0,0}, {0,1,0}, {0,0}},
	{{-0.5f,-0.5f,-0.5f}, {-1,0,0}, {0,1,0}, {1,0}},
	{{-0.5f,+0.5f,-0.5f}, {-1,0,0}, {0,1,0}, {1,1}},
	{{-0.5f,+0.5f,+0.5f}, {-1,0,0}, {0,1,0}, {0,1}},
	// +Y
	{{-0.5f,+0.5f,-0.5f}, {0,+1,0}, {1,0,0}, {0,0}},
	{{+0.5f,+0.5f,-0.5f}, {0,+1,0}, {1,0,0}, {1,0}},
	{{+0.5f,+0.5f,+0.5f}, {0,+1,0}, {1,0,0}, {1,1}},
	{{-0.5f,+0.5f,+0.5f}, {0,+1,0}, {1,0,0}, {0,1}},
	// -Y
	{{-0.5f,-0.5f,+0.5f}, {0,-1,0}, {1,0,0}, {0,0}},
	{{+0.5f,-0.5f,+0.5f}, {0,-1,0}, {1,0,0}, {1,0}},
	{{+0.5f,-0.5f,-0.5f}, {0,-1,0}, {1,0,0}, {1,1}},
	{{-0.5f,-0.5f,-0.5f}, {0,-1,0}, {1,0,0}, {0,1}},
	// +Z
	{{-0.5f,-0.5f,+0.5f}, {0,0,+1}, {1,0,0}, {0,0}},
	{{+0.5f,-0.5f,+0.5f}, {0,0,+1}, {1,0,0}, {1,0}},
	{{+0.5f,+0.5f,+0.5f}, {0,0,+1}, {1,0,0}, {1,1}},
	{{-0.5f,+0.5f,+0.5f}, {0,0,+1}, {1,0,0}, {0,1}},
	// -Z
	{{+0.5f,-0.5f,-0.5f}, {0,0,-1}, {1,0,0}, {0,0}},
	{{-0.5f,-0.5f,-0.5f}, {0,0,-1}, {1,0,0}, {1,0}},
	{{-0.5f,+0.5f,-0.5f}, {0,0,-1}, {1,0,0}, {1,1}},
	{{+0.5f,+0.5f,-0.5f}, {0,0,-1}, {1,0,0}, {0,1}},
};

static const std::vector<uint16_t> kCubeIdx =
{
	0,1,2, 0,2,3,   4,5,6, 4,6,7,   8,9,10, 8,10,11,
	12,13,14, 12,14,15,   16,17,18, 16,18,19,   20,21,22, 20,22,23
};

struct CameraData
{
	glm::mat4 view;
	glm::mat4 proj;
};

struct ObjectData
{
	glm::mat4 model;
	glm::vec4 baseColor;
};

struct ObjectSphere
{
	glm::vec4 boundingSphere; // center.xyz, radius
};

struct IndirectDrawCmd
{
	uint vertexCount;
	uint instanceCount;
	uint firstVertex;
	uint firstInstance;
};

struct IndexedIndirectDrawCmd
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int vertexOffset;
	uint firstInstance;
};

class benchmarkContext : public GraphicContext
{
public:
	benchmarkContext() : GraphicContext() {}
	~benchmarkContext()
	{
		destroy();
	}
	void destroy()
	{
		DLOG3("MEM detection: descriptor_buffer_mutable_type benchmark destroy().");

		m_vertBuffer = nullptr;
		m_indexBuffer = nullptr;
		m_objectDataBuffer = nullptr;
		m_sphereBuffer = nullptr;
		m_indirectDrawBuffer = nullptr;
		m_descBuffer = nullptr;

		m_graphicPipeline = nullptr;
		m_cullingPipeline = nullptr;
		m_pipelineLayout = nullptr;
		m_mutableSetLayout = nullptr;

		if (m_frameFence != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_vulkanSetup.device, m_frameFence, nullptr);
			m_frameFence = VK_NULL_HANDLE;
		}
	}

	// contexts and resources related with benchmark
	uint32_t width = 0 ;
	uint32_t height = 0;
	std::unique_ptr<Buffer> m_vertBuffer;
	std::unique_ptr<Buffer> m_indexBuffer;

	CameraData m_cameraData{};
	std::unique_ptr<Buffer> m_objectDataBuffer;

	std::unique_ptr<Buffer> m_sphereBuffer;
	std::unique_ptr<Buffer> m_indirectDrawBuffer;

	std::unique_ptr<GraphicPipeline> m_graphicPipeline;
	std::unique_ptr<ComputePipeline> m_cullingPipeline;
	std::shared_ptr<PipelineLayout> m_pipelineLayout;

	std::shared_ptr<DescriptorSetLayout> m_mutableSetLayout;
	VkDeviceSize m_layoutSize = 0;
	VkDeviceSize m_binding0Offset = 0;
	VkDeviceSize m_binding1Offset = 0;
	VkDeviceSize m_binding0Stride = 0;

	std::unique_ptr<Buffer> m_descBuffer;

	VkPhysicalDeviceDescriptorBufferPropertiesEXT m_descriptorBuffer_properties;

	uint32_t m_maxObjects = 1024;
	uint32_t m_objectCount = 640;
	VkFence m_frameFence = VK_NULL_HANDLE;
};

static std::unique_ptr<benchmarkContext> p_benchmark = nullptr;

void setup_descriptor_set_layout()
{
	VkDescriptorType mutable_types[] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	VkMutableDescriptorTypeListEXT b0_list;
	b0_list.descriptorTypeCount = 2;
	b0_list.pDescriptorTypes = mutable_types;

	VkMutableDescriptorTypeListEXT mutable_lists[2] {};
	mutable_lists[0] = b0_list;

	VkMutableDescriptorTypeCreateInfoEXT mutable_info {VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT, nullptr};
	mutable_info.mutableDescriptorTypeListCount = 2;
	mutable_info.pMutableDescriptorTypeLists = mutable_lists;

	auto mutableSetLayout = std::make_unique<DescriptorSetLayout>(p_benchmark->m_vulkanSetup.device);
	mutableSetLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_MUTABLE_EXT, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
	mutableSetLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);
	mutableSetLayout->insertNext(mutable_info);
	mutableSetLayout->create(2, VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	MAKEDEVICEPROCADDR(p_benchmark->m_vulkanSetup, vkGetDescriptorSetLayoutSizeEXT);
	MAKEDEVICEPROCADDR(p_benchmark->m_vulkanSetup, vkGetDescriptorSetLayoutBindingOffsetEXT);

	pf_vkGetDescriptorSetLayoutSizeEXT(p_benchmark->m_vulkanSetup.device, mutableSetLayout->getHandle(), &p_benchmark->m_layoutSize);
	pf_vkGetDescriptorSetLayoutBindingOffsetEXT(p_benchmark->m_vulkanSetup.device, mutableSetLayout->getHandle(), 0, &p_benchmark->m_binding0Offset);
	pf_vkGetDescriptorSetLayoutBindingOffsetEXT(p_benchmark->m_vulkanSetup.device, mutableSetLayout->getHandle(), 1, &p_benchmark->m_binding1Offset);

	p_benchmark->m_layoutSize = aligned_size(p_benchmark->m_layoutSize, p_benchmark->m_descriptorBuffer_properties.descriptorBufferOffsetAlignment);

	p_benchmark->m_mutableSetLayout = std::move(mutableSetLayout);

	std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>>  //set num --> descriptorSetLayout
	layoutMap = { {0, p_benchmark->m_mutableSetLayout} };

	VkPushConstantRange const_range{};
	const_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
	const_range.offset     = 0;
	const_range.size       = sizeof(CameraData);

	auto pipelineLayout = std::make_unique<PipelineLayout>(p_benchmark->m_vulkanSetup.device);
	pipelineLayout->create(layoutMap, {const_range});
	p_benchmark->m_pipelineLayout = std::move(pipelineLayout);
}

void prepare_descriptor_buffer()
{
	MAKEDEVICEPROCADDR(p_benchmark->m_vulkanSetup, vkGetDescriptorEXT);

	// this buffer contains resource descriptor for all uniform/storage buffers
	p_benchmark->m_descBuffer = std::make_unique<Buffer>(p_benchmark->m_vulkanSetup);
	p_benchmark->m_descBuffer->create(VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                  2*p_benchmark->m_layoutSize, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	test_set_name(p_benchmark->m_vulkanSetup, VK_OBJECT_TYPE_BUFFER, (uint64_t)p_benchmark->m_descBuffer->getHandle(), "Descriptor buffer");

	p_benchmark->m_descBuffer->map();
	uint8_t* descriptor_buffer_ptr = (uint8_t*)p_benchmark->m_descBuffer->m_mappedAddress;

	VkDescriptorAddressInfoEXT addressInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT, nullptr};
	addressInfo.format  = VK_FORMAT_UNDEFINED;

	VkDescriptorGetInfoEXT getInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT, nullptr };
	getInfo.data.pStorageBuffer = &addressInfo;

	// compute pipeline binding 0: ssbo indirectDrawCmdBuffer
	getInfo.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	addressInfo.address = p_benchmark->m_indirectDrawBuffer->getBufferDeviceAddress();;
	addressInfo.range   = p_benchmark->m_indirectDrawBuffer->getSize();

	VkDeviceSize descSize = p_benchmark->m_descriptorBuffer_properties.storageBufferDescriptorSize;
	uint8_t* dst = descriptor_buffer_ptr + p_benchmark->m_binding0Offset;
	pf_vkGetDescriptorEXT(p_benchmark->m_vulkanSetup.device, &getInfo, descSize, dst);

	// binding 1: ssbo sphereBuffer
	addressInfo.address = p_benchmark->m_sphereBuffer->getBufferDeviceAddress();;
	addressInfo.range   = p_benchmark->m_sphereBuffer->getSize();

	dst = descriptor_buffer_ptr + p_benchmark->m_binding1Offset;
	pf_vkGetDescriptorEXT(p_benchmark->m_vulkanSetup.device, &getInfo, descSize, dst);

	// graphic pipeline binding 0: ubo objectDataBuffer
	getInfo.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	addressInfo.address = p_benchmark->m_objectDataBuffer->getBufferDeviceAddress();;
	addressInfo.range   = p_benchmark->m_objectDataBuffer->getSize();

	descSize = p_benchmark->m_descriptorBuffer_properties.uniformBufferDescriptorSize;
	dst = descriptor_buffer_ptr + p_benchmark->m_layoutSize + p_benchmark->m_binding0Offset;
	pf_vkGetDescriptorEXT(p_benchmark->m_vulkanSetup.device, &getInfo, descSize, dst);

	p_benchmark->m_descBuffer->unmap();
}

void prepare_compute_pipeline()
{
	std::vector<VkSpecializationMapEntry> smentries(4);
	for (unsigned i = 0; i < smentries.size(); i++)
	{
		smentries[i].constantID = i;
		smentries[i].offset = i * 4;
		smentries[i].size = 4;
	}

	std::vector<uint32_t> sdata(4);
	sdata[0] = p_benchmark->wg_size; // workgroup x size
	sdata[1] = p_benchmark->wg_size; // workgroup y size
	sdata[2] = 1; // workgroup z size
	sdata[3] = kCubeIdx.size(); // index count

	auto cullShader = std::make_unique<Shader>(p_benchmark->m_vulkanSetup.device);
	cullShader->create(vulkan_descriptor_buffer_mutable_type_comp_spirv, vulkan_descriptor_buffer_mutable_type_comp_spirv_len);

	ShaderPipelineState cullShaderStage(VK_SHADER_STAGE_COMPUTE_BIT, std::move(cullShader));
	cullShaderStage.setSpecialization(smentries, 4 * sdata.size(), sdata.data());
	p_benchmark->m_cullingPipeline = std::make_unique<ComputePipeline>(p_benchmark->m_pipelineLayout);
	p_benchmark->m_cullingPipeline->create(cullShaderStage, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);
}

void prepare_graphic_pipeline()
{
	/*************************** graphicPipeline state*******************************/
	// input vertext
	GraphicPipelineState pipelineState;
	pipelineState.setVertexBinding(0, *p_benchmark->m_vertBuffer, sizeof(Vertex));
	pipelineState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0u);

	pipelineState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
	pipelineState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);

	/******************************** render pass **********************************/
	// images used as attachments
	auto colorImage = std::make_shared<Image>(p_benchmark->m_vulkanSetup.device);
	colorImage->create({p_benchmark->width, p_benchmark->height, 1}, VK_FORMAT_R8G8B8A8_UNORM,
	                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	auto colorImageView = std::make_shared<ImageView>(std::move(colorImage));
	colorImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

	// attachments: set VkAttachmentDescription
	AttachmentInfo color{ 0, std::move(colorImageView), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	// subpass: set VkAttachmentReference
	SubpassInfo subpass{};
	subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// create renderpass
	auto renderpass = std::make_unique<RenderPass>(p_benchmark->m_vulkanSetup.device);
	renderpass->create({color}, {subpass});

	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	//continue: graphicPipeline state
	pipelineState.setColorBlendAttachment(color.m_location, colorBlendAttachment);

	/********************************** framebuffer *********************************/
	auto framebuffer = std::make_unique<FrameBuffer>(p_benchmark->m_vulkanSetup.device);
	framebuffer->create(*renderpass, {p_benchmark->width, p_benchmark->height});

	/*************************** shader module **************************************/
	auto vertShader = std::make_unique<Shader>(p_benchmark->m_vulkanSetup.device);
	vertShader->create(vulkan_descriptor_buffer_mutable_type_vert_spirv, vulkan_descriptor_buffer_mutable_type_vert_spirv_len);

	auto fragShader = std::make_unique<Shader>(p_benchmark->m_vulkanSetup.device);
	fragShader->create(vulkan_descriptor_buffer_mutable_type_frag_spirv, vulkan_descriptor_buffer_mutable_type_frag_spirv_len);

	/*************************** pipeline shader stage ******************************/
	ShaderPipelineState vertShaderState(VK_SHADER_STAGE_VERTEX_BIT, std::move(vertShader));
	ShaderPipelineState fragShaderState(VK_SHADER_STAGE_FRAGMENT_BIT, std::move(fragShader));

	/*************************** graphic pipeline creation **************************/
	std::vector<ShaderPipelineState> shaderStages = {vertShaderState, fragShaderState};
	auto pipeline = std::make_unique<GraphicPipeline>(p_benchmark->m_pipelineLayout);
	pipeline->create(shaderStages, pipelineState, *renderpass, VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

	p_benchmark->m_renderPass = std::move(renderpass);
	p_benchmark->m_framebuffer = std::move(framebuffer);
	p_benchmark->m_graphicPipeline = std::move(pipeline);
}

void update_object()
{
	// Object data(per-object model + color)
	std::vector<ObjectData> objData(p_benchmark->m_objectCount);
	// Object sphere data(per-object bounding sphere)
	std::vector<ObjectSphere> sphereData(p_benchmark->m_objectCount);

	for (uint32_t i = 0; i < p_benchmark->m_objectCount; ++i)
	{
		float angle = (float)i * (2.0f * 3.1415926f / p_benchmark->m_objectCount);
		float radius = 20.0f;

		// boundingSphere
		glm::vec3 center = glm::vec3(
		                       std::cos(angle) * radius,
		                       0.0f,
		                       std::sin(angle) * radius);
		float boundR = 1.0f;

		// model + basecolor
		glm::mat4 model = glm::translate(glm::mat4(1.0f), center);

		glm::vec4 color = glm::vec4(
		                      0.5f + 0.5f * std::cos(angle),
		                      0.5f + 0.5f * std::sin(angle),
		                      0.5f,
		                      1.0f);

		objData[i].model     = model;
		objData[i].baseColor = color;

		sphereData[i].boundingSphere = glm::vec4(center, boundR);
	}

	p_benchmark->updateBuffer(objData, *p_benchmark->m_objectDataBuffer);
	p_benchmark->updateBuffer(sphereData, *p_benchmark->m_sphereBuffer);
}

void update_camera(float aspect)
{
	glm::vec3 eye    = glm::vec3(0.0f, 5.0f, 30.0f);
	glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 up     = glm::vec3(0.0f, 1.0f, 0.0f);

	p_benchmark->m_cameraData.view = glm::lookAt(eye, center, up);
	p_benchmark->m_cameraData.proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
}

void prepare_object()
{
	VkDeviceSize size;
	// ubo of objectData
	size = sizeof(ObjectData) * p_benchmark->m_objectCount;
	auto objectDataBuffer = std::make_unique<Buffer>(p_benchmark->m_vulkanSetup);
	objectDataBuffer->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                         size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->m_objectDataBuffer = std::move(objectDataBuffer);
	test_set_name(p_benchmark->m_vulkanSetup, VK_OBJECT_TYPE_BUFFER, (uint64_t)p_benchmark->m_objectDataBuffer->getHandle(), "Data buffer");

	// read only ssbo of ObjectSphere
	size = sizeof(ObjectSphere) * p_benchmark->m_objectCount;
	auto sphereBuffer = std::make_unique<Buffer>(p_benchmark->m_vulkanSetup);
	sphereBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                     size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->m_sphereBuffer = std::move(sphereBuffer);
	test_set_name(p_benchmark->m_vulkanSetup, VK_OBJECT_TYPE_BUFFER, (uint64_t)p_benchmark->m_sphereBuffer->getHandle(), "Sphere buffer");

	// ssbo of indirect draw cmd
	size = sizeof(uint32_t) + sizeof(IndexedIndirectDrawCmd) * p_benchmark->m_objectCount;
	auto indirectDrawBuffer = std::make_unique<Buffer>(p_benchmark->m_vulkanSetup);
	indirectDrawBuffer->create(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                           size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	p_benchmark->m_indirectDrawBuffer = std::move(indirectDrawBuffer);
	test_set_name(p_benchmark->m_vulkanSetup, VK_OBJECT_TYPE_BUFFER, (uint64_t)p_benchmark->m_indirectDrawBuffer->getHandle(), "Indirect draw buffer");

	p_benchmark->m_indirectDrawBuffer->map();
	uint8_t* ptr = (uint8_t*)p_benchmark->m_indirectDrawBuffer->m_mappedAddress;
	std::memset(ptr, 0, size);
	p_benchmark->m_indirectDrawBuffer->unmap();

	update_object();
	update_camera(1.0);
}

void prepare_vertex()
{
	VkDeviceSize size = sizeof(Vertex) * kCubeVerts.size();
	// vertex buffer
	auto vertBuffer = std::make_unique<Buffer>(p_benchmark->m_vulkanSetup);
	vertBuffer->create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->m_vertBuffer = std::move(vertBuffer);
	test_set_name(p_benchmark->m_vulkanSetup, VK_OBJECT_TYPE_BUFFER, (uint64_t)p_benchmark->m_vertBuffer->getHandle(), "Vertex buffer");

	p_benchmark->updateBuffer(kCubeVerts, *p_benchmark->m_vertBuffer);

	// index buffer
	size = sizeof(uint16_t) * kCubeIdx.size();
	auto indexBuffer = std::make_unique<Buffer>(p_benchmark->m_vulkanSetup);
	indexBuffer->create(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	p_benchmark->m_indexBuffer = std::move(indexBuffer);
	test_set_name(p_benchmark->m_vulkanSetup, VK_OBJECT_TYPE_BUFFER, (uint64_t)p_benchmark->m_indexBuffer->getHandle(), "Index buffer");

	p_benchmark->updateBuffer(kCubeIdx, *p_benchmark->m_indexBuffer);
}

static void render()
{
	uint32_t groupCount_x = (uint32_t)ceil(p_benchmark->width/float(p_benchmark->wg_size));
	uint32_t groupCount_y = (uint32_t)ceil(p_benchmark->height/float(p_benchmark->wg_size));

	MAKEDEVICEPROCADDR(p_benchmark->m_vulkanSetup, vkCmdBindDescriptorBuffersEXT);
	MAKEDEVICEPROCADDR(p_benchmark->m_vulkanSetup, vkCmdSetDescriptorBufferOffsetsEXT);

	benchmarking bench = p_benchmark->m_vulkanSetup.bench;

	while (p__loops--)
	{
		VkCommandBuffer defaultCmd = p_benchmark->m_defaultCommandBuffer->getHandle();
		vkResetCommandBuffer(defaultCmd, 0);

		p_benchmark->m_defaultCommandBuffer->begin();
		// read-after-write hazard handled
		p_benchmark->m_defaultCommandBuffer->bufferMemoryBarrier(*p_benchmark->m_vertBuffer,
		        0, p_benchmark->m_vertBuffer->getSize(),
		        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);

		p_benchmark->m_defaultCommandBuffer->bufferMemoryBarrier(*p_benchmark->m_indexBuffer,
		        0, p_benchmark->m_indexBuffer->getSize(),
		        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_INDEX_READ_BIT,
		        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);

		p_benchmark->m_defaultCommandBuffer->bufferMemoryBarrier(*p_benchmark->m_objectDataBuffer,
		        0, p_benchmark->m_objectDataBuffer->getSize(),
		        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT,
		        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

		p_benchmark->m_defaultCommandBuffer->bufferMemoryBarrier(*p_benchmark->m_sphereBuffer,
		        0, p_benchmark->m_sphereBuffer->getSize(),
		        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		VkDescriptorBufferBindingInfoEXT descriptor_buffer_binding_info{VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT, nullptr};
		descriptor_buffer_binding_info.address = p_benchmark->m_descBuffer->getBufferDeviceAddress();
		descriptor_buffer_binding_info.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
		pf_vkCmdBindDescriptorBuffersEXT(defaultCmd, 1, &descriptor_buffer_binding_info);
		uint32_t desc_buffer_index = 0;
		VkDeviceSize set_offset = 0;

		// compute pass
		vkCmdBindPipeline(defaultCmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_cullingPipeline->getHandle());
		pf_vkCmdSetDescriptorBufferOffsetsEXT(defaultCmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_benchmark->m_pipelineLayout->getHandle(), 0, 1, &desc_buffer_index, &set_offset);
		vkCmdPushConstants(defaultCmd, p_benchmark->m_pipelineLayout->getHandle(), VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CameraData), &p_benchmark->m_cameraData);
		vkCmdDispatch(defaultCmd, groupCount_x, groupCount_y, 1);

		// compute write -> draw indirect read
		p_benchmark->m_defaultCommandBuffer->bufferMemoryBarrier(*p_benchmark->m_indirectDrawBuffer,
		        0, p_benchmark->m_indirectDrawBuffer->getSize(),
		        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
		        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);

		// graphic pass
		p_benchmark->m_defaultCommandBuffer->beginRenderPass(*p_benchmark->m_renderPass, *p_benchmark->m_framebuffer);
		set_offset = p_benchmark->m_layoutSize;
		vkCmdBindPipeline(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->m_graphicPipeline->getHandle());
		pf_vkCmdSetDescriptorBufferOffsetsEXT(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p_benchmark->m_pipelineLayout->getHandle(), 0, 1, &desc_buffer_index, &set_offset);

		// bind vertex buffer to bindings
		VkBuffer vertexBuffers[] = {p_benchmark->m_vertBuffer->getHandle()};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(defaultCmd, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(defaultCmd, p_benchmark->m_indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT16);
		vkCmdPushConstants(defaultCmd, p_benchmark->m_pipelineLayout->getHandle(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CameraData), &p_benchmark->m_cameraData);

		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(p_benchmark->width);
		viewport.height = static_cast<float>(p_benchmark->height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(defaultCmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.offset = {0, 0};
		scissor.extent = {p_benchmark->width, p_benchmark->height};
		vkCmdSetScissor(defaultCmd, 0, 1, &scissor);

		MAKEDEVICEPROCADDR(p_benchmark->m_vulkanSetup, vkCmdDrawIndexedIndirectCount);
		pf_vkCmdDrawIndexedIndirectCount(defaultCmd, p_benchmark->m_indirectDrawBuffer->getHandle(), sizeof(uint32_t),
		                                 p_benchmark->m_indirectDrawBuffer->getHandle(), 0,
		                                 p_benchmark->m_objectCount, sizeof(VkDrawIndexedIndirectCommand));
		p_benchmark->m_defaultCommandBuffer->endRenderPass();
		p_benchmark->m_defaultCommandBuffer->end();

		bench_start_iteration(bench);

		// submit
		p_benchmark->submit(p_benchmark->m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>> {p_benchmark->m_defaultCommandBuffer}, p_benchmark->m_frameFence);
		vkWaitForFences(p_benchmark->m_vulkanSetup.device, 1, &p_benchmark->m_frameFence, VK_TRUE, UINT64_MAX);
		vkResetFences(p_benchmark->m_vulkanSetup.device, 1, &p_benchmark->m_frameFence);

		bench_stop_iteration(bench);

		update_object();
		update_camera(1.0);
	}
}

int main(int argc, char** argv)
{
	p_benchmark = std::make_unique<benchmarkContext>();

	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	req.minApiVersion = VK_API_VERSION_1_3;
	req.apiVersion = VK_API_VERSION_1_3;

	req.bufferDeviceAddress = true;
	req.reqfeat12.bufferDeviceAddress = VK_TRUE;
	req.reqfeat12.drawIndirectCount = VK_TRUE;

	req.device_extensions.push_back("VK_EXT_descriptor_buffer");
	VkPhysicalDeviceDescriptorBufferFeaturesEXT desc_buffer_feat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT, nullptr};
	desc_buffer_feat.descriptorBuffer = VK_TRUE;

	req.device_extensions.push_back("VK_EXT_mutable_descriptor_type");
	VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_desc_feat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
	mutable_desc_feat.pNext = &desc_buffer_feat;
	mutable_desc_feat.mutableDescriptorType = VK_TRUE;
	req.extension_features = (VkBaseInStructure*)&mutable_desc_feat;

	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_descriptor_buffer_mutable_type", req);

	p_benchmark->initBasic(vulkan, req);

	const uint32_t width = static_cast<uint32_t>(std::get<int>(req.options.at("width")));
	const uint32_t height = static_cast<uint32_t>(std::get<int>(req.options.at("height")));
	p_benchmark->width = width;
	p_benchmark->height = height;

	VkPhysicalDeviceProperties2KHR device_properties{};
	VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptor_buffer_properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT, nullptr};
	device_properties.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
	device_properties.pNext            = &descriptor_buffer_properties;
	vkGetPhysicalDeviceProperties2(vulkan.physical, &device_properties);
	p_benchmark->m_descriptorBuffer_properties = descriptor_buffer_properties;
	p_benchmark->m_binding0Stride = std::max(descriptor_buffer_properties.uniformBufferDescriptorSize,
	                                descriptor_buffer_properties.storageBufferDescriptorSize);

	prepare_vertex();
	prepare_object();
	p_benchmark->submitStaging(true, {}, {}, false);

	setup_descriptor_set_layout();
	prepare_descriptor_buffer();

	prepare_compute_pipeline();
	prepare_graphic_pipeline();

	VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	vkCreateFence(vulkan.device, &fenceInfo, nullptr, &p_benchmark->m_frameFence);

	render();

	vkDeviceWaitIdle(vulkan.device);
	p_benchmark = nullptr;

	return 0;
}
