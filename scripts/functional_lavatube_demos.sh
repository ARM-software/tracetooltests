#!/bin/bash

if [ "$LAVATUBE_LAYER_PATH" != "" ];
then
	LAVATUBE_REPLAYER="$LAVATUBE_LAYER_PATH/lava-replay"
	LAVATUBE_PATH="$LAVATUBE_LAYER_PATH"
else
	LAVATUBE_REPLAYER="/opt/lavatube/bin"
	LAVATUBE_PATH="/opt/lavatube"
fi

REPORTDIR=reports/lavatube/demos${TAG}
REPORT=$REPORTDIR/report.html
DEMO_PARAMS="--benchmark -bfs 100 -bw 0"
TRACEDIR=traces${TAG}
CSV=$REPORTDIR/report.csv
TIMER="/usr/bin/time -f %U -o $(pwd)/time.txt"

rm -f external/vulkan-demos/*.ppm *.ppm $TRACEDIR/demo_*.vk $REPORTDIR/*.png $REPORTDIR/*.html
mkdir -p $TRACEDIR $REPORTDIR

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-demos with lavatube</h1><table><tr><th>Name</th><th>Original</th><th>Replay</th></tr>" >> $REPORT

echo "Test,Mode,Native Time,Capture time,Replay time,Native FPS,Replay FPS,Replay time perf mode,Replay FPS perf mode" > $CSV

function demo
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native $1 **"
	echo

	rm -f external/vulkan-demos/build/bin/*.ppm *.ppm external/vulkan-demos/*.ppm

	# Native run
	NFPS=$(( cd external/vulkan-demos ; $TIMER build/bin/$1 $DEMO_PARAMS )| grep fps | sed 's/fps    : //')
	NTIME=$(cat time.txt)

	( cd external/vulkan-demos ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER build/bin/$1 $DEMO_PARAMS )
	convert -alpha off external/vulkan-demos/3.ppm $REPORTDIR/${1}_f3_native.png
	STIME=$(cat time.txt)
	rm -f external/vulkan-demos/build/bin/*.ppm time.txt

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	export LAVATUBE_DESTINATION=demo_$1
	export VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d
	export LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d
	export VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube
	CFPS=$(( cd external/vulkan-demos/build/bin ; $TIMER ./$1 $DEMO_PARAMS )| grep fps | sed 's/fps    : //')
	CTIME=$(cat time.txt)
	mv external/vulkan-demos/build/bin/demo_$1.vk $TRACEDIR/
	rm -f time.txt

	echo
	echo "** replay $1 **"
	echo

	# Replay
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $LAVATUBE_REPLAYER -v $TRACEDIR/demo_$1.vk
	RTIME=$(cat time.txt)
	RFPS=$(cat lavaresults.txt)
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay.png
	rm -f *.ppm
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay.png $REPORTDIR/$1_f3_compare.png || true

	# Perf mode
	$TIMER $LAVATUBE_REPLAYER -v -vp $TRACEDIR/demo_$1.vk
	RTIMEPERF=$(cat time.txt)
	RFPSPERF=$(cat lavaresults.txt)

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_native.png" /><br>native cpu: $NTIME<br>native+snap cpu: $STIME<br>trace cpu: $CTIME<br>native fps: $NFPS<br>trace fps: $CFPS</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay.png" /><img $HTMLIMGOPTS src="$1_f3_compare.png" /><br>cpu: $RTIME<br>fps: $RFPS</td>" >> $REPORT

	echo "$1,$NTIME,$CTIME,$RTIME,$NFPS,$RFPS,$RTIMEPERF,$RFPSPERF" >> $CSV
}

source scripts/demo_list.sh

echo "</table></body></html>" >> $REPORT
