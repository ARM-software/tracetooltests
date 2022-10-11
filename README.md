tracetooltests
==============

This is a repository of tests designed for testing tracing tools.

Building
--------

To build for linux desktop:
--------------------------

You may need to install the LunarG SDK first to get up to date headers.

```
mkdir build
cd build
cmake ..
make
```

See below for selecting your window system backend.

Linux cross-compile
-------------------

```
mkdir build_name
cd build_name
```

Then ONE of the following for ARMv7 or ARMv8, respectively:
```
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fbdev_arm.cmake ..
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/fbdev_aarch64.cmake ..
```

Then complete as normal with:
```
make
```

Building for other backends
---------------------------

The default backend is fbdev, which is not supported on many systems.
The change backend, pass -DWINDOWSYSTEM=<name of backend> where the
backend can be one of the following:

* sdl
* fbdev
* pbuffers
* x11

The Vulkan tests are currently not using any window system.

Modifying runs
--------------

Most tests have command line parameters. To see which, run it with --help

These environment variables can be set to modify the tests:

* TOOLSTEST_TIMES    - the number of frames or loops to run
* TOOLSTEST_SANITY   - whether or not to inject sanity checking assert calls
  (GLES only for now)
* TOOLSTEST_NULL_RUN - if set, we will skip testing whether results make sense;
  useful for generating test runs on fake drivers (GLES only for now)
* TOOLSTEST_STEP     - enter step mode where we wait for keypress to proceed to
  the next frame; while 'q' will exit immediately (GLES only for now)
* TOOLSTEST_WINSYS   - change Vulkan winsys; only valid value for now is "headless",
  which will force the headless extension to be used (Vulkan only for now)
* TOOLSTEST_GPU      - select which physical device to use by index value

Note that for fake driver runs where TOOLSTEST_NULL_RUN is required and traces are
generated, any traces containing compute jobs will _not_ contain the correct buffer
contents. Not all tests support all environment variables. For the vulkan tests,
usually better to look at their command line options.

Private Vulkan extensions
-------------------------

VK_TRACETOOLTEST_checksum_validation - defines a new command
vkAssertBufferTRACETOOLTEST(VkDevice device, VkBuffer buffer) which injects and
returns an Adler32 checksum of the given buffer into the command stream. This can
be used to validate tools like tracers.
vkAssertImageTRACETOOLTEST(VkDevice device, VkImage image) planned.

VK_TRACETOOLTEST_object_property - defines a new command
vkGetDeviceTracingObjectPropertyTRACETOOLTEST(VkDevice device, VkObjectType objectType,
uint64_t objectHandle, VkTracingObjectPropertyTRACETOOLTEST valueType) which can request
layer internal information from the layer supporting this extension. 'valueType' can be
one of the following enums prefixed with VK_TRACING_OBJECT_PROPERTY_:
* ALLOCATIONS_COUNT_TRACETOOLTEST - number of current GPU memory allocations
* UPDATES_COUNT_TRACETOOLTEST - number of memory tracking updates done
* UPDATES_BYTES_TRACETOOLTEST - number of bytes done in memory tracking updates
* BACKING_DEVICEMEMORY_TRACETOOLTEST - the vulkan device memory of an image or buffer

VK_TRACETOOLTEST_benchmarking - defines a structure that can be "fetched" from
vkGetPhysicalDeviceFeatures2() and "returned" to vkCreateDevice() to negotiate the details of
a benchmarking run. The application should check if the extension is supported before
creating a Vulkan device, and request the desired parameters with a call to
vkGetPhysicalDeviceFeatures2() with the structure passed in as an extension. On device
creation the structure shall be filled out with the benchmarking details actually
fulfilled by the application and passed to vkCreateDevice() as an extension structure.

Structure details:
* VkFlags flags - reserved for future use
* uint32_t fixedTimeStep - if non-zero, run in a fixed timestep mode, always rendering the
  same content in the same number of frames; may be passed with a different value than
  requested
* VkBool32 disablePerformanceAdaptation - disable dynamic graphics adaptations to improve
  performance
* VkBool32 disableVendorAdaptation - disable use of vendor specific adaptations to improve
  performance or rendering quality
* VkBool32 disableLoadingFrames - do not render anything during application loading; this
  may be important when running in very slow simulation environments
* uint32_t visualSettings - visual quality, where one is best; if the best possible
  quality cannot be achieved on the host platform, the value shall not be returned as one;
  if zero then no override of visual settings
* uint32_t scenario - the scenario to run, return zero if fetched value is not supported
* uint32_t loopTime - the number of seconds to loop the main body of the content; this is
  useful for power usage benchmarking
* VkTracingFlagsTRACETOOLTEST tracingFlags - see below

For visualSettings and scenario the values supported by the application and what they mean
can be deduced by trial and error or by asking the application developer.

VkTracingFlagsTRACETOOLTEST, prefixed with VK_TRACING_:
* NO_COHERENT_MEMORY_BIT_TRACETOOLTEST - behave as if no coherent memory exists
* NO_SUBALLOCATION_BIT_TRACETOOLTEST - only use dedicated memory allocation
* NO_MEMORY_ALIASING_BIT_TRACETOOLTEST - do not use memory aliasing
* NO_POINTER_OFFSETS_BIT_TRACETOOLTEST - do not generate memory addresses with an offset from
  pointers from GetDeviceBufferAddress()
* NO_JUST_IN_TIME_REUSE_BIT_TRACETOOLTEST - do not reuse resources as soon as possible but wait
  at least 3 frames

Private GLES extensions
-----------------------

TBD

Known issues
------------

* The multi-surface tests do not work with the SDL backend.
