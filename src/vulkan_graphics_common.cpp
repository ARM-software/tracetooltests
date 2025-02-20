#include "vulkan_graphics_common.h"

// workround for passing compiling quicky
vulkan_setup_t vulkan2;
vulkan_req_t req2;

struct pixel
{
	float r, g, b, a;
};

void graphic_usage()
{
	printf("-i/--image-output      Save an image of the output to disk\n");
	printf("-W/--width             Width of output image (default 640)\n");
	printf("-H/--height            Height of output image (default 480)\n");
	printf("-wg/--workgroup-size   Set workgroup size (default 32)\n");
	printf("-pc/--pipelinecache    Add a pipeline cache to compute pipeline. By default it is empty.\n");
	printf("-pcf/--cachefile N     Save and restore pipeline cache to/from file N\n");
	printf("-fb/--frame-boundary   Use frameboundary extension to publicize output\n");
	printf("-t/--times N           Times to repeat (default %d)\n", (int)p__loops);
}

bool graphic_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-i", "--image-output"))
	{
		reqs.options["image_output"] = true;
		return true;
	}
	else if (match(argv[i], "-t", "--times"))
	{
		p__loops = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-pc", "--pipelinecache"))
	{
		reqs.options["pipelinecache"] = true;
		return true;
	}
	else if (match(argv[i], "-pcf", "--cachefile"))
	{
		reqs.options["cachefile"] = std::string(get_string_arg(argv, ++i, argc));
		return true;
	}
	else if (match(argv[i], "-W", "--width"))
	{
		reqs.options["width"] = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-H", "--height"))
	{
		reqs.options["height"] = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-wg", "--workgroup-size"))
	{
		reqs.options["wg_size"] = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-fb", "--frame-boundary"))
	{
		return enable_frame_boundary(reqs);
	}
	return false;
}

using namespace tracetooltests;

VkResult Buffer::create(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags properties, const std::vector<uint32_t>& queueFamilyIndices /* = { }*/)
{
    m_queueFamilyIndices = { queueFamilyIndices.begin(), queueFamilyIndices.end() };

    m_createInfo.size = size;
    m_createInfo.usage = usage;
    m_createInfo.sharingMode = static_cast<uint32_t>(queueFamilyIndices.size()) > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    m_createInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
    m_createInfo.pQueueFamilyIndices = m_queueFamilyIndices.data();

    m_memoryProperty = properties;

    {
        // feature and version required should be checked at the beginning of benchmark
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        {
            VkMemoryAllocateFlagsInfo* flagInfo = (VkMemoryAllocateFlagsInfo*)malloc(sizeof(VkMemoryAllocateFlagsInfo));
            flagInfo->sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
            flagInfo->pNext = m_pAllocateNext;
            flagInfo->flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            flagInfo->deviceMask = 0;

            m_allocateInfoNexts.push_back((VkBaseInStructure*)flagInfo);

            m_pAllocateNext = (VkBaseInStructure*)flagInfo;
        }

        m_allocateInfo.pNext = m_pAllocateNext;
    }

    return create();
}

VkResult Buffer::create(const BufferCreateInfoFunc& createInfoFunc, const AllocationCreateInfoFunc& allocationInfoFunc)
{
    createInfoFunc(m_createInfo);
    allocationInfoFunc(m_allocateInfo);
    // createInfo.pQueueFamilyIndices
    return VK_SUCCESS;
//    return create();
}

VkResult Buffer::map(VkDeviceSize offset/*=0*/, VkDeviceSize size/*=VK_WHOLE_SIZE*/, VkMemoryMapFlags flag/*=0*/)
{
    VkResult result;

    result = vkMapMemory(m_device, m_memory, offset, size, flag, &m_mappedAddress);
    check(result);
    return result;
}
void Buffer::unmap()
{
    if (m_mappedAddress) vkUnmapMemory(m_device, m_memory);
    m_mappedAddress = nullptr;
}


VkResult Buffer::destroy()
{
    DLOG3("MEM detection: Buffer destroy().");
    if (m_handle != VK_NULL_HANDLE && m_memory != VK_NULL_HANDLE)
    {
        for(auto& iter : m_allocateInfoNexts)
        {
            free(iter);
            iter = nullptr;
        }
        m_allocateInfoNexts.clear();
        m_pAllocateNext = nullptr;

        for(auto& iter : m_createInfoNexts)
        {
            free(iter);
            iter = nullptr;
        }
        m_createInfoNexts.clear();
        m_pCreateInfoNext = nullptr;

        m_queueFamilyIndices.clear();

        vkDestroyBuffer(m_device, m_handle, nullptr);
        vkFreeMemory(m_device, m_memory, nullptr);
        m_handle = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
    }
    return VK_SUCCESS;
}


/************************************ private *************************/
VkResult Buffer::create()
{
    VkResult result = vkCreateBuffer(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);
    assert(m_handle != VK_NULL_HANDLE);

    VkMemoryRequirements memRequirements = {};
    vkGetBufferMemoryRequirements(m_device, m_handle, &memRequirements);
    
    const uint32_t alignMod = memRequirements.size % memRequirements.alignment;
    const uint32_t alignedSize = (alignMod == 0) ? memRequirements.size : (memRequirements.size + memRequirements.alignment - alignMod);
    const uint32_t memoryTypeIndex = get_device_memory_type(memRequirements.memoryTypeBits, m_memoryProperty);
    
    m_allocateInfo.memoryTypeIndex = memoryTypeIndex;
    m_allocateInfo.allocationSize = alignedSize;

    result = vkAllocateMemory(m_device, &m_allocateInfo, nullptr, &m_memory);
    check(result);
    assert(m_memory != VK_NULL_HANDLE);

    {
    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		VkBindBufferMemoryInfo bindBufferInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr };
    		bindBufferInfo.buffer = m_handle;
    		bindBufferInfo.memory = m_memory;
    		bindBufferInfo.memoryOffset = 0;

    		result = vkBindBufferMemory2(m_device, 1, &bindBufferInfo);
    	}
    	else
    	{
    		result = vkBindBufferMemory(m_device, m_handle, m_memory, 0);
        }
        check(result);
    }
    return result;
}

VkResult TexelBufferView::create(VkFormat format, VkDeviceSize offsetInBytes/*= 0*/, VkDeviceSize sizeInBytes/*= VK_WHOLE_SIZE*/)
{
    m_createInfo.buffer                 = m_pBuffer->getHandle();
    m_createInfo.format                 = format;
    m_createInfo.offset                 = offsetInBytes;
    m_createInfo.range                  = sizeInBytes;
    
    VkResult result = vkCreateBufferView(m_pBuffer->m_device, &m_createInfo, nullptr, &m_handle);
    check(result);
    return result;
}

VkResult TexelBufferView::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult Image::create(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t queueFamilyIndexCount /*= 0*/, uint32_t* queueFamilyIndex /*= nullptr*/, uint32_t mipLevels /*= 1*/, VkImageTiling tiling /*= VK_IMAGE_TILING_OPTIMAL*/)
{
    m_format = format;
    setAspectMask(format);

	m_createInfo.flags = 0;
	m_createInfo.imageType = findImageType(extent);
	m_createInfo.format = format;
	m_createInfo.extent = extent;
	m_createInfo.mipLevels = mipLevels;
	m_createInfo.arrayLayers = 1;
	m_createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	m_createInfo.tiling = tiling;
	m_createInfo.usage = usage;
	m_createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	m_createInfo.queueFamilyIndexCount = queueFamilyIndexCount;
	m_createInfo.pQueueFamilyIndices = queueFamilyIndex;
	m_createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(m_device, &m_createInfo, nullptr, &m_handle);
	check(result);

    {
    	VkMemoryRequirements memRequirements = {};
    	vkGetImageMemoryRequirements(m_device, m_handle, &memRequirements);

    	const uint32_t alignMod = memRequirements.size % memRequirements.alignment;
    	const uint32_t alignedSize = (alignMod == 0) ? memRequirements.size : (memRequirements.size + memRequirements.alignment - alignMod);

    	VkMemoryAllocateInfo allocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
    	allocateMemInfo.memoryTypeIndex = get_device_memory_type(memRequirements.memoryTypeBits, properties);;
    	allocateMemInfo.allocationSize = alignedSize;

    	VkMemoryAllocateFlagsInfo flaginfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr, 0, 0 };

    	if (req2.bufferDeviceAddress)
    	{
    		flaginfo.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    	}
    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		allocateMemInfo.pNext = &flaginfo;
    	}

    	result = vkAllocateMemory(m_device, &allocateMemInfo, nullptr, &m_memory);
    	check(result);
    	assert(m_memory != VK_NULL_HANDLE);
    }

    {   
    	if (vulkan2.apiVersion >= VK_API_VERSION_1_1)
    	{
    		VkBindImageMemoryInfo bindImageInfo = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, nullptr };
    		bindImageInfo.image = m_handle;
    		bindImageInfo.memory = m_memory;
    		bindImageInfo.memoryOffset = 0;

    		result = vkBindImageMemory2(m_device, 1, &bindImageInfo);
    	}
    	else
    	{
    		result = vkBindImageMemory(m_device, m_handle, m_memory, 0);
        }
        check(result);
    }

    return result;
}

