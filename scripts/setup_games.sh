#!/bin/bash
#

# If you are not using Ubuntu, you need to figure out these yourself
if [ -n "$(uname -a | grep Ubuntu)" ]; then
	sudo apt install lhasa libxxf86dga-dev libxxf86vm-dev git make gcc libsdl2-dev libvorbis-dev libmad0-dev libx11-xcb-dev
fi

# This clone uses original Quake1 shareware files. These are not freely redistributable
# and must be obtained from ID Software.
git clone https://github.com/Novum/vkQuake.git external/vkquake1
pushd external/vkquake1
patch -p1 < ../../patches/vkquake1_patch.diff
cd Quake
make
wget -q https://ftp.gwdg.de/pub/misc/ftp.idsoftware.com/idstuff/quake/quake106.zip
unzip quake106.zip
lhasa x resource.1 id1/pak0.pak
popd

# This demo uses "Quake 2 Demo content to showcase Vulkan functionality" according to its
# wiki. This demo content by Id Software is freely redistributable but not open source.
#
# You may have to install libxxf86dga-dev and libxxf86vm-dev on Ubuntu (at least).
#
git clone https://github.com/kondrak/vkQuake2.git external/vkquake2
pushd external/vkquake2
patch -p1 < ../../patches/vkquake2_patch.diff
cd linux
make debug
cd debugx64
wget -q https://github.com/kondrak/vkQuake2/releases/download/1.1.1/vkquake2-1.1.1_win32.zip
unzip vkquake2-1.1.1_win32.zip vkquake2-1.1.1_win32/baseq2/pak0.pak
mv vkquake2-1.1.1_win32/baseq2/pak0.pak baseq2/
popd
