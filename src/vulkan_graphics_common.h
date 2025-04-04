#pragma once

#include "vulkan_common.h"
#include <memory>
#include <functional>

namespace tracetooltests
{

class RenderPass;
class FrameBuffer;
class GraphicPipeline;

class Buffer
{
public:
	using BufferCreateInfoFunc = std::function<void(VkBufferCreateInfo&)>;
	using AllocationCreateInfoFunc = std::function<void(VkMemoryAllocateInfo&)>;

	Buffer(VkDevice device): m_device(device) { }
	~Buffer() {
		destroy();
	}

	VkResult create(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags properties, const std::vector<uint32_t>& queueFamilyIndices = { } );
	VkResult map(VkDeviceSize offset =0, VkDeviceSize size = VK_WHOLE_SIZE, VkMemoryMapFlags flag =0);
	void unmap();

	VkResult destroy(); // free of m_pCreateInfoNext and m_pAllocateNext, clear vectors

	inline VkBuffer getHandle() const {
		return m_handle;
	}
	inline VkDeviceMemory getMemory() const {
		return m_memory;
	}
	inline VkMemoryPropertyFlags getMemoryProperty() const {
		return m_memoryProperty;
	}

	/* user definition createinfo: could used in corner cases, eg garbage data */
	VkResult create(const BufferCreateInfoFunc& createInfoFunc, const AllocationCreateInfoFunc& allocationInfoFunc);

	VkDevice m_device;
	void* m_mappedAddress = nullptr;

private:
	VkResult create();

	VkBuffer m_handle = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
	VkMemoryPropertyFlags m_memoryProperty = VK_MEMORY_PROPERTY_FLAG_BITS_MAX_ENUM;

	VkBufferCreateInfo m_createInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
	VkMemoryAllocateInfo m_allocateInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	VkBaseInStructure* m_pCreateInfoNext = nullptr;
	VkBaseInStructure* m_pAllocateNext = nullptr;

	std::vector<VkBaseInStructure*> m_createInfoNexts; // free each elements in destory()
	std::vector<VkBaseInStructure*> m_allocateInfoNexts;
	std::vector<uint32_t> m_queueFamilyIndices;
};

class TexelBufferView
{
public:
	TexelBufferView(std::shared_ptr<Buffer> buffer) : m_pBuffer(buffer) {}
	~TexelBufferView() {
		destroy();
	}

	VkResult create(VkFormat format, VkDeviceSize offsetInBytes = 0, VkDeviceSize sizeInBytes = VK_WHOLE_SIZE);
	VkResult destroy();

	inline VkBufferView getHandle() const {
		return m_handle;
	}

	std::shared_ptr<Buffer> m_pBuffer;

private:
	VkBufferView m_handle = VK_NULL_HANDLE;
	VkBufferViewCreateInfo m_createInfo { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, nullptr };
};

class Image
{
public:
	Image(VkDevice device) : m_device(device) { }
	~Image() {
		destroy();
	}

	VkResult create(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage,
	                VkMemoryPropertyFlags properties, uint32_t queueFamilyIndexCount = 0,
	                uint32_t* queueFamilyIndex = nullptr, uint32_t mipLevels = 1,
	                VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);
	VkResult destroy();

	inline VkImage getHandle() const {
		return m_handle;
	}
	inline VkDeviceMemory getMemory() const {
		return m_memory;
	}
	inline VkImageCreateInfo getCreateInfo() const {
		return m_createInfo;
	}

	VkDevice m_device;
	VkFormat m_format = VK_FORMAT_UNDEFINED;
	VkImageAspectFlags m_aspect = VK_IMAGE_ASPECT_NONE;
	VkImageLayout m_imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

private:
	void setAspectMask(VkFormat format);
	VkImageType findImageType(VkExtent3D extent) const;

	VkImage m_handle = VK_NULL_HANDLE;
	VkDeviceMemory m_memory = VK_NULL_HANDLE;
	VkImageCreateInfo m_createInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
};

class ImageView
{
public:
	ImageView(std::shared_ptr<Image> image) : m_pImage(image) {}
	~ImageView() {
		destroy();
	}

	VkResult create(VkImageViewType viewType, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_NONE);
	VkResult destroy();

