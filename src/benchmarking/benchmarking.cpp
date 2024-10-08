#include "benchmarking.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "external/json.hpp"

namespace bench
{
    /**
     * Returns the JSON string of the enable file if specified
     * 
     * This function follows the standard to try all the different ways of
     * loading the "enable file" (direct environment variable or through an
     * actual file) and returns either an empty string (no enable file
     * specified) or a JSON string.
    */
    void getEnableFileJson(std::string& enableFileJson)
    {
        const char* enablePath = std::getenv("BENCHMARKING_ENABLE_PATH");
        const char* enableJson = std::getenv("BENCHMARKING_ENABLE_JSON");
        assert(!(enablePath && enableJson));

        enableFileJson.clear();

        if (enablePath)
        {
            FILE* fp = std::fopen(enablePath, "rb");
            std::fseek(fp, 0, SEEK_END);
            enableFileJson.resize(std::ftell(fp));
            std::fseek(fp, 0, SEEK_SET);
            std::fread(enableFileJson.data(), enableFileJson.size(), 1, fp);
            std::fclose(fp);
        }
        else if (enableJson)
        {
            enableFileJson.resize(std::strlen(enableJson));
            std::memcpy(enableFileJson.data(), enableJson, enableFileJson.size());
        }
    }

    namespace
    {
        SettingType stringToSettingType(const std::string& type)
        {
            if (type == "selection")
            {
                return SettingType::Selection;
            }
            else if (type == "bool")
            {
                return SettingType::Bool;
            }
            else if (type == "integer")
            {
                return SettingType::Integer;
            }
            else if (type == "float")
            {
                return SettingType::Float;
            }

            assert(false);
        }

        EnableIntent stringToEnableIntent(const std::string& intent)
        {
            if (intent == "showcase")
            {
                return EnableIntent::Showcase;
            }
            else if (intent == "benchmark")
            {
                return EnableIntent::Benchmark;
            }
            else if (intent == "testing")
            {
                return EnableIntent::Testing;
            }

            assert(false);
            return EnableIntent::Undefined;
        }
    }

