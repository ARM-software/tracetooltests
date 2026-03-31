#!/usr/bin/env python3

import sys
import os
import re
import sys

ROOT_PATH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT_PATH)
import spec

source = open('tostring.cpp', 'w')
header = open('tostring.h', 'w')
assert source, 'Could not create source file'
assert header, 'Could not create header file'

print('// This file contains only auto-generated code!', file=header)
print("\n", file=header)
print('#pragma once', file=header)
print('#include "util.h"', file=header)


print('// This file contains only auto-generated code!', file=source)
print("\n", file=source)
print('#include "tostring.h"', file=source)
print('#include <string.h>', file=source)
print("\n", file=source)

bitmask = {}
missing = []
protected = {}
platforms = {}

spec.init()

# Find all platforms
for v in spec.root.findall('platforms/platform'):
	name = v.attrib.get('name')
	prot = v.attrib.get('protect')
	platforms[name] = prot

# Find all flag conversions
for v in spec.root.findall('types/type'):
	category = v.attrib.get('category')
	requires = v.attrib.get('requires')
	api = v.attrib.get('api')
	if api and api == 'vulkansc': continue
	if category == 'bitmask':
		# ignore aliases for now
		if v.find('name') == None:
			continue
		name = v.find('name').text
		if requires:
			bitmask[requires] = name
		else:
			missing.append(name)

# Find ifdef conditionals (that we want to recreate) and disabled extensions (that we want to ignore)
for v in spec.root.findall('extensions/extension'):
	name = v.attrib.get('name')
	conditional = v.attrib.get('platform')
	if conditional:
		for n in v.findall('require/type'):
			protected[n.attrib.get('name')] = platforms[conditional]

# -- Bit to string --

added_case = []
for v in spec.root.findall('enums'):
	rname = v.attrib.get('name')
	type = v.attrib.get('type')
	if not type or type != 'bitmask':
		continue
	if not rname in bitmask or not rname in spec.types:
		continue
	ename = bitmask[rname]
	if ename in protected:
		print('#ifdef %s // %s' % (protected[ename], rname), file=header)
		print('#ifdef %s // %s' % (protected[ename], rname), file=source)
	print('std::string %s_to_string(%s flags);' % (ename, ename), file=header)
	print('std::string %s_to_string(%s flags)' % (ename, ename), file=source)
	print('{', file=source)
	print('\tstd::string result;', file=source)
	print('\twhile (flags)', file=source)
	print('\t{', file=source)
	print('\t\tint bit = 0;', file=source)
	print('\t\twhile (!(flags & 1 << bit)) bit++; // find first set bit', file=source)
	print('\t\tswitch (flags & (1 << bit))', file=source)
	print('\t\t{', file=source)
	for bit in v.findall('enum'):
		name = bit.attrib.get('name')
		pos = bit.attrib.get('bitpos')
		if pos and not name in added_case:
			print('\t\tcase %s: result += "%s"; break;' % (name, name), file=source)
			added_case.append(name)

	# Find and add post-1.0 variants
	for feat in spec.root.findall('feature'):
		api = feat.attrib.get('api', '').split(',')
		if api and not 'vulkan' in api: continue
		for bit in feat.findall('require/enum'):
			extends = bit.attrib.get('extends')
			if extends == rname:
				name = bit.attrib.get('name')
				pos = bit.attrib.get('bitpos')
				if pos and not name in added_case:
					print('\t\tcase %s: result += "%s"; break;' % (name, name), file=source)
					added_case.append(name)

	# Find and add extensions enums
	for ext in spec.root.findall('extensions/extension'):
		supported = ext.attrib.get('supported', '').split(',')
		if supported and not 'vulkan' in supported: continue
		for vv in ext.findall('require'):
			for bit in vv.findall('enum'):
				extends = bit.attrib.get('extends')
				if extends == rname:
					name = bit.attrib.get('name')
					pos = bit.attrib.get('bitpos')
					prot = bit.attrib.get('protect')
					if pos and not name in added_case:
						if prot: print('#ifdef %s' % prot, file=source)
						print('\t\tcase %s: result += "%s"; break;' % (name, name), file=source)
						added_case.append(name)
						if prot: print('#endif', file=source)
	print('\t\tdefault: result += "Bad bitfield value"; break;', file=source)
	print('\t\t}', file=source)
	print('\t\tflags &= ~(1 << bit); // remove bit', file=source)
	print('\t\tif (flags) result += " | ";', file=source)
	print('\t}', file=source)
	print('\treturn result;', file=source)
	print('}', file=source)
	if ename in protected:
		print('#endif', file=header)
		print('#endif', file=source)
	print("\n", file=source)

for name in missing: # create stubs for unused flags
	if name in protected:
		print('#ifdef %s // %s' % (protected[name], rname), file=header)
	print('static inline std::string %s_to_string(%s flags) { return std::string(); }' % (name, name), file=header)
	if name in protected:
		print('#endif', file=header)

# -- Enum to string --

print("\n", file=header)

added_case = []
for v in spec.root.findall('enums'):
	name = v.attrib.get('name')
	type = v.attrib.get('type')
	if not type or type != 'enum' or not name in spec.types:
		continue
	if name in protected:
		print('#ifdef %s // %s' % (protected[name], name), file=header)
		print('#ifdef %s // %s' % (protected[name], name), file=source)
	print('std::string %s_to_string(%s val);' % (name, name), file=header)
	print('std::string %s_to_string(%s val)' % (name, name), file=source)
	print('{', file=source)
	print('\tswitch (val)', file=source)
	print('\t{', file=source)
	for item in v.findall('enum'):
		itemname = item.attrib.get('name')
		if item.attrib.get('alias', None):
			continue
		print('\tcase %s: return "%s";' % (itemname, itemname), file=source)
		added_case.append(itemname)
	# Find and add extensions enums
		supported = ext.attrib.get('supported')
		if supported in ['disabled', 'vulkansc']: continue
	for vv in spec.root.findall('extensions/extension'):
		extname = vv.attrib.get('name')
		if vv.attrib.get('supported') in ['disabled', 'vulkansc']:
			continue
		for item in vv.findall('require/enum'):
			extends = item.attrib.get('extends')
			itemname = item.attrib.get('name')
			if extends == name and not itemname in added_case:
				# FIXME need to handle deduplication of aliases, not trivial from spec
				#print( ,file=source)'\tcase %s: return "%s"; // from %s' % (itemname, itemname, extname)
				added_case.append(itemname)
	print('\tdefault: return "Unhandled enum";', file=source)
	print('\t}', file=source)
	print('\treturn "Error";', file=source)
	print('}', file=source)
	if name in protected:
		print('#endif', file=header)
		print('#endif', file=source)
	print("\n", file=source)
