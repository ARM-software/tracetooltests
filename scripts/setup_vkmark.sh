#!/bin/bash -e

# If you are not using Ubuntu, you need to figure out these yourself
if [ -n "$(uname -a | grep Ubuntu)" ]; then
	sudo apt install meson libglm-dev libassimp-dev libxcb1-dev libxcb-icccm4-dev libwayland-dev wayland-protocols libdrm-dev libgbm-dev
fi

git clone https://github.com/vkmark/vkmark.git external/vkmark
pushd external/vkmark
meson build
ninja -C build
popd
