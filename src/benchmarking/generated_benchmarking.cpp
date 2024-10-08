#include "generated_benchmarking.hpp"

#include <cassert>

namespace bench
{
    void Capabilities::loadFromCapabilitiesFile(const nlohmann::json& root)
    {
        disable_cpu_performance_adaptations.reset();
        disable_gpu_performance_adaptations.reset();
        disable_vendor_performance_adaptations.reset();
        disable_vendor_adaptations.reset();
        disable_os_adaptations.reset();
        disable_loading_screen.reset();
        non_interactive.reset();
        fixed_framerate.reset();
        visual_settings.reset();
        loops.reset();
        loop_time.reset();
        gpu_delay_reuse.reset();
        gpu_no_coherent.reset();
        gpu_frame_deterministic.reset();
        gpu_fully_deterministic.reset();
        frameless.reset();
        file_output.reset();

        if (root.contains("capabilities"))
        {
            const nlohmann::json& node = root.at("capabilities");

            if (node.contains("disable_cpu_performance_adaptations"))
            {
                disable_cpu_performance_adaptations.emplace();
                node.at("disable_cpu_performance_adaptations").at("default").get_to(disable_cpu_performance_adaptations->value);
                node.at("disable_cpu_performance_adaptations").at("modifiable").get_to(disable_cpu_performance_adaptations->modifiable);
            }

            if (node.contains("disable_gpu_performance_adaptations"))
            {
                disable_gpu_performance_adaptations.emplace();
                node.at("disable_gpu_performance_adaptations").at("default").get_to(disable_gpu_performance_adaptations->value);
                node.at("disable_gpu_performance_adaptations").at("modifiable").get_to(disable_gpu_performance_adaptations->modifiable);
            }

            if (node.contains("disable_vendor_performance_adaptations"))
            {
                disable_vendor_performance_adaptations.emplace();
                node.at("disable_vendor_performance_adaptations").at("default").get_to(disable_vendor_performance_adaptations->value);
                node.at("disable_vendor_performance_adaptations").at("modifiable").get_to(disable_vendor_performance_adaptations->modifiable);
            }

            if (node.contains("disable_vendor_adaptations"))
            {
                disable_vendor_adaptations.emplace();
                node.at("disable_vendor_adaptations").at("default").get_to(disable_vendor_adaptations->value);
                node.at("disable_vendor_adaptations").at("modifiable").get_to(disable_vendor_adaptations->modifiable);
            }

            if (node.contains("disable_os_adaptations"))
            {
                disable_os_adaptations.emplace();
                node.at("disable_os_adaptations").at("default").get_to(disable_os_adaptations->value);
                node.at("disable_os_adaptations").at("modifiable").get_to(disable_os_adaptations->modifiable);
            }

            if (node.contains("disable_loading_screen"))
            {
                disable_loading_screen.emplace();
                node.at("disable_loading_screen").at("default").get_to(disable_loading_screen->value);
                node.at("disable_loading_screen").at("modifiable").get_to(disable_loading_screen->modifiable);
            }

            if (node.contains("non_interactive"))
            {
                non_interactive.emplace();
                node.at("non_interactive").at("default").get_to(non_interactive->value);
                node.at("non_interactive").at("modifiable").get_to(non_interactive->modifiable);
            }

            if (node.contains("fixed_framerate"))
            {
                fixed_framerate.emplace();
                node.at("fixed_framerate").at("default").get_to(fixed_framerate->value);
                node.at("fixed_framerate").at("modifiable").get_to(fixed_framerate->modifiable);
            }

            if (node.contains("visual_settings"))
            {
                visual_settings.emplace();
                node.at("visual_settings").at("default").get_to(visual_settings->value);
                node.at("visual_settings").at("modifiable").get_to(visual_settings->modifiable);
            }

            if (node.contains("loops"))
            {
                loops.emplace();
                node.at("loops").at("default").get_to(loops->value);
                node.at("loops").at("modifiable").get_to(loops->modifiable);
            }

            if (node.contains("loop_time"))
            {
                loop_time.emplace();
                node.at("loop_time").at("default").get_to(loop_time->value);
                node.at("loop_time").at("modifiable").get_to(loop_time->modifiable);
            }

            if (node.contains("gpu_delay_reuse"))
            {
                gpu_delay_reuse.emplace();
                node.at("gpu_delay_reuse").at("default").get_to(gpu_delay_reuse->value);
                node.at("gpu_delay_reuse").at("modifiable").get_to(gpu_delay_reuse->modifiable);
            }

            if (node.contains("gpu_no_coherent"))
            {
                gpu_no_coherent.emplace();
                node.at("gpu_no_coherent").at("default").get_to(gpu_no_coherent->value);
                node.at("gpu_no_coherent").at("modifiable").get_to(gpu_no_coherent->modifiable);
            }

            if (node.contains("gpu_frame_deterministic"))
            {
                gpu_frame_deterministic.emplace();
                node.at("gpu_frame_deterministic").at("default").get_to(gpu_frame_deterministic->value);
                node.at("gpu_frame_deterministic").at("modifiable").get_to(gpu_frame_deterministic->modifiable);
            }

            if (node.contains("gpu_fully_deterministic"))
            {
                gpu_fully_deterministic.emplace();
                node.at("gpu_fully_deterministic").at("default").get_to(gpu_fully_deterministic->value);
                node.at("gpu_fully_deterministic").at("modifiable").get_to(gpu_fully_deterministic->modifiable);
            }

            if (node.contains("frameless"))
            {
                frameless.emplace();
                node.at("frameless").at("default").get_to(frameless->value);
                node.at("frameless").at("modifiable").get_to(frameless->modifiable);
            }

            if (node.contains("file_output"))
            {
                file_output.emplace();
                node.at("file_output").at("default").get_to(file_output->value);
                node.at("file_output").at("modifiable").get_to(file_output->modifiable);
            }
        }
    }

