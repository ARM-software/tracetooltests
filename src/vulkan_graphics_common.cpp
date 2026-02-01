#include "vulkan_graphics_common.h"

#include <algorithm>

// workround for passing compiling quicky
// TBD - get rid of this
vulkan_setup_t vulkan2;
vulkan_req_t req2;

void usage()
{
	printf("-W/--width             Width of output image (default 640)\n");
	printf("-H/--height            Height of output image (default 480)\n");
	printf("-wg/--workgroup-size   Set workgroup size for compute dispatch (default 32)\n");
	printf("-fb/--frame-boundary   Use frameboundary extension to publicize output\n");
	printf("-t/--times N           Times to repeat (default %d)\n", (int)p__loops);
}

bool parseCmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-t", "--times"))
	{
		p__loops = get_arg(argv, ++i, argc);
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
	m_createInfo.pNext = m_pCreateInfoNext;

	m_memoryProperty = properties;
	m_size = size;

	// feature and version required should be checked at the beginning of benchmark
	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkMemoryAllocateFlagsInfo* flagInfo = (VkMemoryAllocateFlagsInfo*)malloc(sizeof(VkMemoryAllocateFlagsInfo));
		flagInfo->sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		flagInfo->pNext = m_pAllocateNext;
		flagInfo->flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
		flagInfo->deviceMask = 0;

		m_allocateInfoNexts.push_back((VkBaseInStructure*)flagInfo);

		m_pAllocateNext = (VkBaseInStructure*)flagInfo;
	}

	m_allocateInfo.pNext = m_pAllocateNext;

	return create();
}

VkResult Buffer::create(const BufferCreateInfoFunc& createInfoFunc, const AllocationCreateInfoFunc& allocationInfoFunc)
{
	createInfoFunc(m_createInfo);
	allocationInfoFunc(m_allocateInfo);
	return create();
}

VkResult Buffer::map(VkDeviceSize offset/*=0*/, VkDeviceSize size/*=VK_WHOLE_SIZE*/, VkMemoryMapFlags flag/*=0*/)
{
	VkResult result;

	result = vkMapMemory(m_device, m_memory, offset, size, flag, &m_mappedAddress);
	check(result);
	return result;
}

void Buffer::flush(bool extra)
{
	if (!emit_extra_flushes && extra) return;
	VkFlushRangesFlagsARM frf = { VK_STRUCTURE_TYPE_FLUSH_RANGES_FLAGS_ARM, nullptr };
	frf.flags = VK_FLUSH_OPERATION_INFORMATIVE_BIT_ARM;
	VkMappedMemoryRange mmr = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr };
	if (extra) mmr.pNext = &frf;
	mmr.memory = m_memory;
	mmr.offset = 0;
	mmr.size = VK_WHOLE_SIZE;
	vkFlushMappedMemoryRanges(m_device, 1, &mmr);
}

void Buffer::unmap()
{
	if (m_mappedAddress) vkUnmapMemory(m_device, m_memory);
	m_mappedAddress = nullptr;
}

VkDeviceAddress Buffer::getBufferDeviceAddress()
{
	if (m_deviceAddress == 0)
	{
		VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr};
		address_info.buffer = m_handle;
		m_deviceAddress = vkGetBufferDeviceAddress(m_device, &address_info);
	}
	return m_deviceAddress;
}

VkResult Buffer::destroy()
{
	DLOG3("MEM detection: Buffer destroy().");
	if (m_handle != VK_NULL_HANDLE && m_memory != VK_NULL_HANDLE)
	{
		for(auto& iter : m_allocateInfoNexts)
		{
			free(iter);
		}
		m_allocateInfoNexts.clear();
		m_pAllocateNext = nullptr;

		for(auto& iter : m_createInfoNexts)
		{
			free(iter);
		}
		m_createInfoNexts.clear();
		m_pCreateInfoNext = nullptr;

		m_queueFamilyIndices.clear();

		unmap();
		vkDestroyBuffer(m_device, m_handle, nullptr);
		vkFreeMemory(m_device, m_memory, nullptr);
		m_handle = VK_NULL_HANDLE;
		m_memory = VK_NULL_HANDLE;
		m_deviceAddress = 0;

		m_createInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
		m_allocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr};
	}
	return VK_SUCCESS;
}

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

