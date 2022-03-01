tracetooltests
==============

This is a repository of tests designed for testing tracing tools.

Building
--------

To build for linux desktop:
--------------------------

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

These environment variables can be set to modify the tests:

TOOLSTEST_TIMES    - the number of frames or loops to run
TOOLSTEST_SANITY   - whether or not to inject sanity checking assert calls
                     (GLES only for now)
TOOLSTEST_NULL_RUN - if set, we will skip testing whether results make sense;
                     useful for generating test runs on fake drivers
                     (GLES only for now)
TOOLSTEST_STEP     - enter step mode where we wait for keypress to proceed to the
                     next frame; while 'q' will exit immediately
                     (GLES only for now)
TOOLSTEST_WINSYS   - change Vulkan winsys; only valid value for now is "headless",
                     which will force the headless extension to be used
                     (Vulkan only for now)

Note that for fake driver runs where TOOLSTEST_NULL_RUN is required and traces are
generated, any traces containing compute jobs will _not_ contain the correct buffer
contents. Not all tests support all environment variables. For the vulkan tests,
usually better to look at their command line options.

Known issues
------------

* The multi-surface tests do not work with the SDL backend.
