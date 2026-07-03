#include "vulkan_common.h"

#include <inttypes.h>

static uint32_t image_width = 4096;
static uint32_t image_height = 4096;
static bool verbose_skips = false;

struct format_storage_info
{
	uint32_t block_width = 1;
	uint32_t block_height = 1;
	uint32_t bytes_per_block = 0;
};

static void show_usage()
{
	printf("-W/--width N           Image width, default 4096\n");
	printf("-H/--height N          Image height, default 4096\n");
	printf("-s/--show-skips        Print formats skipped by support queries\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-W", "--width"))
	{
		image_width = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-H", "--height"))
	{
		image_height = get_arg(argv, ++i, argc);
		return true;
	}
	if (match(argv[i], "-s", "--show-skips"))
	{
		verbose_skips = true;
		return true;
	}
	return false;
}

static const char* format_name(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_R4G4_UNORM_PACK8: return "VK_FORMAT_R4G4_UNORM_PACK8";
		case VK_FORMAT_R4G4B4A4_UNORM_PACK16: return "VK_FORMAT_R4G4B4A4_UNORM_PACK16";
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return "VK_FORMAT_B4G4R4A4_UNORM_PACK16";
		case VK_FORMAT_R5G6B5_UNORM_PACK16: return "VK_FORMAT_R5G6B5_UNORM_PACK16";
		case VK_FORMAT_B5G6R5_UNORM_PACK16: return "VK_FORMAT_B5G6R5_UNORM_PACK16";
		case VK_FORMAT_R5G5B5A1_UNORM_PACK16: return "VK_FORMAT_R5G5B5A1_UNORM_PACK16";
		case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return "VK_FORMAT_B5G5R5A1_UNORM_PACK16";
		case VK_FORMAT_A1R5G5B5_UNORM_PACK16: return "VK_FORMAT_A1R5G5B5_UNORM_PACK16";
		case VK_FORMAT_R8_UNORM: return "VK_FORMAT_R8_UNORM";
		case VK_FORMAT_R8_SNORM: return "VK_FORMAT_R8_SNORM";
		case VK_FORMAT_R8_USCALED: return "VK_FORMAT_R8_USCALED";
		case VK_FORMAT_R8_SSCALED: return "VK_FORMAT_R8_SSCALED";
		case VK_FORMAT_R8_UINT: return "VK_FORMAT_R8_UINT";
		case VK_FORMAT_R8_SINT: return "VK_FORMAT_R8_SINT";
		case VK_FORMAT_R8_SRGB: return "VK_FORMAT_R8_SRGB";
		case VK_FORMAT_R8G8_UNORM: return "VK_FORMAT_R8G8_UNORM";
		case VK_FORMAT_R8G8_SNORM: return "VK_FORMAT_R8G8_SNORM";
		case VK_FORMAT_R8G8_USCALED: return "VK_FORMAT_R8G8_USCALED";
		case VK_FORMAT_R8G8_SSCALED: return "VK_FORMAT_R8G8_SSCALED";
		case VK_FORMAT_R8G8_UINT: return "VK_FORMAT_R8G8_UINT";
		case VK_FORMAT_R8G8_SINT: return "VK_FORMAT_R8G8_SINT";
		case VK_FORMAT_R8G8_SRGB: return "VK_FORMAT_R8G8_SRGB";
		case VK_FORMAT_R8G8B8_UNORM: return "VK_FORMAT_R8G8B8_UNORM";
		case VK_FORMAT_R8G8B8_SNORM: return "VK_FORMAT_R8G8B8_SNORM";
		case VK_FORMAT_R8G8B8_USCALED: return "VK_FORMAT_R8G8B8_USCALED";
		case VK_FORMAT_R8G8B8_SSCALED: return "VK_FORMAT_R8G8B8_SSCALED";
		case VK_FORMAT_R8G8B8_UINT: return "VK_FORMAT_R8G8B8_UINT";
		case VK_FORMAT_R8G8B8_SINT: return "VK_FORMAT_R8G8B8_SINT";
		case VK_FORMAT_R8G8B8_SRGB: return "VK_FORMAT_R8G8B8_SRGB";
		case VK_FORMAT_B8G8R8_UNORM: return "VK_FORMAT_B8G8R8_UNORM";
		case VK_FORMAT_B8G8R8_SNORM: return "VK_FORMAT_B8G8R8_SNORM";
		case VK_FORMAT_B8G8R8_USCALED: return "VK_FORMAT_B8G8R8_USCALED";
		case VK_FORMAT_B8G8R8_SSCALED: return "VK_FORMAT_B8G8R8_SSCALED";
		case VK_FORMAT_B8G8R8_UINT: return "VK_FORMAT_B8G8R8_UINT";
		case VK_FORMAT_B8G8R8_SINT: return "VK_FORMAT_B8G8R8_SINT";
		case VK_FORMAT_B8G8R8_SRGB: return "VK_FORMAT_B8G8R8_SRGB";
		case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
		case VK_FORMAT_R8G8B8A8_SNORM: return "VK_FORMAT_R8G8B8A8_SNORM";
		case VK_FORMAT_R8G8B8A8_USCALED: return "VK_FORMAT_R8G8B8A8_USCALED";
		case VK_FORMAT_R8G8B8A8_SSCALED: return "VK_FORMAT_R8G8B8A8_SSCALED";
		case VK_FORMAT_R8G8B8A8_UINT: return "VK_FORMAT_R8G8B8A8_UINT";
		case VK_FORMAT_R8G8B8A8_SINT: return "VK_FORMAT_R8G8B8A8_SINT";
		case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
		case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
		case VK_FORMAT_B8G8R8A8_SNORM: return "VK_FORMAT_B8G8R8A8_SNORM";
		case VK_FORMAT_B8G8R8A8_USCALED: return "VK_FORMAT_B8G8R8A8_USCALED";
		case VK_FORMAT_B8G8R8A8_SSCALED: return "VK_FORMAT_B8G8R8A8_SSCALED";
		case VK_FORMAT_B8G8R8A8_UINT: return "VK_FORMAT_B8G8R8A8_UINT";
		case VK_FORMAT_B8G8R8A8_SINT: return "VK_FORMAT_B8G8R8A8_SINT";
		case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
		case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return "VK_FORMAT_A8B8G8R8_UNORM_PACK32";
		case VK_FORMAT_A8B8G8R8_SNORM_PACK32: return "VK_FORMAT_A8B8G8R8_SNORM_PACK32";
		case VK_FORMAT_A8B8G8R8_USCALED_PACK32: return "VK_FORMAT_A8B8G8R8_USCALED_PACK32";
		case VK_FORMAT_A8B8G8R8_SSCALED_PACK32: return "VK_FORMAT_A8B8G8R8_SSCALED_PACK32";
		case VK_FORMAT_A8B8G8R8_UINT_PACK32: return "VK_FORMAT_A8B8G8R8_UINT_PACK32";
		case VK_FORMAT_A8B8G8R8_SINT_PACK32: return "VK_FORMAT_A8B8G8R8_SINT_PACK32";
		case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return "VK_FORMAT_A8B8G8R8_SRGB_PACK32";
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
		case VK_FORMAT_A2R10G10B10_SNORM_PACK32: return "VK_FORMAT_A2R10G10B10_SNORM_PACK32";
		case VK_FORMAT_A2R10G10B10_USCALED_PACK32: return "VK_FORMAT_A2R10G10B10_USCALED_PACK32";
		case VK_FORMAT_A2R10G10B10_SSCALED_PACK32: return "VK_FORMAT_A2R10G10B10_SSCALED_PACK32";
		case VK_FORMAT_A2R10G10B10_UINT_PACK32: return "VK_FORMAT_A2R10G10B10_UINT_PACK32";
		case VK_FORMAT_A2R10G10B10_SINT_PACK32: return "VK_FORMAT_A2R10G10B10_SINT_PACK32";
		case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
		case VK_FORMAT_A2B10G10R10_SNORM_PACK32: return "VK_FORMAT_A2B10G10R10_SNORM_PACK32";
		case VK_FORMAT_A2B10G10R10_USCALED_PACK32: return "VK_FORMAT_A2B10G10R10_USCALED_PACK32";
		case VK_FORMAT_A2B10G10R10_SSCALED_PACK32: return "VK_FORMAT_A2B10G10R10_SSCALED_PACK32";
		case VK_FORMAT_A2B10G10R10_UINT_PACK32: return "VK_FORMAT_A2B10G10R10_UINT_PACK32";
		case VK_FORMAT_A2B10G10R10_SINT_PACK32: return "VK_FORMAT_A2B10G10R10_SINT_PACK32";
		case VK_FORMAT_R16_UNORM: return "VK_FORMAT_R16_UNORM";
		case VK_FORMAT_R16_SNORM: return "VK_FORMAT_R16_SNORM";
		case VK_FORMAT_R16_USCALED: return "VK_FORMAT_R16_USCALED";
		case VK_FORMAT_R16_SSCALED: return "VK_FORMAT_R16_SSCALED";
		case VK_FORMAT_R16_UINT: return "VK_FORMAT_R16_UINT";
		case VK_FORMAT_R16_SINT: return "VK_FORMAT_R16_SINT";
		case VK_FORMAT_R16_SFLOAT: return "VK_FORMAT_R16_SFLOAT";
		case VK_FORMAT_R16G16_UNORM: return "VK_FORMAT_R16G16_UNORM";
		case VK_FORMAT_R16G16_SNORM: return "VK_FORMAT_R16G16_SNORM";
		case VK_FORMAT_R16G16_USCALED: return "VK_FORMAT_R16G16_USCALED";
		case VK_FORMAT_R16G16_SSCALED: return "VK_FORMAT_R16G16_SSCALED";
		case VK_FORMAT_R16G16_UINT: return "VK_FORMAT_R16G16_UINT";
		case VK_FORMAT_R16G16_SINT: return "VK_FORMAT_R16G16_SINT";
		case VK_FORMAT_R16G16_SFLOAT: return "VK_FORMAT_R16G16_SFLOAT";
		case VK_FORMAT_R16G16B16_UNORM: return "VK_FORMAT_R16G16B16_UNORM";
		case VK_FORMAT_R16G16B16_SNORM: return "VK_FORMAT_R16G16B16_SNORM";
		case VK_FORMAT_R16G16B16_USCALED: return "VK_FORMAT_R16G16B16_USCALED";
		case VK_FORMAT_R16G16B16_SSCALED: return "VK_FORMAT_R16G16B16_SSCALED";
		case VK_FORMAT_R16G16B16_UINT: return "VK_FORMAT_R16G16B16_UINT";
		case VK_FORMAT_R16G16B16_SINT: return "VK_FORMAT_R16G16B16_SINT";
		case VK_FORMAT_R16G16B16_SFLOAT: return "VK_FORMAT_R16G16B16_SFLOAT";
		case VK_FORMAT_R16G16B16A16_UNORM: return "VK_FORMAT_R16G16B16A16_UNORM";
		case VK_FORMAT_R16G16B16A16_SNORM: return "VK_FORMAT_R16G16B16A16_SNORM";
		case VK_FORMAT_R16G16B16A16_USCALED: return "VK_FORMAT_R16G16B16A16_USCALED";
		case VK_FORMAT_R16G16B16A16_SSCALED: return "VK_FORMAT_R16G16B16A16_SSCALED";
		case VK_FORMAT_R16G16B16A16_UINT: return "VK_FORMAT_R16G16B16A16_UINT";
		case VK_FORMAT_R16G16B16A16_SINT: return "VK_FORMAT_R16G16B16A16_SINT";
		case VK_FORMAT_R16G16B16A16_SFLOAT: return "VK_FORMAT_R16G16B16A16_SFLOAT";
		case VK_FORMAT_R32_UINT: return "VK_FORMAT_R32_UINT";
		case VK_FORMAT_R32_SINT: return "VK_FORMAT_R32_SINT";
		case VK_FORMAT_R32_SFLOAT: return "VK_FORMAT_R32_SFLOAT";
		case VK_FORMAT_R32G32_UINT: return "VK_FORMAT_R32G32_UINT";
		case VK_FORMAT_R32G32_SINT: return "VK_FORMAT_R32G32_SINT";
		case VK_FORMAT_R32G32_SFLOAT: return "VK_FORMAT_R32G32_SFLOAT";
		case VK_FORMAT_R32G32B32_UINT: return "VK_FORMAT_R32G32B32_UINT";
		case VK_FORMAT_R32G32B32_SINT: return "VK_FORMAT_R32G32B32_SINT";
		case VK_FORMAT_R32G32B32_SFLOAT: return "VK_FORMAT_R32G32B32_SFLOAT";
		case VK_FORMAT_R32G32B32A32_UINT: return "VK_FORMAT_R32G32B32A32_UINT";
		case VK_FORMAT_R32G32B32A32_SINT: return "VK_FORMAT_R32G32B32A32_SINT";
		case VK_FORMAT_R32G32B32A32_SFLOAT: return "VK_FORMAT_R32G32B32A32_SFLOAT";
		case VK_FORMAT_R64_UINT: return "VK_FORMAT_R64_UINT";
		case VK_FORMAT_R64_SINT: return "VK_FORMAT_R64_SINT";
		case VK_FORMAT_R64_SFLOAT: return "VK_FORMAT_R64_SFLOAT";
		case VK_FORMAT_R64G64_UINT: return "VK_FORMAT_R64G64_UINT";
		case VK_FORMAT_R64G64_SINT: return "VK_FORMAT_R64G64_SINT";
		case VK_FORMAT_R64G64_SFLOAT: return "VK_FORMAT_R64G64_SFLOAT";
		case VK_FORMAT_R64G64B64_UINT: return "VK_FORMAT_R64G64B64_UINT";
		case VK_FORMAT_R64G64B64_SINT: return "VK_FORMAT_R64G64B64_SINT";
		case VK_FORMAT_R64G64B64_SFLOAT: return "VK_FORMAT_R64G64B64_SFLOAT";
		case VK_FORMAT_R64G64B64A64_UINT: return "VK_FORMAT_R64G64B64A64_UINT";
		case VK_FORMAT_R64G64B64A64_SINT: return "VK_FORMAT_R64G64B64A64_SINT";
		case VK_FORMAT_R64G64B64A64_SFLOAT: return "VK_FORMAT_R64G64B64A64_SFLOAT";
		case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return "VK_FORMAT_B10G11R11_UFLOAT_PACK32";
		case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return "VK_FORMAT_E5B9G9R9_UFLOAT_PACK32";
		case VK_FORMAT_D16_UNORM: return "VK_FORMAT_D16_UNORM";
		case VK_FORMAT_X8_D24_UNORM_PACK32: return "VK_FORMAT_X8_D24_UNORM_PACK32";
		case VK_FORMAT_D32_SFLOAT: return "VK_FORMAT_D32_SFLOAT";
		case VK_FORMAT_S8_UINT: return "VK_FORMAT_S8_UINT";
		case VK_FORMAT_D16_UNORM_S8_UINT: return "VK_FORMAT_D16_UNORM_S8_UINT";
		case VK_FORMAT_D24_UNORM_S8_UINT: return "VK_FORMAT_D24_UNORM_S8_UINT";
		case VK_FORMAT_D32_SFLOAT_S8_UINT: return "VK_FORMAT_D32_SFLOAT_S8_UINT";
		case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return "VK_FORMAT_BC1_RGB_UNORM_BLOCK";
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return "VK_FORMAT_BC1_RGB_SRGB_BLOCK";
		case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return "VK_FORMAT_BC1_RGBA_UNORM_BLOCK";
		case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return "VK_FORMAT_BC1_RGBA_SRGB_BLOCK";
		case VK_FORMAT_BC2_UNORM_BLOCK: return "VK_FORMAT_BC2_UNORM_BLOCK";
		case VK_FORMAT_BC2_SRGB_BLOCK: return "VK_FORMAT_BC2_SRGB_BLOCK";
		case VK_FORMAT_BC3_UNORM_BLOCK: return "VK_FORMAT_BC3_UNORM_BLOCK";
		case VK_FORMAT_BC3_SRGB_BLOCK: return "VK_FORMAT_BC3_SRGB_BLOCK";
		case VK_FORMAT_BC4_UNORM_BLOCK: return "VK_FORMAT_BC4_UNORM_BLOCK";
		case VK_FORMAT_BC4_SNORM_BLOCK: return "VK_FORMAT_BC4_SNORM_BLOCK";
		case VK_FORMAT_BC5_UNORM_BLOCK: return "VK_FORMAT_BC5_UNORM_BLOCK";
		case VK_FORMAT_BC5_SNORM_BLOCK: return "VK_FORMAT_BC5_SNORM_BLOCK";
		case VK_FORMAT_BC6H_UFLOAT_BLOCK: return "VK_FORMAT_BC6H_UFLOAT_BLOCK";
		case VK_FORMAT_BC6H_SFLOAT_BLOCK: return "VK_FORMAT_BC6H_SFLOAT_BLOCK";
		case VK_FORMAT_BC7_UNORM_BLOCK: return "VK_FORMAT_BC7_UNORM_BLOCK";
		case VK_FORMAT_BC7_SRGB_BLOCK: return "VK_FORMAT_BC7_SRGB_BLOCK";
		case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK";
		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK";
		case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK";
		case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK";
		case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK";
		case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: return "VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK";
		case VK_FORMAT_EAC_R11_UNORM_BLOCK: return "VK_FORMAT_EAC_R11_UNORM_BLOCK";
		case VK_FORMAT_EAC_R11_SNORM_BLOCK: return "VK_FORMAT_EAC_R11_SNORM_BLOCK";
		case VK_FORMAT_EAC_R11G11_UNORM_BLOCK: return "VK_FORMAT_EAC_R11G11_UNORM_BLOCK";
		case VK_FORMAT_EAC_R11G11_SNORM_BLOCK: return "VK_FORMAT_EAC_R11G11_SNORM_BLOCK";
		case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: return "VK_FORMAT_ASTC_4x4_UNORM_BLOCK";
		case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return "VK_FORMAT_ASTC_4x4_SRGB_BLOCK";
		case VK_FORMAT_ASTC_5x4_UNORM_BLOCK: return "VK_FORMAT_ASTC_5x4_UNORM_BLOCK";
		case VK_FORMAT_ASTC_5x4_SRGB_BLOCK: return "VK_FORMAT_ASTC_5x4_SRGB_BLOCK";
		case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_5x5_UNORM_BLOCK";
		case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_5x5_SRGB_BLOCK";
		case VK_FORMAT_ASTC_6x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_6x5_UNORM_BLOCK";
		case VK_FORMAT_ASTC_6x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_6x5_SRGB_BLOCK";
		case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_6x6_UNORM_BLOCK";
		case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_6x6_SRGB_BLOCK";
		case VK_FORMAT_ASTC_8x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x5_UNORM_BLOCK";
		case VK_FORMAT_ASTC_8x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x5_SRGB_BLOCK";
		case VK_FORMAT_ASTC_8x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x6_UNORM_BLOCK";
		case VK_FORMAT_ASTC_8x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x6_SRGB_BLOCK";
		case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: return "VK_FORMAT_ASTC_8x8_UNORM_BLOCK";
		case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return "VK_FORMAT_ASTC_8x8_SRGB_BLOCK";
		case VK_FORMAT_ASTC_10x5_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x5_UNORM_BLOCK";
		case VK_FORMAT_ASTC_10x5_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x5_SRGB_BLOCK";
		case VK_FORMAT_ASTC_10x6_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x6_UNORM_BLOCK";
		case VK_FORMAT_ASTC_10x6_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x6_SRGB_BLOCK";
		case VK_FORMAT_ASTC_10x8_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x8_UNORM_BLOCK";
		case VK_FORMAT_ASTC_10x8_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x8_SRGB_BLOCK";
		case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: return "VK_FORMAT_ASTC_10x10_UNORM_BLOCK";
		case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: return "VK_FORMAT_ASTC_10x10_SRGB_BLOCK";
		case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: return "VK_FORMAT_ASTC_12x10_UNORM_BLOCK";
		case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: return "VK_FORMAT_ASTC_12x10_SRGB_BLOCK";
		case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: return "VK_FORMAT_ASTC_12x12_UNORM_BLOCK";
		case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: return "VK_FORMAT_ASTC_12x12_SRGB_BLOCK";
		default: return "VK_FORMAT_UNKNOWN";
	}
}