    /**
     * Loads the capabilities file from a JSON string
     * 
     * This function parses the capabilities file JSON, load all the data into
     * a `CapabilitiesFile` instance, and check that the data is valid
     * according to the Benchmarking standard.
    */
    void loadCapabilitiesFile(const char* capabilitiesFileJson, CapabilitiesFile& capabilitiesFile)
    {
        nlohmann::json root = nlohmann::json::parse(capabilitiesFileJson);

        root.at("name").get_to(capabilitiesFile.name);
        capabilitiesFile.description = root.value("description", "");
        capabilitiesFile.version = root.value("version", 1);

        capabilitiesFile.scenes.clear();
        if (root.contains("scenes"))
        {
            for (const auto& sceneJson: root.at("scenes").items())
            {
                const std::string& scene_name = sceneJson.key();
                assert(capabilitiesFile.scenes.find(scene_name) == capabilitiesFile.scenes.end());

                Scene& scene = capabilitiesFile.scenes[scene_name];
                scene.name = scene_name;
                scene.description = sceneJson.value().value("description", "");
            }
        }

        capabilitiesFile.settings.clear();
        if (root.contains("settings"))
        {
            for (const auto& settingJson: root.at("settings").items())
            {
                const std::string& setting_name = settingJson.key();
                assert(capabilitiesFile.settings.find(setting_name) == capabilitiesFile.settings.end());

                SettingType type = stringToSettingType(settingJson.value().at("type").get_ref<const nlohmann::json::string_t&>());
                switch (type)
                {
                    case SettingType::Selection:
                    {
                        std::shared_ptr<SettingSelection> setting = std::make_shared<SettingSelection>();
                        for (const auto& optionJson: settingJson.value().at("options"))
                        {
                            const std::string& option = optionJson.get_ref<const nlohmann::json::string_t&>();
                            assert(std::find(setting->options.begin(), setting->options.end(), option) == setting->options.end());
                            setting->options.push_back(option);
                        }
                        if (settingJson.value().contains("default"))
                        {
                            const std::string& option = settingJson.value().at("default").get_ref<const nlohmann::json::string_t&>();
                            setting->value = std::distance(setting->options.begin(), std::find(setting->options.begin(), setting->options.end(), option));
                            assert(setting->value != setting->options.size());
                        }
                        capabilitiesFile.settings[setting_name] = setting;
                        break;
                    }
                    case SettingType::Bool:
                    {
                        std::shared_ptr<SettingBool> setting = std::make_shared<SettingBool>();
                        if (settingJson.value().contains("default"))
                        {
                            setting->value.emplace(settingJson.value().at("default").get_ref<const nlohmann::json::boolean_t&>());
                        }
                        capabilitiesFile.settings[setting_name] = setting;
                        break;
                    }
                    case SettingType::Integer:
                    {
                        std::shared_ptr<SettingInteger> setting = std::make_shared<SettingInteger>();
                        if (settingJson.value().contains("default"))
                        {
                            setting->value.emplace(settingJson.value().at("default").get_ref<const nlohmann::json::number_integer_t&>());
                        }
                        settingJson.value().at("min").get_to(setting->min);
                        settingJson.value().at("max").get_to(setting->max);
                        capabilitiesFile.settings[setting_name] = setting;
                        break;
                    }
                    case SettingType::Float:
                    {
                        std::shared_ptr<SettingFloat> setting = std::make_shared<SettingFloat>();
                        if (settingJson.value().contains("default"))
                        {
                            setting->value.emplace(settingJson.value().at("default").get_ref<const nlohmann::json::number_float_t&>());
                        }
                        settingJson.value().at("min").get_to(setting->min);
                        settingJson.value().at("max").get_to(setting->max);
                        capabilitiesFile.settings[setting_name] = setting;
                        break;
                    }
                    default:
                    {
                        assert(false);
                        break;
                    }
                }


            }
        }
        
        capabilitiesFile.capabilities.loadFromCapabilitiesFile(root);

        capabilitiesFile.adaptations.clear();
        if (root.contains("adaptations"))
        {
            for (const auto& adaptationJson: root.at("adaptations").items())
            {
                const std::string& adaptation_name = adaptationJson.key();
                assert(capabilitiesFile.adaptations.find(adaptation_name) == capabilitiesFile.adaptations.end());

                Adaptation& adaptation = capabilitiesFile.adaptations[adaptation_name];
                adaptation.name = adaptation_name;
                adaptation.description = adaptationJson.value().value("description", "");
            }
        }
    }