void Image::setAspectMask(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_UNDEFINED:
        m_aspect = VK_IMAGE_ASPECT_NONE;;
        break;
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        m_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    case VK_FORMAT_S8_UINT:
        m_aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        break;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        m_aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        break;
    default:
        m_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        break;
    }
}

    
VkImageType Image::findImageType(VkExtent3D extent) const
{
    VkImageType dimension = VK_IMAGE_TYPE_2D;

    if (extent.depth > 1)       dimension = VK_IMAGE_TYPE_3D;
    else if (extent.height > 1) dimension = VK_IMAGE_TYPE_2D;
    else if (extent.width > 1)  dimension = VK_IMAGE_TYPE_1D;
    else assert(false);  // // should never happen

    return dimension;
}

VkResult Image::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult ImageView::create(VkImageViewType viewType, VkImageAspectFlags aspect /*=VK_IMAGE_ASPECT_NONE*/)
{
    m_createInfo.image = m_pImage->getHandle();
    m_createInfo.viewType = viewType;
    m_createInfo.format = m_pImage->m_format;
    
    m_createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    m_createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    
    m_createInfo.subresourceRange.aspectMask = (aspect==VK_IMAGE_ASPECT_NONE) ? m_pImage->m_aspect : aspect;
    m_createInfo.subresourceRange.baseMipLevel = 0;
    m_createInfo.subresourceRange.levelCount = 1;
    m_createInfo.subresourceRange.baseArrayLayer = 0;
    m_createInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(m_pImage->m_device, &m_createInfo, nullptr, &m_handle);
    check(result);

    return result;
}

VkResult ImageView::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult Sampler::create(VkFilter filter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode addressMode, VkBool32 anisotropyEnable, float maxAnisotropy,
                         float maxLod /*= VK_LOD_CLAMP_NONE*/, float mipLodBias /*= 0.f*/)
{
    m_createInfo.magFilter    = filter;
    m_createInfo.minFilter    = filter;

    m_createInfo.addressModeU = addressMode;
    m_createInfo.addressModeV = addressMode;
    m_createInfo.addressModeW = addressMode;

    m_createInfo.mipmapMode   = mipmapMode;
    m_createInfo.minLod       = 0.0f;
    m_createInfo.maxLod       = maxLod;
    m_createInfo.mipLodBias   = mipLodBias;

    // anisotropic filtering should be enabled in physic device feature during logic device creation
    m_createInfo.anisotropyEnable = anisotropyEnable;
    m_createInfo.maxAnisotropy = anisotropyEnable;

    m_createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    m_createInfo.unnormalizedCoordinates = VK_FALSE; // it's normalized coord with [0,1) range on all axes

    m_createInfo.compareEnable = VK_FALSE; // comparison with a value first
    m_createInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    return create();

}

VkResult Sampler::create(const SamplerCreateInfoFunc& createInfoFunc)
{
    createInfoFunc(m_createInfo);
    return create();
}

VkResult Sampler::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

// private
VkResult Sampler::create()
{
    VkResult result = vkCreateSampler(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);
    return result;
}

VkResult CommandBufferPool::create(VkCommandPoolCreateFlags flags, uint32_t queueFamilyIndex)
{
	m_createInfo.flags = flags;
	m_createInfo.queueFamilyIndex = queueFamilyIndex;

	VkResult result = vkCreateCommandPool(m_device, &m_createInfo, NULL, &m_handle);
	check(result);
    return result;
}

VkResult CommandBufferPool::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

CommandBuffer::CommandBuffer(std::shared_ptr<CommandBufferPool> commandBufferPool)
    :m_pCommandBufferPool(commandBufferPool)
{
    m_device = commandBufferPool->m_device;
}

VkResult CommandBuffer::create(VkCommandBufferLevel commandBufferLevel)
{
	m_createInfo.commandPool = m_pCommandBufferPool->getHandle();;
	m_createInfo.level = commandBufferLevel;
	m_createInfo.commandBufferCount = 1;

	VkResult result = vkAllocateCommandBuffers(m_device, &m_createInfo, &m_handle);
	check(result);

    return result;
}

