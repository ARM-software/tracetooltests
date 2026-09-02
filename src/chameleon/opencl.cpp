#include "opencl_defs.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include <CL/cl_ext.h>

#include "json_util.h"
#include "opencl_auto.h"

#ifndef CHAMELEON_DEFAULT_OPENCL_GPU_PATH
#define CHAMELEON_DEFAULT_OPENCL_GPU_PATH "share/chameleon/devices/Mali-G715"
#endif

#define CL_EXPORT extern "C" __attribute__((visibility("default")))
#define CL_ENTRY(name) ++count_##name

struct OpenCLRuntime
{
	cl_icd_dispatch dispatch = {};
	_cl_platform_id platform;
	_cl_device_id device;
	Json::Value profile;
	Json::Value platform_properties;
	Json::Value device_properties;
	Json::Value platform_extensions;
	Json::Value device_extensions;
	std::string platform_extension_string;
	std::string device_extension_string;
};

static OpenCLRuntime* opencl_runtime = nullptr;
static std::once_flag opencl_runtime_once;

static void require_properties(const Json::Value& properties, const char* const* names, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (!properties.isMember(names[i]))
		{
			fprintf(stderr, "OpenCL profile is missing required property %s\n", names[i]);
			exit(1);
		}
	}
}

static bool unsupported_extension(const std::string& name)
{
	return !supported_opencl_extension(name.c_str());
}

static std::string join_extensions(const Json::Value& extensions, bool require_icd)
{
	std::vector<std::string> names = extensions.getMemberNames();
	names.erase(std::remove_if(names.begin(), names.end(), unsupported_extension), names.end());
	if (require_icd && std::find(names.begin(), names.end(), "cl_khr_icd") == names.end())
	{
		names.push_back("cl_khr_icd");
	}
	std::sort(names.begin(), names.end());
	std::string result;
	for (const std::string& name : names)
	{
		if (!result.empty()) result += " ";
		result += name;
	}
	return result;
}

static void initialize_runtime()
{
	opencl_runtime = new OpenCLRuntime();
	initialize_opencl_dispatch(opencl_runtime->dispatch);
	opencl_runtime->platform.dispatch = &opencl_runtime->dispatch;
	opencl_runtime->device.dispatch = &opencl_runtime->dispatch;

	const char* gpu_path = getenv("CHAMELEON_GPU");
	if (!gpu_path || !gpu_path[0]) gpu_path = CHAMELEON_DEFAULT_OPENCL_GPU_PATH;
	std::string profile_path = std::string(gpu_path) + "/opencl.json";
	opencl_runtime->profile = readJson(profile_path);
	assert(opencl_runtime->profile.isMember("capabilities"));
	assert(opencl_runtime->profile["capabilities"].isMember("platform"));
	assert(opencl_runtime->profile["capabilities"].isMember("device"));
	const Json::Value& platform = opencl_runtime->profile["capabilities"]["platform"];
	const Json::Value& device = opencl_runtime->profile["capabilities"]["device"];
	assert(platform["properties"].isObject());
	assert(platform["extensions"].isObject());
	assert(device["properties"].isObject());
	assert(device["extensions"].isObject());
	opencl_runtime->platform_properties = platform["properties"];
	opencl_runtime->device_properties = device["properties"];
	opencl_runtime->platform_extensions = platform["extensions"];
	opencl_runtime->device_extensions = device["extensions"];
	assert(device["imageFormats"].isArray());
	const char* const required_platform_properties[] = {
		"CL_PLATFORM_PROFILE", "CL_PLATFORM_VERSION", "CL_PLATFORM_NAME", "CL_PLATFORM_VENDOR", "CL_PLATFORM_HOST_TIMER_RESOLUTION"
	};
	const char* const required_device_properties[] = {
		"CL_DEVICE_NAME", "CL_DEVICE_VENDOR", "CL_DEVICE_VENDOR_ID", "CL_DEVICE_VERSION", "CL_DEVICE_NUMERIC_VERSION",
		"CL_DEVICE_AVAILABLE", "CL_DEVICE_MAX_COMPUTE_UNITS", "CL_DEVICE_MAX_WORK_GROUP_SIZE"
	};
	require_properties(opencl_runtime->platform_properties, required_platform_properties,
		sizeof(required_platform_properties) / sizeof(required_platform_properties[0]));
	require_properties(opencl_runtime->device_properties, required_device_properties,
		sizeof(required_device_properties) / sizeof(required_device_properties[0]));
	opencl_runtime->platform_extension_string = join_extensions(opencl_runtime->platform_extensions, true);
	opencl_runtime->device_extension_string = join_extensions(opencl_runtime->device_extensions, false);
}

static OpenCLRuntime& runtime()
{
	std::call_once(opencl_runtime_once, initialize_runtime);
	return *opencl_runtime;
}

template<typename T>
static bool valid_object(const T* object)
{
	return object && object->magic == CHAMELEON_OPENCL_MAGIC && object->dispatch == &runtime().dispatch;
}

static cl_int copy_info(const void* source, size_t source_size, size_t value_size, void* value, size_t* value_size_ret)
{
	if (value_size_ret) *value_size_ret = source_size;
	if (!value) return CL_SUCCESS;
	if (value_size < source_size) return CL_INVALID_VALUE;
	memcpy(value, source, source_size);
	return CL_SUCCESS;
}

template<typename T>
static cl_int copy_scalar(T source, size_t value_size, void* value, size_t* value_size_ret)
{
	return copy_info(&source, sizeof(source), value_size, value, value_size_ret);
}

static cl_int copy_string(const std::string& source, size_t value_size, void* value, size_t* value_size_ret)
{
	return copy_info(source.c_str(), source.size() + 1, value_size, value, value_size_ret);
}

static cl_version parse_version(const std::string& value)
{
	unsigned major = 0;
	unsigned minor = 0;
	unsigned patch = 0;
	const int matched = sscanf(value.c_str(), "%u.%u.%u", &major, &minor, &patch);
	assert(matched >= 2);
	(void)matched;
	return CL_MAKE_VERSION(major, minor, patch);
}

static cl_version version_from_text(const std::string& value)
{
	size_t start = value.find("OpenCL ");
	assert(start != std::string::npos);
	return parse_version(value.substr(start + 7));
}

static std::vector<cl_name_version> extension_versions(const Json::Value& extensions)
{
	std::vector<cl_name_version> result;
	for (const std::string& name : extensions.getMemberNames())
	{
		if (!supported_opencl_extension(name.c_str())) continue;
		cl_name_version entry = {};
		entry.version = parse_version(extensions[name].asString());
		strncpy(entry.name, name.c_str(), CL_NAME_VERSION_MAX_NAME_SIZE - 1);
		result.push_back(entry);
	}
	return result;
}

static std::vector<cl_name_version> named_versions(const Json::Value& values)
{
	std::vector<cl_name_version> result;
	for (const Json::Value& value : values)
	{
		std::string text = value.asString();
		size_t separator = text.rfind(' ');
		assert(separator != std::string::npos);
		cl_name_version entry = {};
		entry.version = parse_version(text.substr(separator + 1));
		strncpy(entry.name, text.substr(0, separator).c_str(), CL_NAME_VERSION_MAX_NAME_SIZE - 1);
		result.push_back(entry);
	}
	return result;
}

static cl_ulong flag_value(const std::string& name)
{
	if (name == "CL_FP_DENORM") return CL_FP_DENORM;
	if (name == "CL_FP_INF_NAN") return CL_FP_INF_NAN;
	if (name == "CL_FP_ROUND_TO_NEAREST") return CL_FP_ROUND_TO_NEAREST;
	if (name == "CL_FP_ROUND_TO_ZERO") return CL_FP_ROUND_TO_ZERO;
	if (name == "CL_FP_ROUND_TO_INF") return CL_FP_ROUND_TO_INF;
	if (name == "CL_FP_FMA") return CL_FP_FMA;
	if (name == "CL_FP_SOFT_FLOAT") return CL_FP_SOFT_FLOAT;
	if (name == "CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT") return CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT;
	if (name == "CL_EXEC_KERNEL") return CL_EXEC_KERNEL;
	if (name == "CL_EXEC_NATIVE_KERNEL") return CL_EXEC_NATIVE_KERNEL;
	if (name == "CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE") return CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;
	if (name == "CL_QUEUE_PROFILING_ENABLE") return CL_QUEUE_PROFILING_ENABLE;
	if (name == "CL_DEVICE_SVM_COARSE_GRAIN_BUFFER") return CL_DEVICE_SVM_COARSE_GRAIN_BUFFER;
	if (name == "CL_DEVICE_SVM_FINE_GRAIN_BUFFER") return CL_DEVICE_SVM_FINE_GRAIN_BUFFER;
	if (name == "CL_DEVICE_SVM_FINE_GRAIN_SYSTEM") return CL_DEVICE_SVM_FINE_GRAIN_SYSTEM;
	if (name == "CL_DEVICE_SVM_ATOMICS") return CL_DEVICE_SVM_ATOMICS;
	if (name == "CL_DEVICE_ATOMIC_ORDER_RELAXED") return CL_DEVICE_ATOMIC_ORDER_RELAXED;
	if (name == "CL_DEVICE_ATOMIC_ORDER_ACQ_REL") return CL_DEVICE_ATOMIC_ORDER_ACQ_REL;
	if (name == "CL_DEVICE_ATOMIC_ORDER_SEQ_CST") return CL_DEVICE_ATOMIC_ORDER_SEQ_CST;
	if (name == "CL_DEVICE_ATOMIC_SCOPE_WORK_ITEM") return CL_DEVICE_ATOMIC_SCOPE_WORK_ITEM;
	if (name == "CL_DEVICE_ATOMIC_SCOPE_WORK_GROUP") return CL_DEVICE_ATOMIC_SCOPE_WORK_GROUP;
	if (name == "CL_DEVICE_ATOMIC_SCOPE_DEVICE") return CL_DEVICE_ATOMIC_SCOPE_DEVICE;
	if (name == "CL_DEVICE_ATOMIC_SCOPE_ALL_DEVICES") return CL_DEVICE_ATOMIC_SCOPE_ALL_DEVICES;
	if (name == "CL_DEVICE_QUEUE_SUPPORTED") return CL_DEVICE_QUEUE_SUPPORTED;
	if (name == "CL_DEVICE_QUEUE_REPLACEABLE_DEFAULT") return CL_DEVICE_QUEUE_REPLACEABLE_DEFAULT;
	return 0;
}