	inline VkImageView getHandle() const {
		return m_handle;
	}

	std::shared_ptr<Image> m_pImage;

private:
	VkImageView m_handle = VK_NULL_HANDLE;
	VkImageViewCreateInfo m_createInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
};

class Sampler
{
public:
	using SamplerCreateInfoFunc = std::function<void(VkSamplerCreateInfo&)>;

	Sampler(VkDevice device) : m_device(device) {}
	~Sampler() {
		destroy();
	}

	VkResult create(VkFilter filter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode addressMode, VkBool32 anisotropyEnable, float maxAnisotropy,
	                float maxLod = VK_LOD_CLAMP_NONE, float mipLodBias = 0.f);
	VkResult destroy();

	VkResult create(const SamplerCreateInfoFunc& createInfoFunc);
	inline VkSampler getHandle() const {
		return m_handle;
	}

	VkDevice m_device;

private:
	VkResult create();

	VkSampler m_handle = VK_NULL_HANDLE;
	VkSamplerCreateInfo m_createInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr };
};

class CommandBufferPool
{
public:
	CommandBufferPool(VkDevice device) : m_device(device) { }
	~CommandBufferPool() {
		destroy();
	}

	VkResult create(VkCommandPoolCreateFlags flags, uint32_t queueFamilyIndex);
	VkResult destroy();

	inline VkCommandPool getHandle() const {
		return m_handle;
	}

	VkDevice m_device;

private:
	VkCommandPool m_handle = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo m_createInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
};

class CommandBuffer
{
public:
	CommandBuffer(std::shared_ptr<CommandBufferPool> commandBufferPool);
	~CommandBuffer() {
		destroy();
	}

	VkResult create(VkCommandBufferLevel commandBufferLevel);
	VkResult destroy();

	VkResult begin(VkCommandBufferUsageFlags flags = 0, const CommandBuffer* baseCommandBuffer = nullptr);
	VkResult end();
	void beginRenderPass(const RenderPass& renderPass, const FrameBuffer& frameBuffer);
	void endRenderPass();
	void bindPipeline(VkPipelineBindPoint bindpoint, const GraphicPipeline& pipeline);
	void imageMemoryBarrier(Image& image, VkImageLayout oldLayout, VkImageLayout newLayout,
	                        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
	                        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
	void copyBuffer(const Buffer& srcBuffer, const Buffer& dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset =0);
	void copyBufferToImage(const Buffer& srcBuffer, Image& image, VkDeviceSize srcOffset, const VkExtent3D& dstExtent, const VkOffset3D& dstOffset = {0,0,0});

	inline VkCommandBuffer getHandle() const {
		return m_handle;
	}

	std::shared_ptr<CommandBufferPool> m_pCommandBufferPool;
	VkDevice m_device;

private:
	VkCommandBuffer m_handle = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo m_createInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
};

typedef struct ShaderResource
{
	std::string        m_name;
	int32_t            m_type            = -1;

	uint32_t           m_dataType        = 0;
	uint32_t           m_location        = 0;
	uint32_t           m_attachmentIndex = 0;
	uint32_t           m_descriptorSet   = 0;
	uint32_t           m_binding         = 0;


	ShaderResource() {}
	ShaderResource(int32_t type, const std::string& name)
		: m_name(name)
		, m_type(type)
	{
	}

} ShaderResource;

typedef struct PushConstantRange
{
	uint32_t m_offset = 0;
	uint32_t m_size   = 0;

	PushConstantRange() {}
	PushConstantRange(uint32_t offset, uint32_t size) : m_offset(offset), m_size(size)
	{
	}
} PushConstantRange;

typedef struct SpecializationConstant
{
	uint32_t m_id     = 0;
	uint32_t m_offset = 0;
	std::vector<char> m_value;

	SpecializationConstant() {}
} SpecializationConstant;

class Shader
{
public:
	using ShaderModuleCreateInfoFunc = std::function<void(VkShaderModuleCreateInfo&)>;

	Shader(VkDevice device) : m_device(device) {}
	~Shader() {
		destroy();
	}

	VkResult create(const std::string& filename);
	VkResult create(const unsigned char* data, unsigned int  byteSize);
	VkResult destroy();