VkResult Image::create(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t queueFamilyIndexCount /*= 0*/,
                       uint32_t* queueFamilyIndex /*= nullptr*/, uint32_t mipLevels /*= 1*/, VkImageTiling tiling /*= VK_IMAGE_TILING_OPTIMAL*/)
{
	m_format = format;
	setAspectMask(format);
	if (req2.options.count("image_output") && (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
	{
		usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

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

	VkMemoryRequirements memRequirements = {};
	vkGetImageMemoryRequirements(m_device, m_handle, &memRequirements);

	const uint32_t alignMod = memRequirements.size % memRequirements.alignment;
	const uint32_t alignedSize = (alignMod == 0) ? memRequirements.size : (memRequirements.size + memRequirements.alignment - alignMod);

	VkMemoryAllocateInfo allocateMemInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr };
	allocateMemInfo.memoryTypeIndex = get_device_memory_type(memRequirements.memoryTypeBits, properties);
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

	return result;
}

void Image::setAspectMask(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_UNDEFINED:
		m_aspect = VK_IMAGE_ASPECT_NONE;
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
	DLOG3("MEM detection: image destroy().");

	vkDestroyImage(m_device, m_handle, nullptr);
	vkFreeMemory(m_device, m_memory, nullptr);
	m_handle = VK_NULL_HANDLE;
	m_memory = VK_NULL_HANDLE;

	m_createInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };

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
	DLOG3("MEM detection: imageView destroy().");

	vkDestroyImageView(m_pImage->m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;
	m_createInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };

	m_pImage = nullptr;

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

	m_createInfo.anisotropyEnable = anisotropyEnable;
	m_createInfo.maxAnisotropy = anisotropyEnable;

	m_createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	m_createInfo.unnormalizedCoordinates = VK_FALSE;

	m_createInfo.compareEnable = VK_FALSE;
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
	DLOG3("MEM detection: sampler destroy().");

	vkDestroySampler(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_createInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr };
	return result;
}

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
	DLOG3("MEM detection: commandBufferPool destroy().");

	vkDestroyCommandPool(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_createInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
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
	DLOG3("MEM detection: commandBuffer destroy().");

	vkFreeCommandBuffers(m_device, m_pCommandBufferPool->getHandle(), 1, &m_handle);
	m_handle = VK_NULL_HANDLE;
	m_createInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };

	m_pCommandBufferPool = nullptr;
	return result;
}

VkResult CommandBuffer::begin(VkCommandBufferUsageFlags flags /*=0*/, const CommandBuffer* baseCommandBuffer /*=nullptr*/)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;
	beginInfo.pInheritanceInfo = nullptr;

	VkCommandBufferInheritanceInfo inheritInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO, nullptr};
	if (baseCommandBuffer)
	{
		inheritInfo.renderPass  = VK_NULL_HANDLE;  //to fix: baseCommandBuffer->m_renderPass->getHandle();
		inheritInfo.framebuffer = VK_NULL_HANDLE; // to fix: baseCommandBuffer->m_framebuffer->getHandle();
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

void CommandBuffer::beginRenderPass(const RenderPass& renderPass, const FrameBuffer& frameBuffer)
{
	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass.getHandle();
	renderPassInfo.framebuffer = frameBuffer.getHandle();
	renderPassInfo.renderArea.offset = {0, 0};
	renderPassInfo.renderArea.extent = { frameBuffer.getCreateInfo().width, frameBuffer.getCreateInfo().height };

	std::vector<VkClearValue> clearValues(renderPass.m_attachmentInfos.size());

	for (auto& attachment : renderPass.m_attachmentInfos)
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

void CommandBuffer::bindPipeline(VkPipelineBindPoint bindpoint, const GraphicPipeline& pipeline)
{
	vkCmdBindPipeline(m_handle, bindpoint, pipeline.getHandle());
}

void CommandBuffer::bufferMemoryBarrier(Buffer& buffer, VkDeviceSize offset, VkDeviceSize size,
                                        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
	VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr};
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer.getHandle();
	barrier.offset = offset;
	barrier.size   = size;

	vkCmdPipelineBarrier(m_handle, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
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

void CommandBuffer::copyBuffer(const Buffer& srcBuffer, const Buffer& dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset /*=0*/, VkDeviceSize dstOffset /*=0*/)
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

void CommandBuffer::copyImageToBuffer(const Image& image, const Buffer& dstBuffer, VkDeviceSize dstOffset, const VkExtent3D& srcExtent, const VkOffset3D& srcOffset /*={0,0,0}*/)
{
	assert(image.m_aspect & VK_IMAGE_ASPECT_COLOR_BIT);
	VkBufferImageCopy copyRegion{};
	copyRegion.bufferOffset = dstOffset;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.imageSubresource.mipLevel = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount = 1;
	copyRegion.imageOffset = srcOffset;
	copyRegion.imageExtent = srcExtent;
	//this command requires the image to be in the right layout first. It's host's responsibilty to ensure it.
	vkCmdCopyImageToBuffer(m_handle, image.getHandle(), image.m_imageLayout, dstBuffer.getHandle(), 1, &copyRegion);
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
	DLOG3("MEM detection: shader destroy().");

	vkDestroyShaderModule(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0 };
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
	m_createInfo.pDependencies = nullptr;

	VkResult result = vkCreateRenderPass(m_device, &m_createInfo, nullptr, &m_handle);
	check(result);

	return result;
}


VkResult RenderPass::destroy()
{
	VkResult result = VK_SUCCESS;
	DLOG3("MEM detection: renderPass destroy().");

	vkDestroyRenderPass(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_attachmentDescriptions.clear();
	m_subpassDescriptions.clear();
	m_subpassDependencies.clear();

	m_attachmentInfos.clear();

	m_createInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };
	return result;
}

VkResult FrameBuffer::create(const RenderPass& renderPass, VkExtent2D extent, uint32_t layers /*=1*/ )
{
	uint32_t count = static_cast<uint32_t>(renderPass.m_attachmentInfos.size());
	m_attachments.resize(count);
	for (auto& attachment : renderPass.m_attachmentInfos)
		m_attachments[attachment.m_location] = attachment.m_pImageView->getHandle();

	m_createInfo.renderPass = renderPass.getHandle();
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
	DLOG3("MEM detection: frameBuffer destroy().");

	vkDestroyFramebuffer(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_attachments.clear();
	m_createInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
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
	m_createInfo.pSpecializationInfo = nullptr;
}

void ShaderPipelineState::setSpecialization(const std::vector<VkSpecializationMapEntry>& mapEntries, size_t dataSize, void *pdata)
{
	for (auto& entry : mapEntries) m_mapEntries.push_back(entry);

	m_specializationInfo.mapEntryCount = static_cast<uint32_t>(m_mapEntries.size());
	m_specializationInfo.pMapEntries = m_mapEntries.data();
	m_specializationInfo.dataSize = dataSize;

	m_data = { reinterpret_cast<const char*>(pdata),
	           reinterpret_cast<const char*>(pdata) + dataSize
	         };
	m_specializationInfo.pData = m_data.data();

	m_createInfo.pSpecializationInfo = &m_specializationInfo;
}

void ShaderPipelineState::destroy()
{
	DLOG3("MEM_detection: shaderPipelineState destroy().");

	m_data.clear();
	m_mapEntries.clear();
	m_specializationInfo = {};
	m_createInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr};

	m_pShader = nullptr;
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

void GraphicPipelineState::setVertexBinding(uint32_t binding, const Buffer& vertexBuffer, uint32_t stride, uint32_t offset /*=0*/, VkVertexInputRate inputRate /*= VK_VERTEX_INPUT_RATE_VERTEX*/)
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
	m_viewports.resize(std::max(index+1u, static_cast<uint32_t>(m_viewports.size()) ));
	m_viewports[index] = viewport;

	m_viewportState.viewportCount = static_cast<uint32_t>(m_viewports.size());
	m_viewportState.pViewports = m_viewports.data();
}

void GraphicPipelineState::setScissor(uint32_t index, const VkRect2D& scissor)
{
	m_scissors.resize(std::max(index+1u, static_cast<uint32_t>(m_scissors.size()) ));
	m_scissors[index] = scissor;

	m_viewportState.scissorCount = static_cast<uint32_t>(m_scissors.size());
	m_viewportState.pScissors = m_scissors.data();
}

void GraphicPipelineState::setColorBlendAttachment(uint32_t index, const VkPipelineColorBlendAttachmentState& state)
{
	m_colorBlendAttachments.resize(std::max(index+1u, static_cast<uint32_t>(m_colorBlendAttachments.size()) ));
	m_colorBlendAttachments[index] = state;

	m_colorBlendState.attachmentCount = static_cast<uint32_t>(m_colorBlendAttachments.size());
	m_colorBlendState.pAttachments = m_colorBlendAttachments.data();
}

void GraphicPipelineState::destroy()
{
	m_vertexInputBindings.clear();
	m_vertexInputAttribs.clear();
	m_dynamics.clear();
	m_viewports.clear();
	m_scissors.clear();
	m_colorBlendAttachments.clear();

	m_inputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
	m_tessellationState = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO, nullptr };
	m_viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	m_rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr };
	m_multiSampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };
	m_depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr };
	m_colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };
	m_dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr };
}