static cl_uint image_enum(const std::string& name)
{
	if (name == "CL_MEM_OBJECT_IMAGE1D") return CL_MEM_OBJECT_IMAGE1D;
	if (name == "CL_MEM_OBJECT_IMAGE1D_ARRAY") return CL_MEM_OBJECT_IMAGE1D_ARRAY;
	if (name == "CL_MEM_OBJECT_IMAGE1D_BUFFER") return CL_MEM_OBJECT_IMAGE1D_BUFFER;
	if (name == "CL_MEM_OBJECT_IMAGE2D") return CL_MEM_OBJECT_IMAGE2D;
	if (name == "CL_MEM_OBJECT_IMAGE2D_ARRAY") return CL_MEM_OBJECT_IMAGE2D_ARRAY;
	if (name == "CL_MEM_OBJECT_IMAGE3D") return CL_MEM_OBJECT_IMAGE3D;
	if (name == "CL_A") return CL_A;
	if (name == "CL_ABGR") return CL_ABGR;
	if (name == "CL_BGRA") return CL_BGRA;
	if (name == "CL_DEPTH") return CL_DEPTH;
	if (name == "CL_INTENSITY") return CL_INTENSITY;
	if (name == "CL_LUMINANCE") return CL_LUMINANCE;
	if (name == "CL_R") return CL_R;
	if (name == "CL_RG") return CL_RG;
	if (name == "CL_RGB") return CL_RGB;
	if (name == "CL_RGBA") return CL_RGBA;
	if (name == "CL_sBGRA") return CL_sBGRA;
	if (name == "CL_sRGB") return CL_sRGB;
	if (name == "CL_sRGBA") return CL_sRGBA;
	if (name == "CL_FLOAT") return CL_FLOAT;
	if (name == "CL_HALF_FLOAT") return CL_HALF_FLOAT;
	if (name == "CL_SIGNED_INT16") return CL_SIGNED_INT16;
	if (name == "CL_SIGNED_INT32") return CL_SIGNED_INT32;
	if (name == "CL_SIGNED_INT8") return CL_SIGNED_INT8;
	if (name == "CL_SNORM_INT16") return CL_SNORM_INT16;
	if (name == "CL_SNORM_INT8") return CL_SNORM_INT8;
	if (name == "CL_UNORM_INT16") return CL_UNORM_INT16;
	if (name == "CL_UNORM_INT8") return CL_UNORM_INT8;
	if (name == "CL_UNORM_INT_101010") return CL_UNORM_INT_101010;
	if (name == "CL_UNORM_INT_101010_2") return CL_UNORM_INT_101010_2;
	if (name == "CL_UNORM_SHORT_555") return CL_UNORM_SHORT_555;
	if (name == "CL_UNORM_SHORT_565") return CL_UNORM_SHORT_565;
	if (name == "CL_UNSIGNED_INT16") return CL_UNSIGNED_INT16;
	if (name == "CL_UNSIGNED_INT32") return CL_UNSIGNED_INT32;
	if (name == "CL_UNSIGNED_INT8") return CL_UNSIGNED_INT8;
	return 0;
}

static cl_ulong json_flags(const Json::Value& value)
{
	if (value.isUInt64()) return value.asUInt64();
	cl_ulong result = 0;
	if (value.isArray())
	{
		for (const Json::Value& entry : value) result |= flag_value(entry.asString());
	}
	return result;
}

static cl_int copy_json_uint(const Json::Value& value, size_t value_size, void* output, size_t* output_size)
{
	assert(value.isNumeric());
	return copy_scalar<cl_uint>(value.asUInt(), value_size, output, output_size);
}

static cl_int copy_json_ulong(const Json::Value& value, size_t value_size, void* output, size_t* output_size)
{
	assert(value.isNumeric());
	return copy_scalar<cl_ulong>(value.asUInt64(), value_size, output, output_size);
}

static cl_int copy_json_size(const Json::Value& value, size_t value_size, void* output, size_t* output_size)
{
	assert(value.isNumeric());
	return copy_scalar<size_t>(value.asUInt64(), value_size, output, output_size);
}

static cl_int copy_json_bool(const Json::Value& value, size_t value_size, void* output, size_t* output_size)
{
	assert(value.isBool());
	return copy_scalar<cl_bool>(value.asBool() ? CL_TRUE : CL_FALSE, value_size, output, output_size);
}

static cl_int copy_json_string(const Json::Value& value, size_t value_size, void* output, size_t* output_size)
{
	assert(value.isString());
	return copy_string(value.asString(), value_size, output, output_size);
}

static cl_int copy_uuid(const Json::Value& value, size_t value_size, void* output, size_t* output_size)
{
	assert(value.isString());
	std::string text;
	for (char c : value.asString()) if (c != '-') text += c;
	assert(text.size() == CL_UUID_SIZE_KHR * 2);
	cl_uchar bytes[CL_UUID_SIZE_KHR] = {};
	for (size_t i = 0; i < CL_UUID_SIZE_KHR; ++i)
	{
		unsigned byte = 0;
		const int matched = sscanf(text.substr(i * 2, 2).c_str(), "%02x", &byte);
		assert(matched == 1);
		(void)matched;
		bytes[i] = static_cast<cl_uchar>(byte);
	}
	return copy_info(bytes, sizeof(bytes), value_size, output, output_size);
}

static cl_int validate_event_wait_list(cl_uint count, const cl_event* events)
{
	if ((count == 0) != (events == nullptr)) return CL_INVALID_EVENT_WAIT_LIST;
	for (cl_uint i = 0; i < count; ++i) if (!valid_object(events[i])) return CL_INVALID_EVENT_WAIT_LIST;
	return CL_SUCCESS;
}

static cl_event create_complete_event(cl_command_queue queue, cl_command_type command_type, cl_event* output)
{
	if (!output) return nullptr;
	_cl_event* event = new _cl_event();
	event->dispatch = &runtime().dispatch;
	event->queue = queue;
	event->command_type = command_type;
	++queue->references;
	*output = event;
	return event;
}

CL_EXPORT cl_int CL_API_CALL clIcdGetPlatformIDsKHR(cl_uint num_entries, cl_platform_id* platforms, cl_uint* num_platforms)
{
	return clGetPlatformIDs(num_entries, platforms, num_platforms);
}

