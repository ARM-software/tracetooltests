#!/bin/bash

if [ "$GFXR_LAYER_PATH" != "" ];
then
	GFXR_REPLAYER="$GFXR_LAYER_PATH/gfxrecon-replay"
fi
if [ "$VKTRACE_LAYER_PATH" != "" ];
then
	VKTRACE_REPLAYER="$VKTRACE_LAYER_PATH/vkreplay"
fi
if [ "$LAVATUBE_LAYER_PATH" != "" ];
then
	LAVATUBE_REPLAYER="$LAVATUBE_LAYER_PATH/lava-replay"
fi

REPORTDIR=reports/gpu_perf/demos${TAG}
CSV=$REPORTDIR/report.csv
TRACEDIR=traces${TAG}
GFXR_REPLAYER=${GFXR_REPLAYER:-"$(which gfxrecon-replay)"}
VKTRACE_REPLAYER=${VKTRACE_REPLAYER:-"$(which vkreplay)"}
LAVATUBE_REPLAYER=${LAVATUBE_REPLAYER:-"$(which lava-replay)"}
REGEX='s/\s*\([0-9\.,]*\).*/\1/'

mkdir -p $REPORTDIR

echo "Test,Gfx FPS,Vktrace FPS,Lava FPS" > $CSV

function demo
{
	NAME="demo_$1"
	echo
	echo "** $1 **"
	echo

	# Gfxr timing run
	GFXRFPS=0
	for i in {1..10}
	do
		$GFXR_REPLAYER -m rebind $TRACEDIR/${NAME}_default.gfxr
		NEWFPS=$(grep -e fps gfxrecon-measurements.json | sed 's/.*: //'| sed 's/,//')
		echo "gfxr $NEWFPS"
		if (( $(echo "$NEWFPS > $GFXRFPS" | bc -l) )); then
			GFXRFPS=$NEWFPS
		fi
	done

	# Vktrace timing run
	VKFPS=0
	for i in {1..10}
	do
		$TIMER $VKTRACE_REPLAYER -evsc TRUE -o $TRACEDIR/${NAME}.vktrace
		NEWFPS=$(grep -e fps /work/tracetooltests/vktrace_result.json | sed 's/.*: //'| sed 's/,//')
		echo "vk $NEWFPS"
		if (( $(echo "$NEWFPS > $VKFPS" | bc -l) )); then
			VKFPS=$NEWFPS
		fi
	done

	# Lavatube timing run
	LAVAFPS=0
	for i in {1..10}
	do
		$TIMER $LAVATUBE_REPLAYER -v $TRACEDIR/${NAME}.vk
		NEWFPS=$(cat lavaresults.txt)
		echo "lava $NEWFPS"
		if (( $(echo "$NEWFPS > $LAVAFPS" | bc -l) )); then
			LAVAFPS=$NEWFPS
		fi
	done

	echo "$1,$GFXRFPS,$VKFPS,$LAVAFPS" >> $CSV
}

source scripts/demo_list.sh