	VkResult create(const ShaderModuleCreateInfoFunc& createInfoFunc);

	inline VkShaderModule getHandle() const {
		return m_handle;
	}

	VkDevice m_device;

private:
	VkResult create();

	VkShaderModule m_handle = VK_NULL_HANDLE;
	// flags must be 0 according to spec
	VkShaderModuleCreateInfo m_createInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0 };

	std::vector<uint32_t> m_spirvCode;
	PushConstantRange                   m_pushConstantRange;
	std::vector<ShaderResource>         m_resources;
	std::vector<SpecializationConstant> m_specializationConstants;
};

class DescriptorSetLayout
{
public:
	DescriptorSetLayout(VkDevice device) : m_device(device) { }
	~DescriptorSetLayout() {
		destroy();
	}

	VkResult create();
	// more flexible usage for corner test case
	VkResult create(uint32_t bindingCount);
	VkResult destroy();
	void insertBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stage, uint32_t count = 1);
	VkDescriptorType getDescriptorType(uint32_t binding) const;

	inline VkDescriptorSetLayout getHandle() const {
		return m_handle;
	}
	inline const std::vector<VkDescriptorSetLayoutBinding>& getBindings() const {
		return m_bindings;
	}

	VkDevice m_device;

private:
	VkDescriptorSetLayout m_handle = VK_NULL_HANDLE;
	VkDescriptorSetLayoutCreateInfo m_createInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	std::vector<VkDescriptorSetLayoutBinding> m_bindings; // the real inserted bindings.
	// While the count and binding number in createInfo may be not matched, for the garbage test case usage
};

class DescriptorSetPool
{
public:
	using DescriptorPoolCreateFuncType = std::function<void(VkDescriptorPoolCreateInfo&)>;

	DescriptorSetPool(std::shared_ptr<DescriptorSetLayout> descSetLayout)
		: m_pDescriptorSetLayout(descSetLayout) { }
	~DescriptorSetPool() {
		destroy();
	}

	VkResult create(uint32_t size);
	VkResult create(const DescriptorPoolCreateFuncType& createFunc);
	VkResult destroy();

	inline VkDescriptorPool getHandle() const {
		return m_handle;
	}

	std::shared_ptr<DescriptorSetLayout> m_pDescriptorSetLayout;

private:
	VkResult create();
	VkDescriptorPool m_handle = VK_NULL_HANDLE;
	VkDescriptorPoolCreateInfo m_createInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
	std::vector<VkDescriptorPoolSize> m_poolSizes;
};

typedef struct DescriptorSetState
{
	std::unordered_map<uint32_t, std::vector<VkDescriptorBufferInfo>> m_buffers; // binding -> info
	std::unordered_map<uint32_t, std::vector<VkDescriptorImageInfo>> m_images;
	std::unordered_map<uint32_t, VkBufferView> m_bufferViews;
	// acceleration Structure related

	DescriptorSetState() {}
	void setBuffer(uint32_t binding, const VkDescriptorBufferInfo& info)
	{
		m_buffers[binding].emplace_back(info);
	}
	void setImage(uint32_t binding, const VkDescriptorImageInfo& info)
	{
		m_images[binding].emplace_back(info);
	}
	void setBufferView(uint32_t binding, const VkBufferView& view)
	{
		m_bufferViews[binding] = view;
	}
} DescriptorSetState;

class DescriptorSet
{
public:
	DescriptorSet(std::shared_ptr<DescriptorSetPool> pool)
		: m_pDescriptorSetPool(pool) { };
	~DescriptorSet() {
		destroy();
	}

	VkResult create();
	VkResult destroy();

	void update();
	void setBuffer(uint32_t binding, const Buffer& buffer, VkDeviceSize offsetInBytes = 0, VkDeviceSize sizeInBytes = VK_WHOLE_SIZE);
	void setCombinedImageSampler(uint32_t binding, const ImageView& imageView, VkImageLayout imageLayout, const Sampler& sampler);
	void setImage(uint32_t binding, const ImageView& imageView, VkImageLayout imageLayout);
	void setTexelBufferView(uint32_t binding, const TexelBufferView& bufferView);
	void setAccelerationStructure();