VkResult DescriptorSetLayout::create(VkDescriptorSetLayoutCreateFlags flags /*=0*/)
{
	return create(static_cast<uint32_t>(m_bindings.size()), flags);
}

VkResult DescriptorSetLayout::create(uint32_t bindingCount, VkDescriptorSetLayoutCreateFlags flags /*=0*/)
{
	m_createInfo.pNext = m_pCreateInfoNext;
	m_createInfo.flags = flags;
	m_createInfo.bindingCount = bindingCount;
	m_createInfo.pBindings = m_bindings.data();
	VkResult result = vkCreateDescriptorSetLayout(m_device, &m_createInfo, nullptr, &m_handle);
	check(result);
	return result;
}

VkResult DescriptorSetLayout::destroy()
{
	VkResult result = VK_SUCCESS;
	DLOG3("MEM detection: descriptorSetLayout destroy().");

	vkDestroyDescriptorSetLayout(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;
	m_bindings.clear();

	for (auto& next : m_createInfoNexts)
	{
		free(next);
	}
	m_createInfoNexts.clear();
	m_bindingFlags.clear();
	m_mutableTypeList.clear();
	m_mutableTypes.clear();
	m_pCreateInfoNext = nullptr;

	m_createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr};
	return result;
}

void DescriptorSetLayout::insertBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stage, uint32_t count /*=1*/)
{
	m_bindings.push_back( {binding, type, count,stage, nullptr} );
}

VkDescriptorType DescriptorSetLayout::getDescriptorType(uint32_t binding) const
{
	auto bindingIter = std::find_if(m_bindings.begin(), m_bindings.end(),
	                                [&](const VkDescriptorSetLayoutBinding& descriptorSetBinding)
	{
		return descriptorSetBinding.binding == binding;
	});

	if (bindingIter == m_bindings.end())
	{
		return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}

	return bindingIter->descriptorType;
}