CL_EXPORT cl_int CL_API_CALL clGetPlatformIDs(cl_uint num_entries, cl_platform_id* platforms, cl_uint* num_platforms)
{
	CL_ENTRY(clGetPlatformIDs);
	if ((!platforms && !num_platforms) || (platforms && num_entries == 0)) return CL_INVALID_VALUE;
	if (num_platforms) *num_platforms = 1;
	if (platforms) platforms[0] = &runtime().platform;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetPlatformInfo(cl_platform_id platform, cl_platform_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetPlatformInfo);
	OpenCLRuntime& rt = runtime();
	if (platform != &rt.platform) return CL_INVALID_PLATFORM;
	const Json::Value& properties = rt.platform_properties;
	switch (param_name)
	{
	case CL_PLATFORM_PROFILE: return copy_json_string(properties["CL_PLATFORM_PROFILE"], value_size, value, value_size_ret);
	case CL_PLATFORM_VERSION: return copy_json_string(properties["CL_PLATFORM_VERSION"], value_size, value, value_size_ret);
	case CL_PLATFORM_NAME: return copy_json_string(properties["CL_PLATFORM_NAME"], value_size, value, value_size_ret);
	case CL_PLATFORM_VENDOR: return copy_json_string(properties["CL_PLATFORM_VENDOR"], value_size, value, value_size_ret);
	case CL_PLATFORM_EXTENSIONS: return copy_string(rt.platform_extension_string, value_size, value, value_size_ret);
	case CL_PLATFORM_HOST_TIMER_RESOLUTION: return copy_json_ulong(properties["CL_PLATFORM_HOST_TIMER_RESOLUTION"], value_size, value, value_size_ret);
	case CL_PLATFORM_NUMERIC_VERSION:
		return copy_scalar<cl_version>(version_from_text(properties["CL_PLATFORM_VERSION"].asString()), value_size, value, value_size_ret);
	case CL_PLATFORM_EXTENSIONS_WITH_VERSION:
	{
		std::vector<cl_name_version> versions = extension_versions(rt.platform_extensions);
		return copy_info(versions.data(), versions.size() * sizeof(cl_name_version), value_size, value, value_size_ret);
	}
	case CL_PLATFORM_ICD_SUFFIX_KHR: return copy_string("CHAMELEON", value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clGetDeviceIDs(cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id* devices, cl_uint* num_devices)
{
	CL_ENTRY(clGetDeviceIDs);
	OpenCLRuntime& rt = runtime();
	if (platform != &rt.platform) return CL_INVALID_PLATFORM;
	if ((!devices && !num_devices) || (devices && num_entries == 0)) return CL_INVALID_VALUE;
	if (device_type == 0) return CL_INVALID_DEVICE_TYPE;
	if (device_type != CL_DEVICE_TYPE_ALL && !(device_type & (CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_DEFAULT)))
	{
		if (num_devices) *num_devices = 0;
		return CL_DEVICE_NOT_FOUND;
	}
	if (num_devices) *num_devices = 1;
	if (devices) devices[0] = &rt.device;
	return CL_SUCCESS;
}

#define DEVICE_UINT_PROPERTY(cl_name) case cl_name: return copy_json_uint(properties[#cl_name], value_size, value, value_size_ret)
#define DEVICE_ULONG_PROPERTY(cl_name) case cl_name: return copy_json_ulong(properties[#cl_name], value_size, value, value_size_ret)
#define DEVICE_SIZE_PROPERTY(cl_name) case cl_name: return copy_json_size(properties[#cl_name], value_size, value, value_size_ret)
#define DEVICE_BOOL_PROPERTY(cl_name) case cl_name: return copy_json_bool(properties[#cl_name], value_size, value, value_size_ret)
#define DEVICE_STRING_PROPERTY(cl_name) case cl_name: return copy_json_string(properties[#cl_name], value_size, value, value_size_ret)

CL_EXPORT cl_int CL_API_CALL clGetDeviceInfo(cl_device_id device, cl_device_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetDeviceInfo);
	OpenCLRuntime& rt = runtime();
	if (device != &rt.device) return CL_INVALID_DEVICE;
	const Json::Value& properties = rt.device_properties;
	switch (param_name)
	{
	case CL_DEVICE_TYPE: return copy_scalar<cl_device_type>(CL_DEVICE_TYPE_GPU, value_size, value, value_size_ret);
	DEVICE_UINT_PROPERTY(CL_DEVICE_VENDOR_ID);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_COMPUTE_UNITS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_MAX_WORK_GROUP_SIZE);
	case CL_DEVICE_MAX_WORK_ITEM_SIZES:
	{
		const Json::Value& sizes = properties["CL_DEVICE_MAX_WORK_ITEM_SIZES"];
		std::vector<size_t> result;
		for (const Json::Value& entry : sizes) result.push_back(entry.asUInt64());
		return copy_info(result.data(), result.size() * sizeof(size_t), value_size, value, value_size_ret);
	}
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_INT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_CLOCK_FREQUENCY);
	DEVICE_UINT_PROPERTY(CL_DEVICE_ADDRESS_BITS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_READ_IMAGE_ARGS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_WRITE_IMAGE_ARGS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_SAMPLERS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MEM_BASE_ADDR_ALIGN);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_CONSTANT_ARGS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PARTITION_MAX_SUB_DEVICES);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_PIPE_ARGS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PIPE_MAX_PACKET_SIZE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_IMAGE_PITCH_ALIGNMENT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT);
	DEVICE_UINT_PROPERTY(CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_ON_DEVICE_QUEUES);
	DEVICE_UINT_PROPERTY(CL_DEVICE_MAX_ON_DEVICE_EVENTS);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE2D_MAX_WIDTH);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE2D_MAX_HEIGHT);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE3D_MAX_WIDTH);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE3D_MAX_HEIGHT);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE3D_MAX_DEPTH);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_MAX_PARAMETER_SIZE);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_PROFILING_TIMER_RESOLUTION);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE_MAX_BUFFER_SIZE);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_IMAGE_MAX_ARRAY_SIZE);
	DEVICE_SIZE_PROPERTY(CL_DEVICE_PRINTF_BUFFER_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_MAX_MEM_ALLOC_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_GLOBAL_MEM_CACHE_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_GLOBAL_MEM_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_LOCAL_MEM_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE);
	DEVICE_ULONG_PROPERTY(CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_IMAGE_SUPPORT);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_ERROR_CORRECTION_SUPPORT);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_ENDIAN_LITTLE);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_AVAILABLE);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_COMPILER_AVAILABLE);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_LINKER_AVAILABLE);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_HOST_UNIFIED_MEMORY);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_PREFERRED_INTEROP_USER_SYNC);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT);
	DEVICE_BOOL_PROPERTY(CL_DEVICE_PIPE_SUPPORT);
	DEVICE_STRING_PROPERTY(CL_DEVICE_NAME);
	DEVICE_STRING_PROPERTY(CL_DEVICE_VENDOR);
	DEVICE_STRING_PROPERTY(CL_DRIVER_VERSION);
	DEVICE_STRING_PROPERTY(CL_DEVICE_PROFILE);
	DEVICE_STRING_PROPERTY(CL_DEVICE_VERSION);
	DEVICE_STRING_PROPERTY(CL_DEVICE_OPENCL_C_VERSION);
	DEVICE_STRING_PROPERTY(CL_DEVICE_BUILT_IN_KERNELS);
	DEVICE_STRING_PROPERTY(CL_DEVICE_IL_VERSION);
	DEVICE_STRING_PROPERTY(CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED);
	case CL_DEVICE_EXTENSIONS: return copy_string(rt.device_extension_string, value_size, value, value_size_ret);
	case CL_DEVICE_PLATFORM: return copy_scalar<cl_platform_id>(&rt.platform, value_size, value, value_size_ret);
	case CL_DEVICE_PARENT_DEVICE: return copy_scalar<cl_device_id>(nullptr, value_size, value, value_size_ret);
	case CL_DEVICE_REFERENCE_COUNT: return copy_scalar<cl_uint>(rt.device.references.load(), value_size, value, value_size_ret);
	case CL_DEVICE_PARTITION_PROPERTIES:
	case CL_DEVICE_PARTITION_TYPE:
	{
		cl_device_partition_property terminator = 0;
		return copy_scalar(terminator, value_size, value, value_size_ret);
	}
	case CL_DEVICE_PARTITION_AFFINITY_DOMAIN:
		return copy_scalar<cl_device_affinity_domain>(0, value_size, value, value_size_ret);
	case CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
		return copy_json_size(properties["CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE"], value_size, value, value_size_ret);
	case CL_DEVICE_NUMERIC_VERSION:
		return copy_scalar<cl_version>(parse_version(properties["CL_DEVICE_NUMERIC_VERSION"].asString()), value_size, value, value_size_ret);
	case CL_DEVICE_EXTENSIONS_WITH_VERSION:
	{
		std::vector<cl_name_version> versions = extension_versions(rt.device_extensions);
		return copy_info(versions.data(), versions.size() * sizeof(cl_name_version), value_size, value, value_size_ret);
	}
	case CL_DEVICE_OPENCL_C_ALL_VERSIONS:
	case CL_DEVICE_OPENCL_C_FEATURES:
	case CL_DEVICE_ILS_WITH_VERSION:
	case CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION:
	{
		const char* key = param_name == CL_DEVICE_OPENCL_C_ALL_VERSIONS ? "CL_DEVICE_OPENCL_C_ALL_VERSIONS" :
			param_name == CL_DEVICE_OPENCL_C_FEATURES ? "CL_DEVICE_OPENCL_C_FEATURES" :
			param_name == CL_DEVICE_ILS_WITH_VERSION ? "CL_DEVICE_ILS_WITH_VERSION" : "CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION";
		std::vector<cl_name_version> versions = named_versions(properties[key]);
		return copy_info(versions.data(), versions.size() * sizeof(cl_name_version), value_size, value, value_size_ret);
	}
	case CL_DEVICE_SINGLE_FP_CONFIG:
	case CL_DEVICE_DOUBLE_FP_CONFIG:
	case CL_DEVICE_HALF_FP_CONFIG:
		return copy_scalar<cl_device_fp_config>(json_flags(properties[param_name == CL_DEVICE_SINGLE_FP_CONFIG ? "CL_DEVICE_SINGLE_FP_CONFIG" :
			param_name == CL_DEVICE_DOUBLE_FP_CONFIG ? "CL_DEVICE_DOUBLE_FP_CONFIG" : "CL_DEVICE_HALF_FP_CONFIG"]), value_size, value, value_size_ret);
	case CL_DEVICE_EXECUTION_CAPABILITIES:
		return copy_scalar<cl_device_exec_capabilities>(json_flags(properties["CL_DEVICE_EXECUTION_CAPABILITIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_QUEUE_ON_HOST_PROPERTIES:
		return copy_scalar<cl_command_queue_properties>(json_flags(properties["CL_DEVICE_QUEUE_ON_HOST_PROPERTIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES:
		return copy_scalar<cl_command_queue_properties>(json_flags(properties["CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_SVM_CAPABILITIES:
		return copy_scalar<cl_device_svm_capabilities>(json_flags(properties["CL_DEVICE_SVM_CAPABILITIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES:
		return copy_scalar<cl_device_atomic_capabilities>(json_flags(properties["CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_ATOMIC_FENCE_CAPABILITIES:
		return copy_scalar<cl_device_atomic_capabilities>(json_flags(properties["CL_DEVICE_ATOMIC_FENCE_CAPABILITIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES:
		return copy_scalar<cl_device_device_enqueue_capabilities>(json_flags(properties["CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES"]), value_size, value, value_size_ret);
	case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:
	{
		std::string type = properties["CL_DEVICE_GLOBAL_MEM_CACHE_TYPE"].asString();
		cl_device_mem_cache_type result = type == "CL_READ_WRITE_CACHE" ? CL_READ_WRITE_CACHE : type == "CL_READ_ONLY_CACHE" ? CL_READ_ONLY_CACHE : CL_NONE;
		return copy_scalar(result, value_size, value, value_size_ret);
	}
	case CL_DEVICE_LOCAL_MEM_TYPE:
	{
		cl_device_local_mem_type result = properties["CL_DEVICE_LOCAL_MEM_TYPE"].asString() == "CL_LOCAL" ? CL_LOCAL : CL_GLOBAL;
		return copy_scalar(result, value_size, value, value_size_ret);
	}
	case CL_DEVICE_UUID_KHR: return copy_uuid(properties["CL_DEVICE_UUID_KHR"], value_size, value, value_size_ret);
	case CL_DRIVER_UUID_KHR: return copy_uuid(properties["CL_DRIVER_UUID_KHR"], value_size, value, value_size_ret);
	case CL_DEVICE_LUID_VALID_KHR: return copy_json_bool(properties["CL_DEVICE_LUID_VALID_KHR"], value_size, value, value_size_ret);
	case CL_DEVICE_LUID_KHR:
	{
		cl_uchar luid[CL_LUID_SIZE_KHR] = {};
		return copy_info(luid, sizeof(luid), value_size, value, value_size_ret);
	}
	case CL_DEVICE_NODE_MASK_KHR: return copy_json_uint(properties["CL_DEVICE_NODE_MASK_KHR"], value_size, value, value_size_ret);
	case CL_DEVICE_OPENCL_C_NUMERIC_VERSION_KHR:
		return copy_scalar<cl_version>(properties["CL_DEVICE_OPENCL_C_NUMERIC_VERSION_KHR"].asUInt(), value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clRetainDevice(cl_device_id device)
{
	CL_ENTRY(clRetainDevice);
	if (device != &runtime().device) return CL_INVALID_DEVICE;
	++device->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseDevice(cl_device_id device)
{
	CL_ENTRY(clReleaseDevice);
	if (device != &runtime().device) return CL_INVALID_DEVICE;
	if (device->references.load() > 1) --device->references;
	return CL_SUCCESS;
}

#undef DEVICE_UINT_PROPERTY
#undef DEVICE_ULONG_PROPERTY
#undef DEVICE_SIZE_PROPERTY
#undef DEVICE_BOOL_PROPERTY
#undef DEVICE_STRING_PROPERTY

CL_EXPORT cl_context CL_API_CALL clCreateContext(const cl_context_properties* properties, cl_uint num_devices, const cl_device_id* devices,
	void (CL_CALLBACK* notify)(const char*, const void*, size_t, void*), void* user_data, cl_int* error)
{
	CL_ENTRY(clCreateContext);
	(void)notify;
	(void)user_data;
	if (num_devices != 1 || !devices || devices[0] != &runtime().device)
	{
		if (error) *error = CL_INVALID_DEVICE;
		return nullptr;
	}
	_cl_context* context = new _cl_context();
	context->dispatch = &runtime().dispatch;
	context->device = devices[0];
	if (properties)
	{
		for (const cl_context_properties* property = properties; *property; property += 2)
		{
			if (*property != CL_CONTEXT_PLATFORM || reinterpret_cast<cl_platform_id>(property[1]) != &runtime().platform)
			{
				delete context;
				if (error) *error = CL_INVALID_PROPERTY;
				return nullptr;
			}
			context->properties.push_back(property[0]);
			context->properties.push_back(property[1]);
		}
		context->properties.push_back(0);
	}
	if (error) *error = CL_SUCCESS;
	return context;
}

CL_EXPORT cl_context CL_API_CALL clCreateContextFromType(const cl_context_properties* properties, cl_device_type device_type,
	void (CL_CALLBACK* notify)(const char*, const void*, size_t, void*), void* user_data, cl_int* error)
{
	CL_ENTRY(clCreateContextFromType);
	if (device_type != CL_DEVICE_TYPE_ALL && !(device_type & (CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_DEFAULT)))
	{
		if (error) *error = CL_DEVICE_NOT_FOUND;
		return nullptr;
	}
	cl_device_id device = &runtime().device;
	return clCreateContext(properties, 1, &device, notify, user_data, error);
}

CL_EXPORT cl_int CL_API_CALL clRetainContext(cl_context context)
{
	CL_ENTRY(clRetainContext);
	if (!valid_object(context)) return CL_INVALID_CONTEXT;
	++context->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseContext(cl_context context)
{
	CL_ENTRY(clReleaseContext);
	if (!valid_object(context)) return CL_INVALID_CONTEXT;
	if (--context->references == 0) delete context;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetContextInfo(cl_context context, cl_context_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetContextInfo);
	if (!valid_object(context)) return CL_INVALID_CONTEXT;
	switch (param_name)
	{
	case CL_CONTEXT_REFERENCE_COUNT: return copy_scalar<cl_uint>(context->references.load(), value_size, value, value_size_ret);
	case CL_CONTEXT_NUM_DEVICES: return copy_scalar<cl_uint>(1, value_size, value, value_size_ret);
	case CL_CONTEXT_DEVICES: return copy_scalar<cl_device_id>(context->device, value_size, value, value_size_ret);
	case CL_CONTEXT_PROPERTIES:
		return copy_info(context->properties.data(), context->properties.size() * sizeof(cl_context_properties), value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

static cl_command_queue create_queue(cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int* error)
{
	if (!valid_object(context))
	{
		if (error) *error = CL_INVALID_CONTEXT;
		return nullptr;
	}
	if (device != context->device)
	{
		if (error) *error = CL_INVALID_DEVICE;
		return nullptr;
	}
	_cl_command_queue* queue = new _cl_command_queue();
	queue->dispatch = &runtime().dispatch;
	queue->context = context;
	queue->device = device;
	queue->properties = properties;
	++context->references;
	if (error) *error = CL_SUCCESS;
	return queue;
}

CL_EXPORT cl_command_queue CL_API_CALL clCreateCommandQueue(cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int* error)
{
	CL_ENTRY(clCreateCommandQueue);
	return create_queue(context, device, properties, error);
}

CL_EXPORT cl_command_queue CL_API_CALL clCreateCommandQueueWithProperties(cl_context context, cl_device_id device,
	const cl_queue_properties* properties, cl_int* error)
{
	CL_ENTRY(clCreateCommandQueueWithProperties);
	cl_command_queue_properties flags = 0;
	if (properties)
	{
		for (const cl_queue_properties* property = properties; *property; property += 2)
		{
			if (*property != CL_QUEUE_PROPERTIES)
			{
				if (error) *error = CL_INVALID_VALUE;
				return nullptr;
			}
			flags = property[1];
		}
	}
	return create_queue(context, device, flags, error);
}

CL_EXPORT cl_int CL_API_CALL clRetainCommandQueue(cl_command_queue queue)
{
	CL_ENTRY(clRetainCommandQueue);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	++queue->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseCommandQueue(cl_command_queue queue)
{
	CL_ENTRY(clReleaseCommandQueue);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (--queue->references == 0)
	{
		clReleaseContext(queue->context);
		delete queue;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetCommandQueueInfo(cl_command_queue queue, cl_command_queue_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetCommandQueueInfo);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	switch (param_name)
	{
	case CL_QUEUE_CONTEXT: return copy_scalar(queue->context, value_size, value, value_size_ret);
	case CL_QUEUE_DEVICE: return copy_scalar(queue->device, value_size, value, value_size_ret);
	case CL_QUEUE_REFERENCE_COUNT: return copy_scalar<cl_uint>(queue->references.load(), value_size, value, value_size_ret);
	case CL_QUEUE_PROPERTIES: return copy_scalar(queue->properties, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clSetCommandQueueProperty(cl_command_queue queue, cl_command_queue_properties properties,
	cl_bool enable, cl_command_queue_properties* old_properties)
{
	CL_ENTRY(clSetCommandQueueProperty);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (properties & ~(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE)) return CL_INVALID_VALUE;
	if (old_properties) *old_properties = queue->properties;
	if (enable) queue->properties |= properties;
	else queue->properties &= ~properties;
	return CL_SUCCESS;
}

static cl_int validate_memory_flags(cl_mem_flags& flags, const void* host_ptr)
{
	const cl_mem_flags device_access = flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY | CL_MEM_KERNEL_READ_AND_WRITE);
	const cl_mem_flags host_access = flags & (CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_NO_ACCESS);
	if (device_access && (device_access & (device_access - 1))) return CL_INVALID_VALUE;
	if (host_access && (host_access & (host_access - 1))) return CL_INVALID_VALUE;
	if ((flags & CL_MEM_USE_HOST_PTR) && (flags & (CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR))) return CL_INVALID_VALUE;
	if (!device_access) flags |= CL_MEM_READ_WRITE;
	if ((flags & (CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR)) && !host_ptr) return CL_INVALID_HOST_PTR;
	if (host_ptr && !(flags & (CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR))) return CL_INVALID_HOST_PTR;
	return CL_SUCCESS;
}

static cl_mem create_buffer(cl_context context, cl_mem_flags flags, size_t size, void* host_ptr, cl_int* error)
{
	if (!valid_object(context))
	{
		if (error) *error = CL_INVALID_CONTEXT;
		return nullptr;
	}
	if (size == 0)
	{
		if (error) *error = CL_INVALID_BUFFER_SIZE;
		return nullptr;
	}
	cl_int result = validate_memory_flags(flags, host_ptr);
	if (result != CL_SUCCESS)
	{
		if (error) *error = result;
		return nullptr;
	}
	_cl_mem* memory = new _cl_mem();
	memory->dispatch = &runtime().dispatch;
	memory->context = context;
	memory->flags = flags;
	memory->host_ptr = flags & CL_MEM_USE_HOST_PTR ? host_ptr : nullptr;
	memory->data.resize(size);
	if (host_ptr) memcpy(memory->data.data(), host_ptr, size);
	++context->references;
	if (error) *error = CL_SUCCESS;
	return memory;
}

CL_EXPORT cl_mem CL_API_CALL clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size, void* host_ptr, cl_int* error)
{
	CL_ENTRY(clCreateBuffer);
	return create_buffer(context, flags, size, host_ptr, error);
}

CL_EXPORT cl_mem CL_API_CALL clCreateBufferWithProperties(cl_context context, const cl_mem_properties* properties, cl_mem_flags flags,
	size_t size, void* host_ptr, cl_int* error)
{
	CL_ENTRY(clCreateBufferWithProperties);
	if (properties && properties[0])
	{
		if (error) *error = CL_INVALID_PROPERTY;
		return nullptr;
	}
	return create_buffer(context, flags, size, host_ptr, error);
}

static const char* memory_access_name(cl_mem_flags flags)
{
	cl_mem_flags access = flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY | CL_MEM_KERNEL_READ_AND_WRITE);
	if (!access || access == CL_MEM_READ_WRITE) return "CL_MEM_READ_WRITE";
	if (access == CL_MEM_WRITE_ONLY) return "CL_MEM_WRITE_ONLY";
	if (access == CL_MEM_READ_ONLY) return "CL_MEM_READ_ONLY";
	if (access == CL_MEM_KERNEL_READ_AND_WRITE) return "CL_MEM_KERNEL_READ_AND_WRITE";
	return nullptr;
}

static bool image_format_supported(cl_mem_flags flags, cl_mem_object_type image_type, const cl_image_format& format)
{
	const char* access_name = memory_access_name(flags);
	if (!access_name) return false;
	const Json::Value& entries = runtime().profile["capabilities"]["device"]["imageFormats"];
	for (const Json::Value& entry : entries)
	{
		if (image_enum(entry["imageType"].asString()) != image_type) continue;
		if (image_enum(entry["channelOrder"].asString()) != format.image_channel_order) continue;
		if (image_enum(entry["channelType"].asString()) != format.image_channel_data_type) continue;
		for (const Json::Value& candidate : entry["access"])
		{
			if (candidate.asString() == access_name) return true;
		}
	}
	return false;
}

static size_t image_channel_count(cl_channel_order order)
{
	switch (order)
	{
	case CL_A:
	case CL_DEPTH:
	case CL_INTENSITY:
	case CL_LUMINANCE:
	case CL_R: return 1;
	case CL_RG: return 2;
	case CL_RGB:
	case CL_sRGB: return 3;
	case CL_ABGR:
	case CL_BGRA:
	case CL_RGBA:
	case CL_sBGRA:
	case CL_sRGBA: return 4;
	default: return 0;
	}
}

static size_t image_element_size(const cl_image_format& format)
{
	if (format.image_channel_data_type == CL_UNORM_SHORT_555 || format.image_channel_data_type == CL_UNORM_SHORT_565) return 2;
	if (format.image_channel_data_type == CL_UNORM_INT_101010 || format.image_channel_data_type == CL_UNORM_INT_101010_2) return 4;
	size_t channel_size = 0;
	switch (format.image_channel_data_type)
	{
	case CL_SIGNED_INT8:
	case CL_SNORM_INT8:
	case CL_UNORM_INT8:
	case CL_UNSIGNED_INT8: channel_size = 1; break;
	case CL_HALF_FLOAT:
	case CL_SIGNED_INT16:
	case CL_SNORM_INT16:
	case CL_UNORM_INT16:
	case CL_UNSIGNED_INT16: channel_size = 2; break;
	case CL_FLOAT:
	case CL_SIGNED_INT32:
	case CL_UNSIGNED_INT32: channel_size = 4; break;
	default: return 0;
	}
	return channel_size * image_channel_count(format.image_channel_order);
}

static bool multiply_size(size_t first, size_t second, size_t& result)
{
	if (first && second > std::numeric_limits<size_t>::max() / first) return false;
	result = first * second;
	return true;
}

static cl_mem create_image(cl_context context, cl_mem_flags flags, const cl_image_format* format,
	const cl_image_desc* description, void* host_ptr, cl_int* error)
{
	if (!valid_object(context))
	{
		if (error) *error = CL_INVALID_CONTEXT;
		return nullptr;
	}
	if (!format || !description)
	{
		if (error) *error = CL_INVALID_VALUE;
		return nullptr;
	}
	cl_int result = validate_memory_flags(flags, host_ptr);
	if (result != CL_SUCCESS)
	{
		if (error) *error = result;
		return nullptr;
	}
	if (!image_format_supported(flags, description->image_type, *format))
	{
		if (error) *error = CL_IMAGE_FORMAT_NOT_SUPPORTED;
		return nullptr;
	}
	size_t rows = 1;
	size_t slices = 1;
	switch (description->image_type)
	{
	case CL_MEM_OBJECT_IMAGE1D:
		if (!description->image_width) result = CL_INVALID_IMAGE_SIZE;
		break;
	case CL_MEM_OBJECT_IMAGE1D_BUFFER:
		if (!description->image_width || !valid_object(description->buffer) || description->buffer->type != CL_MEM_OBJECT_BUFFER
			|| description->buffer->context != context) result = CL_INVALID_IMAGE_DESCRIPTOR;
		break;
	case CL_MEM_OBJECT_IMAGE1D_ARRAY:
		if (!description->image_width || !description->image_array_size) result = CL_INVALID_IMAGE_SIZE;
		slices = description->image_array_size;
		break;
	case CL_MEM_OBJECT_IMAGE2D:
		if (!description->image_width || !description->image_height) result = CL_INVALID_IMAGE_SIZE;
		rows = description->image_height;
		break;
	case CL_MEM_OBJECT_IMAGE2D_ARRAY:
		if (!description->image_width || !description->image_height || !description->image_array_size) result = CL_INVALID_IMAGE_SIZE;
		rows = description->image_height;
		slices = description->image_array_size;
		break;
	case CL_MEM_OBJECT_IMAGE3D:
		if (!description->image_width || !description->image_height || !description->image_depth) result = CL_INVALID_IMAGE_SIZE;
		rows = description->image_height;
		slices = description->image_depth;
		break;
	default: result = CL_INVALID_IMAGE_DESCRIPTOR; break;
	}
	if (result != CL_SUCCESS)
	{
		if (error) *error = result;
		return nullptr;
	}
	size_t element_size = image_element_size(*format);
	size_t row_pitch = 0;
	size_t slice_pitch = 0;
	size_t allocation_size = 0;
	if (!element_size || !multiply_size(description->image_width, element_size, row_pitch)
		|| !multiply_size(row_pitch, rows, slice_pitch) || !multiply_size(slice_pitch, slices, allocation_size))
	{
		if (error) *error = CL_INVALID_IMAGE_SIZE;
		return nullptr;
	}
	if (!host_ptr && (description->image_row_pitch || description->image_slice_pitch))
	{
		if (error) *error = CL_INVALID_IMAGE_DESCRIPTOR;
		return nullptr;
	}
	if (description->image_row_pitch && description->image_row_pitch < row_pitch)
	{
		if (error) *error = CL_INVALID_IMAGE_DESCRIPTOR;
		return nullptr;
	}
	_cl_mem* memory = new _cl_mem();
	memory->dispatch = &runtime().dispatch;
	memory->context = context;
	memory->flags = flags;
	memory->type = description->image_type;
	memory->host_ptr = flags & CL_MEM_USE_HOST_PTR ? host_ptr : nullptr;
	memory->image_format = *format;
	memory->image_description = *description;
	memory->parent_memory = description->image_type == CL_MEM_OBJECT_IMAGE1D_BUFFER ? description->buffer : nullptr;
	memory->image_element_size = element_size;
	memory->image_row_pitch = row_pitch;
	memory->image_slice_pitch = slice_pitch;
	memory->data.resize(allocation_size);
	if (host_ptr)
	{
		size_t source_row_pitch = description->image_row_pitch ? description->image_row_pitch : row_pitch;
		size_t source_slice_pitch = description->image_slice_pitch ? description->image_slice_pitch : source_row_pitch * rows;
		const unsigned char* source = static_cast<const unsigned char*>(host_ptr);
		for (size_t z = 0; z < slices; ++z)
		{
			for (size_t y = 0; y < rows; ++y)
			{
				memcpy(memory->data.data() + z * slice_pitch + y * row_pitch,
					source + z * source_slice_pitch + y * source_row_pitch, row_pitch);
			}
		}
	}
	++context->references;
	if (memory->parent_memory) ++memory->parent_memory->references;
	if (error) *error = CL_SUCCESS;
	return memory;
}

CL_EXPORT cl_mem CL_API_CALL clCreateImage(cl_context context, cl_mem_flags flags, const cl_image_format* format,
	const cl_image_desc* description, void* host_ptr, cl_int* error)
{
	CL_ENTRY(clCreateImage);
	return create_image(context, flags, format, description, host_ptr, error);
}

CL_EXPORT cl_mem CL_API_CALL clCreateImageWithProperties(cl_context context, const cl_mem_properties* properties, cl_mem_flags flags,
	const cl_image_format* format, const cl_image_desc* description, void* host_ptr, cl_int* error)
{
	CL_ENTRY(clCreateImageWithProperties);
	if (properties && properties[0])
	{
		if (error) *error = CL_INVALID_PROPERTY;
		return nullptr;
	}
	return create_image(context, flags, format, description, host_ptr, error);
}

CL_EXPORT cl_mem CL_API_CALL clCreateImage2D(cl_context context, cl_mem_flags flags, const cl_image_format* format,
	size_t width, size_t height, size_t row_pitch, void* host_ptr, cl_int* error)
{
	CL_ENTRY(clCreateImage2D);
	cl_image_desc description = {};
	description.image_type = CL_MEM_OBJECT_IMAGE2D;
	description.image_width = width;
	description.image_height = height;
	description.image_row_pitch = row_pitch;
	return create_image(context, flags, format, &description, host_ptr, error);
}

CL_EXPORT cl_mem CL_API_CALL clCreateImage3D(cl_context context, cl_mem_flags flags, const cl_image_format* format,
	size_t width, size_t height, size_t depth, size_t row_pitch, size_t slice_pitch, void* host_ptr, cl_int* error)
{
	CL_ENTRY(clCreateImage3D);
	cl_image_desc description = {};
	description.image_type = CL_MEM_OBJECT_IMAGE3D;
	description.image_width = width;
	description.image_height = height;
	description.image_depth = depth;
	description.image_row_pitch = row_pitch;
	description.image_slice_pitch = slice_pitch;
	return create_image(context, flags, format, &description, host_ptr, error);
}

CL_EXPORT cl_int CL_API_CALL clRetainMemObject(cl_mem memory)
{
	CL_ENTRY(clRetainMemObject);
	if (!valid_object(memory)) return CL_INVALID_MEM_OBJECT;
	++memory->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseMemObject(cl_mem memory)
{
	CL_ENTRY(clReleaseMemObject);
	if (!valid_object(memory)) return CL_INVALID_MEM_OBJECT;
	if (--memory->references == 0)
	{
		if (memory->parent_memory) clReleaseMemObject(memory->parent_memory);
		clReleaseContext(memory->context);
		delete memory;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetSupportedImageFormats(cl_context context, cl_mem_flags flags, cl_mem_object_type image_type,
	cl_uint num_entries, cl_image_format* image_formats, cl_uint* num_image_formats)
{
	CL_ENTRY(clGetSupportedImageFormats);
	if (!valid_object(context)) return CL_INVALID_CONTEXT;
	if ((!image_formats && !num_image_formats) || (image_formats && num_entries == 0)) return CL_INVALID_VALUE;
	const char* access_name = memory_access_name(flags);
	if (!access_name || flags & ~(CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY | CL_MEM_KERNEL_READ_AND_WRITE)) return CL_INVALID_VALUE;
	std::vector<cl_image_format> formats;
	const Json::Value& entries = runtime().profile["capabilities"]["device"]["imageFormats"];
	for (const Json::Value& entry : entries)
	{
		if (image_enum(entry["imageType"].asString()) != image_type) continue;
		bool supported_access = false;
		for (const Json::Value& candidate : entry["access"])
		{
			if (candidate.asString() == access_name) supported_access = true;
		}
		if (!supported_access) continue;
		cl_image_format format = {};
		format.image_channel_order = image_enum(entry["channelOrder"].asString());
		format.image_channel_data_type = image_enum(entry["channelType"].asString());
		if (!format.image_channel_order || !format.image_channel_data_type) continue;
		formats.push_back(format);
	}
	if (num_image_formats) *num_image_formats = formats.size();
	if (image_formats)
	{
		cl_uint copy_count = std::min<cl_uint>(num_entries, formats.size());
		memcpy(image_formats, formats.data(), copy_count * sizeof(cl_image_format));
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetMemObjectInfo(cl_mem memory, cl_mem_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetMemObjectInfo);
	if (!valid_object(memory)) return CL_INVALID_MEM_OBJECT;
	switch (param_name)
	{
	case CL_MEM_TYPE: return copy_scalar(memory->type, value_size, value, value_size_ret);
	case CL_MEM_FLAGS: return copy_scalar(memory->flags, value_size, value, value_size_ret);
	case CL_MEM_SIZE: return copy_scalar<size_t>(memory->data.size(), value_size, value, value_size_ret);
	case CL_MEM_HOST_PTR: return copy_scalar(memory->host_ptr, value_size, value, value_size_ret);
	case CL_MEM_REFERENCE_COUNT: return copy_scalar<cl_uint>(memory->references.load(), value_size, value, value_size_ret);
	case CL_MEM_CONTEXT: return copy_scalar(memory->context, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clGetImageInfo(cl_mem image, cl_image_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetImageInfo);
	if (!valid_object(image) || image->type == CL_MEM_OBJECT_BUFFER) return CL_INVALID_MEM_OBJECT;
	switch (param_name)
	{
	case CL_IMAGE_FORMAT: return copy_scalar(image->image_format, value_size, value, value_size_ret);
	case CL_IMAGE_ELEMENT_SIZE: return copy_scalar(image->image_element_size, value_size, value, value_size_ret);
	case CL_IMAGE_ROW_PITCH: return copy_scalar(image->image_row_pitch, value_size, value, value_size_ret);
	case CL_IMAGE_SLICE_PITCH: return copy_scalar(image->image_slice_pitch, value_size, value, value_size_ret);
	case CL_IMAGE_WIDTH: return copy_scalar(image->image_description.image_width, value_size, value, value_size_ret);
	case CL_IMAGE_HEIGHT: return copy_scalar(image->image_description.image_height, value_size, value, value_size_ret);
	case CL_IMAGE_DEPTH: return copy_scalar(image->image_description.image_depth, value_size, value, value_size_ret);
	case CL_IMAGE_ARRAY_SIZE: return copy_scalar(image->image_description.image_array_size, value_size, value, value_size_ret);
	case CL_IMAGE_BUFFER: return copy_scalar(image->image_description.buffer, value_size, value, value_size_ret);
	case CL_IMAGE_NUM_MIP_LEVELS: return copy_scalar(image->image_description.num_mip_levels, value_size, value, value_size_ret);
	case CL_IMAGE_NUM_SAMPLES: return copy_scalar(image->image_description.num_samples, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

static cl_sampler create_sampler(cl_context context, cl_bool normalized_coordinates, cl_addressing_mode addressing_mode,
	cl_filter_mode filter_mode, cl_int* error)
{
	if (!valid_object(context))
	{
		if (error) *error = CL_INVALID_CONTEXT;
		return nullptr;
	}
	if (normalized_coordinates != CL_TRUE && normalized_coordinates != CL_FALSE)
	{
		if (error) *error = CL_INVALID_VALUE;
		return nullptr;
	}
	if (addressing_mode != CL_ADDRESS_NONE && addressing_mode != CL_ADDRESS_CLAMP_TO_EDGE && addressing_mode != CL_ADDRESS_CLAMP
		&& addressing_mode != CL_ADDRESS_REPEAT && addressing_mode != CL_ADDRESS_MIRRORED_REPEAT)
	{
		if (error) *error = CL_INVALID_VALUE;
		return nullptr;
	}
	if (filter_mode != CL_FILTER_NEAREST && filter_mode != CL_FILTER_LINEAR)
	{
		if (error) *error = CL_INVALID_VALUE;
		return nullptr;
	}
	_cl_sampler* sampler = new _cl_sampler();
	sampler->dispatch = &runtime().dispatch;
	sampler->context = context;
	sampler->normalized_coordinates = normalized_coordinates;
	sampler->addressing_mode = addressing_mode;
	sampler->filter_mode = filter_mode;
	++context->references;
	if (error) *error = CL_SUCCESS;
	return sampler;
}

CL_EXPORT cl_sampler CL_API_CALL clCreateSampler(cl_context context, cl_bool normalized_coordinates,
	cl_addressing_mode addressing_mode, cl_filter_mode filter_mode, cl_int* error)
{
	CL_ENTRY(clCreateSampler);
	return create_sampler(context, normalized_coordinates, addressing_mode, filter_mode, error);
}

CL_EXPORT cl_sampler CL_API_CALL clCreateSamplerWithProperties(cl_context context, const cl_sampler_properties* properties, cl_int* error)
{
	CL_ENTRY(clCreateSamplerWithProperties);
	cl_bool normalized_coordinates = CL_TRUE;
	cl_addressing_mode addressing_mode = CL_ADDRESS_CLAMP;
	cl_filter_mode filter_mode = CL_FILTER_NEAREST;
	if (properties)
	{
		for (const cl_sampler_properties* property = properties; *property; property += 2)
		{
			switch (*property)
			{
			case CL_SAMPLER_NORMALIZED_COORDS: normalized_coordinates = static_cast<cl_bool>(property[1]); break;
			case CL_SAMPLER_ADDRESSING_MODE: addressing_mode = static_cast<cl_addressing_mode>(property[1]); break;
			case CL_SAMPLER_FILTER_MODE: filter_mode = static_cast<cl_filter_mode>(property[1]); break;
			default:
				if (error) *error = CL_INVALID_VALUE;
				return nullptr;
			}
		}
	}
	return create_sampler(context, normalized_coordinates, addressing_mode, filter_mode, error);
}

CL_EXPORT cl_int CL_API_CALL clRetainSampler(cl_sampler sampler)
{
	CL_ENTRY(clRetainSampler);
	if (!valid_object(sampler)) return CL_INVALID_SAMPLER;
	++sampler->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseSampler(cl_sampler sampler)
{
	CL_ENTRY(clReleaseSampler);
	if (!valid_object(sampler)) return CL_INVALID_SAMPLER;
	if (--sampler->references == 0)
	{
		clReleaseContext(sampler->context);
		delete sampler;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetSamplerInfo(cl_sampler sampler, cl_sampler_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetSamplerInfo);
	if (!valid_object(sampler)) return CL_INVALID_SAMPLER;
	switch (param_name)
	{
	case CL_SAMPLER_REFERENCE_COUNT: return copy_scalar<cl_uint>(sampler->references.load(), value_size, value, value_size_ret);
	case CL_SAMPLER_CONTEXT: return copy_scalar(sampler->context, value_size, value, value_size_ret);
	case CL_SAMPLER_NORMALIZED_COORDS: return copy_scalar(sampler->normalized_coordinates, value_size, value, value_size_ret);
	case CL_SAMPLER_ADDRESSING_MODE: return copy_scalar(sampler->addressing_mode, value_size, value, value_size_ret);
	case CL_SAMPLER_FILTER_MODE: return copy_scalar(sampler->filter_mode, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_program CL_API_CALL clCreateProgramWithSource(cl_context context, cl_uint count, const char** strings, const size_t* lengths, cl_int* error)
{
	CL_ENTRY(clCreateProgramWithSource);
	if (!valid_object(context))
	{
		if (error) *error = CL_INVALID_CONTEXT;
		return nullptr;
	}
	if (!count || !strings)
	{
		if (error) *error = CL_INVALID_VALUE;
		return nullptr;
	}
	_cl_program* program = new _cl_program();
	program->dispatch = &runtime().dispatch;
	program->context = context;
	for (cl_uint i = 0; i < count; ++i)
	{
		if (!strings[i])
		{
			delete program;
			if (error) *error = CL_INVALID_VALUE;
			return nullptr;
		}
		program->source.append(strings[i], lengths && lengths[i] ? lengths[i] : strlen(strings[i]));
	}
	++context->references;
	if (error) *error = CL_SUCCESS;
	return program;
}

CL_EXPORT cl_int CL_API_CALL clRetainProgram(cl_program program)
{
	CL_ENTRY(clRetainProgram);
	if (!valid_object(program)) return CL_INVALID_PROGRAM;
	++program->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseProgram(cl_program program)
{
	CL_ENTRY(clReleaseProgram);
	if (!valid_object(program)) return CL_INVALID_PROGRAM;
	if (--program->references == 0)
	{
		for (auto callback = program->release_callbacks.rbegin(); callback != program->release_callbacks.rend(); ++callback)
		{
			callback->notify(program, callback->user_data);
		}
		clReleaseContext(program->context);
		delete program;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clSetProgramReleaseCallback(cl_program program,
	void (CL_CALLBACK* notify)(cl_program, void*), void* user_data)
{
	CL_ENTRY(clSetProgramReleaseCallback);
	if (!valid_object(program)) return CL_INVALID_PROGRAM;
	if (!notify) return CL_INVALID_VALUE;
	OpenCLProgramReleaseCallback callback;
	callback.notify = notify;
	callback.user_data = user_data;
	program->release_callbacks.push_back(callback);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clBuildProgram(cl_program program, cl_uint num_devices, const cl_device_id* devices, const char* options,
	void (CL_CALLBACK* notify)(cl_program, void*), void* user_data)
{
	CL_ENTRY(clBuildProgram);
	if (!valid_object(program)) return CL_INVALID_PROGRAM;
	if (num_devices && (!devices || num_devices != 1 || devices[0] != program->context->device)) return CL_INVALID_DEVICE;
	program->options = options ? options : "";
	program->build_log.clear();
	program->build_status = CL_BUILD_SUCCESS;
	if (notify) notify(program, user_data);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clUnloadCompiler()
{
	CL_ENTRY(clUnloadCompiler);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clUnloadPlatformCompiler(cl_platform_id platform)
{
	CL_ENTRY(clUnloadPlatformCompiler);
	return platform == &runtime().platform ? CL_SUCCESS : CL_INVALID_PLATFORM;
}

CL_EXPORT cl_int CL_API_CALL clGetProgramInfo(cl_program program, cl_program_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetProgramInfo);
	if (!valid_object(program)) return CL_INVALID_PROGRAM;
	switch (param_name)
	{
	case CL_PROGRAM_REFERENCE_COUNT: return copy_scalar<cl_uint>(program->references.load(), value_size, value, value_size_ret);
	case CL_PROGRAM_CONTEXT: return copy_scalar(program->context, value_size, value, value_size_ret);
	case CL_PROGRAM_NUM_DEVICES: return copy_scalar<cl_uint>(1, value_size, value, value_size_ret);
	case CL_PROGRAM_DEVICES: return copy_scalar(program->context->device, value_size, value, value_size_ret);
	case CL_PROGRAM_SOURCE: return copy_string(program->source, value_size, value, value_size_ret);
	case CL_PROGRAM_BINARY_SIZES: return copy_scalar<size_t>(0, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clGetProgramBuildInfo(cl_program program, cl_device_id device, cl_program_build_info param_name,
	size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetProgramBuildInfo);
	if (!valid_object(program)) return CL_INVALID_PROGRAM;
	if (device != program->context->device) return CL_INVALID_DEVICE;
	switch (param_name)
	{
	case CL_PROGRAM_BUILD_STATUS: return copy_scalar(program->build_status, value_size, value, value_size_ret);
	case CL_PROGRAM_BUILD_OPTIONS: return copy_string(program->options, value_size, value, value_size_ret);
	case CL_PROGRAM_BUILD_LOG: return copy_string(program->build_log, value_size, value, value_size_ret);
	case CL_PROGRAM_BINARY_TYPE: return copy_scalar<cl_program_binary_type>(CL_PROGRAM_BINARY_TYPE_EXECUTABLE, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_kernel CL_API_CALL clCreateKernel(cl_program program, const char* name, cl_int* error)
{
	CL_ENTRY(clCreateKernel);
	if (!valid_object(program))
	{
		if (error) *error = CL_INVALID_PROGRAM;
		return nullptr;
	}
	if (program->build_status != CL_BUILD_SUCCESS)
	{
		if (error) *error = CL_INVALID_PROGRAM_EXECUTABLE;
		return nullptr;
	}
	if (!name || !name[0])
	{
		if (error) *error = CL_INVALID_KERNEL_NAME;
		return nullptr;
	}
	_cl_kernel* kernel = new _cl_kernel();
	kernel->dispatch = &runtime().dispatch;
	kernel->program = program;
	kernel->name = name;
	++program->references;
	if (error) *error = CL_SUCCESS;
	return kernel;
}

CL_EXPORT cl_int CL_API_CALL clRetainKernel(cl_kernel kernel)
{
	CL_ENTRY(clRetainKernel);
	if (!valid_object(kernel)) return CL_INVALID_KERNEL;
	++kernel->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseKernel(cl_kernel kernel)
{
	CL_ENTRY(clReleaseKernel);
	if (!valid_object(kernel)) return CL_INVALID_KERNEL;
	if (--kernel->references == 0)
	{
		clReleaseProgram(kernel->program);
		delete kernel;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clSetKernelArg(cl_kernel kernel, cl_uint index, size_t size, const void* value)
{
	CL_ENTRY(clSetKernelArg);
	if (!valid_object(kernel)) return CL_INVALID_KERNEL;
	if (size == 0) return CL_INVALID_ARG_SIZE;
	if (kernel->arguments.size() <= index) kernel->arguments.resize(index + 1);
	kernel->arguments[index].resize(size);
	if (value) memcpy(kernel->arguments[index].data(), value, size);
	else memset(kernel->arguments[index].data(), 0, size);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetKernelInfo(cl_kernel kernel, cl_kernel_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetKernelInfo);
	if (!valid_object(kernel)) return CL_INVALID_KERNEL;
	switch (param_name)
	{
	case CL_KERNEL_FUNCTION_NAME: return copy_string(kernel->name, value_size, value, value_size_ret);
	case CL_KERNEL_NUM_ARGS: return copy_scalar<cl_uint>(kernel->arguments.size(), value_size, value, value_size_ret);
	case CL_KERNEL_REFERENCE_COUNT: return copy_scalar<cl_uint>(kernel->references.load(), value_size, value, value_size_ret);
	case CL_KERNEL_CONTEXT: return copy_scalar(kernel->program->context, value_size, value, value_size_ret);
	case CL_KERNEL_PROGRAM: return copy_scalar(kernel->program, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clGetKernelWorkGroupInfo(cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param_name,
	size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetKernelWorkGroupInfo);
	if (!valid_object(kernel)) return CL_INVALID_KERNEL;
	if (device && device != kernel->program->context->device) return CL_INVALID_DEVICE;
	switch (param_name)
	{
	case CL_KERNEL_WORK_GROUP_SIZE:
		return copy_json_size(runtime().device_properties["CL_DEVICE_MAX_WORK_GROUP_SIZE"], value_size, value, value_size_ret);
	case CL_KERNEL_COMPILE_WORK_GROUP_SIZE:
	{
		size_t sizes[3] = {};
		return copy_info(sizes, sizeof(sizes), value_size, value, value_size_ret);
	}
	case CL_KERNEL_LOCAL_MEM_SIZE: return copy_scalar<cl_ulong>(0, value_size, value, value_size_ret);
	case CL_KERNEL_PRIVATE_MEM_SIZE: return copy_scalar<cl_ulong>(0, value_size, value, value_size_ret);
	case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE: return copy_scalar<size_t>(1, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clWaitForEvents(cl_uint count, const cl_event* events)
{
	CL_ENTRY(clWaitForEvents);
	if (!count || !events) return CL_INVALID_VALUE;
	for (cl_uint i = 0; i < count; ++i) if (!valid_object(events[i])) return CL_INVALID_EVENT;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetEventInfo(cl_event event, cl_event_info param_name, size_t value_size, void* value, size_t* value_size_ret)
{
	CL_ENTRY(clGetEventInfo);
	if (!valid_object(event)) return CL_INVALID_EVENT;
	switch (param_name)
	{
	case CL_EVENT_COMMAND_QUEUE: return copy_scalar(event->queue, value_size, value, value_size_ret);
	case CL_EVENT_COMMAND_TYPE: return copy_scalar(event->command_type, value_size, value, value_size_ret);
	case CL_EVENT_REFERENCE_COUNT: return copy_scalar<cl_uint>(event->references.load(), value_size, value, value_size_ret);
	case CL_EVENT_COMMAND_EXECUTION_STATUS: return copy_scalar(event->status, value_size, value, value_size_ret);
	case CL_EVENT_CONTEXT: return copy_scalar(event->queue->context, value_size, value, value_size_ret);
	default: return CL_INVALID_VALUE;
	}
}

CL_EXPORT cl_int CL_API_CALL clRetainEvent(cl_event event)
{
	CL_ENTRY(clRetainEvent);
	if (!valid_object(event)) return CL_INVALID_EVENT;
	++event->references;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clReleaseEvent(cl_event event)
{
	CL_ENTRY(clReleaseEvent);
	if (!valid_object(event)) return CL_INVALID_EVENT;
	if (--event->references == 0)
	{
		clReleaseCommandQueue(event->queue);
		delete event;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clGetEventProfilingInfo(cl_event event, cl_profiling_info, size_t, void*, size_t*)
{
	CL_ENTRY(clGetEventProfilingInfo);
	if (!valid_object(event)) return CL_INVALID_EVENT;
	return CL_PROFILING_INFO_NOT_AVAILABLE;
}

CL_EXPORT cl_int CL_API_CALL clFlush(cl_command_queue queue)
{
	CL_ENTRY(clFlush);
	return valid_object(queue) ? CL_SUCCESS : CL_INVALID_COMMAND_QUEUE;
}

CL_EXPORT cl_int CL_API_CALL clFinish(cl_command_queue queue)
{
	CL_ENTRY(clFinish);
	return valid_object(queue) ? CL_SUCCESS : CL_INVALID_COMMAND_QUEUE;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueReadBuffer(cl_command_queue queue, cl_mem memory, cl_bool, size_t offset, size_t size, void* pointer,
	cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueReadBuffer);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!valid_object(memory) || memory->context != queue->context) return CL_INVALID_MEM_OBJECT;
	if (!pointer || offset > memory->data.size() || size > memory->data.size() - offset) return CL_INVALID_VALUE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	memcpy(pointer, memory->data.data() + offset, size);
	create_complete_event(queue, CL_COMMAND_READ_BUFFER, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueWriteBuffer(cl_command_queue queue, cl_mem memory, cl_bool, size_t offset, size_t size, const void* pointer,
	cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueWriteBuffer);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!valid_object(memory) || memory->context != queue->context) return CL_INVALID_MEM_OBJECT;
	if (!pointer || offset > memory->data.size() || size > memory->data.size() - offset) return CL_INVALID_VALUE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	memcpy(memory->data.data() + offset, pointer, size);
	create_complete_event(queue, CL_COMMAND_WRITE_BUFFER, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueCopyBuffer(cl_command_queue queue, cl_mem source, cl_mem destination, size_t source_offset,
	size_t destination_offset, size_t size, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueCopyBuffer);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!valid_object(source) || !valid_object(destination) || source->context != queue->context || destination->context != queue->context)
		return CL_INVALID_MEM_OBJECT;
	if (source_offset > source->data.size() || size > source->data.size() - source_offset || destination_offset > destination->data.size()
		|| size > destination->data.size() - destination_offset) return CL_INVALID_VALUE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	memmove(destination->data.data() + destination_offset, source->data.data() + source_offset, size);
	create_complete_event(queue, CL_COMMAND_COPY_BUFFER, output_event);
	return CL_SUCCESS;
}

static void image_extents(cl_mem image, size_t extents[3])
{
	extents[0] = image->image_description.image_width;
	extents[1] = 1;
	extents[2] = 1;
	if (image->type == CL_MEM_OBJECT_IMAGE1D_ARRAY) extents[1] = image->image_description.image_array_size;
	else if (image->type == CL_MEM_OBJECT_IMAGE2D || image->type == CL_MEM_OBJECT_IMAGE2D_ARRAY || image->type == CL_MEM_OBJECT_IMAGE3D)
		extents[1] = image->image_description.image_height;
	if (image->type == CL_MEM_OBJECT_IMAGE2D_ARRAY) extents[2] = image->image_description.image_array_size;
	else if (image->type == CL_MEM_OBJECT_IMAGE3D) extents[2] = image->image_description.image_depth;
}

static cl_int validate_image_region(cl_mem image, const size_t* origin, const size_t* region)
{
	if (!valid_object(image) || image->type == CL_MEM_OBJECT_BUFFER) return CL_INVALID_MEM_OBJECT;
	if (!origin || !region || !region[0] || !region[1] || !region[2]) return CL_INVALID_VALUE;
	size_t extents[3] = {};
	image_extents(image, extents);
	for (size_t i = 0; i < 3; ++i)
	{
		if (origin[i] > extents[i] || region[i] > extents[i] - origin[i]) return CL_INVALID_VALUE;
	}
	return CL_SUCCESS;
}

static size_t image_offset(cl_mem image, const size_t* origin)
{
	if (image->type == CL_MEM_OBJECT_IMAGE1D_ARRAY)
		return origin[1] * image->image_slice_pitch + origin[0] * image->image_element_size;
	return origin[2] * image->image_slice_pitch + origin[1] * image->image_row_pitch + origin[0] * image->image_element_size;
}

static void read_image_data(cl_mem image, const size_t* origin, const size_t* region, size_t row_pitch, size_t slice_pitch, void* pointer)
{
	size_t bytes_per_row = region[0] * image->image_element_size;
	if (!row_pitch) row_pitch = bytes_per_row;
	if (!slice_pitch) slice_pitch = row_pitch * region[1];
	unsigned char* destination = static_cast<unsigned char*>(pointer);
	size_t base = image_offset(image, origin);
	for (size_t z = 0; z < region[2]; ++z)
	{
		for (size_t y = 0; y < region[1]; ++y)
		{
			size_t source_offset = image->type == CL_MEM_OBJECT_IMAGE1D_ARRAY
				? base + y * image->image_slice_pitch : base + z * image->image_slice_pitch + y * image->image_row_pitch;
			memcpy(destination + z * slice_pitch + y * row_pitch, image->data.data() + source_offset, bytes_per_row);
		}
	}
}

static void write_image_data(cl_mem image, const size_t* origin, const size_t* region, size_t row_pitch, size_t slice_pitch, const void* pointer)
{
	size_t bytes_per_row = region[0] * image->image_element_size;
	if (!row_pitch) row_pitch = bytes_per_row;
	if (!slice_pitch) slice_pitch = row_pitch * region[1];
	const unsigned char* source = static_cast<const unsigned char*>(pointer);
	size_t base = image_offset(image, origin);
	for (size_t z = 0; z < region[2]; ++z)
	{
		for (size_t y = 0; y < region[1]; ++y)
		{
			size_t destination_offset = image->type == CL_MEM_OBJECT_IMAGE1D_ARRAY
				? base + y * image->image_slice_pitch : base + z * image->image_slice_pitch + y * image->image_row_pitch;
			memcpy(image->data.data() + destination_offset, source + z * slice_pitch + y * row_pitch, bytes_per_row);
		}
	}
}

static cl_int validate_image_transfer(cl_command_queue queue, cl_mem image, const size_t* origin, const size_t* region,
	cl_uint event_count, const cl_event* events)
{
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	cl_int result = validate_image_region(image, origin, region);
	if (result != CL_SUCCESS) return result;
	if (image->context != queue->context) return CL_INVALID_CONTEXT;
	return validate_event_wait_list(event_count, events);
}

static cl_int validate_image_host_pitches(cl_mem image, const size_t* region, size_t row_pitch, size_t slice_pitch)
{
	size_t minimum_row_pitch = region[0] * image->image_element_size;
	if (row_pitch && row_pitch < minimum_row_pitch) return CL_INVALID_VALUE;
	if (!row_pitch) row_pitch = minimum_row_pitch;
	if (slice_pitch && slice_pitch < row_pitch * region[1]) return CL_INVALID_VALUE;
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueReadImage(cl_command_queue queue, cl_mem image, cl_bool, const size_t* origin,
	const size_t* region, size_t row_pitch, size_t slice_pitch, void* pointer, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueReadImage);
	cl_int result = validate_image_transfer(queue, image, origin, region, event_count, events);
	if (result != CL_SUCCESS) return result;
	if (!pointer) return CL_INVALID_VALUE;
	result = validate_image_host_pitches(image, region, row_pitch, slice_pitch);
	if (result != CL_SUCCESS) return result;
	read_image_data(image, origin, region, row_pitch, slice_pitch, pointer);
	create_complete_event(queue, CL_COMMAND_READ_IMAGE, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueWriteImage(cl_command_queue queue, cl_mem image, cl_bool, const size_t* origin,
	const size_t* region, size_t row_pitch, size_t slice_pitch, const void* pointer, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueWriteImage);
	cl_int result = validate_image_transfer(queue, image, origin, region, event_count, events);
	if (result != CL_SUCCESS) return result;
	if (!pointer) return CL_INVALID_VALUE;
	result = validate_image_host_pitches(image, region, row_pitch, slice_pitch);
	if (result != CL_SUCCESS) return result;
	write_image_data(image, origin, region, row_pitch, slice_pitch, pointer);
	create_complete_event(queue, CL_COMMAND_WRITE_IMAGE, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueCopyImage(cl_command_queue queue, cl_mem source, cl_mem destination,
	const size_t* source_origin, const size_t* destination_origin, const size_t* region, cl_uint event_count,
	const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueCopyImage);
	cl_int result = validate_image_transfer(queue, source, source_origin, region, event_count, events);
	if (result != CL_SUCCESS) return result;
	result = validate_image_region(destination, destination_origin, region);
	if (result != CL_SUCCESS) return result;
	if (destination->context != queue->context) return CL_INVALID_CONTEXT;
	if (source->image_element_size != destination->image_element_size) return CL_IMAGE_FORMAT_MISMATCH;
	std::vector<unsigned char> temporary(region[0] * region[1] * region[2] * source->image_element_size);
	read_image_data(source, source_origin, region, 0, 0, temporary.data());
	write_image_data(destination, destination_origin, region, 0, 0, temporary.data());
	create_complete_event(queue, CL_COMMAND_COPY_IMAGE, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueCopyImageToBuffer(cl_command_queue queue, cl_mem source, cl_mem destination,
	const size_t* source_origin, const size_t* region, size_t destination_offset, cl_uint event_count,
	const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueCopyImageToBuffer);
	cl_int result = validate_image_transfer(queue, source, source_origin, region, event_count, events);
	if (result != CL_SUCCESS) return result;
	if (!valid_object(destination) || destination->type != CL_MEM_OBJECT_BUFFER) return CL_INVALID_MEM_OBJECT;
	if (destination->context != queue->context) return CL_INVALID_CONTEXT;
	size_t size = region[0] * region[1] * region[2] * source->image_element_size;
	if (destination_offset > destination->data.size() || size > destination->data.size() - destination_offset) return CL_INVALID_VALUE;
	read_image_data(source, source_origin, region, 0, 0, destination->data.data() + destination_offset);
	create_complete_event(queue, CL_COMMAND_COPY_IMAGE_TO_BUFFER, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueCopyBufferToImage(cl_command_queue queue, cl_mem source, cl_mem destination,
	size_t source_offset, const size_t* destination_origin, const size_t* region, cl_uint event_count,
	const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueCopyBufferToImage);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!valid_object(source) || source->type != CL_MEM_OBJECT_BUFFER || source->context != queue->context) return CL_INVALID_MEM_OBJECT;
	cl_int result = validate_image_region(destination, destination_origin, region);
	if (result != CL_SUCCESS) return result;
	if (destination->context != queue->context) return CL_INVALID_CONTEXT;
	result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	size_t size = region[0] * region[1] * region[2] * destination->image_element_size;
	if (source_offset > source->data.size() || size > source->data.size() - source_offset) return CL_INVALID_VALUE;
	write_image_data(destination, destination_origin, region, 0, 0, source->data.data() + source_offset);
	create_complete_event(queue, CL_COMMAND_COPY_BUFFER_TO_IMAGE, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueFillImage(cl_command_queue queue, cl_mem image, const void* fill_color,
	const size_t* origin, const size_t* region, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueFillImage);
	cl_int result = validate_image_transfer(queue, image, origin, region, event_count, events);
	if (result != CL_SUCCESS) return result;
	if (!fill_color) return CL_INVALID_VALUE;
	std::vector<unsigned char> row(region[0] * image->image_element_size);
	for (size_t x = 0; x < region[0]; ++x) memcpy(row.data() + x * image->image_element_size, fill_color, image->image_element_size);
	for (size_t z = 0; z < region[2]; ++z)
	{
		for (size_t y = 0; y < region[1]; ++y)
		{
			size_t row_origin[3] = {origin[0], origin[1] + y, origin[2] + z};
			size_t row_region[3] = {region[0], 1, 1};
			write_image_data(image, row_origin, row_region, 0, 0, row.data());
		}
	}
	create_complete_event(queue, CL_COMMAND_FILL_IMAGE, output_event);
	return CL_SUCCESS;
}

CL_EXPORT void* CL_API_CALL clEnqueueMapBuffer(cl_command_queue queue, cl_mem buffer, cl_bool, cl_map_flags,
	size_t offset, size_t size, cl_uint event_count, const cl_event* events, cl_event* output_event, cl_int* error)
{
	CL_ENTRY(clEnqueueMapBuffer);
	if (!valid_object(queue) || !valid_object(buffer) || buffer->type != CL_MEM_OBJECT_BUFFER || buffer->context != queue->context)
	{
		if (error) *error = !valid_object(queue) ? CL_INVALID_COMMAND_QUEUE : CL_INVALID_MEM_OBJECT;
		return nullptr;
	}
	cl_int result = validate_event_wait_list(event_count, events);
	if (result == CL_SUCCESS && (offset > buffer->data.size() || size > buffer->data.size() - offset)) result = CL_INVALID_VALUE;
	if (result != CL_SUCCESS)
	{
		if (error) *error = result;
		return nullptr;
	}
	void* pointer = buffer->data.data() + offset;
	if (buffer->host_ptr)
	{
		memcpy(buffer->host_ptr, buffer->data.data(), buffer->data.size());
		pointer = static_cast<unsigned char*>(buffer->host_ptr) + offset;
	}
	create_complete_event(queue, CL_COMMAND_MAP_BUFFER, output_event);
	if (error) *error = CL_SUCCESS;
	return pointer;
}

CL_EXPORT void* CL_API_CALL clEnqueueMapImage(cl_command_queue queue, cl_mem image, cl_bool, cl_map_flags,
	const size_t* origin, const size_t* region, size_t* row_pitch, size_t* slice_pitch, cl_uint event_count,
	const cl_event* events, cl_event* output_event, cl_int* error)
{
	CL_ENTRY(clEnqueueMapImage);
	cl_int result = validate_image_transfer(queue, image, origin, region, event_count, events);
	if (result != CL_SUCCESS)
	{
		if (error) *error = result;
		return nullptr;
	}
	if (!row_pitch)
	{
		if (error) *error = CL_INVALID_VALUE;
		return nullptr;
	}
	*row_pitch = image->image_row_pitch;
	if (slice_pitch) *slice_pitch = image->image_slice_pitch;
	create_complete_event(queue, CL_COMMAND_MAP_IMAGE, output_event);
	if (error) *error = CL_SUCCESS;
	return image->data.data() + image_offset(image, origin);
}

CL_EXPORT cl_int CL_API_CALL clEnqueueUnmapMemObject(cl_command_queue queue, cl_mem memory, void* pointer,
	cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueUnmapMemObject);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!valid_object(memory) || memory->context != queue->context) return CL_INVALID_MEM_OBJECT;
	if (!pointer) return CL_INVALID_VALUE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	if (memory->host_ptr && memory->type == CL_MEM_OBJECT_BUFFER) memcpy(memory->data.data(), memory->host_ptr, memory->data.size());
	create_complete_event(queue, CL_COMMAND_UNMAP_MEM_OBJECT, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueNDRangeKernel(cl_command_queue queue, cl_kernel kernel, cl_uint dimensions, const size_t*,
	const size_t* global_sizes, const size_t*, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueNDRangeKernel);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!valid_object(kernel) || kernel->program->context != queue->context) return CL_INVALID_KERNEL;
	if (!dimensions || dimensions > 3 || !global_sizes) return CL_INVALID_WORK_DIMENSION;
	for (cl_uint i = 0; i < dimensions; ++i) if (!global_sizes[i]) return CL_INVALID_GLOBAL_WORK_SIZE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	create_complete_event(queue, CL_COMMAND_NDRANGE_KERNEL, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueTask(cl_command_queue queue, cl_kernel kernel, cl_uint event_count,
	const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueTask);
	size_t global_size = 1;
	return clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global_size, nullptr, event_count, events, output_event);
}

CL_EXPORT cl_int CL_API_CALL clEnqueueMarker(cl_command_queue queue, cl_event* output_event)
{
	CL_ENTRY(clEnqueueMarker);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	create_complete_event(queue, CL_COMMAND_MARKER, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueWaitForEvents(cl_command_queue queue, cl_uint event_count, const cl_event* events)
{
	CL_ENTRY(clEnqueueWaitForEvents);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	if (!event_count || !events) return CL_INVALID_VALUE;
	for (cl_uint i = 0; i < event_count; ++i)
	{
		if (!valid_object(events[i])) return CL_INVALID_EVENT;
		if (events[i]->queue->context != queue->context) return CL_INVALID_CONTEXT;
	}
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueBarrier(cl_command_queue queue)
{
	CL_ENTRY(clEnqueueBarrier);
	return valid_object(queue) ? CL_SUCCESS : CL_INVALID_COMMAND_QUEUE;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueMarkerWithWaitList(cl_command_queue queue, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueMarkerWithWaitList);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	create_complete_event(queue, CL_COMMAND_MARKER, output_event);
	return CL_SUCCESS;
}

CL_EXPORT cl_int CL_API_CALL clEnqueueBarrierWithWaitList(cl_command_queue queue, cl_uint event_count, const cl_event* events, cl_event* output_event)
{
	CL_ENTRY(clEnqueueBarrierWithWaitList);
	if (!valid_object(queue)) return CL_INVALID_COMMAND_QUEUE;
	cl_int result = validate_event_wait_list(event_count, events);
	if (result != CL_SUCCESS) return result;
	create_complete_event(queue, CL_COMMAND_BARRIER, output_event);
	return CL_SUCCESS;
}

static void* function_address(const char* name)
{
	if (!name) return nullptr;
#define FUNCTION_ADDRESS(function) if (strcmp(name, #function) == 0) return reinterpret_cast<void*>(function)
	FUNCTION_ADDRESS(clIcdGetPlatformIDsKHR);
	FUNCTION_ADDRESS(clGetPlatformIDs);
	FUNCTION_ADDRESS(clGetPlatformInfo);
	FUNCTION_ADDRESS(clGetDeviceIDs);
	FUNCTION_ADDRESS(clGetDeviceInfo);
	FUNCTION_ADDRESS(clRetainDevice);
	FUNCTION_ADDRESS(clReleaseDevice);
	FUNCTION_ADDRESS(clCreateContext);
	FUNCTION_ADDRESS(clCreateContextFromType);
	FUNCTION_ADDRESS(clRetainContext);
	FUNCTION_ADDRESS(clReleaseContext);
	FUNCTION_ADDRESS(clGetContextInfo);
	FUNCTION_ADDRESS(clCreateCommandQueue);
	FUNCTION_ADDRESS(clCreateCommandQueueWithProperties);
	FUNCTION_ADDRESS(clRetainCommandQueue);
	FUNCTION_ADDRESS(clReleaseCommandQueue);
	FUNCTION_ADDRESS(clGetCommandQueueInfo);
	FUNCTION_ADDRESS(clCreateBuffer);
	FUNCTION_ADDRESS(clCreateBufferWithProperties);
	FUNCTION_ADDRESS(clRetainMemObject);
	FUNCTION_ADDRESS(clReleaseMemObject);
	FUNCTION_ADDRESS(clGetSupportedImageFormats);
	FUNCTION_ADDRESS(clGetMemObjectInfo);
	FUNCTION_ADDRESS(clCreateProgramWithSource);
	FUNCTION_ADDRESS(clRetainProgram);
	FUNCTION_ADDRESS(clReleaseProgram);
	FUNCTION_ADDRESS(clBuildProgram);
	FUNCTION_ADDRESS(clGetProgramInfo);
	FUNCTION_ADDRESS(clGetProgramBuildInfo);
	FUNCTION_ADDRESS(clCreateKernel);
	FUNCTION_ADDRESS(clRetainKernel);
	FUNCTION_ADDRESS(clReleaseKernel);
	FUNCTION_ADDRESS(clSetKernelArg);
	FUNCTION_ADDRESS(clGetKernelInfo);
	FUNCTION_ADDRESS(clGetKernelWorkGroupInfo);
	FUNCTION_ADDRESS(clWaitForEvents);
	FUNCTION_ADDRESS(clGetEventInfo);
	FUNCTION_ADDRESS(clRetainEvent);
	FUNCTION_ADDRESS(clReleaseEvent);
	FUNCTION_ADDRESS(clGetEventProfilingInfo);
	FUNCTION_ADDRESS(clFlush);
	FUNCTION_ADDRESS(clFinish);
	FUNCTION_ADDRESS(clEnqueueReadBuffer);
	FUNCTION_ADDRESS(clEnqueueWriteBuffer);
	FUNCTION_ADDRESS(clEnqueueCopyBuffer);
	FUNCTION_ADDRESS(clEnqueueNDRangeKernel);
	FUNCTION_ADDRESS(clEnqueueMarkerWithWaitList);
	FUNCTION_ADDRESS(clEnqueueBarrierWithWaitList);
#undef FUNCTION_ADDRESS
	return nullptr;
}

CL_EXPORT void* CL_API_CALL clGetExtensionFunctionAddress(const char* name)
{
	CL_ENTRY(clGetExtensionFunctionAddress);
	return function_address(name);
}

CL_EXPORT void* CL_API_CALL clGetExtensionFunctionAddressForPlatform(cl_platform_id platform, const char* name)
{
	CL_ENTRY(clGetExtensionFunctionAddressForPlatform);
	if (platform != &runtime().platform) return nullptr;
	return function_address(name);
}