VkResult CommandBuffer::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult CommandBuffer::begin(VkCommandBufferUsageFlags flags /*=0*/, std::shared_ptr<CommandBuffer> baseCommandBuffer /*=nullptr*/)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    beginInfo.pInheritanceInfo = nullptr; // Optional

    VkCommandBufferInheritanceInfo inheritInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, nullptr};    
    if (baseCommandBuffer)
    {
        inheritInfo.renderPass  = VK_NULL_HANDLE;  //to fix: baseCommandBuffer->m_renderPass->getHandle();
        inheritInfo.framebuffer  = VK_NULL_HANDLE; // to fix: baseCommandBuffer->m_framebuffer->getHandle();
        inheritInfo.subpass     = 0; //baseCommandBuffer->m_subpassIndex;
        
        beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        beginInfo.pInheritanceInfo = &inheritInfo;
    }

    VkResult result = vkBeginCommandBuffer(m_handle, &beginInfo);
    check(result);
    return result;
}

VkResult CommandBuffer::end()
{
    return vkEndCommandBuffer(m_handle);
}

void CommandBuffer::beginRenderPass(std::shared_ptr<RenderPass> renderPass, std::shared_ptr<FrameBuffer> frameBuffer)
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass->getHandle();
    renderPassInfo.framebuffer = frameBuffer->getHandle();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = { frameBuffer->getCreateInfo().width, frameBuffer->getCreateInfo().height };

    std::vector<VkClearValue> clearValues(renderPass->m_attachmentInfos.size());
    
    for (auto& attachment : renderPass->m_attachmentInfos)
    {
        clearValues[attachment.m_location] = attachment.m_clear;
    }

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(m_handle, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

}

void CommandBuffer::endRenderPass()
{
    vkCmdEndRenderPass(m_handle);
}

void CommandBuffer::bindPipeline(VkPipelineBindPoint bindpoint, std::shared_ptr<GraphicPipeline> pipeline)
{
    vkCmdBindPipeline(m_handle, bindpoint, pipeline->getHandle());
}

void CommandBuffer::imageMemoryBarrier(Image& image, VkImageLayout oldLayout, VkImageLayout newLayout,
                        VkAccessFlags srcAccess, VkAccessFlags dstAccess, 
                        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    if (image.m_imageLayout != oldLayout)
    {
        //warning the uncontinuious layout transition
    }

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.getHandle();

    barrier.subresourceRange.aspectMask = image.m_aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(m_handle, srcStage, dstStage, 0,  0, nullptr,  0, nullptr,  1, &barrier);

    image.m_imageLayout = newLayout;
}

void CommandBuffer::copyBuffer(const     Buffer& srcBuffer, Buffer& dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset /*=0*/, VkDeviceSize dstOffset /*=0*/)
{
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(m_handle, srcBuffer.getHandle(), dstBuffer.getHandle(), 1, &copyRegion);
}

void CommandBuffer::copyBufferToImage(const Buffer& srcBuffer, Image& image, VkDeviceSize srcOffset, const VkExtent3D& dstExtent, const VkOffset3D& dstOffset /*={0,0,0}*/)
{
    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = srcOffset;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = dstOffset;
    copyRegion.imageExtent = dstExtent;
    //this command requires the image to be in the right layout first. It's host's responsibilty to ensure it.
    vkCmdCopyBufferToImage(m_handle, srcBuffer.getHandle(), image.getHandle(), image.m_imageLayout, 1, &copyRegion);
}

VkResult Shader::create(const std::string& filename)
{
    auto shaderCode = readFile(filename);

    m_createInfo.codeSize= shaderCode.size();
    m_createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());
    return create();
}

VkResult Shader::create(const unsigned char* data, unsigned int  byteSize)
{
    m_createInfo.codeSize = (size_t) byteSize;
    m_createInfo.pCode = reinterpret_cast<const uint32_t*>(data);
    return create();
}

VkResult Shader::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult Shader::create(const ShaderModuleCreateInfoFunc& createInfoFunc)
{
    createInfoFunc(m_createInfo);
    return create();
}

VkResult Shader::create()
{
    VkResult result = vkCreateShaderModule(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);
    return result;
}

VkResult RenderPass::create(const std::vector<AttachmentInfo>& attachments, const std::vector<SubpassInfo>& subpasses)
{
    return create(static_cast<uint32_t>(attachments.size()), attachments,
                  static_cast<uint32_t>(subpasses.size()), subpasses);
}

VkResult RenderPass::create(uint32_t attachmentCount, const std::vector<AttachmentInfo>& attachments, uint32_t subpassCount, const std::vector<SubpassInfo>& subpasses)
{
    m_attachmentInfos = attachments;

    m_attachmentDescriptions.resize(attachments.size());
    for (auto& attachment : attachments)
        m_attachmentDescriptions[attachment.m_location] = attachment.m_description;


    for (auto& subpass : subpasses)
    {
        VkSubpassDescription subDescription{};

        subDescription.flags = 0;
        subDescription.pipelineBindPoint = subpass.m_pipelineBindPoint;
        subDescription.inputAttachmentCount = static_cast<uint32_t>(subpass.m_inputAttachments.size());
        subDescription.pInputAttachments = subpass.m_inputAttachments.data();
        subDescription.colorAttachmentCount = static_cast<uint32_t>(subpass.m_colorAttachments.size());;
        subDescription.pColorAttachments = subpass.m_colorAttachments.data();
        subDescription.pResolveAttachments = subpass.m_resolveAttachments.data();
        subDescription.pDepthStencilAttachment =
            (subpass.m_depthAttachment.attachment != VK_ATTACHMENT_UNUSED)? &subpass.m_depthAttachment : nullptr;
        subDescription.preserveAttachmentCount = 0;
        subDescription.pPreserveAttachments = nullptr;
        m_subpassDescriptions.emplace_back(subDescription);
    }

    m_createInfo.attachmentCount = attachmentCount;
    m_createInfo.pAttachments = m_attachmentDescriptions.data();
    m_createInfo.subpassCount = subpassCount;
    m_createInfo.pSubpasses = m_subpassDescriptions.data();
    m_createInfo.dependencyCount = 0;
    m_createInfo.pDependencies = nullptr; //&dependency;

    VkResult result = vkCreateRenderPass(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);

    return result;
}


VkResult RenderPass::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult FrameBuffer::create(std::shared_ptr<RenderPass> renderPass, VkExtent2D extent, uint32_t layers /*=1*/ )
{
    uint32_t count = static_cast<uint32_t>(renderPass->m_attachmentInfos.size());
    m_attachments.resize(count);
    for (auto& attachment : renderPass->m_attachmentInfos)
        m_attachments[attachment.m_location] = attachment.m_pImageView->getHandle();

    m_createInfo.renderPass = renderPass->getHandle();
    m_createInfo.attachmentCount = count;
    m_createInfo.pAttachments = m_attachments.data();
    m_createInfo.width = extent.width;
    m_createInfo.height = extent.height;
    m_createInfo.layers = layers;
    
    VkResult result = vkCreateFramebuffer(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);

    return result;
}

