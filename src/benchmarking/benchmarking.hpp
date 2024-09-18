#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

#include "generated_benchmarking.hpp"

namespace bench
{
    enum class SettingType
    {
        Selection,
        Bool,
        Integer,
        Float
    };

    struct Setting
    {
        std::string name;
        std::string description;
        SettingType type;
    };

    struct SettingSelection : Setting
    {
        std::optional<int> value;

        std::vector<std::string> options;
    };

    struct SettingBool : Setting
    {
        std::optional<bool> value;
    };

    struct SettingInteger : Setting
    {
        std::optional<long long> value;

        int min;
        int max;
    };

    struct SettingFloat : Setting
    {
        std::optional<double> value;

        float min;
        float max;
    };

    struct Scene
    {
        std::string name;
        std::string description;
    };

    struct Adaptation
    {
        std::string name;
        std::string description;
    };

    struct CapabilitiesFile
    {
        std::string name;
        std::string description;
        int version;
        std::unordered_map<std::string, Scene> scenes;
        std::unordered_map<std::string, std::shared_ptr<Setting>> settings;
        Capabilities capabilities;
        std::unordered_map<std::string, Adaptation> adaptations;
    };

    enum class EnableIntent
    {
        Undefined,
        Showcase,
        Benchmark,
        Testing
    };

    struct EnableFile
    {
        std::string target;
        std::vector<std::string> scenes;
        EnableIntent intent;
        std::string results;
        std::unordered_map<std::string, std::shared_ptr<Setting>> settings;
        Capabilities capabilities;
        std::unordered_map<std::string, bool> adaptations;
    };

    void getEnableFileJson(std::string& enableFileJson);

    void loadCapabilitiesFile(const char* capabilitiesFileJson, CapabilitiesFile& capabilitiesFile);
    void loadEnableFile(const CapabilitiesFile& capabilitiesFile, const char* enableFileJson, EnableFile& enableFile, bool addCapabilitiesFileDefaults);
}