template <>
void DescriptorSetLayout::insertNext<VkDescriptorSetLayoutBindingFlagsCreateInfo>(const VkDescriptorSetLayoutBindingFlagsCreateInfo& next)
{
	VkDescriptorSetLayoutBindingFlagsCreateInfo* pInfo = (VkDescriptorSetLayoutBindingFlagsCreateInfo*)malloc(sizeof(VkDescriptorSetLayoutBindingFlagsCreateInfo));
	pInfo->sType = next.sType;
	pInfo->pNext = m_pCreateInfoNext;
	pInfo->bindingCount = next.bindingCount;
	if (next.pBindingFlags)
		m_bindingFlags = { next.pBindingFlags, next.pBindingFlags+next.bindingCount };
	pInfo->pBindingFlags = m_bindingFlags.data();

	m_pCreateInfoNext = (VkBaseInStructure*)pInfo;
	m_createInfoNexts.push_back((VkBaseInStructure*)pInfo);
}

template <>
void DescriptorSetLayout::insertNext<VkMutableDescriptorTypeCreateInfoEXT>(const VkMutableDescriptorTypeCreateInfoEXT& next)
{
	VkMutableDescriptorTypeCreateInfoEXT* pInfo = (VkMutableDescriptorTypeCreateInfoEXT*)malloc(sizeof(VkMutableDescriptorTypeCreateInfoEXT));
	pInfo->sType = next.sType;
	pInfo->pNext = m_pCreateInfoNext;
	pInfo->mutableDescriptorTypeListCount = next.mutableDescriptorTypeListCount;
	if (next.pMutableDescriptorTypeLists)
	{
		uint32_t i = 0;
		while (i < next.mutableDescriptorTypeListCount)
		{
			if (next.pMutableDescriptorTypeLists[i].pDescriptorTypes)
			{
				m_mutableTypes.push_back({next.pMutableDescriptorTypeLists[i].pDescriptorTypes,
				                          next.pMutableDescriptorTypeLists[i].pDescriptorTypes + next.pMutableDescriptorTypeLists[i].descriptorTypeCount});
			}
			VkMutableDescriptorTypeListEXT mutable_desc;
			mutable_desc.descriptorTypeCount = next.pMutableDescriptorTypeLists[i].descriptorTypeCount;
			mutable_desc.pDescriptorTypes = (next.pMutableDescriptorTypeLists[i].pDescriptorTypes) ? m_mutableTypes.at(i).data() : nullptr;
			m_mutableTypeList.push_back(mutable_desc);
			i++;
		}
	}
	pInfo->pMutableDescriptorTypeLists = m_mutableTypeList.data();

	m_pCreateInfoNext = (VkBaseInStructure*)pInfo;
	m_createInfoNexts.push_back((VkBaseInStructure*)pInfo);
}

VkResult DescriptorSetPool::create(uint32_t maxSets, VkDescriptorPoolCreateFlags flags /*=0*/)
{
	std::vector<VkDescriptorSetLayoutBinding> bindings = m_pDescriptorSetLayout->getBindings();
	for (auto& binding : bindings)
	{
		m_poolSizes.push_back({binding.descriptorType, binding.descriptorCount * maxSets});
	}

	m_createInfo.flags = flags;
	m_createInfo.poolSizeCount = static_cast<uint32_t>(m_poolSizes.size());
	m_createInfo.pPoolSizes    = m_poolSizes.data();
	m_createInfo.maxSets       = maxSets;
	return create();
}

VkResult DescriptorSetPool::create(const DescriptorPoolCreateFuncType& createFunc)
{
	createFunc(m_createInfo);
	return create();
}

VkResult DescriptorSetPool::create()
{
	VkResult result = vkCreateDescriptorPool(m_pDescriptorSetLayout->m_device, &m_createInfo, nullptr, &m_handle);
	check(result);
	return result;
}

VkResult DescriptorSetPool::destroy()
{
	VkResult result = VK_SUCCESS;
	DLOG3("MEM detection: descriptorSetPool destroy().");

	vkDestroyDescriptorPool(m_pDescriptorSetLayout->m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_poolSizes.clear();
	m_createInfo =  { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };

	m_pDescriptorSetLayout = nullptr;
	return result;
}

VkResult DescriptorSet::create()
{
	m_createInfo.pNext = m_pCreateInfoNext;
	m_createInfo.descriptorPool = m_pDescriptorSetPool->getHandle();
	m_createInfo.descriptorSetCount = 1;
	std::vector<VkDescriptorSetLayout> setLayouts = {m_pDescriptorSetPool->m_pDescriptorSetLayout->getHandle()};
	m_createInfo.pSetLayouts = setLayouts.data();

	VkResult result = vkAllocateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device, &m_createInfo, &m_handle);
	check(result);
	return result;
}

void DescriptorSet::insertNext(const VkDescriptorSetVariableDescriptorCountAllocateInfo& next)
{
	VkDescriptorSetVariableDescriptorCountAllocateInfo* pInfo
	    = (VkDescriptorSetVariableDescriptorCountAllocateInfo*)malloc(sizeof(VkDescriptorSetVariableDescriptorCountAllocateInfo));
	pInfo->sType = next.sType;
	pInfo->pNext = m_pCreateInfoNext;
	pInfo->descriptorSetCount = next.descriptorSetCount;
	if(next.pDescriptorCounts)
	{
		m_variabledSizeDescriptorCount = { next.pDescriptorCounts, next.pDescriptorCounts+next.descriptorSetCount };
	}
	pInfo->pDescriptorCounts = m_variabledSizeDescriptorCount.data();

	m_pCreateInfoNext = (VkBaseInStructure*)pInfo;
	m_createInfoNexts.push_back((VkBaseInStructure*)pInfo);
}