VkResult FrameBuffer::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

ShaderPipelineState::ShaderPipelineState(VkShaderStageFlagBits shaderStage, std::shared_ptr<Shader>shader, const std::string& entry /*="main"*/)
    : m_pShader(shader)
    , m_entry(entry)
{
    m_createInfo.flags = 0;
    m_createInfo.stage = shaderStage;
    m_createInfo.module = m_pShader->getHandle();
    m_createInfo.pName = m_entry.c_str();
    m_createInfo.pSpecializationInfo = nullptr; // further work
}

void ShaderPipelineState::setSpecialization(const std::vector<VkSpecializationMapEntry>& mapEntries, size_t dataSize, void *pdata)
{
    for (auto& entry : mapEntries) m_mapEntries.push_back(entry);

    m_specializationInfo.mapEntryCount = static_cast<uint32_t>(m_mapEntries.size());
    m_specializationInfo.pMapEntries = m_mapEntries.data();
    m_specializationInfo.dataSize = dataSize;

    m_data = { reinterpret_cast<const char*>(pdata),
               reinterpret_cast<const char*>(pdata) + dataSize };
    m_specializationInfo.pData = m_data.data();    
}

GraphicPipelineState::GraphicPipelineState()
    : m_inputAssemblyState { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,   nullptr }
    , m_tessellationState { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO, nullptr }
    , m_viewportState { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr }
    , m_rasterizationState { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr }
    , m_multiSampleState { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr }
    , m_depthStencilState { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr }
    , m_colorBlendState { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr }
    , m_dynamicState { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr }
{
    m_inputAssemblyState.flags = 0;
    m_inputAssemblyState.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_inputAssemblyState.primitiveRestartEnable = false;

    m_viewportState.viewportCount = 1;
    m_viewportState.pViewports = nullptr;
    m_viewportState.scissorCount = 1;
    m_viewportState.pScissors = nullptr;

    m_rasterizationState.depthClampEnable        = false;
    m_rasterizationState.rasterizerDiscardEnable = false;
    m_rasterizationState.polygonMode             = VK_POLYGON_MODE_FILL;
    m_rasterizationState.cullMode                = VK_CULL_MODE_BACK_BIT;
    m_rasterizationState.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    m_rasterizationState.depthBiasEnable         = false;
    m_rasterizationState.depthBiasConstantFactor = 0.0f;
    m_rasterizationState.depthBiasClamp          = 0.0f;
    m_rasterizationState.depthBiasSlopeFactor    = 1.0f;
    m_rasterizationState.lineWidth               = 1.0f;

    m_multiSampleState.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    m_multiSampleState.sampleShadingEnable   = false;
    m_multiSampleState.minSampleShading      = 0.0f;
    m_multiSampleState.pSampleMask                = nullptr;
    m_multiSampleState.alphaToCoverageEnable = false;
    m_multiSampleState.alphaToOneEnable      = false;

    m_depthStencilState.depthTestEnable       = true;
    m_depthStencilState.depthWriteEnable      = true;
    m_depthStencilState.depthCompareOp        = VK_COMPARE_OP_LESS;
    m_depthStencilState.depthBoundsTestEnable = false;
    m_depthStencilState.stencilTestEnable     = false;
    m_depthStencilState.front
        = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 255u, 255u, 255u };
    m_depthStencilState.back
        = { VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 255u, 255u, 255u };
    m_depthStencilState.minDepthBounds = 0.0f;
    m_depthStencilState.maxDepthBounds = 1.0f;

    m_colorBlendState.logicOpEnable     = false;
    m_colorBlendState.logicOp           = VK_LOGIC_OP_CLEAR;
    m_colorBlendState.flags = 0;
    m_colorBlendState.attachmentCount = 0;
    m_colorBlendState.pAttachments = nullptr;
    m_colorBlendState.blendConstants[0] = 0.0f;
    m_colorBlendState.blendConstants[1] = 0.0f;
    m_colorBlendState.blendConstants[2] = 0.0f;
    m_colorBlendState.blendConstants[3] = 0.0f;

    m_dynamicState.flags = 0;
    m_dynamicState.dynamicStateCount = 0;
    m_dynamicState.pDynamicStates = nullptr;
}

/* set the vertexBuffer to the binding which is used in vkBindVertexBuffers */
/*     binding may be uncontinoius */
/* describe the vertexBuffer binding info, VkVertexBindingDescription, including binding,stride */
/*     stride: related with the data structure stored in the vertexBuffer */
void GraphicPipelineState::setVertexBinding(uint32_t binding, std::shared_ptr<Buffer> vertexBuffer , uint32_t stride, uint32_t offset /*=0*/, VkVertexInputRate inputRate /*= VK_VERTEX_INPUT_RATE_VERTEX*/)
{
    auto iter = m_vertexInputBindings.find(binding);
    if (iter != m_vertexInputBindings.end())
    {
        iter->second = {binding, stride, inputRate};
    }
    else
    {
        m_vertexInputBindings[binding] = {binding, stride, inputRate};
    }
}

/* set each attrib for vertex, VkVertexInputAttributeDescription */
/*     offset: offset to access the attrib within the binding */
/* one binding(that's also one vertexBuffer) could contain one or more attribs, depending on user definition */
void GraphicPipelineState::setVertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset)
{
    m_vertexInputAttribs.push_back( {location, binding, format, offset} );
}

void GraphicPipelineState::setDynamic(uint32_t index, VkDynamicState dynamicState)
{
    m_dynamics.push_back(dynamicState);
    m_dynamicState.dynamicStateCount = static_cast<uint32_t>(m_dynamics.size());
    m_dynamicState.pDynamicStates = m_dynamics.data();
}

void GraphicPipelineState::setViewport(uint32_t index, const VkViewport& viewport)
{
    m_viewports.resize(std::max(index+1u , static_cast<uint32_t>(m_viewports.size()) ));
    m_viewports[index] = viewport;

    m_viewportState.viewportCount = static_cast<uint32_t>(m_viewports.size());
    m_viewportState.pViewports = m_viewports.data();
}

void GraphicPipelineState::setScissor(uint32_t index, const VkRect2D& scissor)
{
    m_scissors.resize(std::max(index+1u , static_cast<uint32_t>(m_scissors.size()) ));
    m_scissors[index] = scissor;

    m_viewportState.scissorCount = static_cast<uint32_t>(m_scissors.size());
    m_viewportState.pScissors = m_scissors.data();
}

