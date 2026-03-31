What
====

A fake Vulkan driver to run native applications on a fake GPU for fast testing or
collecting content metrics. There is no visual output so the application needs to
be fully automated.

Why
===

* For when you need to run Vulkan content really fast for quick tool validation.
* For when you want to quickly collect usage data from static content (eg traces).

How to build
============

Chameleon is built as a Vulkan ICD shared library together with a JSON manifest for
the system Vulkan loader. You can also use `LD_LIBRARY_PATH` and `LD_PRELOAD` if you
wish to link it directly.

How to run
==========

Linux
-----

```
VK_DRIVER_FILES=/path/to/chameleon/build/chameleon_icd.json CHAMELEON_GPU=/path/to/chameleon/GPUs/Mali-G71 ./my_application
```

If CHAMELEON_GPU is pointed to the exact GPU directory you want, then the Vulkan driver will only expose
this one GPU. If it is pointed to the root directory of the GPUs, it will expose all of them (which for
some applications could generate some amusing and/or fairly random results).

If `CHAMELEON_GPU` is not set, Chameleon falls back to `GPUs/Mali-G925`.

You can also bypass the Vulkan loader to inject Chameleon though LD_PRELOAD:

```
CHAMELEON_GPU=/path/to/chameleon/GPUs/Mali-G71 LD_PRELOAD=/path/to/chameleon/build/libvulkan.so ./my_application
```

Logging
-------

Set the environment variable CHAMELEON_LOGGING to enable call logging, one line per function. If the value
is set to "verbose", then we add additional information for the current call on subsequent lines indented
with one tab. If it is set to "full", then the log contains both the verbose information, and all vkCmd*()
calls as well. By default vkCmd*() functions are not shown in the log.

If you want to log to a specific file, set the CHAMELEON_LOG_FILE_PATH environment variable to the full
(absolute) path to the file to store the log in.

If you want to dump all SPIRV shaders to disk, you can set the CHAMELEON_SHADERDUMP environment variable
to "1".

If you want to generate deterministic output every run, you can set the CHAMELEON_DETERMINISTIC environment
variable to "1". This may reduce the amount of information generated where such output would not be generated,
though.

Understanding output
====================

Chameleon can create an overview JSON report and call statistics.

To enable the overview report, set the CHAMELEON_REPORT environment variable to the base filename for your report.
The index number of each instance will be appended to this base filename, and one report will be written out for
each Vulkan instance generated in the run. The report is written out on vkDestroyInstance(). If the environment
variable CHAMELEON_REPORT_HW is set, hardware information will be written out as well, which is usually just the
same as what was given it as input through CHAMELEON_GPU.

You can change the verbosity of the report by changing CHAMELEON_VERBOSITY. Set it to one of 0, 1 or 2.

You can get more detailed output for specific frames by setting CHAMELEON_FRAMES. It can be set to a comma-
separated list of frames.

How it works
============

Chameleon has these components:

1) GPU definition JSON files

   These files define the basic API interface to the GPU.

Writing a GPU
=============

Create a new directory under 'GPUs'. Populate it with the following files:

1) gpu.json -- JSON definition for this GPU in vkjson format. You can use one for an existing GPU by
   downloading it from vulkan.gpuinfo.org
2) gpu_override.json -- JSON definitions for this GPU that cannot fit in gpu.json because it does
   not belong to vkjson format. Includes for example memory heaps and memory types description. You must
   likely write this yourself, but the information is provided by the above page.
