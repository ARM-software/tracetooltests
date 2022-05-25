#!/bin/bash

mkdir -p external
git clone https://github.com/KhronosGroup/Vulkan-Samples.git external/vulkan-samples
pushd external/vulkan-samples
git submodule update --init
cmake -G "Unix Makefiles" -H. -Bbuild/linux -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux --config Debug --target vulkan_samples -- -j8
popd