    void Capabilities::loadFromEnableFile(const Capabilities& capabilities, const nlohmann::json& root, bool addCapabilitiesFileDefaults)
    {
        disable_cpu_performance_adaptations.reset();
        disable_gpu_performance_adaptations.reset();
        disable_vendor_performance_adaptations.reset();
        disable_vendor_adaptations.reset();
        disable_os_adaptations.reset();
        disable_loading_screen.reset();
        non_interactive.reset();
        fixed_framerate.reset();
        visual_settings.reset();
        loops.reset();
        loop_time.reset();
        gpu_delay_reuse.reset();
        gpu_no_coherent.reset();
        gpu_frame_deterministic.reset();
        gpu_fully_deterministic.reset();
        frameless.reset();
        file_output.reset();

        if (root.contains("capabilities"))
        {
            const nlohmann::json& node = root.at("capabilities");

            if (node.contains("disable_cpu_performance_adaptations"))
            {
                assert(capabilities.disable_cpu_performance_adaptations.has_value() && capabilities.disable_cpu_performance_adaptations->modifiable);
                disable_cpu_performance_adaptations = capabilities.disable_cpu_performance_adaptations;
                node.at("disable_cpu_performance_adaptations").at("default").get_to(disable_cpu_performance_adaptations->value);
            }
            else if (capabilities.disable_cpu_performance_adaptations.has_value() && addCapabilitiesFileDefaults)
            {
                disable_cpu_performance_adaptations = capabilities.disable_cpu_performance_adaptations;
            }

            if (node.contains("disable_gpu_performance_adaptations"))
            {
                assert(capabilities.disable_gpu_performance_adaptations.has_value() && capabilities.disable_gpu_performance_adaptations->modifiable);
                disable_gpu_performance_adaptations = capabilities.disable_gpu_performance_adaptations;
                node.at("disable_gpu_performance_adaptations").at("default").get_to(disable_gpu_performance_adaptations->value);
            }
            else if (capabilities.disable_gpu_performance_adaptations.has_value() && addCapabilitiesFileDefaults)
            {
                disable_gpu_performance_adaptations = capabilities.disable_gpu_performance_adaptations;
            }

            if (node.contains("disable_vendor_performance_adaptations"))
            {
                assert(capabilities.disable_vendor_performance_adaptations.has_value() && capabilities.disable_vendor_performance_adaptations->modifiable);
                disable_vendor_performance_adaptations = capabilities.disable_vendor_performance_adaptations;
                node.at("disable_vendor_performance_adaptations").at("default").get_to(disable_vendor_performance_adaptations->value);
            }
            else if (capabilities.disable_vendor_performance_adaptations.has_value() && addCapabilitiesFileDefaults)
            {
                disable_vendor_performance_adaptations = capabilities.disable_vendor_performance_adaptations;
            }

            if (node.contains("disable_vendor_adaptations"))
            {
                assert(capabilities.disable_vendor_adaptations.has_value() && capabilities.disable_vendor_adaptations->modifiable);
                disable_vendor_adaptations = capabilities.disable_vendor_adaptations;
                node.at("disable_vendor_adaptations").at("default").get_to(disable_vendor_adaptations->value);
            }
            else if (capabilities.disable_vendor_adaptations.has_value() && addCapabilitiesFileDefaults)
            {
                disable_vendor_adaptations = capabilities.disable_vendor_adaptations;
            }

            if (node.contains("disable_os_adaptations"))
            {
                assert(capabilities.disable_os_adaptations.has_value() && capabilities.disable_os_adaptations->modifiable);
                disable_os_adaptations = capabilities.disable_os_adaptations;
                node.at("disable_os_adaptations").at("default").get_to(disable_os_adaptations->value);
            }
            else if (capabilities.disable_os_adaptations.has_value() && addCapabilitiesFileDefaults)
            {
                disable_os_adaptations = capabilities.disable_os_adaptations;
            }

            if (node.contains("disable_loading_screen"))
            {
                assert(capabilities.disable_loading_screen.has_value() && capabilities.disable_loading_screen->modifiable);
                disable_loading_screen = capabilities.disable_loading_screen;
                node.at("disable_loading_screen").at("default").get_to(disable_loading_screen->value);
            }
            else if (capabilities.disable_loading_screen.has_value() && addCapabilitiesFileDefaults)
            {
                disable_loading_screen = capabilities.disable_loading_screen;
            }

            if (node.contains("non_interactive"))
            {
                assert(capabilities.non_interactive.has_value() && capabilities.non_interactive->modifiable);
                non_interactive = capabilities.non_interactive;
                node.at("non_interactive").at("default").get_to(non_interactive->value);
            }
            else if (capabilities.non_interactive.has_value() && addCapabilitiesFileDefaults)
            {
                non_interactive = capabilities.non_interactive;
            }

            if (node.contains("fixed_framerate"))
            {
                assert(capabilities.fixed_framerate.has_value() && capabilities.fixed_framerate->modifiable);
                fixed_framerate = capabilities.fixed_framerate;
                node.at("fixed_framerate").at("default").get_to(fixed_framerate->value);
                assert(fixed_framerate->value >= 0);
            }
            else if (capabilities.fixed_framerate.has_value() && addCapabilitiesFileDefaults)
            {
                fixed_framerate = capabilities.fixed_framerate;
            }

            if (node.contains("visual_settings"))
            {
                assert(capabilities.visual_settings.has_value() && capabilities.visual_settings->modifiable);
                visual_settings = capabilities.visual_settings;
                node.at("visual_settings").at("default").get_to(visual_settings->value);
                assert(visual_settings->value >= 1);
                assert(visual_settings->value <= 100);
            }
            else if (capabilities.visual_settings.has_value() && addCapabilitiesFileDefaults)
            {
                visual_settings = capabilities.visual_settings;
            }

            if (node.contains("loops"))
            {
                assert(capabilities.loops.has_value() && capabilities.loops->modifiable);
                loops = capabilities.loops;
                node.at("loops").at("default").get_to(loops->value);
                assert(loops->value >= 0);
            }
            else if (capabilities.loops.has_value() && addCapabilitiesFileDefaults)
            {
                loops = capabilities.loops;
            }

            if (node.contains("loop_time"))
            {
                assert(capabilities.loop_time.has_value() && capabilities.loop_time->modifiable);
                loop_time = capabilities.loop_time;
                node.at("loop_time").at("default").get_to(loop_time->value);
                assert(loop_time->value >= 0);
            }
            else if (capabilities.loop_time.has_value() && addCapabilitiesFileDefaults)
            {
                loop_time = capabilities.loop_time;
            }

            if (node.contains("gpu_delay_reuse"))
            {
                assert(capabilities.gpu_delay_reuse.has_value() && capabilities.gpu_delay_reuse->modifiable);
                gpu_delay_reuse = capabilities.gpu_delay_reuse;
                node.at("gpu_delay_reuse").at("default").get_to(gpu_delay_reuse->value);
                assert(gpu_delay_reuse->value >= 1);
            }
            else if (capabilities.gpu_delay_reuse.has_value() && addCapabilitiesFileDefaults)
            {
                gpu_delay_reuse = capabilities.gpu_delay_reuse;
            }

            if (node.contains("gpu_no_coherent"))
            {
                assert(capabilities.gpu_no_coherent.has_value() && capabilities.gpu_no_coherent->modifiable);
                gpu_no_coherent = capabilities.gpu_no_coherent;
                node.at("gpu_no_coherent").at("default").get_to(gpu_no_coherent->value);
            }
            else if (capabilities.gpu_no_coherent.has_value() && addCapabilitiesFileDefaults)
            {
                gpu_no_coherent = capabilities.gpu_no_coherent;
            }

            if (node.contains("gpu_frame_deterministic"))
            {
                assert(capabilities.gpu_frame_deterministic.has_value() && capabilities.gpu_frame_deterministic->modifiable);
                gpu_frame_deterministic = capabilities.gpu_frame_deterministic;
                node.at("gpu_frame_deterministic").at("default").get_to(gpu_frame_deterministic->value);
            }
            else if (capabilities.gpu_frame_deterministic.has_value() && addCapabilitiesFileDefaults)
            {
                gpu_frame_deterministic = capabilities.gpu_frame_deterministic;
            }

            if (node.contains("gpu_fully_deterministic"))
            {
                assert(capabilities.gpu_fully_deterministic.has_value() && capabilities.gpu_fully_deterministic->modifiable);
                gpu_fully_deterministic = capabilities.gpu_fully_deterministic;
                node.at("gpu_fully_deterministic").at("default").get_to(gpu_fully_deterministic->value);
            }
            else if (capabilities.gpu_fully_deterministic.has_value() && addCapabilitiesFileDefaults)
            {
                gpu_fully_deterministic = capabilities.gpu_fully_deterministic;
            }

            if (node.contains("frameless"))
            {
                assert(capabilities.frameless.has_value() && capabilities.frameless->modifiable);
                frameless = capabilities.frameless;
                node.at("frameless").at("default").get_to(frameless->value);
            }
            else if (capabilities.frameless.has_value() && addCapabilitiesFileDefaults)
            {
                frameless = capabilities.frameless;
            }

            if (node.contains("file_output"))
            {
                assert(capabilities.file_output.has_value() && capabilities.file_output->modifiable);
                file_output = capabilities.file_output;
                node.at("file_output").at("default").get_to(file_output->value);
            }
            else if (capabilities.file_output.has_value() && addCapabilitiesFileDefaults)
            {
                file_output = capabilities.file_output;
            }
        }
    }
}