void GraphicPipelineState::setColorBlendAttachment(uint32_t index, const VkPipelineColorBlendAttachmentState& state)
{
    // the order of m_colorBlendAttachments element matches the order of subpass's color attachment ? how about multi-subpass in a renderpass
    m_colorBlendAttachments.resize(std::max(index+1u , static_cast<uint32_t>(m_colorBlendAttachments.size()) ));
    m_colorBlendAttachments[index] = state;

    m_colorBlendState.attachmentCount = static_cast<uint32_t>(m_colorBlendAttachments.size());
    m_colorBlendState.pAttachments = m_colorBlendAttachments.data();
}

VkResult DescriptorSetLayout::create()
{
    return create(static_cast<uint32_t>(m_bindings.size()));
}

VkResult DescriptorSetLayout::create(uint32_t bindingCount)
{
    m_createInfo.bindingCount = bindingCount;
    m_createInfo.pBindings = m_bindings.data();
    VkResult result = vkCreateDescriptorSetLayout(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);
    return result;
}

VkResult DescriptorSetLayout::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

void DescriptorSetLayout::insertBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stage, uint32_t count /*=1*/)
{
    m_bindings.push_back( {binding, type, count,stage, nullptr} );
}

VkDescriptorType DescriptorSetLayout::getDescriptorType(uint32_t binding) const 
{
    auto bindingIter = std::find_if(m_bindings.begin(), m_bindings.end(),
                            [&](const VkDescriptorSetLayoutBinding& descriptorSetBinding) {
                                return descriptorSetBinding.binding == binding;
                            });

    if (bindingIter == m_bindings.end())
    {
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    return bindingIter->descriptorType;
}

VkResult DescriptorSetPool::create(uint32_t size)
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = m_pDescriptorSetLayout->getBindings();
    for (auto& binding : bindings)
    {
        m_poolSizes.push_back({binding.descriptorType, binding.descriptorCount * size});
    }

    m_createInfo.poolSizeCount = static_cast<uint32_t>(m_poolSizes.size());
    m_createInfo.pPoolSizes    = m_poolSizes.data();
    m_createInfo.maxSets       = size;
    return create();
}

VkResult DescriptorSetPool::create(const DescriptorPoolCreateFuncType& createFunc)
{
    createFunc(m_createInfo);
    //m_poolSizes
    return create();
}

VkResult DescriptorSetPool::create()
{
    VkResult result = vkCreateDescriptorPool(m_pDescriptorSetLayout->m_device, 
        &m_createInfo, nullptr, &m_handle);
    check(result);
    return result;
}

VkResult DescriptorSetPool::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult DescriptorSet::create()
{
    m_createInfo.descriptorPool = m_pDescriptorSetPool->getHandle();
    m_createInfo.descriptorSetCount = 1;
    std::vector<VkDescriptorSetLayout> setLayouts = {m_pDescriptorSetPool->m_pDescriptorSetLayout->getHandle()};
    m_createInfo.pSetLayouts = setLayouts.data();

    VkResult result = vkAllocateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device,
        &m_createInfo, &m_handle);
    check(result);
    return result;
}

void DescriptorSet::setBuffer(uint32_t binding, std::shared_ptr<Buffer> pBuffer, VkDeviceSize offsetInBytes/*= 0*/, VkDeviceSize sizeInBytes/* = VK_WHOLE_SIZE*/)
{
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = pBuffer->getHandle();
    bufferInfo.offset = offsetInBytes;
    bufferInfo.range = sizeInBytes;

    m_setState.setBuffer(binding, bufferInfo);
}

void DescriptorSet::setCombinedImageSampler(uint32_t binding, std::shared_ptr<ImageView> imageView, VkImageLayout imageLayout, std::shared_ptr<Sampler> sampler)
{

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler->getHandle();
    imageInfo.imageView = imageView->getHandle();
    imageInfo.imageLayout = imageLayout;

    m_setState.setImage(binding, imageInfo);
}

void DescriptorSet::setImage(uint32_t binding, std::shared_ptr<ImageView> imageView, VkImageLayout imageLayout)
{
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = VK_NULL_HANDLE;
    imageInfo.imageView = imageView->getHandle();
    imageInfo.imageLayout = imageLayout;

    m_setState.setImage(binding, imageInfo);

}

void DescriptorSet::setTexelBufferView(uint32_t binding, std::shared_ptr<TexelBufferView> bufferView)
{
    m_setState.setBufferView(binding, bufferView->getHandle());
}

void DescriptorSet::update()
{
    std::vector<VkWriteDescriptorSet> writeDescriptorSets;

    VkDescriptorType type; // get from descriptorSetLayout.m_bindings: binding's type

    for (auto& iter : m_setState.m_buffers)
    {
        auto info = iter.second; // info is vector<VkDescriptorBufferInfo>
        if (info.size() > 0)
        {
            if (info[0].buffer == VK_NULL_HANDLE) continue; // no VkBuffer at the binding

            type = m_pDescriptorSetPool->m_pDescriptorSetLayout->getDescriptorType(iter.first);

            VkWriteDescriptorSet writeDescriptor { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };

            writeDescriptor.dstSet = m_handle;
            writeDescriptor.dstBinding = iter.first;
            writeDescriptor.dstArrayElement = 0;
            writeDescriptor.descriptorCount = static_cast<uint32_t>(info.size()); // always 1? in what case it'll be more than 1?
            writeDescriptor.descriptorType = type;
            writeDescriptor.pBufferInfo = info.data();
            writeDescriptor.pImageInfo = nullptr;
            writeDescriptor.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device, 1, &writeDescriptor, 0, nullptr);
            //writeDescriptorSets.emplace_back(writeDescriptor);
        }
    }

    for (auto& iter : m_setState.m_images)
    {
        auto info = iter.second;
        if (info.size() > 0)
        {
            if (info[0].sampler == VK_NULL_HANDLE && info[0].imageView == VK_NULL_HANDLE)
                continue;

            type = m_pDescriptorSetPool->m_pDescriptorSetLayout->getDescriptorType(iter.first);

            VkWriteDescriptorSet writeDescriptor { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
            
            writeDescriptor.dstSet = m_handle;
            writeDescriptor.dstBinding = iter.first;
            writeDescriptor.dstArrayElement = 0;
            writeDescriptor.descriptorCount = static_cast<uint32_t>(info.size());
            writeDescriptor.descriptorType = type;
            writeDescriptor.pBufferInfo = nullptr;
            writeDescriptor.pImageInfo = info.data();
            writeDescriptor.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device, 1, &writeDescriptor, 0, nullptr);
            //writeDescriptorSets.emplace_back(writeDescriptor);
        }
    }

    for (auto& iter : m_setState.m_bufferViews)
    {
        if (iter.second == VK_NULL_HANDLE) continue;

        type = m_pDescriptorSetPool->m_pDescriptorSetLayout->getDescriptorType(iter.first);

        VkWriteDescriptorSet writeDescriptor { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
        
        writeDescriptor.dstSet = m_handle;
        writeDescriptor.dstBinding = iter.first;
        writeDescriptor.dstArrayElement = 0;
        writeDescriptor.descriptorCount = 1;
        writeDescriptor.descriptorType = type;
        writeDescriptor.pBufferInfo = nullptr;
        writeDescriptor.pImageInfo = nullptr;
        writeDescriptor.pTexelBufferView = &iter.second;

        vkUpdateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device, 1, &writeDescriptor, 0, nullptr);
        //writeDescriptorSets.emplace_back(writeDescriptor);
    }
    // AS TBD

    // segfault with vector: writeDescriptorSets. content of pImageInfo/pBufferInfo is undefined.
    // Though the local variable "writeDescriptor".pImageInfo/pBufferInfo is correct, coming from m_setState, after writeDescriptorSets.push_back(), element in this Sets vector 
    // could not contain the valid value for pointer-pImageInfo/pBufferInfo.
