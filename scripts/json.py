#!/usr/bin/env python3

import xml.etree.ElementTree as ET
import re
import sys
import os

ROOT_PATH = os.path.dirname(os.path.abspath(__file__))

tree = ET.parse(os.path.join(ROOT_PATH, '..', 'external', 'Vulkan-Headers', 'registry', 'vk.xml'))
root = tree.getroot()

# For now, limit this to the functions we actually use. Maintaining this for the general
# case got too much work...
actually_used = ['VkPhysicalDeviceLimits', 'VkPhysicalDeviceSparseProperties', 'VkQueueFamilyProperties']

platforms = {}
structures = []
structdefs = {}
handles = []
protected = {}
badtypes = ['VkRect3D', 'VkApplicationInfo', 'VkDeviceCreateInfo', 'VkInstanceCreateInfo',
            'VkDebugMarkerMarkerInfoEXT', 'VkDebugMarkerObjectNameInfoEXT', 'VkPhysicalDeviceGroupPropertiesKHX',
            'VkNativeBufferANDROID', 'VkPhysicalDeviceFeatures2', 'VkPhysicalDeviceVulkanSC10Features']
badmembers = ['clearValue', 'pName', 'displayName'] # unions, const char pointers, and stuff
physdev_features = []
physdev_properties = []
supported_types = set()
reader_structs = set()
enum_types = set()
enum_aliases = {}
bitmask_types = {}
bitmask_aliases = {}
count_members = {}
used_enum_converters = set(['VkFormat', 'VkFormatFeatureFlagBits'])
enum_value_aliases = { # to work around an apparent gpuinfo.org bug -- TBD investigate further
	'VkMemoryHeapFlagBits': {
		'VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT': 'VK_MEMORY_HEAP_DEVICE_LOCAL_BIT'
	}
}

# Find all platforms
for v in root.findall('platforms/platform'):
	name = v.attrib.get('name')
	prot = v.attrib.get('protect')
	platforms[name] = prot

# Find all non-VulkanSC types that are actually exposed by enabled core versions and extensions.
for v in root.findall('extensions/extension'):
	supported = v.attrib.get('supported')
	if supported == 'disabled' or supported == 'vulkansc':
		continue
	for req in v.findall('require'):
		if req.attrib.get('api') == 'vulkansc':
			continue
		for n in req.findall('type'):
			supported_types.add(n.attrib.get('name'))

for v in root.findall('feature'):
	if v.attrib.get('api') == 'vulkansc':
		continue
	for req in v.findall('require'):
		if req.attrib.get('api') == 'vulkansc':
			continue
		for n in req.findall('type'):
			supported_types.add(n.attrib.get('name'))

# Find all structures and handles
for v in root.findall('types/type'):
	if v.attrib.get('alias'): # ignore aliases for now
		if v.attrib.get('api') == 'vulkansc':
			continue
		category = v.attrib.get('category')
		if category == 'enum':
			enum_aliases[v.attrib.get('name')] = v.attrib.get('alias')
		elif category == 'bitmask':
			bitmask_aliases[v.attrib.get('name')] = v.attrib.get('alias')
		continue
	if v.attrib.get('api') == 'vulkansc':
		continue
	category = v.attrib.get('category')
	if category == 'struct':
		name = v.attrib.get('name')
		structures.append(name)
		structdefs[name] = v
	elif category == 'handle':
		handles.append(v.find('name').text)
	elif category == 'enum':
		enum_types.add(v.attrib.get('name'))
	elif category == 'bitmask':
		name = v.find('name').text
		bits = v.attrib.get('bitvalues') or v.attrib.get('requires')
		if bits:
			bitmask_types[name] = bits

# Find ifdef conditionals (that we want to recreate) and disabled extensions (that we want to ignore)
for v in root.findall('extensions/extension'):
	conditional = v.attrib.get('platform')
	if conditional:
		for n in v.findall('require/type'):
			protected[n.attrib.get('name')] = platforms[conditional]

