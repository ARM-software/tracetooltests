#!/bin/bash -e

git clone https://github.com/google/uVkCompute.git external/uvkcompute
pushd external/uvkcompute
git submodule update --init
mkdir build
cd build
cmake ..
make
popd
