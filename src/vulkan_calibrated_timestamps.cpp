#include "vulkan_common.h"

int main(int argc, char **argv)
{
    vulkan_req_t reqs;
    reqs.apiVersion = VK_API_VERSION_1_1;
    reqs.device_extensions.push_back(VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
    vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_calibrated_timestamps", reqs);
    MAKEINSTANCEPROCADDR(vulkan, vkGetPhysicalDeviceCalibrateableTimeDomainsKHR);
    MAKEDEVICEPROCADDR(vulkan, vkGetCalibratedTimestampsKHR);

    const bool null_run = get_env_int("TOOLSTEST_NULL_RUN", 0) > 0;
    uint32_t time_domain_count = 0;

    VkResult result = pf_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(vulkan.physical, &time_domain_count, nullptr);
    check(result);
    if (!null_run)
    {
        assert(time_domain_count > 0);
    }

    std::vector<VkTimeDomainKHR> domains(time_domain_count);
    result = pf_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(vulkan.physical, &time_domain_count, domains.data());
    check(result);
    for (uint32_t i = 0; i < time_domain_count; i++)
    {
        for (uint32_t j = i + 1; j < time_domain_count; j++)
        {
            assert(domains[i] != domains[j]);
        }
    }

    std::vector<VkTimeDomainKHR> usable_domains;
    for (uint32_t i = 0; i < time_domain_count; i++)
    {
        if (domains[i] == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT || domains[i] == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT)
        {
            continue;
        }

        usable_domains.push_back(domains[i]);
    }

    uint32_t usable_domain_count = static_cast<uint32_t>(usable_domains.size());
    if (usable_domain_count == 0)
    {
        test_done(vulkan);
        return null_run ? 0 : 77;
    }

    std::vector<VkCalibratedTimestampInfoKHR> timestamp_info(usable_domain_count);
    for (uint32_t i = 0; i < usable_domain_count; i++)
    {
        VkCalibratedTimestampInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        info.pNext = nullptr;
        info.timeDomain = usable_domains[i];
        timestamp_info[i] = info;
    }

    uint64_t max_deviation = 0;
    std::vector<uint64_t> timestamps(usable_domain_count);

    for (uint32_t i = 0; i < 3; i++)
    {
        result = pf_vkGetCalibratedTimestampsKHR(vulkan.device, usable_domain_count, timestamp_info.data(), timestamps.data(), &max_deviation);
        check(result);
    }
    test_done(vulkan);
    return 0;
}
