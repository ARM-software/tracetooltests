#pragma once

#include "vulkan_defs.h"

void json_overview(const std::string& report_name, cVkInstance* instance, bool hw_info);
int get_env_int(const char* name, int fallback);
std::unordered_set<int> get_env_ints(const char* name);

// Fast implementation for very small sets
template<typename T>
static inline bool naive_intersect(const std::unordered_set<T>& a, const std::unordered_set<T>& b)
{
	for (const auto i : a)
	{
		for (const auto j : b)
		{
			if (i == j)
			{
				return true;
			}
		}
	}
	return false;
}
