#!/bin/bash
mkdir -p tmp
( cd tmp ; wget -N https://powerboard4.basemark.com/distribution/amd64-ubuntu.amd64-launcher.relicoflife-free/relicoflife_linux_x64_launcher_free_1.0.0.tar.gz )
( cd tmp ; wget -N https://powerboard4.basemark.com/distribution/sacredpath-1.0.2/amd64_ubuntu20_amd64_launcher_sacredpath_free_1.0.2.tar.gz )
mkdir -p external/relicoflife
( cd external/relicoflife/ ; tar xzvkf ../../tmp/relicoflife_linux_x64_launcher_free_1.0.0.tar.gz )
mkdir -p external/sacredpath
( cd external/sacredpath/ ; tar xzvkf ../../tmp/amd64_ubuntu20_amd64_launcher_sacredpath_free_1.0.2.tar.gz )
mv external/sacredpath/linux-unpacked/* external/sacredpath/
rmdir external/sacredpath/linux-unpacked