	inline VkDescriptorSet getHandle() const {
		return m_handle;
	}

	std::shared_ptr<DescriptorSetPool> m_pDescriptorSetPool;
	DescriptorSetState m_setState {};

private:
	VkDescriptorSet m_handle = VK_NULL_HANDLE;
	VkDescriptorSetAllocateInfo m_createInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
};

class PipelineLayout
{
public:
	PipelineLayout(VkDevice device) : m_device(device) { }
	~PipelineLayout() {
		destroy();
	}

	VkResult create(const std::unordered_map<uint32_t,std::shared_ptr<DescriptorSetLayout>>& setLayoutMap, const std::vector<VkPushConstantRange>& pushConstantRanges = {});
	VkResult create(uint32_t setLayoutCount, const std::unordered_map<uint32_t,std::shared_ptr<DescriptorSetLayout>>& setLayoutMap,
	                uint32_t pushConstantRangeCount, const std::vector<VkPushConstantRange>& pushConstantRanges);
	VkResult destroy();

	inline VkPipelineLayout getHandle() const {
		return m_handle;
	}

	VkDevice m_device;
	std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>> m_pDescriptorSetLayouts; // set -> setLayout

private:
	VkResult create(uint32_t layoutCount, uint32_t constantCount);

	VkPipelineLayout m_handle = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };

	std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
	std::vector<VkPushConstantRange> m_pushConstantRanges;
};

class AttachmentInfo
{
public:
	uint32_t m_location;
	std::shared_ptr<ImageView> m_pImageView; // we need some members of ImageView
	VkAttachmentDescription m_description;
	VkClearValue m_clear;

	AttachmentInfo ()
	{
		resetDescription();
		m_clear.color.float32[0] = 0.0;
		m_clear.color.float32[1] = 0.0;
		m_clear.color.float32[2] = 0.0;
		m_clear.color.float32[3] = 1.0;
		m_clear.depthStencil.depth = 1.0;
		m_clear.depthStencil.stencil = 0;
	}

	AttachmentInfo(uint32_t location, std::shared_ptr<ImageView> imageView, VkImageLayout finalLayout) : AttachmentInfo()
	{
		m_location = location;
		m_pImageView = imageView;

		VkImageCreateInfo createInfo = m_pImageView->m_pImage->getCreateInfo();

		m_description.format = createInfo.format;
		m_description.samples = createInfo.samples;
		m_description.initialLayout = createInfo.initialLayout;
		m_description.finalLayout = finalLayout;
	}

	~AttachmentInfo()
	{
		DLOG3("MEM detection: attacmentInfo destructor().");
		resetDescription();
		m_pImageView = nullptr;
	}

private:
	void resetDescription()
	{
		m_description.flags          = 0;
		m_description.format         = VK_FORMAT_UNDEFINED;
		m_description.samples        = VK_SAMPLE_COUNT_1_BIT;
		m_description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
		m_description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
		m_description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		m_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		m_description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		m_description.finalLayout    = VK_IMAGE_LAYOUT_GENERAL;
	}
};

typedef struct SubpassInfo
{
	VkPipelineBindPoint m_pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	std::vector<VkAttachmentReference> m_inputAttachments;
	std::vector<VkAttachmentReference> m_colorAttachments;
	VkAttachmentReference m_depthAttachment;
	std::vector<VkAttachmentReference> m_resolveAttachments;

	SubpassInfo(VkPipelineBindPoint pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS)
	{
		m_pipelineBindPoint = pipelineBindPoint;
	}
	void addInputAttachment(const AttachmentInfo& input, VkImageLayout layout)
	{
		m_inputAttachments.push_back( {input.m_location, layout} );
	}
	void addColorAttachment(const AttachmentInfo& color, VkImageLayout layout)
	{
		m_colorAttachments.push_back( {color.m_location, layout} );
	}
	void setDepthStencilAttachment(const AttachmentInfo& depthStencil, VkImageLayout layout)
	{
		m_depthAttachment.attachment = depthStencil.m_location;
		m_depthAttachment.layout = layout;
	}
	void addResolveAttachments(const AttachmentInfo& resolve, VkImageLayout layout)
	{
		m_resolveAttachments.push_back( {resolve.m_location, layout} );
	}
} SubpassInfo;

