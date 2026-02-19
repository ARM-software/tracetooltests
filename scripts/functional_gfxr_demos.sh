#!/bin/bash

if [ "$LAYERPATH" != "" ];
then
	REPLAYER="${LAYERPATH}/gfxrecon-replay"
	OPTIMIZER="${LAYERPATH}/gfxrecon-optimize"
	TRACER="${LAYERPATH}/gfxrecon-capture-vulkan.py --capture-layer ${LAYERPATH}"
fi

REPORTDIR=reports/gfxr/demos${TAG}
REPORT=$REPORTDIR/report.html
CSV=$REPORTDIR/report.csv
DEMO_PARAMS="--benchmark -bfs 100 -bw 0"
TRACEDIR=traces${TAG}
REPLAYER=${REPLAYER:-"$(which gfxrecon-replay)"}
OPTIMIZER=${OPTIMIZER:-"$(which gfxrecon-optimize)"}
TRACER=${TRACER:-"$(which gfxrecon-capture-vulkan.py)"}
TIMER="/usr/bin/time -f %U -o $(pwd)/time.txt"

rm -rf external/vulkan-demos/*.ppm *.ppm $TRACEDIR/demo_*.gfxr $REPORTDIR external/vulkan-demos/*.gfxr
mkdir -p $TRACEDIR $REPORTDIR

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-demos with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th>" >> $REPORT
echo "<th>Replay -m rebind</th><th>Replay offscreen</th><th>Replay trim</th></tr>" >> $REPORT

echo "Test,Native Time,Capture time,Replay time,Native FPS,Replay FPS,Replay time perf mode,Replay FPS perf mode" > $CSV

function demo_runner
{
	NAME="demo_$1_$3"

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-demos/*.ppm external/vulkan-demos/*.gfxr *.ppm

	# Native timing run
	NFPS=$(( cd external/vulkan-demos ; $TIMER build/bin/$1 $DEMO_PARAMS )| grep fps | sed 's/fps    : //')
	NTIME=$(cat time.txt)

	# Native screenshot run
	( cd external/vulkan-demos ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER build/bin/$1 $DEMO_PARAMS )
	STIME=$(cat time.txt)
	convert -alpha off external/vulkan-demos/3.ppm $REPORTDIR/${NAME}_f3_native.png
	rm -f external/vulkan-demos/*.ppm *.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	( cd external/vulkan-demos ; ${TRACER} -o ${NAME}.gfxr $TIMER build/bin/$1 $DEMO_PARAMS )
	CTIME=$(cat time.txt)
	$OPTIMIZER external/vulkan-demos/${NAME}*.gfxr $TRACEDIR/${NAME}.gfxr
	rm -f external/vulkan-demos/${NAME}*.gfxr # delete non-optimized original

	echo
	echo "** replay $1 using $REPLAYER **"
	echo

	# Replay -m rebind
	$TIMER $REPLAYER -m rebind --vssb --measurement-frame-range 1-99999 $TRACEDIR/${NAME}.gfxr
	RTIMERB=$(cat time.txt)
	RFPSRB=$(grep -e fps gfxrecon-measurements.json | sed 's/.*: //'| sed 's/,//')
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $REPLAYER -m rebind --measurement-frame-range 1-99999 $TRACEDIR/${NAME}.gfxr
	RTIMERBSS=$(cat time.txt)
	RFPSRBSS=$(grep -e fps gfxrecon-measurements.json | sed 's/.*: //'| sed 's/,//')
	convert -alpha off 3.ppm $REPORTDIR/${NAME}_f3_replay_rebind.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_rebind.png $REPORTDIR/${NAME}_f3_compare_rebind.png || true
	rm -f *.ppm gfxrecon-measurements.json

	# Replay --swapchain offscreen
	$TIMER $REPLAYER --swapchain offscreen -m rebind $TRACEDIR/${NAME}.gfxr
	RTIMEOFF=$(cat time.txt)
	RFPSOFF=$(grep -e fps gfxrecon-measurements.json | sed 's/.*: //'| sed 's/,//')
	$TIMER $REPLAYER --swapchain offscreen --screenshots 4 --screenshot-format png -m rebind --measurement-frame-range 1-99999 $TRACEDIR/${NAME}.gfxr
	RTIMEOFFSS=$(cat time.txt)
	convert -alpha off screenshot_frame_4.png $REPORTDIR/${NAME}_f3_replay_offscreen.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_replay_offscreen.png $REPORTDIR/${NAME}_f3_compare_offscreen.png || true
	rm -f *.png gfxrecon-measurements.json

	# Make fastforwarded trace from existing trace
	${TRACER} -f 3-5 -o ${NAME}_ff.gfxr $REPLAYER -m none $TRACEDIR/$NAME.gfxr
	$OPTIMIZER ${NAME}_ff* $TRACEDIR/${NAME}_ff_frame3.gfxr
	rm -f ${NAME}_ff* # delete non-optimized original

	# Run the fastforwarded trace
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=1 $TIMER $REPLAYER -m rebind $TRACEDIR/${NAME}_ff_frame3.gfxr
	RTIMEFF=$(cat time.txt)
	convert -alpha off 1.ppm $REPORTDIR/${NAME}_f3_ff_replay.png
	compare -alpha off $REPORTDIR/${NAME}_f3_native.png $REPORTDIR/${NAME}_f3_ff_replay.png $REPORTDIR/${NAME}_f3_compare_ff.png || true

	echo "<tr><td>$1 $3</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_native.png" /><br>native cpu: $NTIME<br>screenshot cpu: $STIME<br>native fps: $NFPS<br>tracing cpu: $CTIME</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_rebind.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_rebind.png" /><br>cpu: $RTIMERBSS<br>fps: $RFPSRBSS<br>cpu perf: $RTIMERB<br>fps perf: $RFPSRB</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_replay_offscreen.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_offscreen.png" /><br>cpu: $RTIMEOFF<br>cpu (w/ snap): $RTIMEOFFSS<br>fps: $RFPSOFF</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="${NAME}_f3_ff_replay.png" /><br><img $HTMLIMGOPTS src="${NAME}_f3_compare_ff.png" /><br>cpu: $RTIMEFF</td>" >> $REPORT
	echo "</tr>" >> $REPORT

	echo "$1,$NTIME,$CTIME,$RTIMERBSS,$NFPS,$RFPSRBSS,$RTIMERB,$RFPSRB" >> $CSV
}

function demo
{
	echo
	echo "****** $1 ******"
	echo

	demo_runner $1 "" "default"
}

source scripts/demo_list.sh

echo "</table></body></html>" >> $REPORT
