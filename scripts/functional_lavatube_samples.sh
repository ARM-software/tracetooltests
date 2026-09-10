#!/bin/bash

if [ "$LAVATUBE_LAYER_PATH" != "" ];
then
	LAVATUBE_REPLAYER="$LAVATUBE_LAYER_PATH/lava-replay"
	LAVATUBE_POSTPROCESSOR="$LAVATUBE_LAYER_PATH/lava-tool -S"
	LAVATUBE_PATH="$LAVATUBE_LAYER_PATH"
else
	LAVATUBE_REPLAYER="/opt/lavatube/bin/lava-replay"
	LAVATUBE_POSTPROCESSOR="/opt/lavatube/bin/lava-tool -S"
	LAVATUBE_PATH="/opt/lavatube"
fi

REPORTDIR=reports/lavatube/samples${TAG}
REPORT=$REPORTDIR/report.html
TRACEDIR=traces${TAG}
APP=build/app/bin/Release/x86_64/vulkan_samples

mkdir -p $TRACEDIR
mkdir -p $REPORTDIR
rm -f $TRACEDIR/sample_*.api
rm -f $REPORTDIR/*.png
rm -f $REPORTDIR/*.html

HTMLIMGOPTS="width=200 height=200"
PARAMS="--benchmark --stop-after-frame 5 --force-close"

# vulkan-samples is configured for XCB, while GLFW may otherwise select
# Wayland dynamically when both backends are available.
export XDG_SESSION_TYPE=x11

unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH
export MESA_VK_ABORT_ON_DEVICE_LOSS=1

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-samples with lavatube</h1><table><tr><th>Name</th><th>Original</th><th>Replay</th><th>Replay postprocess</th></tr>" >> $REPORT

function run
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-samples/*.ppm

	# Native run
	rm -f external/vulkan-samples/*.ppm
	( cd external/vulkan-samples ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $APP sample $1 $PARAMS )
	convert -alpha off external/vulkan-samples/3.ppm $REPORTDIR/sample_$1_f3_native.png
	rm -f external/vulkan-samples/*.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	export LAVATUBE_DESTINATION=sample_$1
	export VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d
	export LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d
	export VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube
	( cd external/vulkan-samples ; $APP sample $1 $PARAMS )
	mv external/vulkan-samples/sample_$1.api $TRACEDIR/

	echo
	echo "** replay $1 virtual **"
	echo

	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $LAVATUBE_REPLAYER $TRACEDIR/sample_$1.api
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_virtual.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_virtual.png $REPORTDIR/sample_$1_f3_compare_virtual.png || true

	echo
	echo "** replay $1 postprocessed **"
	echo
	$LAVATUBE_POSTPROCESSOR $TRACEDIR/sample_$1.api $TRACEDIR/sample_$1_postprocess.api
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $LAVATUBE_REPLAYER $TRACEDIR/sample_$1_postprocess.api
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_postprocess.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_postprocess.png $REPORTDIR/sample_$1_f3_compare_postprocess.png || true

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_virtual.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare_virtual.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_postprocess.png" /><img $HTMLIMGOPTS src="sample_$1_f3_compare_postprocess.png" /></td></tr>" >> $REPORT
}

source scripts/samples_list.sh

echo "</table></body></html>" >> $REPORT
