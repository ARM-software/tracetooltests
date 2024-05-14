#!/bin/bash

if [ "$LAYERPATH" != "" ];
then
	REPLAYER="$LAYERPATH/gfxrecon-replay"
	TRACER="$LAYERPATH/gfxrecon-capture-vulkan.py --capture-layer $LAYERPATH"
fi

REPORTDIR=reports/gfxr/samples${TAG}
REPORT=$REPORTDIR/report.html
TRACEDIR=traces${TAG}
REPLAYER=${REPLAYER:-"$(which gfxrecon-replay)"}
TRACER=${TRACER:-"$(which gfxrecon-capture-vulkan.py)"}
PARAMS="--hideui --benchmark --stop-after-frame=10 --force-close"

rm -rf external/vulkan-samples/*.ppm *.ppm $TRACEDIR/sample_*.gfxr $REPORTDIR external/vulkan-demos/*.gfxr
mkdir -p $TRACEDIR
mkdir -p $REPORTDIR

HTMLIMGOPTS="width=200 height=200"

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vulkan-samples with gfxreconstruct</h1><table><tr><th>Name</th><th>Original</th><th>Replay -m none</th><th>Replay -m rebind</th><th>Replay trim</th></tr>" >> $REPORT

function run
{
	echo
	echo "****** $1 ******"
	echo

	echo
	echo "** native $1 **"
	echo


	# Native run
	( cd external/vulkan-samples ; VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 build/linux/app/bin/Debug/x86_64/vulkan_samples $PARAMS sample $1 )
	convert -alpha off external/vulkan-samples/3.ppm $REPORTDIR/sample_$1_f3_native.png
	rm external/vulkan-samples/*.ppm

	echo
	echo "** trace $1 **"
	echo

	# Make trace
	rm -f external/vulkan-samples/*.gfxr
	( cd external/vulkan-samples ; ${TRACER} -o sample_$1.gfxr build/linux/app/bin/Debug/x86_64/vulkan_samples $PARAMS sample $1 )
	mv external/vulkan-samples/sample_$1*.gfxr $TRACEDIR/sample_$1.gfxr

	echo
	echo "** replay $1 using $REPLAYER **"
	echo

	# Replay -m none
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m none $TRACEDIR/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay.png $REPORTDIR/sample_$1_f3_compare.png || true
	rm -f *.ppm

	# Replay -m rebind
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=3 $REPLAYER -m rebind $TRACEDIR/sample_$1.gfxr
	convert -alpha off 3.ppm $REPORTDIR/sample_$1_f3_replay_rebind.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_replay_rebind.png $REPORTDIR/sample_$1_f3_compare_rebind.png || true
	rm -f *.ppm

	# Make fastforwarded trace
	${TRACER} -f 3-5 -o sample_$1_ff.gfxr $REPLAYER -m none $TRACEDIR/sample_$1.gfxr
	mv sample_$1_ff* $TRACEDIR/sample_$1_ff_frame3.gfxr

	# Run the fastforwarded trace
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=1 $REPLAYER -m none $TRACEDIR/sample_$1_ff_frame3.gfxr
	convert -alpha off 1.ppm $REPORTDIR/sample_$1_f3_ff_replay.png
	compare -alpha off $REPORTDIR/sample_$1_f3_native.png $REPORTDIR/sample_$1_f3_ff_replay.png $REPORTDIR/sample_$1_f3_compare_ff.png || true

	echo "<tr><td>$1</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_native.png" /><br></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_replay_rebind.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare_rebind.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="sample_$1_f3_ff_replay.png" /><br><img $HTMLIMGOPTS src="sample_$1_f3_compare_ff.png" /></td>" >> $REPORT
}

source scripts/samples_list.sh

echo "</table></body></html>" >> $REPORT
