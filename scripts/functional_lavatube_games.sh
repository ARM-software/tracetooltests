#!/bin/bash
#
# This suite contains some open source games.
#

REPORTDIR=$(pwd)/reports/lavatube/games
REPORT=$REPORTDIR/report.html
TRACEDIR=$(pwd)/traces

mkdir -p $TRACEDIR
mkdir -p $REPORTDIR

unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

LAVATUBE_PATH=/work/lava/build
HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan games with lavatube</h1><table><tr><th>Name</th><th>Original</th><th>Replay original swapchain</th><th>Replay virtual swapchain</th></tr>" >> $REPORT

function replay
{
	echo
	echo "** replay $2 **"
	echo
	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$3 $LAVATUBE_PATH/replay traces/$1.vk
	convert -alpha off $3.ppm $REPORTDIR/$1_f${FRAME}_replay.png
	compare -alpha off $REPORTDIR/$1_f$3_native.png $REPORTDIR/$1_f$3_replay.png $REPORTDIR/$1_f$3_compare.png || true
	rm -f *.ppm

	# Replay virtual swapchain
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$3 $LAVATUBE_PATH/replay -v traces/$1.vk
	convert -alpha off $3.ppm $REPORTDIR/$1_f$3_replay_virtual.png
	compare -alpha off $REPORTDIR/$1_f$3_native.png $REPORTDIR/$1_f$3_replay_virtual.png $REPORTDIR/$1_f$3_compare_virtual.png || true
	rm -f *.ppm

	echo "<tr><td>$2</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_replay.png" /><img $HTMLIMGOPTS src="$1_f$3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_replay_virtual.png" /><img $HTMLIMGOPTS src="$1_f$3_compare_virtual.png" /></td>" >> $REPORT

}

# --- vkQuake 1 ---

FRAME=30

echo
echo "****** vkquake1 ******"
echo

echo
echo "** native **"
echo
pushd external/vkquake1/Quake
rm -f *.ppm
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$FRAME ./vkquake -fitz
convert -alpha off $FRAME.ppm $REPORTDIR/vkquake1_f${FRAME}_native.png
rm -f *.ppm

echo
echo "** trace **"
echo
LAVATUBE_DESTINATION=vkquake1 VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube ./vkquake -fitz
mv vkquake1*.vk $TRACEDIR/vkquake1.vk

popd

replay vkquake1 "VkQuake 1" $FRAME

# --- vkQuake 2 ---

FRAME=300

echo
echo "****** vkquake2 ******"
echo
# other relevant options: vk_msaa 0-4, vk_mode -1 -> r_customwidth r_customheight
QUAKE2_OPTS="+set vk_validation 0 +set vk_mode -1 +set vid_full screen 0 +set vk_strings 1 +set timedemo 1 +set map demo1.dm2"
pushd external/vkquake2/linux/debugx64

echo
echo "** native **"
echo
rm -f *.ppm
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$FRAME ./quake2 $QUAKE2_OPTS
convert -alpha off $FRAME.ppm $REPORTDIR/vkquake2_f${FRAME}_native.png
rm -f *.ppm

echo
echo "** trace **"
echo
LAVATUBE_DEBUG=3 LAVATUBE_DEBUG_FILE=$REPORTDIR/vkquake2.txt LAVATUBE_DESTINATION=vkquake2 VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube ./quake2 $QUAKE2_OPTS
mv vkquake2*.vk $TRACEDIR/vkquake2.vk

popd

replay vkquake2 "VkQuake 2" $FRAME

### Epilogue

echo "</table></body></html>" >> $REPORT
