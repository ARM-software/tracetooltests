#include "util.h"

#include <fstream>

#include "json/json.h"
#include "vkjson.h"

FILE* logfp = nullptr;
int verbose_logging = 0;

int android_hw_level(const VkPhysicalDeviceFeatures& f)
{
	if (!f.textureCompressionETC2)
	{
		return -1;
	}
	else if (f.fullDrawIndexUint32 && f.imageCubeArray && f.independentBlend && f.geometryShader && f.tessellationShader
	         && f.sampleRateShading && f.textureCompressionASTC_LDR && f.fragmentStoresAndAtomics && f.shaderImageGatherExtended
	         && f.shaderUniformBufferArrayDynamicIndexing && f.shaderSampledImageArrayDynamicIndexing)
	{
		return 1;
	}
	return 0;
}

Json::Value readJson(const std::string& path)
{
	Json::Value value;
	std::ifstream input(path);
	Json::CharReaderBuilder builder;
	std::string errors;
	bool success = Json::parseFromStream(builder, input, &value, &errors);
	if (!success)
	{
		fprintf(stderr, "Could not parse JSON %s: %s\n", path.c_str(), errors.c_str());
		exit(1);
	}
	return value;
}

void mergeJson(Json::Value& node, const Json::Value& node_override)
{
	if (node.isObject())
	{
		const Json::Value::Members& members = node_override.getMemberNames();
		for (const std::string& member : members)
		{
			if (node.isMember(member))
			{
				mergeJson(node[member], node_override[member]);
			}
			else
			{
				node[member] = node_override[member];
			}
		}
	}
	else
	{
		node = node_override;
	}
}

void readFormats(const Json::Value& formatsRoot, std::map<VkFormat, VkFormatProperties>& map)
{
	for (const std::string& formatName : formatsRoot.getMemberNames())
	{
		VkFormatProperties properties = {};

		for (const Json::Value& feature : formatsRoot[formatName]["VkFormatProperties"]["linearTilingFeatures"])
		{
			properties.linearTilingFeatures |= stringToVkFormatFeatureFlagBits(feature.asString());
		}
		for (const Json::Value& feature : formatsRoot[formatName]["VkFormatProperties"]["optimalTilingFeatures"])
		{
			properties.optimalTilingFeatures |= stringToVkFormatFeatureFlagBits(feature.asString());
		}
		for (const Json::Value& feature : formatsRoot[formatName]["VkFormatProperties"]["bufferFeatures"])
		{
			properties.bufferFeatures |= stringToVkFormatFeatureFlagBits(feature.asString());
		}

		map[stringToVkFormat(formatName)] = properties;
	}
}

void* find_extension_parent(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = reinterpret_cast<VkBaseOutStructure*>(sptr);
	while (ptr != nullptr && (!ptr->pNext || ptr->pNext->sType != sType))
	{
		ptr = ptr->pNext;
	}
	return ptr;
}

void* find_extension(void* sptr, VkStructureType sType)
{
	VkBaseOutStructure* ptr = reinterpret_cast<VkBaseOutStructure*>(sptr);
	while (ptr != nullptr && ptr->sType != sType)
	{
		ptr = ptr->pNext;
	}
	return ptr;
}

const void* find_extension(const void* sptr, VkStructureType sType)
{
	const VkBaseOutStructure* ptr = reinterpret_cast<const VkBaseOutStructure*>(sptr);
	while (ptr != nullptr && ptr->sType != sType)
	{
		ptr = ptr->pNext;
	}
	return ptr;
}

std::string shader_name(VkShaderStageFlagBits bit)
{
	switch (bit)
	{
	case VK_SHADER_STAGE_COMPUTE_BIT: return "comp";
	case VK_SHADER_STAGE_VERTEX_BIT: return "vert";
	case VK_SHADER_STAGE_FRAGMENT_BIT: return "frag";
	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return "tesc";
	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return "tese";
	case VK_SHADER_STAGE_GEOMETRY_BIT: return "geom";
	case VK_SHADER_STAGE_RAYGEN_BIT_KHR: return "rgen";
	case VK_SHADER_STAGE_ANY_HIT_BIT_KHR: return "rahit";
	case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: return "rchit";
	case VK_SHADER_STAGE_MISS_BIT_KHR: return "rmiss";
	case VK_SHADER_STAGE_INTERSECTION_BIT_KHR: return "rint";
	case VK_SHADER_STAGE_ALL_GRAPHICS: return "error";
	case VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM: return "unused";
	default: break;
	}
	return "unknown";
}

int get_env_int(const char* name, int fallback)
{
	int value = fallback;
	const char* tmpstr = getenv(name);
	if (tmpstr)
	{
		value = atoi(tmpstr);
	}
	return value;
}
