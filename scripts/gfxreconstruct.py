#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys


TMP_DIR = os.path.join(os.path.dirname(__file__), 'tmp')


def run(args, exec_name, exec_args):

    print(f'\n\n========== Running test "{exec_name}" with args "{exec_args}" ==========\n')

    print('========== Capturing... ==========')
    os.environ['VK_INSTANCE_LAYERS'] = 'VK_LAYER_LUNARG_gfxreconstruct'
    result = subprocess.run([os.path.join(args.path_to_tracetooltests_build_dir, exec_name)] + exec_args)
    assert result.returncode == 0
    assert os.path.isfile(os.environ['GFXRECON_CAPTURE_FILE'])

    print('========== Replaying... ==========')
    os.environ['VK_INSTANCE_LAYERS'] = ''
    result = subprocess.run([args.path_to_gfxrecon_replay, os.environ['GFXRECON_CAPTURE_FILE']])
    assert result.returncode == 0


def main():
    
    arg_parser = argparse.ArgumentParser(prog='GFXReconstruct test suite.')

    arg_parser.add_argument('path_to_layer_dir')
    arg_parser.add_argument('path_to_gfxrecon_replay')
    arg_parser.add_argument('path_to_tracetooltests_build_dir')

    args = arg_parser.parse_args()

    assert os.path.isdir(args.path_to_layer_dir)
    assert os.path.isfile(os.path.join(args.path_to_layer_dir, 'libVkLayer_gfxreconstruct.so'))
    assert os.path.isfile(os.path.join(args.path_to_layer_dir, 'VkLayer_gfxreconstruct.json'))

    assert os.path.isfile(args.path_to_gfxrecon_replay)

    assert os.path.isdir(args.path_to_tracetooltests_build_dir)

    os.makedirs(TMP_DIR, mode=0o744, exist_ok=True)

    os.environ['VK_LAYER_PATH'] = args.path_to_layer_dir
    os.environ['GFXRECON_CAPTURE_FILE'] = os.path.join(TMP_DIR, 'capture.gfxr')
    os.environ['GFXRECON_CAPTURE_FILE_TIMESTAMP'] = '0'

    run(args, 'vulkan_tool_1', [])
    run(args, 'vulkan_memory_1', [])
    run(args, 'vulkan_memory_1_1', [])
    run(args, 'vulkan_thread_1', [])
    run(args, 'vulkan_thread_2', [])
    run(args, 'vulkan_general', ['-V', '0'])
    run(args, 'vulkan_general', ['-V', '1'])
    run(args, 'vulkan_general', ['-V', '2'])
    run(args, 'vulkan_general', ['-V', '3'])
    run(args, 'vulkan_copying_1', [])
    run(args, 'vulkan_copying_1', ['-f', '1'])
    run(args, 'vulkan_copying_1', ['-q', '1'])
    run(args, 'vulkan_copying_1', ['-q', '2'])
    run(args, 'vulkan_copying_1', ['-q', '3'])
    run(args, 'vulkan_copying_1', ['-q', '4'])
    run(args, 'vulkan_copying_1', ['-m', '1'])
    run(args, 'vulkan_copying_1', ['-m', '2'])
    run(args, 'vulkan_copying_2', ['-q', '1'])
    run(args, 'vulkan_copying_3', [])
    run(args, 'vulkan_copying_3', ['-c', '1'])
    run(args, 'vulkan_compute_1', [])
    run(args, 'vulkan_compute_1', ['-pc'])
    run(args, 'vulkan_compute_1', ['-pc', '-pcf', os.path.join(TMP_DIR, 'cache.bin')])
    run(args, 'vulkan_compute_1', ['-pc', '-pcf', os.path.join(TMP_DIR, 'cache.bin')])
    run(args, 'vulkan_pipelinecache_1', [])


if __name__ == '__main__':
    main()