static format_storage_info storage_info(VkFormat format)
{
	const int value = static_cast<int>(format);

	if (value == 1) return { 1, 1, 1 };
	if ((value >= 2 && value <= 8) || (value >= 16 && value <= 22) || (value >= 70 && value <= 76) || value == 124) return { 1, 1, 2 };
	if ((value >= 9 && value <= 15) || value == 127) return { 1, 1, 1 };
	if (value >= 23 && value <= 36) return { 1, 1, 3 };
	if ((value >= 37 && value <= 69) || (value >= 77 && value <= 83) || (value >= 98 && value <= 100) ||
	    value == 122 || value == 123 || value == 125 || value == 126 || value == 129) return { 1, 1, 4 };
	if (value >= 84 && value <= 90) return { 1, 1, 6 };
	if ((value >= 91 && value <= 97) || (value >= 101 && value <= 103) || (value >= 110 && value <= 112)) return { 1, 1, 8 };
	if ((value >= 104 && value <= 106)) return { 1, 1, 12 };
	if ((value >= 107 && value <= 109) || (value >= 113 && value <= 115)) return { 1, 1, 16 };
	if (value >= 116 && value <= 118) return { 1, 1, 24 };
	if (value >= 119 && value <= 121) return { 1, 1, 32 };
	if (value == 130) return { 1, 1, 5 };
	if (value == 128) return { 1, 1, 3 };

	if (format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK) return { 4, 4, 8 };
	if (format >= VK_FORMAT_BC2_UNORM_BLOCK && format <= VK_FORMAT_BC3_SRGB_BLOCK) return { 4, 4, 16 };
	if (format >= VK_FORMAT_BC4_UNORM_BLOCK && format <= VK_FORMAT_BC4_SNORM_BLOCK) return { 4, 4, 8 };
	if (format >= VK_FORMAT_BC5_UNORM_BLOCK && format <= VK_FORMAT_BC7_SRGB_BLOCK) return { 4, 4, 16 };
	if (format >= VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK && format <= VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK) return { 4, 4, 8 };
	if (format >= VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK && format <= VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK) return { 4, 4, 16 };
	if (format >= VK_FORMAT_EAC_R11_UNORM_BLOCK && format <= VK_FORMAT_EAC_R11_SNORM_BLOCK) return { 4, 4, 8 };
	if (format >= VK_FORMAT_EAC_R11G11_UNORM_BLOCK && format <= VK_FORMAT_EAC_R11G11_SNORM_BLOCK) return { 4, 4, 16 };

	switch (format)
	{
		case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
		case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: return { 4, 4, 16 };
		case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
		case VK_FORMAT_ASTC_5x4_SRGB_BLOCK: return { 5, 4, 16 };
		case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: return { 5, 5, 16 };
		case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_6x5_SRGB_BLOCK: return { 6, 5, 16 };
		case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
		case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: return { 6, 6, 16 };
		case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_8x5_SRGB_BLOCK: return { 8, 5, 16 };
		case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
		case VK_FORMAT_ASTC_8x6_SRGB_BLOCK: return { 8, 6, 16 };
		case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
		case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: return { 8, 8, 16 };
		case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x5_SRGB_BLOCK: return { 10, 5, 16 };
		case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x6_SRGB_BLOCK: return { 10, 6, 16 };
		case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x8_SRGB_BLOCK: return { 10, 8, 16 };
		case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
		case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: return { 10, 10, 16 };
		case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
		case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: return { 12, 10, 16 };
		case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
		case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: return { 12, 12, 16 };
		default: return {};
	}
}