void DescriptorSet::setBuffer(uint32_t binding, const Buffer& buffer, VkDeviceSize offsetInBytes/*= 0*/, VkDeviceSize sizeInBytes/* = VK_WHOLE_SIZE*/)
{
	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = buffer.getHandle();
	bufferInfo.offset = offsetInBytes;
	bufferInfo.range = sizeInBytes;

	m_setState.setBuffer(binding, bufferInfo);
}

void DescriptorSet::setCombinedImageSampler(uint32_t binding, const ImageView& imageView, VkImageLayout imageLayout, const Sampler& sampler)
{

	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = sampler.getHandle();
	imageInfo.imageView = imageView.getHandle();
	imageInfo.imageLayout = imageLayout;

	m_setState.setImage(binding, imageInfo);
}

void DescriptorSet::setImage(uint32_t binding, const ImageView& imageView, VkImageLayout imageLayout)
{
	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = VK_NULL_HANDLE;
	imageInfo.imageView = imageView.getHandle();
	imageInfo.imageLayout = imageLayout;

	m_setState.setImage(binding, imageInfo);
}

void DescriptorSet::setSampler(uint32_t binding, const Sampler& sampler)
{
	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = sampler.getHandle();
	imageInfo.imageView = VK_NULL_HANDLE;
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	m_setState.setImage(binding, imageInfo);
}

void DescriptorSet::setTexelBufferView(uint32_t binding, const TexelBufferView& bufferView)
{
	m_setState.setBufferView(binding, bufferView.getHandle());
}

void DescriptorSet::update()
{
	VkDescriptorType type;

	for (auto& iter : m_setState.m_buffers)
	{
		auto info = iter.second;
		if (info.size() > 0)
		{
			if (info[0].buffer == VK_NULL_HANDLE) continue;

			type = m_pDescriptorSetPool->m_pDescriptorSetLayout->getDescriptorType(iter.first);

			VkWriteDescriptorSet writeDescriptor { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };

			writeDescriptor.dstSet = m_handle;
			writeDescriptor.dstBinding = iter.first;
			writeDescriptor.dstArrayElement = 0;
			writeDescriptor.descriptorCount = static_cast<uint32_t>(info.size());
			writeDescriptor.descriptorType = type;
			writeDescriptor.pBufferInfo = info.data();
			writeDescriptor.pImageInfo = nullptr;
			writeDescriptor.pTexelBufferView = nullptr;

			vkUpdateDescriptorSets(m_pDescriptorSetPool->m_pDescriptorSetLayout->m_device, 1, &writeDescriptor, 0, nullptr);
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
	}

	// AS TBD
}

VkResult DescriptorSet::destroy()
{
	VkResult result = VK_SUCCESS;
	DLOG3("MEM detection: descriptorSet destroy().");

	m_pDescriptorSetPool = nullptr;

	m_createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };

	m_setState.m_buffers.clear();
	m_setState.m_images.clear();
	m_setState.m_bufferViews.clear();

	for (auto& next : m_createInfoNexts)
	{
		free(next);
	}
	m_createInfoNexts.clear();
	m_pCreateInfoNext = nullptr;
	m_variabledSizeDescriptorCount.clear();

	return result;
}

VkResult PipelineLayout::create(const std::unordered_map<uint32_t,std::shared_ptr<DescriptorSetLayout>>& setLayoutMap, const std::vector<VkPushConstantRange>& pushConstantRanges/*={}*/)
{
	return create(setLayoutMap.size(), setLayoutMap, pushConstantRanges.size(), pushConstantRanges);
}

VkResult PipelineLayout::create(const std::vector<VkPushConstantRange>& pushConstantRanges)
{
	m_pushConstantRanges = {pushConstantRanges.begin(), pushConstantRanges.end()};
	return create(m_descriptorSetLayouts.size(), m_pushConstantRanges.size());
}

