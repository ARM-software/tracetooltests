#pragma once

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "json/json.h"

#pragma GCC visibility push(default)
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_beta.h>
#pragma GCC visibility pop

template <typename T, unsigned N>
char (&ComputeArraySize(T (&)[N]))[N];
#define ARRAY_SIZE(Array) sizeof(ComputeArraySize(Array))

#define _to_string(_x) std::to_string(_x)

static inline const char* bool2str(bool val)
{
	return val ? "true" : "false";
}

void readFormats(const Json::Value& formatsRoot, std::map<VkFormat, VkFormatProperties>& map);
Json::Value readJson(const std::string& path);
void mergeJson(Json::Value& node, const Json::Value& node_override);
int android_hw_level(const VkPhysicalDeviceFeatures& f);
std::string shader_name(VkShaderStageFlagBits bit);
int get_env_int(const char* name, int fallback);

extern thread_local long thread_id;

extern FILE* logfp;
extern int verbose_logging;

#define CLOG(_format, ...) if (logfp) fprintf(logfp, "%s(" _format ")\n", __func__, ## __VA_ARGS__)
#define XLOG(_format, ...) if (logfp && verbose_logging) fprintf(logfp, "\t" _format "\n", ## __VA_ARGS__)
#define ELOG(_format, ...) fprintf(stderr, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__)
#define CMDLOG(_format, ...) if (verbose_logging > 1) fprintf(logfp, "%s(" _format ")\n", __func__, ## __VA_ARGS__)
#define DIE(_format, ...) do { fprintf(stderr, "%s:%d " _format "\n", __FILE__, __LINE__, ## __VA_ARGS__); exit(-1); } while(0)

void* find_extension_parent(void* sptr, VkStructureType sType);
void* find_extension(void* sptr, VkStructureType sType);
const void* find_extension(const void* sptr, VkStructureType sType);