/*    vkUpdateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device,
        static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr); */
}

VkResult DescriptorSet::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult PipelineLayout::create(const std::unordered_map<uint32_t,std::shared_ptr<DescriptorSetLayout>>& setLayoutMap, const std::vector<VkPushConstantRange>& pushConstantRanges)
{
    for (auto& setLayout: setLayoutMap)
    {
        m_pDescriptorSetLayouts[setLayout.first] = setLayout.second;
        m_descriptorSetLayouts.push_back(setLayout.second->getHandle());
    }
    m_pushConstantRanges = {pushConstantRanges.begin(), pushConstantRanges.end()};

    return create(static_cast<uint32_t>(m_descriptorSetLayouts.size()), static_cast<uint32_t>(m_pushConstantRanges.size()));
}

VkResult PipelineLayout::create(uint32_t setLayoutCount, const std::unordered_map<uint32_t,std::shared_ptr<DescriptorSetLayout>>& setLayoutMap, uint32_t pushConstantRangeCount, const std::vector<VkPushConstantRange>& pushConstantRanges)
{
    for (auto& setLayout: setLayoutMap)
    {
        m_pDescriptorSetLayouts[setLayout.first] = setLayout.second;
        m_descriptorSetLayouts.push_back(setLayout.second->getHandle());
    }
    m_pushConstantRanges = {pushConstantRanges.begin(), pushConstantRanges.end()};

    return create(setLayoutCount, pushConstantRangeCount);
}


VkResult PipelineLayout::create(uint32_t layoutCount, uint32_t constantCount)
{
    m_createInfo.setLayoutCount = layoutCount;
    m_createInfo.pSetLayouts = m_descriptorSetLayouts.data();
    m_createInfo.pushConstantRangeCount = constantCount;
    m_createInfo.pPushConstantRanges = m_pushConstantRanges.data();

    VkResult result = vkCreatePipelineLayout(m_device, &m_createInfo, nullptr, &m_handle);
    check(result);
    return result;
}

VkResult PipelineLayout::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

VkResult GraphicPipeline::create(const std::vector<ShaderPipelineState>& shaderStages, const GraphicPipelineState& graphicPipelineState, std::shared_ptr<RenderPass> renderPass, uint32_t subpassIndex /*=0*/)
{
    /* store resources to local storage, so that the objects in param list could be released */

    // shader stage
    uint32_t count = static_cast<uint32_t>(shaderStages.size());
    m_shaderStageCreateInfos.resize(count);
    m_shaderEntries.resize(count);
    m_specializationInfos.resize(count);
    m_specializationMapEntries.resize(count);
    m_specializationData.resize(count);

    for (uint32_t index = 0; index < count; index++)
    {
        VkPipelineShaderStageCreateInfo info = shaderStages[index].getCreateInfo();

        m_shaderStageCreateInfos[index] = info;

        m_shaderEntries[index] = info.pName;
        m_shaderStageCreateInfos[index].pName = m_shaderEntries[index].c_str();

        if (info.pSpecializationInfo)
        {
            m_specializationInfos[index] = *info.pSpecializationInfo;
            m_specializationMapEntries[index] = { info.pSpecializationInfo->pMapEntries,
                                           info.pSpecializationInfo->pMapEntries + info.pSpecializationInfo->mapEntryCount };
            m_specializationData[index] = { reinterpret_cast<const char*>(info.pSpecializationInfo->pData),
                                     reinterpret_cast<const char*>(info.pSpecializationInfo->pData) + info.pSpecializationInfo->dataSize };

            m_specializationInfos[index].pMapEntries = m_specializationMapEntries[index].data();
            m_specializationInfos[index].pData = m_specializationData[index].data();

            m_shaderStageCreateInfos[index].pSpecializationInfo = &m_specializationInfos[index];
        }
    }

    // vertex input
    for (const auto& iter : graphicPipelineState.m_vertexInputBindings)
    {
        const VkVertexInputBindingDescription& binding = iter.second;
        m_vertexInputBindingDescriptions.push_back(binding);
    }

    m_vertexInputAttributeDescriptions = { graphicPipelineState.m_vertexInputAttribs.begin(),
                                           graphicPipelineState.m_vertexInputAttribs.end() };
    
    m_vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    m_vertexInputStateCreateInfo.pNext = nullptr;
    m_vertexInputStateCreateInfo.flags = 0;
    m_vertexInputStateCreateInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertexInputBindingDescriptions.size());
    m_vertexInputStateCreateInfo.pVertexBindingDescriptions = m_vertexInputBindingDescriptions.data();
    m_vertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertexInputAttributeDescriptions.size());
    m_vertexInputStateCreateInfo.pVertexAttributeDescriptions = m_vertexInputAttributeDescriptions.data();

    // other state: pNext pointer is a potential risk of crash.
    m_inputAssemblyStateCreateInfo = graphicPipelineState.m_inputAssemblyState;
    m_rasterizationStateCreateInfo = graphicPipelineState.m_rasterizationState;
    m_depthStencilStateCreateInfo = graphicPipelineState.m_depthStencilState;

    m_dynamicStateCreateInfo = graphicPipelineState.m_dynamicState;
    if (graphicPipelineState.m_dynamicState.pDynamicStates)
    {
        m_dynamicStates = { graphicPipelineState.m_dynamicState.pDynamicStates,
                            graphicPipelineState.m_dynamicState.pDynamicStates + graphicPipelineState.m_dynamicState.dynamicStateCount };
        m_dynamicStateCreateInfo.pDynamicStates = m_dynamicStates.data();
    }

    m_sampleMasks = (graphicPipelineState.m_multiSampleState.pSampleMask == nullptr) ? 0 : *graphicPipelineState.m_multiSampleState.pSampleMask;
    m_multisampleStateCreateInfo = graphicPipelineState.m_multiSampleState;
    m_multisampleStateCreateInfo.pSampleMask = &m_sampleMasks;

    m_colorBlendStateCreateInfo = graphicPipelineState.m_colorBlendState;
    if (graphicPipelineState.m_colorBlendState.pAttachments)
    {
        m_colorBlendAttachments = { graphicPipelineState.m_colorBlendState.pAttachments,
                                graphicPipelineState.m_colorBlendState.pAttachments + graphicPipelineState.m_colorBlendState.attachmentCount };
        m_colorBlendStateCreateInfo.pAttachments = m_colorBlendAttachments.data();
    }

    m_viewportStateCreateInfo = graphicPipelineState.m_viewportState;
    if (graphicPipelineState.m_viewportState.pViewports)
    {
        m_viewports = { graphicPipelineState.m_viewportState.pViewports,
                    graphicPipelineState.m_viewportState.pViewports + graphicPipelineState.m_viewportState.viewportCount };
        m_viewportStateCreateInfo.pViewports = m_viewports.data();
    }
    if (graphicPipelineState.m_viewportState.pScissors)
    {
        m_scissors = { graphicPipelineState.m_viewportState.pScissors,
                    graphicPipelineState.m_viewportState.pScissors + graphicPipelineState.m_viewportState.scissorCount };
        m_viewportStateCreateInfo.pScissors = m_scissors.data();
    }

    /******************************* setup createinfo *******************************/
    m_createInfo.stageCount = static_cast<uint32_t>(m_shaderStageCreateInfos.size());
    m_createInfo.pStages = m_shaderStageCreateInfos.data();
    m_createInfo.pVertexInputState = &m_vertexInputStateCreateInfo;
    m_createInfo.pInputAssemblyState = &m_inputAssemblyStateCreateInfo;
    m_createInfo.pRasterizationState = &m_rasterizationStateCreateInfo;
    m_createInfo.pDepthStencilState = &m_depthStencilStateCreateInfo;
    m_createInfo.pDynamicState = &m_dynamicStateCreateInfo;
    m_createInfo.pMultisampleState = &m_multisampleStateCreateInfo;
    m_createInfo.pColorBlendState = &m_colorBlendStateCreateInfo;
    m_createInfo.pViewportState = &m_viewportStateCreateInfo;

    m_createInfo.layout = m_pipelineLayout->getHandle();
    m_createInfo.renderPass = renderPass->getHandle();
    m_createInfo.subpass = subpassIndex;
    m_createInfo.basePipelineHandle = VK_NULL_HANDLE; // how to guaranteen the less expensive and quick switch in implementation??
    m_createInfo.basePipelineIndex = -1; // Optional  // in software? hardware?

    VkResult result = vkCreateGraphicsPipelines(m_pipelineLayout->m_device, VK_NULL_HANDLE, 1, &m_createInfo, nullptr, &m_handle);

    check(result);
    return result;
}

