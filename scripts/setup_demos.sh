#!/bin/bash

git clone https://github.com/SaschaWillems/Vulkan.git external/vulkan-demos
pushd external/vulkan-demos
./download_assets.py
git submodule update --init --recursive
mkdir -p build
ln -s "$(pwd)/data" "$(pwd)/build/data"
cd build
cmake -DCMAKE_BUILD_TYPE:STRING=Debug -DCMAKE_CXX_FLAGS_DEBUG:STRING=-g ..
make
popd