class RenderPass
{
public:
	RenderPass(VkDevice device) : m_device(device) { }
	~RenderPass() {
		destroy();
	}

	VkResult create(const std::vector<AttachmentInfo>& attachments, const std::vector<SubpassInfo>& subpasses);
	VkResult create(uint32_t attachmentCount, const std::vector<AttachmentInfo>& attachments,
	                uint32_t subpassCount,    const std::vector<SubpassInfo>& subpasses);
	VkResult destroy();

	inline VkRenderPass getHandle() const {
		return m_handle;
	}

	VkDevice m_device;
	std::vector<AttachmentInfo> m_attachmentInfos;

private:
	VkRenderPass m_handle = VK_NULL_HANDLE;
	VkRenderPassCreateInfo m_createInfo { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };

	std::vector<VkAttachmentDescription> m_attachmentDescriptions;
	std::vector<VkSubpassDescription> m_subpassDescriptions;
	std::vector<VkSubpassDependency> m_subpassDependencies;
};

class FrameBuffer
{
public:
	using FrameBufferCreateInfoFunc = std::function<void(VkFramebufferCreateInfo&)>;

	FrameBuffer(VkDevice device) : m_device(device) { }
	~FrameBuffer() {
		destroy();
	}

	VkResult create(const RenderPass& renderPass, VkExtent2D extent, uint32_t layers = 1);
	VkResult destroy();

	inline VkFramebuffer getHandle() const {
		return m_handle;
	}
	inline VkFramebufferCreateInfo getCreateInfo() const {
		return m_createInfo;
	}

	VkResult create(const FrameBufferCreateInfoFunc& createInfoFunc);

	VkDevice m_device;

private:
	VkFramebuffer m_handle = VK_NULL_HANDLE;
	VkFramebufferCreateInfo m_createInfo { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
	std::vector<VkImageView> m_attachments;
};

class ShaderPipelineState
{
public:
	ShaderPipelineState() {}
	ShaderPipelineState(VkShaderStageFlagBits shaderStage, std::shared_ptr<Shader>shader, const std::string& entry = "main");
	~ShaderPipelineState() {
		destroy();
	}

	void setSpecialization(const std::vector<VkSpecializationMapEntry>& mapEntries, size_t dataSize, void *pdata);
	void destroy();
	inline VkPipelineShaderStageCreateInfo getCreateInfo () const {
		return m_createInfo;
	}

	std::shared_ptr<Shader> m_pShader;

private:
	std::string m_entry;
	VkPipelineShaderStageCreateInfo m_createInfo { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};
	VkSpecializationInfo m_specializationInfo {};
	std::vector<VkSpecializationMapEntry> m_mapEntries;
	std::vector<char> m_data;
};

class GraphicPipelineState
{
public:
	GraphicPipelineState();
	~GraphicPipelineState() {
		destroy();
	}
	void destroy();

	// set the vertexBuffer to the binding which is used in vkBindVertexBuffers
	//     binding may be uncontinoius
	// describe the vertexBuffer binding info, VkVertexBindingDescription, including binding,stride
	//     stride: related with the data structure stored in the vertexBuffer
	void setVertexBinding(uint32_t binding, const Buffer& vertexBuffer, uint32_t stride, uint32_t offset = 0, VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX);

	// set each attrib for vertex, VkVertexInputAttributeDescription
	//     offset: offset to access the attrib within the binding
	// one binding(that's also one vertexBuffer) could contain one or more attribs, depending on user definition
	void setVertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);
	void setDynamic(uint32_t index, VkDynamicState dynamicState);
	void setViewport(uint32_t index, const VkViewport& viewport);
	void setScissor(uint32_t index, const VkRect2D& scissor);
	void setColorBlendAttachment(uint32_t index, const VkPipelineColorBlendAttachmentState& state);

	// the following info is used with the default value in most cases. For special cases, provide the interfaces.
	void setAssembly(const VkPipelineInputAssemblyStateCreateInfo& info);
	void setTessellation(const VkPipelineTessellationStateCreateInfo& info);
	void setRasterization(const VkPipelineRasterizationStateCreateInfo& info);
	void setMultiSample(const VkPipelineMultisampleStateCreateInfo& info);
	void setDepthStencil(const VkPipelineDepthStencilStateCreateInfo& info);
	void setColorBlend(const VkPipelineColorBlendStateCreateInfo& info);