    /**
     * Loads the enable file from a JSON string and the "supposedly associated"
     * capabilities file.
     * 
     * This function parses the enable file JSON, load all the data into an
     * `EnableFile` instance, and check that the data is valid according to
     * the Benchmarking standard and the associated capabilities file.
     * 
     * If `addCapabilitiesFileDefaults` is set to `false`, then the
     * `EnableFile` instance only contains settings and capabilities specified
     * in the enable file.
     * If it is set to `true`, then capabilities set in the capabilities file
     * and settings set in the capabilities file with a default value are also
     * added in the enable file.
    */
    void loadEnableFile(const CapabilitiesFile& capabilitiesFile, const char* enableFileJson, EnableFile& enableFile, bool addCapabilitiesFileDefaults)
    {
        nlohmann::json root = nlohmann::json::parse(enableFileJson);

        enableFile.target = root.at("target");
        assert(enableFile.target == capabilitiesFile.name);

        enableFile.scenes.clear();
        if (root.contains("scenes"))
        {
            for (const auto& sceneJson: root.at("scenes"))
            {
                const std::string& scene_name = sceneJson.get_ref<const nlohmann::json::string_t&>();
                assert(capabilitiesFile.scenes.find(scene_name) != capabilitiesFile.scenes.end());
                assert(std::find(enableFile.scenes.begin(), enableFile.scenes.end(), scene_name) == enableFile.scenes.end());
                enableFile.scenes.push_back(scene_name);
            }
        }

        enableFile.intent = EnableIntent::Undefined;
        if (root.contains("intent"))
        {
            enableFile.intent = stringToEnableIntent(root.at("intent").get_ref<const nlohmann::json::string_t&>());
        }

        enableFile.results = root.value("results", "");

        enableFile.settings.clear();
        if (root.contains("settings"))
        {
            for (const auto& settingJson: root.at("settings").items())
            {
                const std::string& setting_name = settingJson.key();
                const auto it = capabilitiesFile.settings.find(setting_name);
                assert(it != capabilitiesFile.settings.end());

                switch (it->second->type)
                {
                    case SettingType::Selection:
                    {
                        std::shared_ptr<SettingSelection> setting = std::make_shared<SettingSelection>(*reinterpret_cast<const SettingSelection*>(it->second.get()));
                        const std::string& option = settingJson.value().get_ref<const nlohmann::json::string_t&>();
                        setting->value = std::distance(setting->options.begin(), std::find(setting->options.begin(), setting->options.end(), option));
                        assert(setting->value != setting->options.size());
                        enableFile.settings[setting_name] = setting;
                        break;
                    }
                    case SettingType::Bool:
                    {
                        std::shared_ptr<SettingBool> setting = std::make_shared<SettingBool>(*reinterpret_cast<const SettingBool*>(it->second.get()));
                        setting->value = settingJson.value().get_ref<const nlohmann::json::boolean_t&>();
                        enableFile.settings[setting_name] = setting;
                        break;
                    }
                    case SettingType::Integer:
                    {
                        std::shared_ptr<SettingInteger> setting = std::make_shared<SettingInteger>(*reinterpret_cast<const SettingInteger*>(it->second.get()));
                        setting->value = settingJson.value().get_ref<const nlohmann::json::number_integer_t&>();
                        assert(setting->value >= setting->min && setting->value <= setting->max);
                        enableFile.settings[setting_name] = setting;
                        break;
                    }
                    case SettingType::Float:
                    {
                        std::shared_ptr<SettingFloat> setting = std::make_shared<SettingFloat>(*reinterpret_cast<const SettingFloat*>(it->second.get()));
                        setting->value = settingJson.value().get_ref<const nlohmann::json::number_float_t&>();
                        assert(setting->value >= setting->min && setting->value <= setting->max);
                        enableFile.settings[setting_name] = setting;
                        break;
                    }
                    default:
                    {
                        assert(false);
                        break;
                    }
                }
            }

            if (addCapabilitiesFileDefaults)
            {
                for (const std::pair<std::string, std::shared_ptr<Setting>>& elt: capabilitiesFile.settings)
                {
                    if (enableFile.settings.find(elt.first) != enableFile.settings.end())
                    {
                        continue;
                    }

                    switch (elt.second->type)
                    {
                        case SettingType::Selection:
                        {
                            const SettingSelection& setting = *reinterpret_cast<const SettingSelection*>(elt.second.get());
                            if (setting.value.has_value())
                            {
                                enableFile.settings[elt.first] = std::make_shared<SettingSelection>(setting);
                            }
                            break;
                        }
                        case SettingType::Bool:
                        {
                            const SettingBool& setting = *reinterpret_cast<const SettingBool*>(elt.second.get());
                            if (setting.value.has_value())
                            {
                                enableFile.settings[elt.first] = std::make_shared<SettingBool>(setting);
                            }
                            break;
                        }
                        case SettingType::Integer:
                        {
                            const SettingInteger& setting = *reinterpret_cast<const SettingInteger*>(elt.second.get());
                            if (setting.value.has_value())
                            {
                                enableFile.settings[elt.first] = std::make_shared<SettingInteger>(setting);
                            }
                            break;
                        }
                        case SettingType::Float:
                        {
                            const SettingFloat& setting = *reinterpret_cast<const SettingFloat*>(elt.second.get());
                            if (setting.value.has_value())
                            {
                                enableFile.settings[elt.first] = std::make_shared<SettingFloat>(setting);
                            }
                            break;
                        }
                        default:
                        {
                            assert(false);
                            break;
                        }
                    }
                }
            }
        }
        
        enableFile.capabilities.loadFromEnableFile(capabilitiesFile.capabilities, root, addCapabilitiesFileDefaults);

        enableFile.adaptations.clear();
        if (root.contains("adaptations"))
        {
            for (const auto& adaptationJson: root.at("adaptations").items())
            {
                const std::string& adaptation_name = adaptationJson.key();
                assert(capabilitiesFile.adaptations.find(adaptation_name) != capabilitiesFile.adaptations.end());
                assert(enableFile.adaptations.find(adaptation_name) == enableFile.adaptations.end());
                adaptationJson.value().get_to(enableFile.adaptations[adaptation_name]);
            }
        }
    }
}