VkResult PipelineLayout::create(uint32_t setLayoutCount, const std::unordered_map<uint32_t,std::shared_ptr<DescriptorSetLayout>>& setLayoutMap, uint32_t pushConstantRangeCount, const std::vector<VkPushConstantRange>& pushConstantRanges)
{
	m_pDescriptorSetLayouts.clear();
	m_descriptorSetLayouts.clear();

	uint32_t layoutCount = setLayoutCount;
	if (layoutCount == 0) layoutCount = setLayoutMap.size();
	for (const auto& setLayout : setLayoutMap)
	{
		if (setLayout.first + 1 > layoutCount) layoutCount = setLayout.first + 1;
	}
	m_descriptorSetLayouts.assign(layoutCount, VK_NULL_HANDLE);
	for (const auto& setLayout: setLayoutMap)
	{
		m_pDescriptorSetLayouts[setLayout.first] = setLayout.second;
		m_descriptorSetLayouts[setLayout.first] = setLayout.second->getHandle();
	}
	m_pushConstantRanges = {pushConstantRanges.begin(), pushConstantRanges.end()};

	return create(static_cast<uint32_t>(m_descriptorSetLayouts.size()), pushConstantRangeCount);
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
	DLOG3("MEM detection: pipelineLayout destroy().");

	m_pDescriptorSetLayouts.clear();

	vkDestroyPipelineLayout(m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_pushConstantRanges.clear();
	m_descriptorSetLayouts.clear();

	m_createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
	return result;
}

VkResult GraphicPipeline::create(const std::vector<ShaderPipelineState>& shaderStages, const GraphicPipelineState& graphicPipelineState, const RenderPass& renderPass, VkPipelineCreateFlags flags/* = 0*/, uint32_t subpassIndex /*=0*/)
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
			                                      info.pSpecializationInfo->pMapEntries + info.pSpecializationInfo->mapEntryCount
			                                    };
			m_specializationData[index] = { reinterpret_cast<const char*>(info.pSpecializationInfo->pData),
			                                reinterpret_cast<const char*>(info.pSpecializationInfo->pData) + info.pSpecializationInfo->dataSize
			                              };

			m_specializationInfos[index].pMapEntries = m_specializationMapEntries[index].data();
			m_specializationInfos[index].pData = m_specializationData[index].data();

			m_shaderStageCreateInfos[index].pSpecializationInfo = &m_specializationInfos[index];
		}
		m_shaders[info.stage] = shaderStages[index].m_pShader;
	}

	// vertex input
	for (const auto& iter : graphicPipelineState.m_vertexInputBindings)
	{
		const VkVertexInputBindingDescription& binding = iter.second;
		m_vertexInputBindingDescriptions.push_back(binding);
	}

	m_vertexInputAttributeDescriptions = { graphicPipelineState.m_vertexInputAttribs.begin(),
	                                       graphicPipelineState.m_vertexInputAttribs.end()
	                                     };

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
		                    graphicPipelineState.m_dynamicState.pDynamicStates + graphicPipelineState.m_dynamicState.dynamicStateCount
		                  };
		m_dynamicStateCreateInfo.pDynamicStates = m_dynamicStates.data();
	}

	m_multisampleStateCreateInfo = graphicPipelineState.m_multiSampleState;
	if (graphicPipelineState.m_multiSampleState.pSampleMask)
	{
		m_sampleMasks = *graphicPipelineState.m_multiSampleState.pSampleMask;
		m_multisampleStateCreateInfo.pSampleMask = &m_sampleMasks;
	}
	else
	{
		m_multisampleStateCreateInfo.pSampleMask = nullptr;
	}

	m_colorBlendStateCreateInfo = graphicPipelineState.m_colorBlendState;
	if (graphicPipelineState.m_colorBlendState.pAttachments)
	{
		m_colorBlendAttachments = { graphicPipelineState.m_colorBlendState.pAttachments,
		                            graphicPipelineState.m_colorBlendState.pAttachments + graphicPipelineState.m_colorBlendState.attachmentCount
		                          };
		m_colorBlendStateCreateInfo.pAttachments = m_colorBlendAttachments.data();
	}

	m_viewportStateCreateInfo = graphicPipelineState.m_viewportState;
	if (graphicPipelineState.m_viewportState.pViewports)
	{
		m_viewports = { graphicPipelineState.m_viewportState.pViewports,
		                graphicPipelineState.m_viewportState.pViewports + graphicPipelineState.m_viewportState.viewportCount
		              };
		m_viewportStateCreateInfo.pViewports = m_viewports.data();
	}
	if (graphicPipelineState.m_viewportState.pScissors)
	{
		m_scissors = { graphicPipelineState.m_viewportState.pScissors,
		               graphicPipelineState.m_viewportState.pScissors + graphicPipelineState.m_viewportState.scissorCount
		             };
		m_viewportStateCreateInfo.pScissors = m_scissors.data();
	}

	/******************************* setup createinfo *******************************/
	m_createInfo.flags = flags;
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
	m_createInfo.renderPass = renderPass.getHandle();
	m_createInfo.subpass = subpassIndex;
	m_createInfo.basePipelineHandle = VK_NULL_HANDLE;
	m_createInfo.basePipelineIndex = -1;

	VkResult result = vkCreateGraphicsPipelines(m_pipelineLayout->m_device, VK_NULL_HANDLE, 1, &m_createInfo, nullptr, &m_handle);

	check(result);
	return result;
}

VkResult GraphicPipeline::destroy()
{
	VkResult result = VK_SUCCESS;
	DLOG3("MEM detection: graphicPipeline destroy().");

	vkDestroyPipeline(m_pipelineLayout->m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_specializationData.clear();
	m_specializationMapEntries.clear();
	m_specializationInfos.clear();
	m_shaderEntries.clear();
	m_shaderStageCreateInfos.clear();

	m_vertexInputBindingDescriptions.clear();
	m_vertexInputAttributeDescriptions.clear();
	m_viewports.clear();
	m_scissors.clear();
	m_colorBlendAttachments.clear();
	m_dynamicStates.clear();

	m_vertexInputStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr };
	m_inputAssemblyStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
	m_tessellationStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO, nullptr };
	m_viewportStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
	m_rasterizationStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr };
	m_multisampleStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };
	m_sampleMasks = 0u;
	m_depthStencilStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr };
	m_colorBlendStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };
	m_dynamicStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr };
	m_createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };

	m_shaders.clear();
	m_pipelineLayout = nullptr;

	return result;
}

bool GraphicPipeline::hasDynamicState(VkDynamicState dynamic) const
{
	return std::binary_search(m_dynamicStates.begin(), m_dynamicStates.end(), dynamic);
}