	std::unordered_map<uint32_t, VkVertexInputBindingDescription> m_vertexInputBindings; // binding -> vertextBuffer Bindingdescription
	std::vector<VkVertexInputAttributeDescription> m_vertexInputAttribs;
	VkPipelineInputAssemblyStateCreateInfo m_inputAssemblyState;
	VkPipelineTessellationStateCreateInfo m_tessellationState;
	VkPipelineViewportStateCreateInfo m_viewportState;
	VkPipelineRasterizationStateCreateInfo m_rasterizationState;
	VkPipelineMultisampleStateCreateInfo m_multiSampleState;
	VkPipelineDepthStencilStateCreateInfo m_depthStencilState;
	VkPipelineColorBlendStateCreateInfo m_colorBlendState;
	VkPipelineDynamicStateCreateInfo m_dynamicState;

private:
	std::vector<VkDynamicState> m_dynamics;
	std::vector<VkViewport> m_viewports;
	std::vector<VkRect2D> m_scissors;
	std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachments;
};

class GraphicPipeline
{
public:
	GraphicPipeline(std::shared_ptr<PipelineLayout> pipelineLayout) : m_pipelineLayout(pipelineLayout) {}
	~GraphicPipeline() {
		destroy();
	}

	VkResult create(const std::vector<ShaderPipelineState>& shaderStages, const GraphicPipelineState& graphicPipelineState, const RenderPass& renderPass, uint32_t subpassIndex = 0);
	VkResult destroy();
	bool hasDynamicState(VkDynamicState dynamic) const;
	inline VkPipeline getHandle() const {
		return m_handle;
	}

	std::shared_ptr<PipelineLayout> m_pipelineLayout;

private:
	VkPipeline m_handle = VK_NULL_HANDLE;
	VkGraphicsPipelineCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };

	// Store variables for pointer member variables of vulkan structures
	std::vector<VkPipelineShaderStageCreateInfo>       m_shaderStageCreateInfos;
	std::vector<std::string>                           m_shaderEntries;
	std::vector<VkSpecializationInfo>                  m_specializationInfos;
	std::vector<std::vector<VkSpecializationMapEntry>> m_specializationMapEntries;
	std::vector<std::vector<char>>                     m_specializationData;
	std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> m_shaders;

	VkPipelineVertexInputStateCreateInfo               m_vertexInputStateCreateInfo;
	std::vector<VkVertexInputBindingDescription>       m_vertexInputBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription>     m_vertexInputAttributeDescriptions;

	VkPipelineInputAssemblyStateCreateInfo             m_inputAssemblyStateCreateInfo;
	VkPipelineTessellationStateCreateInfo              m_tessellationStateCreateInfo;

	VkPipelineViewportStateCreateInfo                  m_viewportStateCreateInfo;
	std::vector<VkViewport>                            m_viewports;
	std::vector<VkRect2D>                              m_scissors;

	VkPipelineRasterizationStateCreateInfo             m_rasterizationStateCreateInfo;
	VkPipelineMultisampleStateCreateInfo               m_multisampleStateCreateInfo;
	VkSampleMask                                       m_sampleMasks;
	VkPipelineDepthStencilStateCreateInfo              m_depthStencilStateCreateInfo;

	VkPipelineColorBlendStateCreateInfo                m_colorBlendStateCreateInfo;
	std::vector<VkPipelineColorBlendAttachmentState>   m_colorBlendAttachments;

	VkPipelineDynamicStateCreateInfo                   m_dynamicStateCreateInfo;
	std::vector<VkDynamicState>                        m_dynamicStates;

};

class BasicContext
{
public:
	BasicContext() { }
	VkResult initBasic(vulkan_setup_t& vulkan, vulkan_req_t& reqs);

	std::shared_ptr<CommandBufferPool> m_defaultCommandPool;
	std::shared_ptr<CommandBuffer> m_defaultCommandBuffer;
	std::shared_ptr<CommandBuffer> m_secondCommandBuffer; // reserved

