#pragma once

#include "vulkan_common.h"
#include <memory>

namespace tracetooltests
{

class Buffer
{
public:
    Buffer(VkDevice device):  m_device(device) { }
    ~Buffer() { destroy(); }

    VkResult create(VkBufferUsageFlags usage, VkDeviceSize size, VkSharingMode mode, VkMemoryPropertyFlags properties);
    VkResult destroy();

    inline VkBuffer getHandle() const { return m_handle; }
    inline VkDeviceMemory getMemory() const { return m_memory; }

    VkDevice m_device;

private:
    VkBuffer m_handle = VK_NULL_HANDLE;
    VkDeviceMemory m_memory;
    VkBufferCreateInfo m_createInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
};

class Image
{
public:
    Image(VkDevice device) : m_device(device) { }
    ~Image() { destroy(); }

    VkResult create(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage,
                VkMemoryPropertyFlags properties, uint32_t queueFamilyIndexCount = 0,
                uint32_t* queueFamilyIndex = nullptr, uint32_t mipLevels = 1,
                VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);
    VkResult destroy();

    inline VkImage getHandle() const { return m_handle; }
    inline VkDeviceMemory getMemory() const { return m_memory; }
    inline VkImageCreateInfo getCreateInfo() const { return m_createInfo; }

    VkDevice m_device;
    VkFormat m_format;

private:
    VkImageType findImageType(VkExtent3D extent) const;

    VkImage m_handle = VK_NULL_HANDLE;
    VkDeviceMemory m_memory;
    VkImageCreateInfo m_createInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
};

class ImageView
{
public:
    ImageView(std::shared_ptr<Image>);
    ~ImageView() { destroy(); }

    VkResult create(VkImageViewType viewType, VkImageAspectFlags aspect);
    VkResult destroy();

    inline VkImageView getHandle() const { return m_handle; }

    VkDevice m_device;
    std::shared_ptr<Image> m_pImage;

private:
    VkImageView m_handle = VK_NULL_HANDLE;
    VkImageViewCreateInfo m_createInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
};

class CommandBufferPool
{
public:
    CommandBufferPool(VkDevice device) : m_device(device) { }
    ~CommandBufferPool() { destroy(); }

    VkResult create(VkCommandPoolCreateFlags flags, uint32_t queueFamilyIndex);
    VkResult destroy();

    inline VkCommandPool getHandle() const { return m_handle; }

    VkDevice m_device;

private:
    VkCommandPool m_handle = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo m_createInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
};

class CommandBuffer
{
public:
    CommandBuffer(std::shared_ptr<CommandBufferPool> commandBufferPool);
    ~CommandBuffer() { destroy(); }

    VkResult create(VkCommandBufferLevel commandBufferLevel);
    VkResult destroy();

    inline VkCommandBuffer getHandle() const { return m_handle;}

    VkDevice m_device;
    std::shared_ptr<CommandBufferPool> m_pCommandBufferPool;

private:
    VkCommandBuffer m_handle = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo m_createInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
};

typedef struct AttachmentInfo
{
    uint32_t m_location;
    std::shared_ptr<ImageView> m_pImageView; // we need some members of ImageView
    VkAttachmentDescription m_description;
    VkClearValue m_clear;

    AttachmentInfo ()
    {
        m_description.format         = VK_FORMAT_UNDEFINED;
        m_description.samples        = VK_SAMPLE_COUNT_1_BIT;
        m_description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        m_description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        m_description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        m_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        m_description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        m_description.finalLayout    = VK_IMAGE_LAYOUT_GENERAL;
        m_clear.color =  { 0.0, 0.0, 0.0, 1.0 };
        m_clear.depthStencil = {1.0, 0};
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
}AttachmentInfo;

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
}SubpassInfo;

class RenderPass
{
public:
    RenderPass(VkDevice device) : m_device(device) { }
    ~RenderPass() { destroy(); }

    VkResult create(const std::vector<AttachmentInfo>& attachments, const std::vector<SubpassInfo>& subpasses);
    VkResult destroy();

    inline VkRenderPass getHandle() const { return m_handle; }

    VkDevice m_device;

private:
    VkRenderPass m_handle = VK_NULL_HANDLE;
    VkRenderPassCreateInfo m_createInfo { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };

    std::vector<VkAttachmentDescription> m_attachmentDescriptions;
    std::vector<VkSubpassDescription> m_subpassDescriptions;
    std::vector<VkSubpassDependency> m_subpassDependencies;
};
} // namespace tracetooltests

struct graphic_resources
{
	VkQueue queue = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkShaderModule computeShaderModule = VK_NULL_HANDLE;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
	VkPipelineCache cache = VK_NULL_HANDLE;
	std::vector<uint32_t> code;
	int buffer_size = -1;

	// used for frame boundary extension
	VkImage image = VK_NULL_HANDLE;
	VkCommandBuffer commandBufferFrameBoundary = VK_NULL_HANDLE;
	int frame = 0;
};

bool graphic_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs);
graphic_resources graphic_init(vulkan_setup_t& vulkan, vulkan_req_t& reqs);
void graphic_done(vulkan_setup_t& vulkan, graphic_resources& r, vulkan_req_t& reqs);
void graphic_submit(vulkan_setup_t& vulkan, graphic_resources&  r, vulkan_req_t& reqs);
void graphic_create_pipeline(vulkan_setup_t& vulkan, graphic_resources& r, vulkan_req_t& reqs);
void graphic_usage();