VkResult GraphicPipeline::destroy()
{
    VkResult result = VK_SUCCESS;
    return result;
}

bool GraphicPipeline::hasDynamicState(VkDynamicState dynamic) const
{
    return std::binary_search(m_dynamicStates.begin(), m_dynamicStates.end(), dynamic);
}

void GraphicContext::updateBuffer(const char* srcData, VkDeviceSize size, std::shared_ptr<Buffer> dstBuffer, VkDeviceSize dstOffset /*=0*/, VkDeviceSize srcOffset /*=0*/, bool submitOnce /*=false*/ )
{
    auto staging = std::make_shared<Buffer>(m_vulkanSetup.device);
    staging->create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data = nullptr;
    vkMapMemory(m_vulkanSetup.device, staging->getMemory(), 0, size, 0, &data);
    memcpy(data, srcData, (size_t) size);
    vkUnmapMemory(m_vulkanSetup.device, staging->getMemory());
    data = nullptr;

    m_usingBuffers.push_back(staging);

    auto commandBufferStaging = std::make_shared<CommandBuffer>(m_defaultCommandPool);
    commandBufferStaging->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    m_stagingCommandBuffers.push_back(commandBufferStaging);

    commandBufferStaging->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    commandBufferStaging->copyBuffer(*staging, *dstBuffer, size, srcOffset, dstOffset);
    commandBufferStaging->end();

    // submit the command buffer immediately
    if (submitOnce)
        submitStaging(true, { }, { }, false);
    
}

