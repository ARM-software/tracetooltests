#!/bin/bash -e

set -x

REPORTDIR=$(pwd)/reports/lavatube/vkmark
TRACEDIR=$(pwd)/traces
FRAME=30

mkdir -p $TRACEDIR
mkdir -p $REPORTDIR
rm -f $TRACEDIR/vkmark_*.vk
rm -f $REPORTDIR/*.png
rm -f $REPORTDIR/*.html

REPORT=$REPORTDIR/report.html
unset VK_INSTANCE_LAYERS
unset VK_LAYER_PATH

HTMLIMGOPTS="width=200 height=200"
export MESA_VK_ABORT_ON_DEVICE_LOSS=1
LAVATUBE_PATH=/work/lava/build

echo "<html><head><style>table, th, td { border: 1px solid black; } th, td { padding: 10px; }</style></head>" > $REPORT
echo "<body><h1>Comparison for vkmark with lavatube</h1><table><tr><th>Name</th><th>Original</th><th>Replay original</th><th>Replay virtual</th></tr>" >> $REPORT

function vkmark
{
	echo
	echo "** native **"
	echo
	pushd external/vkmark
	rm -f *.ppm
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$FRAME build/src/vkmark --winsys-dir build/src --data-dir data -p fifo -b $2:duration=2.0
	convert -alpha off $FRAME.ppm $REPORTDIR/vkmark_$1_f${FRAME}_native.png
	rm -f *.ppm

	echo
	echo "** trace **"
	echo
	export LAVATUBE_DESTINATION=vkmark_$1
	export VK_LAYER_PATH=$LAVATUBE_PATH/implicit_layer.d
	export LD_LIBRARY_PATH=$LAVATUBE_PATH/implicit_layer.d
	export VK_INSTANCE_LAYERS=VK_LAYER_ARM_lavatube
	build/src/vkmark --winsys-dir build/src --data-dir data -p fifo -b $2:duration=2.0
	mv vkmark_$1*.vk $TRACEDIR/vkmark_$1.vk
	popd

	echo
	echo "** replay $2 original **"
	echo
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$FRAME $LAVATUBE_PATH/replay $TRACEDIR/vkmark_$1.vk
	convert -alpha off $FRAME.ppm $REPORTDIR/vkmark_$1_f${FRAME}_replay.png
	compare -alpha off $REPORTDIR/vkmark_$1_f${FRAME}_native.png $REPORTDIR/vkmark_$1_f${FRAME}_replay.png $REPORTDIR/vkmark_$1_f${FRAME}_compare.png || true
	rm -f *.ppm

	echo
	echo "** replay $2 virtual **"
	echo
	unset VK_INSTANCE_LAYERS
	unset VK_LAYER_PATH
	# Replay
	VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_screenshot VK_SCREENSHOT_FRAMES=$FRAME $LAVATUBE_PATH/replay -v $TRACEDIR/vkmark_$1.vk
	convert -alpha off $FRAME.ppm $REPORTDIR/vkmark_$1_f${FRAME}_replay_virtual.png
	compare -alpha off $REPORTDIR/vkmark_$1_f${FRAME}_native.png $REPORTDIR/vkmark_$1_f${FRAME}_replay_virtual.png $REPORTDIR/vkmark_$1_f${FRAME}_compare_virtual.png || true
	rm -f *.ppm

	echo "<tr><td>$2</td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="vkmark_$1_f${FRAME}_native.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="vkmark_$1_f${FRAME}_replay.png" /><img $HTMLIMGOPTS src="vkmark_$1_f${FRAME}_compare.png" /></td>" >> $REPORT
	echo "<td><img $HTMLIMGOPTS src="vkmark_$1_f${FRAME}_replay_virtual.png" /><img $HTMLIMGOPTS src="vkmark_$1_f${FRAME}_compare_virtual.png" /></td></tr>" >> $REPORT
}

vkmark vertex "vertex:device-local=true:interleave=true"
vkmark texture "texture:texture-filter=linear:anisotropy=0"
vkmark texture_anisotropy16 "texture:texture-filter=linear:anisotropy=16"
vkmark shading_gouraud "shading:shading=gouraud"
vkmark shading_blinn-phong-inf "shading:shading=blinn-phong-inf"
vkmark shading_phong "shading:shading=phong"
vkmark shading_cel "shading:shading=cel"
vkmark effect2d_none "effect2d:background-resolution=800x600:kernel=none"
vkmark effect2d_blur "effect2d:background-resolution=800x600:kernel=blur"
vkmark effect2d_edge "effect2d:background-resolution=800x600:kernel=edge"
vkmark desktop "desktop:background-resolution=800x600:window-size=0.35:windows=1"
vkmark desktop-many "desktop:background-resolution=800x600:window-size=0.35:windows=4"
vkmark cube "cube"
vkmark clear "clear:color=cycle"

### Epilogue

echo "</table></body></html>" >> $REPORT
