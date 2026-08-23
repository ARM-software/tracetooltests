#!/usr/bin/env python3

import opencl_spec

SUPPORTED_COMMANDS = [
    "clGetPlatformIDs",
    "clGetPlatformInfo",
    "clGetDeviceIDs",
    "clGetDeviceInfo",
    "clRetainDevice",
    "clReleaseDevice",
    "clCreateContext",
    "clCreateContextFromType",
    "clRetainContext",
    "clReleaseContext",
    "clGetContextInfo",
    "clCreateCommandQueue",
    "clCreateCommandQueueWithProperties",
    "clRetainCommandQueue",
    "clReleaseCommandQueue",
    "clGetCommandQueueInfo",
    "clCreateBuffer",
    "clCreateBufferWithProperties",
    "clCreateImage",
    "clCreateImageWithProperties",
    "clCreateImage2D",
    "clCreateImage3D",
    "clRetainMemObject",
    "clReleaseMemObject",
    "clGetSupportedImageFormats",
    "clGetMemObjectInfo",
    "clGetImageInfo",
    "clCreateSampler",
    "clCreateSamplerWithProperties",
    "clRetainSampler",
    "clReleaseSampler",
    "clGetSamplerInfo",
    "clCreateProgramWithSource",
    "clRetainProgram",
    "clReleaseProgram",
    "clBuildProgram",
    "clSetProgramReleaseCallback",
    "clUnloadCompiler",
    "clUnloadPlatformCompiler",
    "clGetProgramInfo",
    "clGetProgramBuildInfo",
    "clCreateKernel",
    "clRetainKernel",
    "clReleaseKernel",
    "clSetKernelArg",
    "clGetKernelInfo",
    "clGetKernelWorkGroupInfo",
    "clWaitForEvents",
    "clGetEventInfo",
    "clRetainEvent",
    "clReleaseEvent",
    "clGetEventProfilingInfo",
    "clFlush",
    "clFinish",
    "clEnqueueReadBuffer",
    "clEnqueueWriteBuffer",
    "clEnqueueCopyBuffer",
    "clEnqueueReadImage",
    "clEnqueueWriteImage",
    "clEnqueueCopyImage",
    "clEnqueueCopyImageToBuffer",
    "clEnqueueCopyBufferToImage",
    "clEnqueueFillImage",
    "clEnqueueMapBuffer",
    "clEnqueueMapImage",
    "clEnqueueUnmapMemObject",
    "clEnqueueNDRangeKernel",
    "clSetCommandQueueProperty",
    "clEnqueueMarker",
    "clEnqueueWaitForEvents",
    "clEnqueueBarrier",
    "clEnqueueTask",
    "clEnqueueMarkerWithWaitList",
    "clEnqueueBarrierWithWaitList",
    "clGetExtensionFunctionAddress",
    "clGetExtensionFunctionAddressForPlatform",
]

def main():
    commands = opencl_spec.command_names()
    details = opencl_spec.command_details()
    dispatch_commands = opencl_spec.icd_dispatch_commands()
    missing = sorted(set(SUPPORTED_COMMANDS) - set(commands))
    if missing:
        raise RuntimeError(f"Commands missing from cl.xml: {', '.join(missing)}")
    stub_commands = [
        name
        for name in commands
        if name in dispatch_commands
        and name not in SUPPORTED_COMMANDS
    ]
    extension_commands = opencl_spec.extension_commands()
    extension_dependencies = opencl_spec.extension_dependencies()
    implemented_commands = set(SUPPORTED_COMMANDS) | {"clIcdGetPlatformIDsKHR"}
    supported_extensions = {
        name
        for name, required_commands in extension_commands.items()
        if required_commands.issubset(implemented_commands)
    }
    supported_extensions.add("cl_khr_icd")
    changed = True
    while changed:
        changed = False
        for name in list(supported_extensions):
            if not extension_dependencies.get(name, set()).issubset(supported_extensions):
                supported_extensions.remove(name)
                changed = True
    supported_extensions = sorted(supported_extensions)

    with open("opencl_auto.h", "w", encoding="utf-8") as header:
        print("// This file contains only auto-generated code!", file=header)
        print("#pragma once", file=header)
        print("#include <cstdint>", file=header)
        print("#include <CL/cl_icd.h>", file=header)
        print(file=header)
        for name in commands:
            print(f"extern uint64_t count_{name};", file=header)
        print(file=header)
        print("void initialize_opencl_dispatch(cl_icd_dispatch& dispatch);", file=header)
        print("bool supported_opencl_extension(const char* name);", file=header)
        print("void save_opencl_counts(const char* filename);", file=header)

    with open("opencl_auto.cpp", "w", encoding="utf-8") as source:
        print("// This file contains only auto-generated code!", file=source)
        print('#include "opencl_auto.h"', file=source)
        print("#include <cstdio>", file=source)
        print("#include <cstring>", file=source)
        print(file=source)
        for name in commands:
            print(f"uint64_t count_{name} = 0;", file=source)
        print(file=source)
        for name in SUPPORTED_COMMANDS:
            command = details[name]
            parameters = ", ".join(
                declaration for declaration, _parameter_name in command["parameters"]
            )
            print(
                f'extern "C" {command["return_type"]} CL_API_CALL {name}({parameters});',
                file=source,
            )
        print(file=source)
        for name in stub_commands:
            command = details[name]
            parameters = ", ".join(
                declaration for declaration, _parameter_name in command["parameters"]
            )
            print(
                f'extern "C" {command["return_type"]} CL_API_CALL {name}({parameters})',
                file=source,
            )
            print("{", file=source)
            print(f"\t++count_{name};", file=source)
            for _declaration, parameter_name in command["parameters"]:
                print(f"\t(void){parameter_name};", file=source)
            if any(
                parameter_name == "errcode_ret"
                for _declaration, parameter_name in command["parameters"]
            ):
                print("\tif (errcode_ret) *errcode_ret = CL_INVALID_OPERATION;", file=source)
            if command["return_type"] == "cl_int":
                print("\treturn CL_INVALID_OPERATION;", file=source)
            elif command["return_type"] != "void":
                print("\treturn nullptr;", file=source)
            print("}", file=source)
            print(file=source)
        print("void initialize_opencl_dispatch(cl_icd_dispatch& dispatch)", file=source)
        print("{", file=source)
        print("\tdispatch = {};", file=source)
        for name in SUPPORTED_COMMANDS:
            print(f"\tdispatch.{name} = {name};", file=source)
        for name in stub_commands:
            print(f"\tdispatch.{name} = {name};", file=source)
        print("}", file=source)
        print(file=source)
        print("bool supported_opencl_extension(const char* name)", file=source)
        print("{", file=source)
        print("\tif (!name) return false;", file=source)
        for name in supported_extensions:
            print(f'\tif (strcmp(name, "{name}") == 0) return true;', file=source)
        print("\treturn false;", file=source)
        print("}", file=source)
        print(file=source)
        print("void save_opencl_counts(const char* filename)", file=source)
        print("{", file=source)
        print('\tFILE* fp = fopen(filename, "w");', file=source)
        print("\tif (!fp) return;", file=source)
        print('\tfprintf(fp, "Function,Count\\n");', file=source)
        for name in commands:
            print(f'\tif (count_{name}) fprintf(fp, "{name},%lu\\n", (unsigned long)count_{name});', file=source)
        print("\tfclose(fp);", file=source)
        print("}", file=source)

if __name__ == "__main__":
    main()