static uint64_t naive_size(uint32_t width, uint32_t height, const format_storage_info& info)
{
	assert(width > 0);
	assert(height > 0);
	assert(info.block_width > 0);
	assert(info.block_height > 0);
	assert(info.bytes_per_block > 0);
	const uint64_t blocks_x = (static_cast<uint64_t>(width) + info.block_width - 1) / info.block_width;
	const uint64_t blocks_y = (static_cast<uint64_t>(height) + info.block_height - 1) / info.block_height;
	return blocks_x * blocks_y * info.bytes_per_block;
}

int main(int argc, char** argv)
{
	vulkan_req_t reqs;
	reqs.apiVersion = VK_API_VERSION_1_1;
	reqs.minApiVersion = VK_API_VERSION_1_1;
	reqs.maxApiVersion = VK_API_VERSION_1_3;
	reqs.device_extensions.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
	reqs.usage = show_usage;
	reqs.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_format_query", reqs);
	MAKEDEVICEPROCADDR(vulkan, vkGetDeviceImageMemoryRequirementsKHR);

	assert(image_width > 0);
	assert(image_height > 0);

	const VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkExtent3D extent = { image_width, image_height, 1 };

	printf("Querying 2D optimal images: %ux%u, 1 mip, 1 layer, 1 sample, usage sampled|transfer_src|transfer_dst\n",
	       image_width, image_height);
	printf("%-48s %16s %16s %10s %10s\n", "format", "vk_bytes", "naive_bytes", "% naive", "alignment");

	uint32_t supported = 0;
	uint32_t measured = 0;
	uint32_t unsupported = 0;
	uint32_t too_small = 0;

	bench_start_iteration(vulkan.bench);

	for (int format_value = VK_FORMAT_R4G4_UNORM_PACK8; format_value <= VK_FORMAT_ASTC_12x12_SRGB_BLOCK; ++format_value)
	{
		const VkFormat format = static_cast<VkFormat>(format_value);
		const format_storage_info info = storage_info(format);
		assert(info.bytes_per_block > 0);

		VkPhysicalDeviceImageFormatInfo2 format_info = {
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2, nullptr,
			format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0
		};
		VkImageFormatProperties2 format_properties = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, nullptr };
		VkResult result = vkGetPhysicalDeviceImageFormatProperties2(vulkan.physical, &format_info, &format_properties);
		if (result == VK_ERROR_FORMAT_NOT_SUPPORTED)
		{
			unsupported++;
			if (verbose_skips) printf("%-48s unsupported for selected usage\n", format_name(format));
			continue;
		}
		check(result);
		supported++;

		if (format_properties.imageFormatProperties.maxExtent.width < image_width ||
		    format_properties.imageFormatProperties.maxExtent.height < image_height)
		{
			too_small++;
			if (verbose_skips) printf("%-48s max extent %ux%u is smaller than requested image\n", format_name(format),
			                         format_properties.imageFormatProperties.maxExtent.width,
			                         format_properties.imageFormatProperties.maxExtent.height);
			continue;
		}

		VkImageCreateInfo create_info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
		create_info.imageType = VK_IMAGE_TYPE_2D;
		create_info.format = format;
		create_info.extent = extent;
		create_info.mipLevels = 1;
		create_info.arrayLayers = 1;
		create_info.samples = VK_SAMPLE_COUNT_1_BIT;
		create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
		create_info.usage = usage;
		create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkDeviceImageMemoryRequirementsKHR memory_info = { VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS_KHR, nullptr };
		memory_info.pCreateInfo = &create_info;
		memory_info.planeAspect = static_cast<VkImageAspectFlagBits>(0);
		VkMemoryRequirements2KHR requirements = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR, nullptr };
		pf_vkGetDeviceImageMemoryRequirementsKHR(vulkan.device, &memory_info, &requirements);

		const uint64_t naive_bytes = naive_size(image_width, image_height, info);
		const double percent = (100.0 * static_cast<double>(requirements.memoryRequirements.size)) / static_cast<double>(naive_bytes);
		printf("%-48s %16" PRIu64 " %16" PRIu64 " %9.2f%% %10" PRIu64 "\n",
		       format_name(format),
		       static_cast<uint64_t>(requirements.memoryRequirements.size),
		       naive_bytes,
		       percent,
		       static_cast<uint64_t>(requirements.memoryRequirements.alignment));
		measured++;
	}

	bench_stop_iteration(vulkan.bench);

	printf("Measured %u formats, unsupported %u, skipped due to max extent %u, supported for selected usage %u\n",
	       measured, unsupported, too_small, supported);

	test_done(vulkan);
	return 0;
}
