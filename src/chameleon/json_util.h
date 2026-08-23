#pragma once

#include <string>

#include "json/json.h"

Json::Value readJson(const std::string& path);
void mergeJson(Json::Value& node, const Json::Value& node_override);