# Find physical device features and physical device properties
for v in root.findall('types/type'):
	if v.attrib.get('alias'): # ignore aliases for now
		continue
	if v.attrib.get('api') == 'vulkansc':
		continue
	if v.attrib.get('category') != 'struct':
		continue
	name = v.attrib.get('name')
	if name not in supported_types:
		continue
	if re.match(r'^VkPhysicalDevice.*Features', name):
		physdev_features.append(name)
	elif re.match(r'^VkPhysicalDevice.*Properties', name):
		physdev_properties.append(name)
actually_used.extend(physdev_features)
actually_used.extend(physdev_properties)

for name, struct in structdefs.items():
	count_members[name] = {}
	for member in struct.findall('member'):
		length = member.attrib.get('len')
		if not length:
			continue
		array_name = member.find('name').text
		for count_name in [part.strip() for part in length.split(',')]:
			if re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', count_name):
				count_members[name][count_name] = array_name

def resolve_enum(name):
	while name in enum_aliases:
		name = enum_aliases[name]
	return name

def resolve_bitmask(name):
	while name in bitmask_aliases:
		name = bitmask_aliases[name]
	bits = bitmask_types.get(name)
	if bits:
		return resolve_enum(bits)
	return None

def is_enum_type(name):
	return resolve_enum(name) in enum_types

def add_reader_struct(name):
	if name in reader_structs or name in badtypes or name not in structdefs:
		return
	reader_structs.add(name)
	for member in structdefs[name].findall('member'):
		member_type = member.find('type')
		if member_type is None:
			continue
		member_name = member.find('name')
		if member_name is not None and member_name.text in badmembers:
			continue
		if member_name is not None and member_name.text == 'sType':
			continue
		child = member_type.text
		if is_enum_type(child):
			used_enum_converters.add(resolve_enum(child))
		bitmask = resolve_bitmask(child)
		if bitmask:
			used_enum_converters.add(bitmask)
		if child in structures:
			add_reader_struct(child)

for name in actually_used:
	add_reader_struct(name)

source = open('vkjson.cpp', 'w')
header = open('vkjson.h', 'w')
assert source, 'Could not create source file'
assert header, 'Could not create header file'

print('// This file contains only auto-generated code!', file=header)
print("\n", file=header)
print('#pragma once', file=header)
print('#include "util.h"', file=header)
print("\n", file=header)

print('// This file contains only auto-generated code!', file=source)
print("\n", file=source)
print('#include "vkjson.h"', file=source)
print('#include <string.h>', file=source)
print("\n", file=source)

# Parse element size, which can be weird
def getraw(val):
	raw = ''
	for s in val.iter():
		if s.tag == 'enum':
			raw += s.text.strip()
		if s.tail:
			raw += s.tail.strip()
	return raw

def getsize(raw):
	if '[' in raw and not type == 'char':
		try:
			return int(re.search(r'.*\[(\d+)\]', raw).group(1)), False
		except:
			return re.search(r'.*\[(.+)\]', raw).group(1), True
	else:
		return 1, False

# -- JSON read --

def print_read_scalar(jsonexpr, targetexpr, type, indent='\t'):
	enum_type = resolve_enum(type) if is_enum_type(type) else None
	bitmask = resolve_bitmask(type)
	if enum_type:
		print('%sif (%s.isString())' % (indent, jsonexpr), file=source)
		print('%s{' % indent, file=source)
		print('%s\t%s = (%s)stringTo%s(%s.asString());' % (indent, targetexpr, type, enum_type, jsonexpr), file=source)
		print('%s}' % indent, file=source)
		print('%selse' % indent, file=source)
		print('%s{' % indent, file=source)
		print('%s\t%s = (%s)%s.asInt64();' % (indent, targetexpr, type, jsonexpr), file=source)
		print('%s}' % indent, file=source)
	elif bitmask:
		print('%s%s = 0;' % (indent, targetexpr), file=source)
		print('%sif (%s.isArray())' % (indent, jsonexpr), file=source)
		print('%s{' % indent, file=source)
		print('%s\tfor (const Json::Value& value : %s)' % (indent, jsonexpr), file=source)
		print('%s\t{' % indent, file=source)
		print('%s\t\t%s |= (%s)stringTo%s(value.asString());' % (indent, targetexpr, type, bitmask), file=source)
		print('%s\t}' % indent, file=source)
		print('%s}' % indent, file=source)
		print('%selse if (%s.isString())' % (indent, jsonexpr), file=source)
		print('%s{' % indent, file=source)
		print('%s\t%s = (%s)stringTo%s(%s.asString());' % (indent, targetexpr, type, bitmask, jsonexpr), file=source)
		print('%s}' % indent, file=source)
		print('%selse' % indent, file=source)
		print('%s{' % indent, file=source)
		print('%s\t%s = (%s)%s.asInt64();' % (indent, targetexpr, type, jsonexpr), file=source)
		print('%s}' % indent, file=source)
	elif type == 'VkBool32':
		print('%s%s = (%s)%s.asBool();' % (indent, targetexpr, type, jsonexpr), file=source)
	elif type == 'float':
		print('%s%s = %s.asFloat();' % (indent, targetexpr, jsonexpr), file=source)
	elif type == 'VkDeviceSize':
		print('%s%s = (%s)%s.asUInt64();' % (indent, targetexpr, type, jsonexpr), file=source)
	else:
		print('%s%s = (%s)%s.asInt64();' % (indent, targetexpr, type, jsonexpr), file=source)

