#pragma once

#include <assert.h>
#include <string.h>
#include <vector>
#include "spirv/unified1/spirv.h"
#include "vulkan/vulkan.h"

static inline void* find_extension(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	while (ptr != nullptr && ptr->sType != sType) ptr = ptr->pNext;
	return ptr;
}

static inline const void* find_extension(const void* sptr, VkStructureType sType)
{
	const VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	while (ptr != nullptr && ptr->sType != sType) ptr = ptr->pNext;
	return ptr;
}

static inline bool shader_has_device_addresses(const std::vector<uint32_t>& code)
{
	uint16_t opcode;
	uint16_t word_count;
	const uint32_t* insn = code.data() + 5;
	do {
		opcode = uint16_t(insn[0]);
		word_count = uint16_t(insn[0] >> 16);
		if (opcode == SpvOpExtension && strcmp((char*)&insn[2], "KHR_physical_storage_buffer") == 0) return true;
		insn += word_count;
	}
	while (insn != code.data() + code.size() && opcode != SpvOpMemoryModel);
	return false;
}

static inline void* find_extension_parent(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	while (ptr != nullptr && (!ptr->pNext || ptr->pNext->sType != sType)) ptr = ptr->pNext;
	return ptr;
}

static inline void purge_extension_parent(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = (VkBaseOutStructure*)sptr;
	while (ptr != nullptr && ptr->pNext != nullptr)
	{
		if (ptr->pNext->sType == sType)
		{
			ptr->pNext = ptr->pNext->pNext;
		}
		ptr = ptr->pNext;
	}
}
