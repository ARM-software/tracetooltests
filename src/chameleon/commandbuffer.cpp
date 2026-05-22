#include <assert.h>
#include <string.h>

#include "commandbuffer.h"

enum
{
	STATISTIC_INPUT_ASSEMBLY_VERTICES = 0,
	STATISTIC_INPUT_ASSEMBLY_PRIMITIVES,
	STATISTIC_VERTEX_SHADER_INVOCATIONS,
	STATISTIC_GEOMETRY_SHADER_INVOCATIONS,
	STATISTIC_GEOMETRY_SHADER_PRIMITIVES,
	STATISTIC_CLIPPING_INVOCATIONS,
	STATISTIC_CLIPPING_PRIMITIVES,
	STATISTIC_FRAGMENT_SHADER_INVOCATIONS,
	STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES,
	STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS,
	STATISTIC_COMPUTE_SHADER_INVOCATIONS
};

bool write_queries(cVkQueryPool* pool, uint32_t firstQuery, uint32_t queryCount,
                   size_t dataSize, VkDeviceSize stride, void* pData, VkQueryResultFlags flags)
{
	assert(stride % (VK_QUERY_RESULT_64_BIT ? 8 : 4) == 0);
	stride = stride / (VK_QUERY_RESULT_64_BIT ? 8 : 4);
	for (unsigned i = 0; i < queryCount; i++)
	{
		if ((flags & VK_QUERY_RESULT_WAIT_BIT) && !pool->availability[firstQuery + i] && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
		{
			// if we ever implement any deferred work, wait for them here
		}

		uint64_t value = pool->data[firstQuery + i];
		const int idx = i * (stride + (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
		if (flags & VK_QUERY_RESULT_64_BIT)
		{
			uint64_t* dst = reinterpret_cast<uint64_t*>(pData);
			dst[idx] = value;
			if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
			{
				dst[idx + 1] = 1;
			}
		}
		else
		{
			uint32_t* dst = reinterpret_cast<uint32_t*>(pData);
			dst[idx] = value;
			if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
			{
				dst[idx + 1] = 1;
			}
		}
	}
	return true;
}

void reset_command_buffer(cVkCommandBuffer* cmdbuf, bool release_resource)
{
	cmdbuf->flags = 0;
	cmdbuf->commands.clear();
#ifndef FAST
	cmdbuf->count.clear();
	cmdbuf->count.resize(ENUM_MAX_COMMANDS);
	cmdbuf->sum = MetricUnit(0);
#endif
}

void execute_command_buffer_command(const cVkCommand& cmd, cVkCmdState& cmdstate, bool primary)
{
#ifndef FAST // TBD - should only envelop the statistics part, but keep here until the comparing by string is gone
	// Touch all bound resources to update access information
	for (auto r : cmd.bindings)
	{
		touch(r);
	}

	uint64_t* queryData = nullptr;
	if (cmdstate.queryPool)
	{
		queryData = &cmdstate.queryPool->data[cmdstate.queryPool->stride * cmdstate.query];
	}

	switch (cmd.name)
	{
	case ENUM_vkCmdBindPipeline:
		cmdstate.pipeline = (cVkPipeline*)cmd.bindings[0];
		break;
	case ENUM_vkCmdBindDescriptorSets:
		cmdstate.descriptorSets = (cVkDescriptorSet**)(cmd.bindings.data() + 1);
		cmdstate.descriptorSetCount = cmd.bindings.size() - 1;
		break;
	case ENUM_vkCmdDispatch:
	case ENUM_vkCmdDispatchIndirect:
		assert(cmdstate.pipeline);
		cmdstate.pipeline->count.dispatches++;
		touch(cmdstate.pipeline);
		for (cVkPipelineStage& stage : cmdstate.pipeline->stages)
		{
			if (stage.stage == VK_SHADER_STAGE_COMPUTE_BIT && stage.module)
			{
				stage.module->count.dispatches++;
				touch(stage.module);
			}
		}
		for (unsigned i = 0; i < cmdstate.descriptorSetCount; ++i)
		{
			cVkDescriptorSet* target_set = *(cmdstate.descriptorSets + i);
			if (target_set == nullptr) continue;

			for (cVkDescriptorSetLayoutBinding& state_binding : target_set->state_bindings)
			{
				state_binding.log_usage();
			}
		}

		touch(cmdstate.pipeline->layout);
		touch(cmdstate.pipeline->renderPass);

		if (queryData)
		{
			queryData[STATISTIC_COMPUTE_SHADER_INVOCATIONS]++;
		}
		break;
	case ENUM_vkCmdDraw:
	case ENUM_vkCmdDrawIndexed:
	case ENUM_vkCmdDrawIndirect:
	case ENUM_vkCmdDrawIndexedIndirect:
	{
		// TBD: multiply counting by instancing...
		assert(cmdstate.pipeline);
		// do metrics counting for various involved components
		cmdstate.pipeline->count.draws += cmd.count.metrics[1];
		cmdstate.pipeline->count.vertices += cmd.count.metrics[2];
		touch(cmdstate.pipeline);
		for (cVkPipelineStage& stage : cmdstate.pipeline->stages)
		{
			if (stage.module)
			{
				stage.module->count.draws += cmd.count.metrics[1];
				stage.module->count.vertices += cmd.count.metrics[2];
				touch(stage.module);
			}
		}
		for (unsigned i = 0; i < cmdstate.descriptorSetCount; ++i)
		{
			cVkDescriptorSet* target_set = *(cmdstate.descriptorSets + i);
			if (target_set == nullptr) continue;

			for (cVkDescriptorSetLayoutBinding& state_binding : target_set->state_bindings)
			{
				state_binding.log_usage();
			}
		}

		touch(cmdstate.pipeline->layout);
		touch(cmdstate.pipeline->renderPass);

		if (queryData)
		{
			const int verts = cmd.count.metrics[2] * cmd.count.metrics[3];
			queryData[STATISTIC_INPUT_ASSEMBLY_VERTICES] += verts;
			queryData[STATISTIC_INPUT_ASSEMBLY_PRIMITIVES] += verts / 3;
			// "count the number of vertex shader invocations"
			queryData[STATISTIC_VERTEX_SHADER_INVOCATIONS] += verts;
			// "count the number of primitives processed by the Primitive Clipping stage of the pipeline"
			queryData[STATISTIC_CLIPPING_INVOCATIONS] += verts / 3;
			// "count the number of primitives output by the Primitive Clipping stage of the pipeline"
			queryData[STATISTIC_CLIPPING_PRIMITIVES] += verts / 3;
		}
		break;
	}
	case ENUM_vkCmdBeginQuery:
	case ENUM_vkCmdEndQuery:
	{
		cVkPayloadQuery* q = (cVkPayloadQuery*)cmd.payload;
		cmdstate.queryPool = q->queryPool;
		cmdstate.query = q->query;
		break;
	}
	case ENUM_vkCmdCopyQueryPoolResults:
	{
		cVkPayloadCopyQuery* q = (cVkPayloadCopyQuery*)cmd.payload;
		void* pData = q->dstBuffer->memory->ptr + q->dstBuffer->memoryOffset + q->dstOffset;
		size_t dataSize = q->dstBuffer->memory->allocationSize;
		write_queries(q->queryPool, q->firstQuery, q->queryCount, dataSize, q->stride, pData, q->flags);
		break;
	}
	case ENUM_vkCmdResetQueryPool:
	{
		cVkQueryPool* qp = (cVkQueryPool*)cmd.bindings[0];
		const cVkPayloadQueryReset* payload = (cVkPayloadQueryReset*)cmd.payload;
		assert(payload->firstQuery + payload->queryCount <= qp->data.size());
		for (unsigned i = payload->firstQuery; i < payload->firstQuery + payload->queryCount; i++)
		{
			qp->data[i] = 0;
			qp->availability[i] = false;
		}
		break;
	}
	case ENUM_vkCmdCopyBuffer:
	case ENUM_vkCmdCopyBuffer2:
	case ENUM_vkCmdCopyBuffer2KHR:
	{
		const cVkPayloadCopyBuffer* payload = (const cVkPayloadCopyBuffer*)cmd.payload;
		assert(payload);
		assert(payload->srcBuffer);
		assert(payload->dstBuffer);
		assert(payload->srcBuffer->memory);
		assert(payload->dstBuffer->memory);
		for (const VkBufferCopy& region : payload->regions)
		{
			assert(region.srcOffset + region.size <= payload->srcBuffer->size);
			assert(region.dstOffset + region.size <= payload->dstBuffer->size);
			assert(payload->srcBuffer->memoryOffset + region.srcOffset + region.size <= payload->srcBuffer->memory->allocationSize);
			assert(payload->dstBuffer->memoryOffset + region.dstOffset + region.size <= payload->dstBuffer->memory->allocationSize);
			const char* src = payload->srcBuffer->memory->ptr + payload->srcBuffer->memoryOffset + region.srcOffset;
			char* dst = payload->dstBuffer->memory->ptr + payload->dstBuffer->memoryOffset + region.dstOffset;
			memmove(dst, src, region.size);
		}
		break;
	}
	case ENUM_vkCmdSetEvent:
	case ENUM_vkCmdSetEvent2:
	case ENUM_vkCmdSetEvent2KHR:
	{
		cVkEvent* event = (cVkEvent*)cmd.bindings[0];
		event->signalled = true;
		break;
	}
	case ENUM_vkCmdResetEvent:
	case ENUM_vkCmdResetEvent2:
	case ENUM_vkCmdResetEvent2KHR:
	{
		cVkEvent* event = (cVkEvent*)cmd.bindings[0];
		event->signalled = false;
		break;
	}
	case ENUM_vkCmdWriteTimestamp:
	case ENUM_vkCmdWriteTimestamp2:
	case ENUM_vkCmdWriteTimestamp2KHR:
	{
		cVkQueryPool* qp = (cVkQueryPool*)cmd.bindings[0];
		const cVkPayloadQuery* payload = (const cVkPayloadQuery*)cmd.payload;
		assert(payload);
		assert(payload->query < qp->data.size());
		qp->data[payload->query] = cVkBase::current_frame;
		qp->availability[payload->query] = true;
		break;
	}
	case ENUM_vkCmdExecuteCommands: // recurse
	{
		if (!primary)
		{
			ELOG("Trying to execute a secondary command buffer inside a secondary command buffer!");
			return;
		}
		for (const cVkBase* secondary_cmd_buffer : cmd.bindings)
		{
			const cVkCommandBuffer* buffer = reinterpret_cast<const cVkCommandBuffer*>(secondary_cmd_buffer);
			// inherits state? but not overwrites?
			for (const cVkCommand& secondary_cmd : buffer->commands)
			{
				execute_command_buffer_command(secondary_cmd, cmdstate, false);
			}
		}
		break;
	}
	case ENUM_vkCmdWriteAccelerationStructuresPropertiesKHR:
	{
		cVkQueryPool* qp = (cVkQueryPool*)cmd.bindings[0];
		const cVkPayloadWriteAccelerationStructuresPropertiesKHR* payload = (cVkPayloadWriteAccelerationStructuresPropertiesKHR*)cmd.payload;
		for (unsigned i = 0; i < payload->accelerationStructureCount; i++)
		{
			const unsigned query_index = payload->firstQuery + i;
			if (query_index >= qp->data.size()) break;
			const uint64_t value = (i < payload->sizes.size() && payload->sizes[i] != 0) ? payload->sizes[i] : 1;
			qp->data[query_index] = value;
			qp->availability[query_index] = true;
		}
		break;
	}
	case ENUM_vkCmdWriteMicromapsPropertiesEXT:
	{
		cVkQueryPool* qp = (cVkQueryPool*)cmd.bindings[0];
		const cVkPayloadWriteMicromapsPropertiesEXT* payload = (cVkPayloadWriteMicromapsPropertiesEXT*)cmd.payload;
		for (unsigned i = payload->firstQuery; i < payload->micromapCount; i++)
		{
			qp->data[i] = 1;
			qp->availability[i] = true;
		}
		break;
	}
	default:
		break;
	} // switch
#endif
}
