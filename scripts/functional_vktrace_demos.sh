#!/bin/bash

REPORTDIR=reports/vktrace/demos
REPORT=$REPORTDIR/report.html
DEMO_PARAMS="--benchmark -bfs 100 -bw 0"
REPLAYER=vkreplay
TIMER="/usr/bin/time -f %U -o $(pwd)/time.txt"
CSV=$REPORTDIR/report.csv

mkdir -p traces
mkdir -p $REPORTDIR
rm -f external/vulkan-demos/*.ppm
rm -f traces/demo_*.vktrace

SERVER=$(ps aux|grep vktrace|grep -v grep|grep -v functional|wc -l)
if [[ "$SERVER" != "1" ]]; then
	echo "You need to run the vktrace server on this path in the background first"
	exit -1
fi

#export VK_LAYER_PATH=/work/vulkan_trace/VulkanTools/dbuild_x64/vktrace
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/work/vulkan_trace/VulkanTools/dbuild_x64/vktrace
LAYER_PATH=/home/permat01/share/vulkan/explicit_layer.d

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-samples with arm vktrace</h1><table><tr><th>Name</th><th>Original</th><th>Replay</th></tr>" >> $REPORT

echo "Test,Mode,Native Time,Capture time,Replay time,Native FPS,Replay FPS" > $CSV

function demo
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native **"
	echo

	rm -f vulkan-demos/*.ppm *.ppm *.vktrace

	# Native timing run
	NFPS=$(( cd external/vulkan-demos ; $TIMER build/bin/$1 $DEMO_PARAMS )| grep fps | sed 's/fps    : //')
	NTIME=$(cat time.txt)

	# Native screenshot run
	( cd external/vulkan-demos ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER build/bin/$1 $DEMO_PARAMS )
	STIME=$(cat time.txt)
	convert -alpha off external/vulkan-demos/3.ppm $REPORTDIR/$1_f3_native.png
	rm -f external/vulkan-demos/*.ppm

	echo
	echo "** trace **"
	echo

	# Make trace
	( cd external/vulkan-demos ; VK_LAYER_PATH=$LAYER_PATH VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_vktrace $TIMER build/bin/$1 $DEMO_PARAMS )
	CTIME=$(cat time.txt)
	mv *.vktrace traces/demo_$1.vktrace

	echo
	echo "** replay **"
	echo

	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $TIMER $REPLAYER -evsc TRUE -o traces/demo_$1.vktrace
	RTIME=$(cat time.txt)
	RFPS=$(grep -e fps /work/tracetooltests/vktrace_result.json | sed 's/.*: //'| sed 's/,//')
	convert -alpha off 3.ppm $REPORTDIR/$1_f3_replay.png
	compare -alpha off $REPORTDIR/$1_f3_native.png $REPORTDIR/$1_f3_replay.png $REPORTDIR/$1_f3_compare.png || true
	rm -f *.ppm

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_native.png" /><br>native cpu: $NTIME<br>screenshot cpu: $STIME<br>tracing cpu: $CTIME<br>native fps: $NFPS</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="$1_f3_replay.png" /><img $HTMLIMGOPTS src="$1_f3_compare.png" /><br>cpu: $RTIME<br>fps: $RFPS</td>" >> $REPORT

	echo "$1,$NTIME,$CTIME,$RTIME,$NFPS,$RFPS" >> $CSV
}

source scripts/demo_list.sh

echo "</table></body></html>" >> $REPORT

