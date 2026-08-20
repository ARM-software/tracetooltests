#!/bin/bash

if [[ -z "${GFXR_LAYERPATH:-}" ]]; then
	echo "Error: GFXR_LAYERPATH must be set" >&2
	exit 1
fi
if [[ -z "${LAVATUBE_LAYERPATH:-}" ]]; then
	echo "Error: LAVATUBE_LAYERPATH must be set" >&2
	exit 1
fi

GFXR_REPLAYER="${GFXR_LAYERPATH}/gfxrecon-replay"
GFXR_OPTIMIZER="${GFXR_LAYERPATH}/gfxrecon-optimize"
GFXR_TRACER="${GFXR_LAYERPATH}/gfxrecon-capture-vulkan.py --capture-layer ${GFXR_LAYERPATH}"
LAVATUBE_REPLAYER="$LAVATUBE_LAYERPATH/lava-replay"
LAVATUBE_PATH="$LAVATUBE_LAYERPATH"
LAVATUBE_CONVERTER="$LAVATUBE_LAYERPATH/lava-gfxr-import"
LAVATUBE_OPTIMIZER="$LAVATUBE_LAYERPATH/lava-tool -S"

REPORTDIR=reports/combined/demos
REPORT=$REPORTDIR/report.html
DEMO_PARAMS="--benchmark -bfs 100 -bw 0"
TRACEDIR=traces_combined
TIMER="/usr/bin/time -f %U -o $(pwd)/time.txt"

rm -rf external/vulkan-demos/*.ppm *.ppm $TRACEDIR/demo_*.gfxr $REPORTDIR external/vulkan-demos/*.gfxr $TRACEDIR/demo_*.api $REPORTDIR/*.png $REPORTDIR/*.html
mkdir -p $TRACEDIR $REPORTDIR

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-demos with gfxreconstruct and lavatube </h1><table><tr><th>Name</th><th>Original</th>" >> $REPORT
echo "<th>Replay - gfxr</th><th>Replay - lavatube</th><th>Replay - lavatube post-processed</th><th>Replay - gfxr to lavatube</th></tr>" >> $REPORT

function demo_runner
{
	NAME="demo_$1"

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-demos/*.ppm external/vulkan-demos/*.gfxr *.ppm

	# Native screenshot run
	NFPS=$(( cd external/vulkan-demos ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER build/bin/$1 $DEMO_PARAMS ) | grep fps | sed 's/fps    : //')
	NTIME=$(cat time.txt)
	convert -alpha off external/vulkan-demos/3.ppm $REPORTDIR/${NAME}_f3_native.png
	rm -f external/vulkan-demos/*.ppm *.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace - gfxr
	( cd external/vulkan-demos ; ${GFXR_TRACER} -o ${NAME}.gfxr build/bin/$1 $DEMO_PARAMS )
	$GFXR_OPTIMIZER external/vulkan-demos/${NAME}*.gfxr $TRACEDIR/${NAME}.gfxr
	$LAVATUBE_CONVERTER $TRACEDIR/${NAME}.gfxr $TRACEDIR/${NAME}_converted.api
	rm -f external/vulkan-demos/${NAME}*.gfxr # delete non-optimized original

	# Make trace - lavatube
	export LAVATUBE_DESTINATION=${NAME}.api
	export VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d
	export LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d
	export VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube
	( cd external/vulkan-demos/build/bin ; ./$1 -g 0 $DEMO_PARAMS )
	$LAVATUBE_OPTIMIZER external/vulkan-demos/build/bin/demo_$1.api $TRACEDIR/demo_$1_optimized.api # some content require post-processing
	mv external/vulkan-demos/build/bin/demo_$1.api $TRACEDIR/
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	unset LD_LIBRARY_PATH

	echo
	echo "** replay $1 **"
	echo

	# Replay - gfxr
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $GFXR_REPLAYER -m rebind --measurement-frame-range 1-99999 $TRACEDIR/${NAME}.gfxr
	GFXRTIME=$(cat time.txt)
	RFPSRBSS=$(grep -e fps gfxrecon-measurements.json | sed 's/.*: //'| sed 's/,//')
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_gfxr.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_gfxr.png $REPORTDIR/${NAME}_f3_compare_gfxr.png || true
	rm -f *.ppm gfxrecon-measurements.json

	# Replay - lavatube (without post-processing)
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $LAVATUBE_REPLAYER --gpu $TRACEDIR/${NAME}.api
	LAVATIME=$(cat time.txt)
	RFPS=$(cat lavaresults.json | grep fps | sed 's/.*: //'| sed 's/,//')
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_lavatube.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_lavatube.png $REPORTDIR/${NAME}_f3_compare_lavatube.png || true
	rm -f *.ppm lavaresults.json

	# Replay - lavatube (with post-processing)
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $LAVATUBE_REPLAYER --gpu $TRACEDIR/${NAME}_optimized.api
	LAVATIMEPP=$(cat time.txt)
	RFPSPP=$(cat lavaresults.json | grep fps | sed 's/.*: //'| sed 's/,//')
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_lavatube_pp.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_lavatube_pp.png $REPORTDIR/${NAME}_f3_compare_lavatube_pp.png || true
	rm -f *.ppm lavaresults.json

	# Replay - lavatube converted
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $LAVATUBE_REPLAYER --gpu $TRACEDIR/${NAME}_converted.api
	CONVTIME=$(cat time.txt)
	RFPSC=$(cat lavaresults.json | grep fps | sed 's/.*: //'| sed 's/,//')
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_converted.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_converted.png $REPORTDIR/${NAME}_f3_compare_converted.png || true
	rm -f *.ppm lavaresults.json

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_native.png" /><br>cpu time: $NTIME<br>native fps: $NFPS</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_gfxr.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_gfxr.png" /><br>cpu replay time: $GFXRTIME<br>fps: $RFPSRBSS</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_lavatube.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_lavatube.png" /><br>cpu replay time: $LAVATIME<br>fps: $RFPS</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_lavatube_pp.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_lavatube_pp.png" /><br>cpu replay time: $LAVATIMEPP<br>fps: $RFPSPP</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_converted.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_converted.png" /><br>cpu replay time: $CONVTIME<br>fps: $RFPSC</td>" >> $REPORT
	echo "</tr>" >> $REPORT
}

function demo
{
	echo
	echo "****** $1 ******"
	echo

	demo_runner $1
}

source scripts/demo_list.sh

echo "</table></body></html>" >> $REPORT
