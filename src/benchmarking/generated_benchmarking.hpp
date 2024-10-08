#pragma once

#include <optional>

#include "external/json.hpp"

namespace bench
{
    template<typename T>
    struct Capability
    {
        T value;
        bool modifiable;
    };

    struct Capabilities
    {
        std::optional<Capability<bool>> disable_cpu_performance_adaptations;
        std::optional<Capability<bool>> disable_gpu_performance_adaptations;
        std::optional<Capability<bool>> disable_vendor_performance_adaptations;
        std::optional<Capability<bool>> disable_vendor_adaptations;
        std::optional<Capability<bool>> disable_os_adaptations;
        std::optional<Capability<bool>> disable_loading_screen;
        std::optional<Capability<bool>> non_interactive;
        std::optional<Capability<float>> fixed_framerate;
        std::optional<Capability<float>> visual_settings;
        std::optional<Capability<int>> loops;
        std::optional<Capability<float>> loop_time;
        std::optional<Capability<int>> gpu_delay_reuse;
        std::optional<Capability<bool>> gpu_no_coherent;
        std::optional<Capability<bool>> gpu_frame_deterministic;
        std::optional<Capability<bool>> gpu_fully_deterministic;
        std::optional<Capability<bool>> frameless;
        std::optional<Capability<bool>> file_output;

        void loadFromCapabilitiesFile(const nlohmann::json& root);
        void loadFromEnableFile(const Capabilities& capabilities, const nlohmann::json& root, bool addCapabilitiesFileDefaults);
    };
}
