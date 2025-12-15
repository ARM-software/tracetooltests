#ifndef CL_TARGET_OPENCL_VERSION
#error You need to set CL_TARGET_OPENCL_VERSION
#endif

#pragma once

#include <stdint.h>
#include <stdio.h>

#include <cmath>
#include <vector>
#include <string>
#include <variant>
#include <unordered_set>
#include <unordered_map>
#include <CL/cl.h>
#include <CL/cl_ext.h>

#ifdef NDEBUG
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#else
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#endif

#include "util.h"

// ---- Common code ----

#define MAKEPLATFORMPROCADDR(v, name) \
	name ## _fn pf_ ## name = (name ## _fn)clGetExtensionFunctionAddressForPlatform(v.platform_id, # name); \
	assert(pf_ ## name);

const char* errorString(const int errorCode);

inline void cl_check(int result)
{
	if (result != CL_SUCCESS)
	{
		fprintf(stderr, "Error 0x%04x: %s\n", result, errorString(result));
	}
	assert(result == CL_SUCCESS);
}

struct opencl_req_t;
typedef void (*TOOLSTEST_CL_CALLBACK_USAGE)();
typedef bool (*TOOLSTEST_CL_CALLBACK_CMDOPT)(int& i, int argc, char **argv, opencl_req_t& reqs);

struct opencl_req_t // OpenCL context requirements
{
	TOOLSTEST_CL_CALLBACK_USAGE usage = nullptr;
	TOOLSTEST_CL_CALLBACK_CMDOPT cmdopt = nullptr;
	std::unordered_map<std::string, std::variant<int, bool, std::string>> options;
	std::vector<std::string> extensions;
	uint8_t* device_by_uuid = nullptr; // request a specific device by UUID
	cl_version minApiVersion = 0;
};

struct opencl_setup_t
{
	cl_device_id device_id;
	cl_context context;
	cl_command_queue commands;
	cl_platform_id platform_id;
	benchmarking bench;
	bool device_by_uuid = false; // user requested a specific device by UUID
};

opencl_setup_t cl_test_init(int argc, char** argv, const std::string& testname, opencl_req_t& reqs);
void cl_test_done(opencl_setup_t& cl);

// Utility functions
std::string query_platform_string(cl_platform_id id, cl_platform_info param);
std::string query_device_string(cl_device_id id, cl_device_info param);

template<typename T>
static inline T query_platform(cl_platform_id id, cl_platform_info param)
{
	T value = 0;
	int r = clGetPlatformInfo(id, param, sizeof(T), &value, nullptr);
	assert(r == CL_SUCCESS);
	(void)r;
	return value;
}

template<typename T>
static inline T query_device(cl_device_id id, cl_device_info param)
{
	T value = 0;
	int r = clGetDeviceInfo(id, param, sizeof(T), &value, nullptr);
	assert(r == CL_SUCCESS);
	(void)r;
	return value;
}
