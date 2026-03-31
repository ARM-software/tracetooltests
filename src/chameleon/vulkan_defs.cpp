#include "vulkan_defs.h"

void cVkCommandPool::update(cVkBase* parent)
{
	for (auto& v : commandBuffers)
	{
		v.update(this);
	}
	cVkBase::update(parent);
}

void cVkPhysicalDevice::update(cVkBase* parent)
{
	for (auto& v : devices)
	{
		v.update(this);
	}
	for (auto& v : displays)
	{
		v.update(this);
	}
	cVkBase::update(parent);
}

void cVkInstance::update()
{
	for (auto& v : GPUs)
	{
		v.update(this);
	}
	for (auto& v : surfaces)
	{
		v.update(this);
	}
	for (auto& v : displays)
	{
		v.update(this);
	}
}

void cVkPipelineCache::update(cVkBase* parent)
{
	cVkBase::update(parent);
}

void cVkDescriptorPool::update(cVkBase* parent)
{
	for (auto& v : sets)
	{
		v.update(this);
	}
	cVkBase::update(parent);
}

void cVkDisplayKHR::update(cVkBase* parent)
{
	for (auto& v : displayModes)
	{
		v.update(this);
	}
	cVkBase::update(parent);
}

void cVkPipeline::update(cVkBase* parent)
{
	for (auto& v : stages)
	{
		if (v.module)
		{
			v.module->update(this);
		}
	}
}

void cVkDevice::update(cVkBase* parent)
{
	for (auto& v : samplerycbcrconversions)
	{
		v.update(this);
	}
	for (auto& v : commandPools)
	{
		v.update(this);
	}
	for (auto& v : queues)
	{
		v.update(this);
	}
	for (auto& v : deviceMemory)
	{
		v.update(this);
	}
	for (auto& v : swapchains)
	{
		v.update(this);
	}
	for (auto& v : fences)
	{
		v.update(this);
	}
	for (auto& v : semaphores)
	{
		v.update(this);
	}
	for (auto& v : events)
	{
		v.update(this);
	}
	for (auto& v : images)
	{
		v.update(this);
	}
	for (auto& v : imageViews)
	{
		v.update(this);
	}
	for (auto& v : buffers)
	{
		v.update(this);
	}
	for (auto& v : bufferViews)
	{
		v.update(this);
	}
	for (auto& v : queryPools)
	{
		v.update(this);
	}
	for (auto& v : shaderModules)
	{
		v.update(this);
	}
	for (auto& v : pipelineCaches)
	{
		v.update(this);
	}
	for (auto& v : pipelineLayouts)
	{
		v.update(this);
	}
	for (auto& v : samplers)
	{
		v.update(this);
	}
	for (auto& v : descriptorSetLayouts)
	{
		v.update(this);
	}
	for (auto& v : renderpasses)
	{
		v.update(this);
	}
	for (auto& v : descriptorPools)
	{
		v.update(this);
	}
	for (auto& v : framebuffers)
	{
		v.update(this);
	}
	for (auto& v : pipelines)
	{
		v.update(this);
	}
	for (auto& v : weights)
	{
		v.update(this);
	}
	for (auto& v : tensors)
	{
		v.update(this);
	}
	for (auto& v : tensorviews)
	{
		v.update(this);
	}
	cVkBase::update(parent);
}

void cVkPipeline::_log_usage()
{
	// Set shader usage flags
	for (cVkPipelineStage& pipeline_stage : stages)
	{
		pipeline_stage.log_usage();
	}
}

void cVkDescriptorSetLayoutBinding::log_usage()
{
	switch (descriptorType)
	{
	case VK_DESCRIPTOR_TYPE_PARTITIONED_ACCELERATION_STRUCTURE_NV:
	case VK_DESCRIPTOR_TYPE_TENSOR_ARM:
		// TBD
		break;
	case VK_DESCRIPTOR_TYPE_SAMPLER:
	case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		if (immutableSamplers.size() > 0)
		{
			assert(immutableSamplers.size() >= descriptorCount);
			for (unsigned j = 0; j < descriptorCount; j++)
			{
				if (immutableSamplers[j] == VK_NULL_HANDLE) continue;
				// These call touch, no need to do anything else
				ccast<cVkSampler, VkSampler>(immutableSamplers[j]);
			}
		}
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
	case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
		if (pImageInfo.size() > 0)
		{
			assert(pImageInfo.size() >= descriptorCount);
			for (unsigned j = 0; j < descriptorCount; j++)
			{
				if (pImageInfo[j].imageView == VK_NULL_HANDLE) continue;
				// These call touch, no need to do anything else
				ccast<cVkSampler, VkSampler>(pImageInfo[j].sampler);
				ccast<cVkImageView, VkImageView>(pImageInfo[j].imageView);
				cVkImageView* image_view = ccast<cVkImageView, VkImageView>(pImageInfo[j].imageView);
				touch(image_view->image);
			}
		}
		break;
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		if (pBufferInfo.size() > 0)
		{
			assert(pBufferInfo.size() >= descriptorCount);
			for (unsigned j = 0; j < descriptorCount; j++)
			{
				if (pBufferInfo[j].buffer == VK_NULL_HANDLE) continue;
				// This calls touch, no need to do anything else
				ccast<cVkBuffer, VkBuffer>(pBufferInfo[j].buffer);
			}
		}
		break;
	case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		if (pTexelBufferView.size() > 0)
		{
			assert(pTexelBufferView.size() >= descriptorCount);
			for (unsigned j = 0; j < descriptorCount; j++)
			{
				if (pTexelBufferView[j] == VK_NULL_HANDLE) continue;
				// This calls touch, no need to do anything else
				ccast<cVkBufferView, VkBufferView>(pTexelBufferView[j]);
			}
		}
		break;
	case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: // Provided by VK_VERSION_1_3
		break;
	case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: // Provided by VK_KHR_acceleration_structure
		break;
	case VK_DESCRIPTOR_TYPE_MUTABLE_EXT: // Provided by VK_EXT_mutable_descriptor_type
		break;
	case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM: // Provided by VK_QCOM_image_processing
	case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM: // Provided by VK_QCOM_image_processing
		break;
	case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV: // Provided by VK_NV_ray_tracing
		break;
	case VK_DESCRIPTOR_TYPE_MAX_ENUM:
		break;
	}
}
