#include "json_util.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

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