def print_read_json(struct_name, val):
	name = val.find('name').text
	type = val.find('type').text
	raw = getraw(val)
	size, enumsize = getsize(raw)
	if raw and '*' in raw and not type == 'char':
		print('\tin.%s = nullptr;' % name, file=source)
		return

	if name == 'sType':
		print('\tin.sType = %s;' % val.attrib.get('values'), file=source)
		return
	elif name in badmembers:
		print('\t//in.%s' % name, file=source)
		return
	count_array = count_members.get(struct_name, {}).get(name)
	if count_array:
		print('\tif (root.isMember("%s"))' % name, file=source)
		print('\t{', file=source)
		print_read_scalar('root["%s"]' % name, 'in.%s' % name, type, '\t\t')
		print('\t}', file=source)
		print('\telse', file=source)
		print('\t{', file=source)
		print('\t\tin.%s = root["%s"].size();' % (name, count_array), file=source)
		print('\t}', file=source)
		return
	if type == 'char':
		print('\tstrcpy(in.%s, root["%s"].asString().c_str());' % (name, name), file=source)
		return
	if type in structures:
		if enumsize:
			print('\tfor (uint32_t i = 0; i < %s; i++)' % size, file=source)
			print('\t{', file=source)
			print('\t\tread%s(root["%s"][i], in.%s[i]);' % (type, name, name), file=source)
			print('\t}', file=source)
		elif size > 1:
			for i in range(0, size):
				print('\tread%s(root["%s"][%d], in.%s[%d]);' % (type, name, i, name, i), file=source)
		else:
			print('\tread%s(root["%s"], in.%s);' % (type, name, name), file=source)
		return
	if type in handles:
		if enumsize:
			print('\tfor (uint32_t i = 0; i < %s; i++)' % size, file=source)
			print('\t{', file=source)
			print('\t\tin.%s[i] = VK_NULL_HANDLE; // handle not handled' % name, file=source)
			print('\t}', file=source)
		elif size > 1:
			for i in range(0, size):
				print('\tin.%s[%d] = VK_NULL_HANDLE; // handle not handled' % (name, i), file=source)
		else:
			print('\tin.%s = VK_NULL_HANDLE; // handle not handled' % name, file=source)
		return

	if enumsize:
		print('\tfor (uint32_t i = 0; i < %s; i++)' % size, file=source)
		print('\t{', file=source)
		print_read_scalar('root["%s"][i]' % name, 'in.%s[i]' % name, type, '\t\t')
		print('\t}', file=source)
	elif size > 1:
		for i in range(0, size):
			print_read_scalar('root["%s"][%d]' % (name, i), 'in.%s[%d]' % (name, i), type)
	else:
		print_read_scalar('root["%s"]' % name, 'in.%s' % name, type)

