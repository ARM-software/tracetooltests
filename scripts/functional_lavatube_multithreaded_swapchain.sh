#!/bin/bash

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
OUTPUT_DIR=${OUTPUT_DIR:-"$ROOT/reports/lavatube/multithreaded_swapchain"}
LAVATUBE_LAYER_PATH=${LAVATUBE_LAYER_PATH:-}
TIMEOUT_SECONDS=${TIMEOUT_SECONDS:-90}
FRAMES=${FRAMES:-600}

if [ "$FRAMES" -lt 600 ]; then
	echo "FRAMES must be at least 600." >&2
	exit 2
fi

if [ -z "$LAVATUBE_LAYER_PATH" ]; then
	echo "Set LAVATUBE_LAYER_PATH to the Lavatube build or installation directory." >&2
	exit 2
fi

REPLAYER=${LAVATUBE_REPLAYER:-"$LAVATUBE_LAYER_PATH/lava-replay"}
LAYER_DIR="$LAVATUBE_LAYER_PATH/implicit_layer.d"
TEST="$BUILD_DIR/vulkan_multithreaded_swapchain"
SCREENSHOT_FRAMES="0,1,2,127,255,511,599"

if [ ! -x "$TEST" ] || [ ! -x "$REPLAYER" ]; then
	echo "Missing executable: $TEST or $REPLAYER" >&2
	exit 2
fi
if ! command -v xvfb-run >/dev/null || ! command -v cmp >/dev/null; then
	echo "xvfb-run and cmp are required." >&2
	exit 2
fi

mkdir -p "$OUTPUT_DIR"
ran=0

for images in 2 3; do
	if [ "$images" -eq 2 ]; then remap_images=3; else remap_images=4; fi
	for present in fifo immediate; do
		name="images${images}_${present}"
		trace="$OUTPUT_DIR/$name.api"
		rm -f "$trace" "$OUTPUT_DIR/${name}_"*.png "$OUTPUT_DIR/${name}_"*.log
		rm -rf "$OUTPUT_DIR/${name}_tmp"

		echo "Capturing $name"
		(
			cd "$BUILD_DIR" || exit 1
			LAVATUBE_DESTINATION="$OUTPUT_DIR/$name" \
			VK_LAYER_PATH="$LAYER_DIR" \
			LD_LIBRARY_PATH="$LAYER_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
			VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube \
			timeout "$TIMEOUT_SECONDS" xvfb-run -a "$TEST" --cpu --frames "$FRAMES" --images "$images" --present "$present"
		)
		capture_status=$?
		if [ "$capture_status" -eq 77 ]; then
			echo "Skipping unsupported variant $name"
			rm -f "$trace"
			rm -rf "$OUTPUT_DIR/${name}_tmp"
			continue
		fi
		if [ "$capture_status" -ne 0 ] || [ ! -f "$trace" ]; then
			echo "Capture failed for $name with status $capture_status" >&2
			exit 1
		fi

		baseline_prefix="$OUTPUT_DIR/${name}_baseline_"
		remap_prefix="$OUTPUT_DIR/${name}_remap_"
		baseline_log="$OUTPUT_DIR/${name}_baseline.log"
		remap_log="$OUTPUT_DIR/${name}_remap.log"

		echo "Replaying baseline $name"
		(
			cd "$OUTPUT_DIR" || exit 1
			LAVATUBE_DEBUG=1 LAVATUBE_DEBUG_FILE="$baseline_log" \
				timeout "$TIMEOUT_SECONDS" xvfb-run -a "$REPLAYER" -C -V --swapchain virtual --swapchainimages "$images" \
				--screenshots "$SCREENSHOT_FRAMES" --screenshot-prefix "$baseline_prefix" "$trace"
		) || exit 1

		echo "Replaying remapped $name with $remap_images requested images"
		(
			cd "$OUTPUT_DIR" || exit 1
			LAVATUBE_DEBUG=1 LAVATUBE_DEBUG_FILE="$remap_log" \
				timeout "$TIMEOUT_SECONDS" xvfb-run -a "$REPLAYER" -C -V --swapchain virtual --swapchainimages "$remap_images" \
				--screenshots "$SCREENSHOT_FRAMES" --screenshot-prefix "$remap_prefix" "$trace"
		) || exit 1

		if ! sed -n 's/.*index=\([0-9][0-9]*\) (stored next image was \([0-9][0-9]*\)).*/\1 \2/p' "$remap_log" | awk '$1 != $2 { found = 1 } END { exit !found }'; then
			echo "Replay did not demonstrate a real/stored swapchain index permutation for $name" >&2
			exit 1
		fi

		for frame in 0 1 2 127 255 511 599; do
			reference="${baseline_prefix}${frame}.png"
			candidate="${remap_prefix}${frame}.png"
			if [ ! -f "$reference" ] || [ ! -f "$candidate" ]; then
				echo "Missing screenshot for frame $frame in $name" >&2
				exit 1
			fi
			cmp -s "$reference" "$candidate" || {
				echo "Screenshot mismatch for $name frame $frame" >&2
				exit 1
			}
		done
		ran=$((ran + 1))
	done
done

if [ "$ran" -eq 0 ]; then
	echo "No supported variants ran." >&2
	exit 77
fi

echo "Validated $ran multithreaded swapchain trace variants."