VkResult ComputePipeline::create(const ShaderPipelineState& shaderStage, VkPipelineCreateFlags flags/* = 0*/)
{
	/* store resources to local storage, so that the objects in param list could be released */

	// shader stage
	m_shaderStageCreateInfo = shaderStage.getCreateInfo();

	m_shaderEntry = m_shaderStageCreateInfo.pName;
	m_shaderStageCreateInfo.pName = m_shaderEntry.c_str();

	if (m_shaderStageCreateInfo.pSpecializationInfo)
	{
		m_specializationInfo = *m_shaderStageCreateInfo.pSpecializationInfo;
		m_specializationMapEntries = { m_shaderStageCreateInfo.pSpecializationInfo->pMapEntries,
		                               m_shaderStageCreateInfo.pSpecializationInfo->pMapEntries + m_shaderStageCreateInfo.pSpecializationInfo->mapEntryCount
		                             };
		m_specializationData = { reinterpret_cast<const char*>(m_shaderStageCreateInfo.pSpecializationInfo->pData),
		                         reinterpret_cast<const char*>(m_shaderStageCreateInfo.pSpecializationInfo->pData) + m_shaderStageCreateInfo.pSpecializationInfo->dataSize
		                       };

		m_specializationInfo.pMapEntries = m_specializationMapEntries.data();
		m_specializationInfo.pData = m_specializationData.data();

		m_shaderStageCreateInfo.pSpecializationInfo = &m_specializationInfo;
	}
	m_shader = shaderStage.m_pShader;

	m_createInfo.flags = flags;
	m_createInfo.stage = m_shaderStageCreateInfo;
	m_createInfo.layout = m_pipelineLayout->getHandle();
	m_createInfo.basePipelineHandle = VK_NULL_HANDLE;
	m_createInfo.basePipelineIndex = -1;

	VkResult result = vkCreateComputePipelines(m_pipelineLayout->m_device, VK_NULL_HANDLE, 1, &m_createInfo, nullptr, &m_handle);

	check(result);
	return result;
}

VkResult ComputePipeline::destroy()
{
	VkResult result = VK_SUCCESS;
	DLOG3("MEM detection: computePipeline destroy().");

	vkDestroyPipeline(m_pipelineLayout->m_device, m_handle, nullptr);
	m_handle = VK_NULL_HANDLE;

	m_createInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
	m_specializationMapEntries.clear();
	m_specializationData.clear();

	m_shader = nullptr;
	m_pipelineLayout = nullptr;

	return result;
}

void GraphicContext::updateBuffer(const char* srcData, VkDeviceSize size, const Buffer& dstBuffer, VkDeviceSize dstOffset /*=0*/, VkDeviceSize srcOffset /*=0*/, bool submitOnce /*=false*/ )
{
	auto staging = std::make_unique<Buffer>(m_vulkanSetup);
	staging->create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	staging->map();
	memcpy(staging->m_mappedAddress, srcData, (size_t)size);
	staging->flush(true);
	staging->unmap();

	auto commandBufferStaging = std::make_shared<CommandBuffer>(m_defaultCommandPool);
	commandBufferStaging->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	commandBufferStaging->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	commandBufferStaging->copyBuffer(*staging, dstBuffer, size, srcOffset, dstOffset);
	commandBufferStaging->end();

	m_stagingCommandBuffers.push_back(std::move(commandBufferStaging));

	// submit the command buffer immediately
	if (submitOnce)
		submitStaging(true, { }, { }, false);

	m_usingBuffers.push_back(std::move(staging));
}