print('void readVulkanFeatures(const Json::Value& root, std::map<VkStructureType, std::pair<void*, size_t>>& features);', file=header)
print('void readVulkanFeatures(const Json::Value& root, std::map<VkStructureType, std::pair<void*, size_t>>& features)', file=source)
print('{', file=source)
for s in root.findall("types/type"):
	name = s.attrib.get('name')
	if name not in physdev_features or name in badtypes:
		continue
	if name == 'VkPhysicalDeviceFeatures':
		key = 'VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2'
	else:
		for m in s.findall('member'):
			if m.find('name').text == 'sType':
				key = m.attrib.get('values')
				break
	if name in protected:
		print('#ifdef %s // %s' % (protected[name], name), file=source)
	print('\tif (root.isMember("%s"))' % name, file=source)
	print('\t{', file=source)
	print('\t\tsize_t feature_size = sizeof(%s);' % name, file=source)
	print('\t\t%s* feature = reinterpret_cast<%s*>(malloc(feature_size));' % (name, name), file=source)
	print('\t\tread%s(root["%s"], *feature);' % (name, name), file=source)
	print('\t\tfeatures[%s] = { feature, feature_size };' % key, file=source)
	print('\t}', file=source)
	if name in protected:
		print('#endif', file=source)
print('}', file=source)

for s in root.findall("types/type"):
	name = s.attrib.get('name')
	category = s.attrib.get('category')
	if not name in reader_structs:
		continue
	if not category or not category == 'struct' or name in badtypes:
		continue
	if name in protected:
		print('#ifdef %s // %s' % (protected[name], name), file=header)
		print('#ifdef %s // %s' % (protected[name], name), file=source)
	print('void read%s(const Json::Value& root, %s& in);' % (name, name), file=header)
	print('void read%s(const Json::Value& root, %s& in)' % (name, name), file=source)
	print('{', file=source)
	for v in s.findall("member"):
		print_read_json(name, v)
	print('}', file=source)
	if name in protected:
		print('#endif', file=header)
		print('#endif', file=source)
	print("\n", file=source)

# -- enum conversion

def print_string_to_enum(name):

	print('%s stringTo%s(const std::string& str);' % (name, name), file=header)
	print('%s stringTo%s(const std::string& str)' % (name, name), file=source)
	print('{', file=source)

	started = False
	def print_case(label, value, guard=None):
		nonlocal started
		if label is None or value is None:
			return
		if guard:
			print('#ifdef %s' % guard, file=source)
		if started:
			print('\telse if (str == "%s") { return %s; }' % (label, value), file=source)
		else:
			print('\tif (str == "%s") { return %s; }' % (label, value), file=source)
			if guard is None:
				started = True
		if guard:
			print('#endif', file=source)

	for enum in root.findall('enums'):
		if enum.attrib.get('name') == name:
			for value in enum:
				print_case(value.attrib.get('name'), value.attrib.get('name'))
	for feature in root.findall('feature'):
		if feature.attrib.get('api') == 'vulkansc':
			continue
		for req in feature.findall('require'):
			if req.attrib.get('api') == 'vulkansc':
				continue
			for enum in req.findall('enum'):
				if enum.attrib.get('extends') == name:
					guard = enum.attrib.get('protect')
					print_case(enum.attrib.get('name'), enum.attrib.get('name'), guard)
					if enum.attrib.get('alias'):
						print_case(enum.attrib.get('alias'), enum.attrib.get('alias'), guard)

	for extension in root.findall('extensions/extension'):
		if extension.attrib.get('supported') == 'disabled' or extension.attrib.get('supported') == 'vulkansc':
			continue
		ext_guard = None
		if extension.attrib.get('platform'):
			ext_guard = platforms[extension.attrib.get('platform')]
		for req in extension.findall('require'):
			if req.attrib.get('api') == 'vulkansc':
				continue
			for enum in req.findall('enum'):
				if enum.attrib.get('extends') != name:
					continue
				guard = enum.attrib.get('protect') or ext_guard
				print_case(enum.attrib.get('name'), enum.attrib.get('name'), guard)
				if enum.attrib.get('alias'):
					print_case(enum.attrib.get('alias'), enum.attrib.get('alias'), guard)

	for alias, value in enum_value_aliases.get(name, {}).items():
		print_case(alias, value)

	if started:
		print('\telse { assert(false); return (%s) 0; }' % name, file=source)
	else:
		print('\tassert(false); return (%s) 0;' % name, file=source)
	print('}', file=source)
	print("\n", file=source)

for name in sorted(used_enum_converters):
	print_string_to_enum(name)

source.close()
header.close()
