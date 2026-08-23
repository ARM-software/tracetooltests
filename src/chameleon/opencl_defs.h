#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <CL/cl.h>
#include <CL/cl_icd.h>

static constexpr uint64_t CHAMELEON_OPENCL_MAGIC = 0x43484d434c4f424aULL;

struct _cl_platform_id
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
};

struct _cl_device_id
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
};

struct _cl_context
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_device_id device = nullptr;
	std::vector<cl_context_properties> properties;
};

struct _cl_command_queue
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_context context = nullptr;
	cl_device_id device = nullptr;
	cl_command_queue_properties properties = 0;
};

struct _cl_mem
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_context context = nullptr;
	cl_mem_flags flags = 0;
	cl_mem_object_type type = CL_MEM_OBJECT_BUFFER;
	void* host_ptr = nullptr;
	cl_mem parent_memory = nullptr;
	cl_image_format image_format = {};
	cl_image_desc image_description = {};
	size_t image_element_size = 0;
	size_t image_row_pitch = 0;
	size_t image_slice_pitch = 0;
	std::vector<unsigned char> data;
};

struct _cl_sampler
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_context context = nullptr;
	cl_bool normalized_coordinates = CL_TRUE;
	cl_addressing_mode addressing_mode = CL_ADDRESS_CLAMP;
	cl_filter_mode filter_mode = CL_FILTER_NEAREST;
};

struct OpenCLProgramReleaseCallback
{
	void (CL_CALLBACK* notify)(cl_program, void*) = nullptr;
	void* user_data = nullptr;
};

struct _cl_program
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_context context = nullptr;
	std::string source;
	std::string options;
	std::string build_log;
	cl_build_status build_status = CL_BUILD_NONE;
	std::vector<OpenCLProgramReleaseCallback> release_callbacks;
};

struct _cl_kernel
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_program program = nullptr;
	std::string name;
	std::vector<std::vector<unsigned char>> arguments;
};

struct _cl_event
{
	const cl_icd_dispatch* dispatch = nullptr;
	uint64_t magic = CHAMELEON_OPENCL_MAGIC;
	std::atomic_uint references = 1;
	cl_command_queue queue = nullptr;
	cl_command_type command_type = 0;
	cl_int status = CL_COMPLETE;
};