void GraphicContext::updateImage(const char* srcData, VkDeviceSize size, Image& dstImage, const VkExtent3D& dstExtent, const VkOffset3D & dstOffset /*={0,0,0}*/, VkDeviceSize srcOffset /*=0*/, bool submitOnce /*=false*/)
{
	auto staging = std::make_unique<Buffer>(m_vulkanSetup);
	staging->create(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	staging->map();
	memcpy(staging->m_mappedAddress, srcData, (size_t)size);
	staging->flush(true);
	staging->unmap();

	auto commandBufferStaging = std::make_shared<CommandBuffer>(m_defaultCommandPool);
	commandBufferStaging->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	commandBufferStaging->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	commandBufferStaging->imageMemoryBarrier(dstImage, dstImage.m_imageLayout,
	        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
	        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

	//before copyBufferToImage, host should make sure the right layout of image
	commandBufferStaging->copyBufferToImage(*staging, dstImage, srcOffset, dstExtent, dstOffset);

	//just workround : layout -> shader_read_only.  To be fixed
	commandBufferStaging->imageMemoryBarrier(dstImage, dstImage.m_imageLayout,
	        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
	        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	commandBufferStaging->end();

	m_stagingCommandBuffers.push_back(std::move(commandBufferStaging));

	// submit the command buffer immediately
	if (submitOnce)
		submitStaging(true, { }, { }, false);

	m_usingBuffers.push_back(std::move(staging));
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
		m_stagingCommandBuffers.clear();
	}

	return signalSemaphore;
}

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
	submitInfo.pWaitDstStageMask = waitPipelineStageFlags.data();
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
	if (returnSignalSemaphore)
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		vkCreateSemaphore(m_vulkanSetup.device, &semaphoreInfo, nullptr, &signalSemaphore);
		m_returnSignalSemaphores.push_back(signalSemaphore);
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

bool GraphicContext::saveImageOutput()
{
	if (!m_imageOutput)
	{
		return false;
	}
	assert(m_renderPass);
	assert(!m_renderPass->m_attachmentInfos.empty());

	AttachmentInfo* colorAttachment = nullptr;
	for (auto& attachment : m_renderPass->m_attachmentInfos)
	{
		if (!attachment.m_pImageView || !attachment.m_pImageView->m_pImage)
		{
			continue;
		}
		if (attachment.m_pImageView->m_pImage->m_aspect & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			colorAttachment = &attachment;
			break;
		}
	}
	assert(colorAttachment);
	assert(colorAttachment->m_pImageView);
	assert(colorAttachment->m_pImageView->m_pImage);
	if (!colorAttachment || !colorAttachment->m_pImageView || !colorAttachment->m_pImageView->m_pImage)
	{
		return false;
	}

	Image& image = *colorAttachment->m_pImageView->m_pImage;
	assert(image.m_aspect & VK_IMAGE_ASPECT_COLOR_BIT);
	VkFormat format = image.m_format;
	const bool format_ok = format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
	                       format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
	assert(format_ok);
	if (!format_ok)
	{
		return false;
	}

	VkExtent3D extent = image.getCreateInfo().extent;
	assert(extent.width > 0);
	assert(extent.height > 0);
	const bool has_transfer_src = (image.getCreateInfo().usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
	assert(has_transfer_src);
	if (!has_transfer_src)
	{
		return false;
	}
	VkDeviceSize size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
	if (!m_imageOutputBuffer || m_imageOutputBufferSize < size)
	{
		m_imageOutputBuffer = std::make_unique<Buffer>(m_vulkanSetup);
		m_imageOutputBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT, size,
		                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		m_imageOutputBufferSize = size;
	}
	if (!m_secondCommandBuffer)
	{
		m_secondCommandBuffer = std::make_shared<CommandBuffer>(m_defaultCommandPool);
		m_secondCommandBuffer->create(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	}

	VkImageLayout originalLayout = colorAttachment->m_description.finalLayout;
	if (originalLayout == VK_IMAGE_LAYOUT_UNDEFINED)
	{
		originalLayout = image.m_imageLayout;
	}
	assert(originalLayout != VK_IMAGE_LAYOUT_UNDEFINED);
	if (originalLayout == VK_IMAGE_LAYOUT_UNDEFINED)
	{
		return false;
	}

	VkCommandBuffer cmd = m_secondCommandBuffer->getHandle();
	vkResetCommandBuffer(cmd, 0);
	m_secondCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	m_secondCommandBuffer->imageMemoryBarrier(image, originalLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                                          VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
	                                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	m_secondCommandBuffer->copyImageToBuffer(image, *m_imageOutputBuffer, 0, { extent.width, extent.height, 1 });
	m_secondCommandBuffer->bufferMemoryBarrier(*m_imageOutputBuffer, 0, size,
	                                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
	                                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT);
	m_secondCommandBuffer->imageMemoryBarrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, originalLayout,
	                                          VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
	                                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

	m_secondCommandBuffer->end();

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence = VK_NULL_HANDLE;
	VkResult result = vkCreateFence(m_vulkanSetup.device, &fenceInfo, nullptr, &fence);
	check(result);

	VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;
	result = vkQueueSubmit(m_defaultQueue, 1, &submitInfo, fence);
	check(result);
	result = vkWaitForFences(m_vulkanSetup.device, 1, &fence, VK_TRUE, UINT64_MAX);
	check(result);
	vkDestroyFence(m_vulkanSetup.device, fence, nullptr);

	std::string base = m_vulkanSetup.bench.test_name.empty() ? "graphics" : m_vulkanSetup.bench.test_name;
	std::string filename = base + "_" + std::to_string(m_imageOutputFrame++) + ".png";
	test_save_image(m_vulkanSetup, filename.c_str(), m_imageOutputBuffer->getMemory(), 0,
	                extent.width, extent.height, format);
	bench_stop_scene(m_vulkanSetup.bench, filename.c_str());
	return true;
}

VkResult BasicContext::initBasic(vulkan_setup_t& vulkan, vulkan_req_t& reqs)
{
	m_vulkanSetup = vulkan;
	vulkan2 = vulkan;
	req2 = reqs;
	vkGetDeviceQueue(vulkan.device, 0, 0, &m_defaultQueue);

	// set defaults if not overridden
	if (!reqs.options.count("width")) reqs.options["width"] = 640;
	if (!reqs.options.count("height")) reqs.options["height"] = 480;
	if (!reqs.options.count("wg_size")) reqs.options["wg_size"] = 32;

	m_imageOutput = reqs.options.count("image_output") > 0;
	m_imageOutputFrame = 0;
	m_imageOutputBufferSize = 0;

	width = static_cast<uint32_t>(std::get<int>(reqs.options.at("width")));
	height = static_cast<uint32_t>(std::get<int>(reqs.options.at("height")));
	wg_size = static_cast<uint32_t>(std::get<int>(reqs.options.at("wg_size")));

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
	                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, &queueFamilyIndex);

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

	if (!file.is_open())
	{
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
