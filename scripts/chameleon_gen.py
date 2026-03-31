#!/usr/bin/env python3

import sys
import os
import xml.etree.ElementTree as ET
import re

ROOT_PATH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT_PATH)
import spec

source = open('vulkan_auto.cpp', 'w')
header = open('vulkan_auto.h', 'w')
assert source, 'Could not create source file'
assert header, 'Could not create header file'

# Find all platforms
platforms = {}
for v in spec.root.findall('platforms/platform'):
	name = v.attrib.get('name')
	prot = v.attrib.get('protect')
	platforms[name] = prot

print('// This file contains only auto-generated code!', file=source)
print("\n", file=source)
print( '#include "vulkan_auto.h"', file=source)
print("\n", file=source)
print('// This file contains only auto-generated code!', file=header)
print("\n", file=header)
print('#pragma once', file=header)
print("\n", file=header)
print('#include <string>', file=header)
print('#include <stdio.h>', file=header)
print('#include <unordered_map>', file=header)
print('#pragma GCC visibility push(default)', file=header)
print('#include "external/Vulkan-Headers/include/vulkan/vulkan.h"', file=header)
print('#pragma GCC visibility pop', file=header)
print("\n", file=header)

# Functions not yet in vk.xml
new_funcs = [
]
# Handles not yet in vk.xml
new_ndisp_handles = [
]

spec.init()
spec.functions += new_funcs
spec.disp_handles += []
spec.nondisp_handles += new_ndisp_handles
spec.all_handles = new_ndisp_handles

# -- Print utility --

def print_info(val):
	size = 1
	raw = str(ET.tostring(val))
	if '[' in raw:
		size = int(re.search(r'.*\[(\d+)\]', raw).group(1))
	name = val.find('name').text
	type = val.find('type').text
	suffix = ''
	if type == 'VkBool32':
		fmt = '%s'
		prefix = 'in.%s' % name
		suffix = ' ? "true": "false"'
	elif type == 'float':
		fmt = '%.02f'
		prefix = 'in.%s' % name
	elif type == 'int32_t':
		fmt = '%d'
		prefix = 'in.%s' % name
	else:
		fmt = '%lu'
		prefix = '(long unsigned)in.%s' % name
	if size > 1:
		fmt_list = []
		param_list = []
		for i in range(0, size):
			fmt_list.append(fmt)
			param_list.append('%s[%d]%s' % (prefix, i, suffix))
		print('\tprintf("\\t\\"%s\\": [ %s ],\\n", %s);' % (name, ', '.join(fmt_list), ', '.join(param_list)), file=source) 
	else:
		print('\tprintf("\\t\\"%s\\": %s,\\n", %s%s);' % (name, fmt, prefix, suffix), file=source)

print('void printLimits(const VkPhysicalDeviceLimits& in);', file=header)
print('void printLimits(const VkPhysicalDeviceLimits& in)', file=source)
print('{', file=source)
for v in spec.root.findall("types/type[@name='VkPhysicalDeviceLimits']/member"):
	print_info(v)
print('\tprintf("\\t\\"null\\": 0\\n");', file=source)# hack for valid JSON 
print('}', file=source)

print("\n", file=source)
print('void printFeatures(const VkPhysicalDeviceFeatures& in);', file=header)
print('void printFeatures(const VkPhysicalDeviceFeatures& in)', file=source)
print('{', file=source)
for v in spec.root.findall("types/type[@name='VkPhysicalDeviceFeatures']/member"):
	print_info(v)
print('\tprintf("\\t\\"null\\": 0\\n");', file=source)  # hack for valid JSON 
print('}', file=source)

print("\n", file=source)


# -- Function call counters --

for name in spec.functions:
	print('uint64_t count_%s = 0;' % name, file=source)
	print('extern uint64_t count_%s;' % name, file=header)

print("\n", file=source)
print("\n", file=header)
print('void save_counts(const char* filename);', file=header)
print("\n", file=source)
print('void save_counts(const char* filename)', file=source)
print('{', file=source)
print('\tFILE* fp = fopen(filename, "w");', file=source)
print('\tfprintf(fp, "Function,Count\\n");', file=source)
for f in spec.functions:
	print('\tif (count_%s > 0) fprintf(fp, "%s,%%lu\\n", (unsigned long)count_%s);' % (f, f, f), file=source)
print('\tfclose(fp);', file=source)
print('}', file=source)

# -- Function pointer utility --

print('extern std::unordered_map<std::string, PFN_vkVoidFunction> function_map;', file=header)
print('std::unordered_map<std::string, PFN_vkVoidFunction> function_map = {', file=source)
for n in spec.functions:
	if n in spec.protected_funcs:
		print('#ifdef %s' % spec.protected_funcs[n], file=source)
	print('\t{ "%s", (PFN_vkVoidFunction)%s },' % (n, n), file=source)
	if n in spec.protected_funcs:
		print('#endif // %s' % spec.protected_funcs[n], file=source)
print('};', file=source)
print("\n", file=source)

# -- Command enum generator --
# See above for description.
print("\n", file=header)
print('enum vk_command', file=header)
print('{', file=header)
for n in spec.functions:
	if 'vkCmd' in n:
		print('\tENUM_%s,' % (n), file=header)
print('\tENUM_MAX_COMMANDS', file=header)
print('};', file=header)

# -- Command enum to string converter --
# See above for description.

print("\n", file=header)
print('const char* vk_command_to_string(vk_command cmd);', file=header)
print("\n", file=source)
print('const char* vk_command_to_string(vk_command cmd)', file=source)
print('{', file=source)
print('\tswitch (cmd)', file=source)
print('\t{', file=source)
for n in spec.functions:
	if 'vkCmd' in n:
		print('\tcase ENUM_%s: return "%s";' % (n, n), file=source)
print('\tdefault: return "Unknown";', file=source)
print('\t}', file=source)
print('}', file=source)

source.close()
header.close()
