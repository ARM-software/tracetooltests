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

const char* errorString(const int errorCode);

inline void check(int result)
{
	if (result != CL_SUCCESS)
	{
		fprintf(stderr, "Error 0x%04x: %s\n", result, errorString(result));
	}
	assert(result == CL_SUCCESS);
}

struct opencl_req_t;
typedef void (*TOOLSTEST_CALLBACK_USAGE)();
typedef bool (*TOOLSTEST_CALLBACK_CMDOPT)(int& i, int argc, char **argv, opencl_req_t& reqs);

struct opencl_req_t // OpenCL context requirements
{
	TOOLSTEST_CALLBACK_USAGE usage = nullptr;
	TOOLSTEST_CALLBACK_CMDOPT cmdopt = nullptr;
	std::unordered_map<std::string, std::variant<int, bool, std::string>> options;
};

struct opencl_setup_t
{
	cl_device_id device_id;
	cl_context context;
	cl_command_queue commands;

	std::unordered_set<std::string> instance_extensions;
	benchmarking bench;
};

opencl_setup_t cl_test_init(int argc, char** argv, const std::string& testname, opencl_req_t& reqs);
void cl_test_done(opencl_setup_t& cl, bool shared_instance = false);
