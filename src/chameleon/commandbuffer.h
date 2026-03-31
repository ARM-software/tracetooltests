#pragma once

#include "vulkan_defs.h"
#include "util.h"

void execute_command_buffer_command(const cVkCommand& cmd, cVkCmdState& cmdstate, bool primary);
void reset_command_buffer(cVkCommandBuffer* cmdbuf, bool release_resource);
bool write_queries(cVkQueryPool* queryPool, uint32_t firstQuery, uint32_t queryCount,
                   size_t dataSize, VkDeviceSize stride, void* pData, VkQueryResultFlags flags);
