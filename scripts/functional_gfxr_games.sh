#!/bin/bash
#
# This suite contains some open source games.
#

if [ "$LAYERPATH" != "" ];
then
	REPLAYER="$LAYERPATH/gfxrecon-replay"
	TRACER="$LAYERPATH/gfxrecon-capture-vulkan.py --capture-layer $LAYERPATH"
fi

REPORTDIR=$(pwd)/reports/gfxr/games
REPORT=$REPORTDIR/report.html
TRACEDIR=$(pwd)/traces
REPLAYER=${REPLAYER:-"$(which gfxrecon-replay)"}
TRACER=${TRACER:-"$(which gfxrecon-capture-vulkan.py)"}

mkdir -p $TRACEDIR
mkdir -p $REPORTDIR

unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan games with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th><th>Replay</th><th>Replay -m remap</th><th>Replay -m realign</th><th>Replay -m rebind</th></tr>" >> $REPORT

function replay
{
	echo
	echo "** replay $2 **"
	echo
	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$3 ${REPLAYER} traces/$1.gfxr
	convert -alpha off $3.ppm $REPORTDIR/$1_f${FRAME}_replay.png
	compare -alpha off $REPORTDIR/$1_f$3_native.png $REPORTDIR/$1_f$3_replay.png $REPORTDIR/$1_f$3_compare.png || true
	rm -f *.ppm

	# Replay -m remap
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$3 ${REPLAYER} -m remap traces/$1.gfxr
	convert -alpha off $3.ppm $REPORTDIR/$1_f$3_replay_remap.png
	compare -alpha off $REPORTDIR/$1_f$3_native.png $REPORTDIR/$1_f$3_replay_remap.png $REPORTDIR/$1_f$3_compare_remap.png || true
	rm -f *.ppm

	# Replay -m realign
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$3 ${REPLAYER} -m realign traces/$1.gfxr
	convert -alpha off $3.ppm $REPORTDIR/$1_f$3_replay_realign.png
	compare -alpha off $REPORTDIR/$1_f$3_native.png $REPORTDIR/$1_f$3_replay_realign.png $REPORTDIR/$1_f$3_compare_realign.png || true
	rm -f *.ppm

	# Replay -m rebind
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$3 ${REPLAYER} -m rebind traces/$1.gfxr
	convert -alpha off $3.ppm $REPORTDIR/$1_f$3_replay_rebind.png
	compare -alpha off $REPORTDIR/$1_f$3_native.png $REPORTDIR/$1_f$3_replay_rebind.png $REPORTDIR/$1_f$3_compare_rebind.png || true
	rm -f *.ppm

	echo "<tr><td>$2</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_replay.png" /><img $HTMLIMGOPTS src="$1_f$3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_replay_remap.png" /><img $HTMLIMGOPTS src="$1_f$3_compare_remap.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_replay_realign.png" /><img $HTMLIMGOPTS src="$1_f$3_compare_realign.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f$3_replay_rebind.png" /><img $HTMLIMGOPTS src="$1_f$3_compare_rebind.png" /></td>" >> $REPORT

}

# --- vkQuake 1 ---

function vkquake1
{
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
	${TRACER} -o vkquake1.gfxr ./vkquake -fitz
	mv vkquake1*.gfxr $TRACEDIR/vkquake1.gfxr

	popd
	replay vkquake1 "VkQuake 1" $FRAME
}

# --- vkQuake 2 ---

function vkquake2
{
	FRAME=300

	echo
	echo "****** vkquake2 ******"
	echo
	QUAKE2_OPTS="+set vk_validation 0 +set vid_fullscreen 0 +set vk_strings 1 +set timedemo 1 +set map demo1.dm2"
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
	${TRACER} -o vkquake2.gfxr ./quake2 $QUAKE2_OPTS
	mv vkquake2*.gfxr $TRACEDIR/vkquake2.gfxr

	popd
	replay vkquake2 "VkQuake 2" $FRAME
}

### Run list

vkquake1
vkquake2

### Epilogue

echo "</table></body></html>" >> $REPORT