	// used for frame boundary extension
	std::shared_ptr<Image> m_imageBoundary;
	std::shared_ptr<CommandBuffer> m_frameBoundaryCommandBuffer;

	vulkan_setup_t m_vulkanSetup { };
	VkQueue m_defaultQueue = VK_NULL_HANDLE;
	uint32_t frameNo = 0;

protected:
	virtual ~BasicContext() {
		destroy();
	}
	virtual void destroy()
	{
		DLOG3("MEM detection: BasicContext destroy().");
		m_imageBoundary = nullptr;
		m_secondCommandBuffer = nullptr;
		m_defaultCommandBuffer = nullptr;
		m_frameBoundaryCommandBuffer = nullptr;
		m_defaultCommandPool = nullptr;
		frameNo = 0;

		test_done(m_vulkanSetup);
	}

};

class GraphicContext : public BasicContext
{
public:
	GraphicContext() { }
	virtual ~GraphicContext() {
		destroy();
	}

	//implying the image layout transition, current->TRANSFER_DST, before CopyBufferToImage
	void updateImage(const char* srcData, VkDeviceSize size, Image& dstImage, const VkExtent3D& dstExtent, const VkOffset3D& dstOffset = {0,0,0}, VkDeviceSize srcOffset = 0, bool submitOnce = false);
	void updateBuffer(const char* srcData, VkDeviceSize size, const Buffer& dstBuffer, VkDeviceSize dstOffset = 0, VkDeviceSize srcOffset = 0, bool submitOnce = false);

	template <class T>
	inline void updateImage(const std::vector<T>& srcData, Image& dstImage, const VkExtent3D& dstExtent, const VkOffset3D& dstOffset = {0,0,0}, VkDeviceSize srcOffset = 0, bool submitOnce = false)
	{
		updateImage(reinterpret_cast<const char*>(srcData.data()), sizeof(T)*srcData.size(), dstImage, dstExtent, dstOffset, srcOffset, submitOnce);
	}

	template <class T>
	inline void updateBuffer(const std::vector<T>& srcData, const Buffer& dstBuffer, VkDeviceSize dstOffset = 0, VkDeviceSize srcOffset = 0, bool submitOnce = false)
	{
		updateBuffer(reinterpret_cast<const char*>(srcData.data()), sizeof(T)*srcData.size(), dstBuffer, dstOffset, srcOffset, submitOnce);
	}

	VkSemaphore submit(VkQueue queue, const std::vector<std::shared_ptr<CommandBuffer>>& commandBuffers,
	                   VkFence fence = VK_NULL_HANDLE,
	                   const std::vector<VkSemaphore>&          waitSemaphores = {},
	                   const std::vector<VkPipelineStageFlags>& waitPipelineStageFlags = {},
	                   bool returnSignalSemaphore = true,  bool returnFrameImage = false);

	// submit the stagingCommandBuffers and waitFence if needed,, and then clear it
	VkSemaphore submitStaging(bool waitFence = true,
	                          const std::vector<VkSemaphore>&          waitSemaphores = {},
	                          const std::vector<VkPipelineStageFlags>& waitPipelineStageFlags = {},
	                          bool returnSignalSemaphore = true);

	virtual void destroy()
	{
		DLOG3("MEM detection: GraphicContext destroy().");
		m_framebuffer = nullptr;
		m_renderPass = nullptr;

		for (auto semaphore : m_returnSignalSemaphores)
		{
			vkDestroySemaphore(m_vulkanSetup.device, semaphore, nullptr);
		}
		m_returnSignalSemaphores.clear();
		m_usingBuffers.clear();
		if (m_stagingCommandBuffers.size() > 0)
			m_stagingCommandBuffers.clear();
	}

	std::vector<VkSemaphore> m_returnSignalSemaphores;
	std::shared_ptr<RenderPass> m_renderPass;
	std::shared_ptr<FrameBuffer> m_framebuffer; // vector future
	std::vector<std::shared_ptr<CommandBuffer>> m_stagingCommandBuffers;
	std::vector<std::unique_ptr<Buffer>> m_usingBuffers; // recyling

};


} // namespace tracetooltests


#include <iostream>
#include <fstream>

void graphic_usage();
bool graphic_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs);
std::vector<char> readFile(const std::string& filename);