void GraphicContext::updateImage(const char* srcData, VkDeviceSize size, std::shared_ptr<Image> dstImage, const VkExtent3D& dstExtent, const VkOffset3D & dstOffset /*={0,0,0}*/, VkDeviceSize srcOffset /*=0*/, bool submitOnce /*=false*/)
{
    auto staging = std::make_shared<Buffer>(m_vulkanSetup.device);
    staging->create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    void* data = nullptr;
    vkMapMemory(m_vulkanSetup.device, staging->getMemory(), 0, size, 0, &data);
    memcpy(data, srcData, (size_t) size);
    vkUnmapMemory(m_vulkanSetup.device, staging->getMemory());
    data = nullptr;

    m_usingBuffers.push_back(staging);

    auto commandBufferStaging = std::make_shared<CommandBuffer>(m_defaultCommandPool);
    commandBufferStaging->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    m_stagingCommandBuffers.push_back(commandBufferStaging);
    
    commandBufferStaging->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    commandBufferStaging->imageMemoryBarrier(*dstImage, dstImage->m_imageLayout, 
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    //before copyBufferToImage, host should make sure the right layout of image
    commandBufferStaging->copyBufferToImage(*staging, *dstImage, srcOffset, dstExtent, dstOffset);

    //just workround : layout -> shader_read_only.  To be fixed
    commandBufferStaging->imageMemoryBarrier(*dstImage, dstImage->m_imageLayout, 
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    commandBufferStaging->end();
    
    // submit the command buffer immediately
    if (submitOnce)
        submitStaging(true, { }, { }, false);
}

VkSemaphore GraphicContext::submitStaging(     bool waitFence/*=true*/,
         const std::vector<VkSemaphore>&          waitSemaphores /*={}*/,
         const std::vector<VkPipelineStageFlags>& waitPipelineStageFlags /*={}*/,
         bool returnSignalSemaphore /*=true*/)
{
    if(m_stagingCommandBuffers.size() == 0)
        return VK_NULL_HANDLE;

    VkFence fence = VK_NULL_HANDLE;
    if (waitFence)
    {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = 0;

        vkCreateFence(m_vulkanSetup.device, &fenceInfo, nullptr, &fence);
    }
    VkSemaphore signalSemaphore = submit(m_defaultQueue, m_stagingCommandBuffers, fence, waitSemaphores, 
                                         waitPipelineStageFlags, returnSignalSemaphore, false);
    if (fence != VK_NULL_HANDLE)
    {
        VkResult result = vkWaitForFences(m_vulkanSetup.device, 1, &fence, VK_TRUE, UINT64_MAX);
        check(result);
        vkDestroyFence(m_vulkanSetup.device, fence, nullptr);
    }
    m_stagingCommandBuffers.clear();

    return signalSemaphore;
}

 /* App should handle the wait fence if needed */
VkSemaphore GraphicContext::submit (VkQueue queue, const std::vector<std::shared_ptr<CommandBuffer>>& commandBuffers,
          VkFence fence /*=VK_NULL_HANDLE*/,
          const std::vector<VkSemaphore>&          waitSemaphores /*={}*/,
          const std::vector<VkPipelineStageFlags>& waitPipelineStageFlags /*={}*/,
          bool returnSignalSemaphore /*=true*/, bool returnFrameImage /*=false*/)
{
    std::vector<VkCommandBuffer> commands;
    for (auto& iter : commandBuffers)
        commands.push_back(iter->getHandle());
 
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
 
    VkFrameBoundaryEXT fbinfo = { VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT, nullptr };
     
    submitInfo.commandBufferCount = static_cast<uint32_t>(commands.size());
    submitInfo.pCommandBuffers = commands.data();
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitPipelineStageFlags.data(); // Each entry in the waitStages array corresponds to the semaphore with the same index in waitSemaphores
    if (returnFrameImage)
    {
        fbinfo.flags = VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT;
        fbinfo.frameID = frameNo++;
        fbinfo.imageCount = 1;
        VkImage image = m_imageBoundary->getHandle(); 
        fbinfo.pImages = &image;
        fbinfo.bufferCount = 0;
        fbinfo.pBuffers = nullptr;
        submitInfo.pNext = &fbinfo;
    }
 
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
    if (returnSignalSemaphore) {
        VkSemaphoreCreateInfo semaphoreInfo{};        
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(m_vulkanSetup.device, &semaphoreInfo, nullptr, &signalSemaphore);
    }
    submitInfo.signalSemaphoreCount = (signalSemaphore == VK_NULL_HANDLE) ? 0 : 1;
    submitInfo.pSignalSemaphores = (signalSemaphore == VK_NULL_HANDLE) ? nullptr: &signalSemaphore;
 
    // TBD
    if (returnFrameImage)
    {
        // need RaW? - waiting for rendering completed and copy the framebuffer image to imageFrameBoundary
    }
 
    VkResult result = vkQueueSubmit(queue, 1, &submitInfo, fence);
    check(result);
 
    return signalSemaphore;
 }


VkResult BasicContext::initBasic(vulkan_setup_t& vulkan, vulkan_req_t& reqs)
{
    m_vulkanSetup = vulkan;
	vkGetDeviceQueue(vulkan.device, 0, 0, &m_defaultQueue);

	// set defaults if not overridden
	if (!reqs.options.count("width")) reqs.options["width"] = 640;
	if (!reqs.options.count("height")) reqs.options["height"] = 480;
	if (!reqs.options.count("wg_size")) reqs.options["wg_size"] = 32;

	const uint32_t width = std::get<int>(reqs.options.at("width"));
	const uint32_t height = std::get<int>(reqs.options.at("height"));

    m_defaultCommandPool = std::make_shared<CommandBufferPool>(vulkan.device);
    m_frameBoundaryCommandBuffer = std::make_shared<CommandBuffer>(m_defaultCommandPool);
    m_defaultCommandBuffer = std::make_shared<CommandBuffer>(m_defaultCommandPool);

    m_defaultCommandPool->create(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0);
    m_frameBoundaryCommandBuffer->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    m_defaultCommandBuffer->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	// Create an commandBufferDefaultimage for the frame boundary, in case we need it
    m_imageBoundary = std::make_shared<Image>(vulkan.device);
   	uint32_t queueFamilyIndex = 0;
    m_imageBoundary->create({width, height, 1}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1, &queueFamilyIndex);

	// Transition the image already to VK_IMAGE_LAYOUT_GENERAL
    m_frameBoundaryCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    m_frameBoundaryCommandBuffer->imageMemoryBarrier(*m_imageBoundary, 
                        m_imageBoundary->m_imageLayout, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_NONE, VK_ACCESS_NONE, 
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    m_frameBoundaryCommandBuffer->end();

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
	submitInfo.commandBufferCount = 1;
    std::vector<VkCommandBuffer> cmdBuffers = {m_frameBoundaryCommandBuffer->getHandle()};
    submitInfo.pCommandBuffers = cmdBuffers.data();
	VkResult result = vkQueueSubmit(m_defaultQueue, 1, &submitInfo, VK_NULL_HANDLE);
	check(result);

	result = vkQueueWaitIdle(m_defaultQueue);
    check(result);

    return result;
}

std::vector<char> readFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        fprintf(stderr, "failed to open file %s.\n", filename.c_str());
    }
    assert(file.is_open());

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();
    return buffer;
}

#if 0
void graphics_done(vulkan_setup_t& vulkan, compute_resources& r, vulkan_req_t& reqs)
{
	if (reqs.options.count("pipelinecache") && reqs.options.count("cachefile"))
	{
		std::string file = std::get<std::string>(reqs.options.at("cachefile"));
		size_t size = 0;
		VkResult result = vkGetPipelineCacheData(vulkan.device, r.cache, &size, nullptr); // get size
		check(result);
		std::vector<char> blob(size);
		result = vkGetPipelineCacheData(vulkan.device, r.cache, &size, blob.data()); // get data
		check(result);
		save_blob(file, blob.data(), blob.size());
		ILOG("Saved pipeline cache data to %s", file.c_str());
		vkDestroyPipelineCache(vulkan.device, r.cache, nullptr);
	}
	if (r.image) vkDestroyImage(vulkan.device, r.image, NULL);
	vkDestroyBuffer(vulkan.device, r.buffer, NULL);
	testFreeMemory(vulkan, r.memory);
	vkDestroyShaderModule(vulkan.device, r.computeShaderModule, NULL);
	vkDestroyDescriptorPool(vulkan.device, r.descriptorPool, NULL);
	vkDestroyDescriptorSetLayout(vulkan.device, r.descriptorSetLayout, NULL);
	vkDestroyPipelineLayout(vulkan.device, r.pipelineLayout, NULL);
	vkDestroyPipeline(vulkan.device, r.pipeline, NULL);
	vkDestroyCommandPool(vulkan.device, r.commandPool, NULL);
}

#endif
