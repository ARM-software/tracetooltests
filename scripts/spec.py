#!/usr/bin/python3

import os
import xml.etree.ElementTree as ET
import re
import collections
import typing as t
import itertools

## --- Utils ---

# We need this to ensure our generated code is deterministic. From https://github.com/bustawin/ordered-set-37
T = t.TypeVar("T")
class OrderedSet(t.MutableSet[T]):
    __slots__ = ('_d',)
    def __init__(self, iterable: t.Optional[t.Iterable[T]] = None):
        self._d = dict.fromkeys(iterable) if iterable else {}
    def add(self, x: T) -> None:
        self._d[x] = None
    def clear(self) -> None:
        self._d.clear()
    def discard(self, x: T) -> None:
        self._d.pop(x, None)
    def __getitem__(self, index) -> T:
        try:
            return next(itertools.islice(self._d, index, index + 1))
        except StopIteration:
            raise IndexError(f"index {index} out of range")
    def __contains__(self, x: object) -> bool:
        return self._d.__contains__(x)
    def __len__(self) -> int:
        return self._d.__len__()
    def __iter__(self) -> t.Iterator[T]:
        return self._d.__iter__()
    def __str__(self):
        return f"{{{', '.join(str(i) for i in self)}}}"
    def __repr__(self):
        return f"<OrderedSet {self}>"

## --- Provided data ---

INSTANCE_CHAIN_PARAMETERS = ["VkInstance", "VkPhysicalDevice"]
DEVICE_CHAIN_PARAMETERS = ["VkDevice", "VkQueue", "VkCommandBuffer"]

our_path = os.path.dirname(__file__)
tree = ET.parse('%s/../external/Vulkan-Headers/registry/vk.xml' % our_path)
root = tree.getroot()
videotree = ET.parse('%s/../external/Vulkan-Headers/registry/video.xml' % our_path)
videoroot = videotree.getroot()

disabled = OrderedSet()
disabled_functions = OrderedSet()
functions = [] # must be ordered, so cannot use set
valid_functions = [] # internal
protected_funcs = collections.OrderedDict()
protected_types = collections.OrderedDict()
structures = []
disp_handles = []
nondisp_handles = []
all_handles = []
platforms = collections.OrderedDict()
extension_structs = OrderedSet() # list of extension structs
type2sType = collections.OrderedDict() # map struct type -> sType enum
sType2type = collections.OrderedDict() # map stype -> struct type
function_aliases = collections.OrderedDict() # goes from vendor extension -> core extension -> core
aliases_to_functions_map = collections.OrderedDict()
extension_tags = []
parents = collections.OrderedDict() # dictionary of lists
externally_synchronized = OrderedSet() # tuples with (vulkan command, parameter name)
enums = [] # list of actually used, valid Vulkan enums
types = [] # list of actually used, valid Vulkan types
feature_structs = OrderedSet() # list of pNext feature structs

externally_synchronized_members = {
	'VkDescriptorSetAllocateInfo' : [ 'descriptorPool' ],
	'VkCommandBufferAllocateInfo' : [ 'commandPool' ],
	'VkSwapchainCreateInfoKHR' : [ 'surface', 'oldSwapchain' ],
	'VkDebugMarkerObjectTagInfoEXT' : [ 'object' ],
	'VkDebugMarkerObjectNameInfoEXT' : [ 'object' ],
	'VkDebugUtilsObjectNameInfoEXT' : [ 'objectHandle' ],
	'vkSetDebugUtilsObjectTagEXT' : [ 'objectHandle' ],
}

# We want to manually override some of these. We also add 'basetype' category types in here.
# This mapping is extended below with information from the XML.
type_mappings = {
	'char' : 'uint8_t',
	'int' : 'int32_t', # for all platforms we care about
	'long' : 'int64_t',
	'size_t' : 'uint64_t',
	'VkFlags' : 'uint32_t',
	'VkFlags64' : 'uint64_t',
	'void' : 'uint8_t', # buffer contents
	'xcb_visualid_t' : 'uint32_t',
}

packed_bitfields = [
	'StdVideoAV1ColorConfigFlags',
	'StdVideoAV1TimingInfoFlags',
	'StdVideoAV1LoopFilterFlags',
	'StdVideoAV1QuantizationFlags',
	'StdVideoAV1TileInfoFlags',
	'StdVideoAV1FilmGrainFlvags',
	'StdVideoAV1SequenceHeaderFlags',
	'StdVideoDecodeAV1PictureInfoFlags',
	'StdVideoDecodeAV1ReferenceInfoFlags',
	'StdVideoDecodeH264PictureInfoFlags',
	'StdVideoDecodeH264ReferenceInfoFlags',
	'StdVideoDecodeH265PictureInfoFlags',
	'StdVideoDecodeH265ReferenceInfoFlags',
	'StdVideoEncodeH265SliceSegmentHeaderFlags',
	'StdVideoEncodeH265PictureInfoFlags',
	'StdVideoEncodeH265ReferenceInfoFlags',
]

# Parameters not named *Count with type uint32_t or size_t that need temporaries created for access to them by other parameters.
other_counts = {
	'VkPipelineShaderStageModuleIdentifierCreateInfoEXT' : [ 'identifierSize' ],
	'VkPushConstantsInfoKHR' : [ 'size' ],
	'VkShaderModuleIdentifierEXT' : [ 'identifierSize' ],
	'vkCreateRayTracingPipelinesKHR' : [ 'dataSize' ],
	'vkGetPipelineExecutableInternalRepresentationsKHR' : [ 'dataSize' ],
	'vkCreateGraphicsPipelines' : [ 'rasterizationSamples', 'dataSize', 'pRasterizationState' ],
	'vkCreateComputePipelines' : [ 'dataSize' ],
	'vkUpdateDescriptorSets' : [ 'dataSize', 'descriptorType' ],
	'vkCmdPushDescriptorSetKHR' : [ 'dataSize', 'descriptorType' ],
	'vkSetDebugUtilsObjectTagEXT' : [ 'tagSize' ],
	'vkCreateShaderModule' : [ 'codeSize' ],
	'vkCreateValidationCacheEXT' : [ 'initialDataSize' ],
	'vkDebugMarkerSetObjectTagEXT' : [ 'tagSize' ],
	'vkCreatePipelineCache' : [ 'initialDataSize' ],
}

indirect_command_c_struct_names = {
	'vkCmdDispatchIndirect': 'VkDispatchIndirectCommand',
	'vkCmdDrawIndirect': 'VkDrawIndirectCommand',
	'vkCmdDrawIndexedIndirect': 'VkDrawIndexedIndirectCommand',
	'vkCmdDrawMeshTasksIndirectEXT': 'VkDrawMeshTasksIndirectCommandEXT',
	'vkCmdDrawIndirectCount': 'VkDrawIndirectCommand',
	'vkCmdDrawIndirectCountAMD': 'VkDrawIndirectCommand',
	'vkCmdDrawIndexedIndirectCount': 'VkDrawIndexedIndirectCommand',
	'vkCmdDrawIndexedIndirectCountAMD': 'VkDrawIndexedIndirectCommand',
}

# special call-me-twice query functions
special_count_funcs = collections.OrderedDict()

# dictionary values contain: name of variable holding created handle, name of variable holding number of handles to create (or hard-coded value), type of created handle
functions_create = collections.OrderedDict()

# Functions that destroy Vulkan objects
functions_destroy = collections.OrderedDict()

# Functions that can be called before an instance is created (loader implementations).
special_commands = []

# Functions that can be called before a logical device is created.
instance_chain_commands = []

# Functions that are called on existing logical devices, queues or command buffers.
device_chain_commands = []

## --- Code to fill the above with data ---

def str_contains_vendor(s):
	if not s: return False
	if 'KHR' in s or 'EXT' in s: return False # not a vendor and we want these
	if 'ANDROID' in s or 'GOOGLE' in s or 'ARM' in s: return False # permit these
	for t in extension_tags:
		if s.endswith(t):
			return True
	return False

def scan(req):
	api = req.attrib.get('api')
	if api and api == 'vulkansc': return
	for sc in req.findall('command'):
		sname = sc.attrib.get('name')
		if sname not in valid_functions and not str_contains_vendor(sname): valid_functions.append(sname)
	for sc in req.findall('enum'):
		sname = sc.attrib.get('name')
		if not sname in enums and not str_contains_vendor(sname): enums.append(sname)
	for sc in req.findall('type'):
		sname = sc.attrib.get('name')
		if not sname in types and not str_contains_vendor(sname): types.append(sname)

def scan_type(v):
	api = v.attrib.get('api')
	if api and api == 'vulkansc': return
	category = v.attrib.get('category')
	name = v.attrib.get('name')
	alias = v.attrib.get('alias')
	if category == 'struct':
		sType = None
		for m in v.findall('member'):
			sType = m.attrib.get('values')
			if sType and 'VK_STRUCTURE_TYPE' in sType:
				break
		if str_contains_vendor(sType) or not name in types: return
		if sType:
			type2sType[name] = sType
			sType2type[sType] = name
		# Look for extensions
		extendstr = v.attrib.get('structextends')
		extends = []
		if name in ['VkBaseOutStructure', 'VkBaseInStructure']: return
		if name in packed_bitfields: return
		structures.append(name)
		if 'Features' in name and not alias and not name in ['VkPhysicalDeviceFeatures2', 'VkPhysicalDeviceFeatures', 'VkValidationFeaturesEXT']:
			feature_structs.add(name)
		if name in protected_types:
			return # TBD: need a better way?
		if extendstr:
			assert sType, 'Failed to find structure type for %s' % name
			extends = extendstr.split(',')
		for e in extends:
			if str_contains_vendor(e): return
			if str_contains_vendor(name): return
			extension_structs.add(name)
	elif category == 'handle':
		if v.find('name') == None: # ignore aliases for now
			return
		name = v.find('name').text
		parenttext = v.attrib.get('parent')
		if str_contains_vendor(name): return
		if parenttext:
			parentsplit = parenttext.split(',')
			for p in parentsplit:
				if not name in parents:
					parents[name] = []
				parents[name].append(p)
		if v.find('type').text == 'VK_DEFINE_HANDLE':
			disp_handles.append(name)
		else: # non-dispatchable
			nondisp_handles.append(name)
		all_handles.append(name)
	elif category == 'enum':
		type_mappings[name] = 'uint32_t'
	elif category == 'basetype':
		name = v.find('name').text
		atype = v.find('type')
		if atype is not None: type_mappings[name] = atype.text
	elif category == 'bitmask':
		# ignore aliases for now
		if v.find('name') == None:
			return
		atype = v.find('type').text
		name = v.find('name').text
		if atype == 'VkFlags64':
			type_mappings[name] = 'uint64_t'
		elif atype == 'VkFlags':
			type_mappings[name] = 'uint32_t'
		else:
			assert false, 'Unknown bitmask type: %s' % atype

def init():
	# Find extension tags
	for v in root.findall("tags/tag"):
			name = v.attrib.get('name')
			if name not in extension_tags: extension_tags.append(name)

	# Find all platforms
	for v in root.findall('platforms/platform'):
		name = v.attrib.get('name')
		prot = v.attrib.get('protect')
		platforms[name] = prot

	# Find ifdef conditionals (that we want to recreate) and disabled extensions (that we want to ignore)
	for v in root.findall('extensions/extension'):
		name = v.attrib.get('name')
		conditional = v.attrib.get('platform')
		supported = v.attrib.get('supported')
		if supported == 'disabled':
			for dc in v.findall('require/command'):
				disabled_functions.add(dc.attrib.get('name'))
			disabled.add(name)
		elif supported != 'vulkansc':
			for req in v.findall('require'):
				scan(req)
		if conditional:
			for n in v.findall('require/command'):
				protected_funcs[n.attrib.get('name')] = platforms[conditional]
			for n in v.findall('require/type'):
				protected_types[n.attrib.get('name')] = platforms[conditional]
	for v in videoroot.findall('extensions/extension'):
		name = v.attrib.get('name')
		conditional = v.attrib.get('platform')
		supported = v.attrib.get('supported')
		if supported == 'disabled':
			for dc in v.findall('require/command'):
				disabled_functions.add(dc.attrib.get('name'))
			disabled.add(name)
		for req in v.findall('require'):
			scan(req)

	# Find all valid commands/functions/types
	for v in root.findall("feature"):
		apiname = v.attrib.get('name')
		apis = v.attrib.get('api')
		if apis == 'vulkansc': continue # skip these for now until Khronos sorts out this mess
		for req in v.findall('require'):
			scan(req)

	# Find all structures and handles
	for v in root.findall('types/type'):
		scan_type(v)
	for v in videoroot.findall('types/type'):
		scan_type(v)

	# Find aliases
	for v in root.findall("commands/command"):
		if v.attrib.get('alias'):
			alias_name = v.attrib.get('alias')
			name = v.attrib.get('name')
			if str_contains_vendor(name): continue
			function_aliases[name] = alias_name
			if not alias_name in aliases_to_functions_map:
				aliases_to_functions_map[alias_name] = []
			aliases_to_functions_map[alias_name].append(name)
		else:
			proto = v.find('proto')
			name = proto.find('name').text

	# Find all commands/function info
	for v in root.findall("commands/command"):
		api = v.attrib.get('api')
		if api and api == 'vulkansc': continue
		alias_name = v.attrib.get('alias')
		targets = []
		if not alias_name:
			targets.append(v.findall('proto/name')[0].text)
		else:
			continue # generate from aliased target instead

		origname = v.findall('proto/name')[0].text
		for a in aliases_to_functions_map.get(origname, []): # generate for any aliases as well, since they do not have proper data themselves
			targets.append(a)

		for name in targets:
			is_instance_chain_command = False
			is_device_chain_command = False

			if 'vkGet' in name or 'vkEnum' in name: # find special call-me-twice functions
				typename = None
				lastname = None
				counttype = None
				params = v.findall('param')
				assert len(params) > 0
				for param in params:
					ptype = param.text
					ptype = param.find('type').text
					pname = param.find('name').text
					optional = param.attrib.get('optional')
					typename = ptype # always the last parameter
					lastname = pname
					if not optional: continue
					if not 'true' in optional or not 'false' in optional: continue
					if pname and ptype and ('Count' in pname or 'Size' in pname) and (ptype == 'uint32_t' or ptype == 'size_t'):
						countname = pname
						counttype = ptype
				if counttype != None:
					if typename == 'void': typename = 'char'
					if 'vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCounters' in name: # special case it for now, only case of double out param
						special_count_funcs[name] = [ countname, counttype, [ [ 'pCounters', 'VkPerformanceCounterKHR' ], [ lastname, typename ] ] ]
					else:
						special_count_funcs[name] = [ countname, counttype, [ [ lastname, typename ] ] ]

			if 'vkFree' in name or 'vkDestroy' in name or 'vkCreate' in name or 'vkAllocate' in name:
				countname = "1"
				typename = None
				lastname = None
				params = v.findall('param')
				assert len(params) > 0
				for param in params:
					ptype = param.text
					ptype = param.find('type').text
					pname = param.find('name').text
					if pname and ptype and ('Count' in pname or 'Size' in pname) and ptype == 'uint32_t':
						countname = pname
					elif name == 'vkAllocateDescriptorSets': # some ugly exceptions where the count is inside a passed in struct
						countname = 'descriptorSetCount'
					elif name == 'vkAllocateCommandBuffers':
						countname = 'commandBufferCount'
					if pname == 'pAllocator' or pname == None:
						continue
					typename = ptype # always the last parameter
					lastname = pname
				if 'vkFree' in name or 'vkDestroy' in name:
					functions_destroy[name] = [ lastname, countname, typename ]
				elif 'vkCreate' in name or 'vkAllocate' in name:
					functions_create[name] = [ lastname, countname, typename ]

			# Find externally synchronized parameters
			params = v.findall('param')
			for param in params:
				extern = param.attrib.get('externsync')
				if not extern: continue # not externally synchronized
				typename = param.find('name').text
				type = param.find('type').text
				if extern == 'true':
					externally_synchronized.add((name, typename))
					continue
				all_exts = extern.split(',')
				for ext in all_exts:
					if '>' in ext:
						ext = ext.replace('[]', '')
						externally_synchronized.add((type, ext.split('>')[1]))
					elif '[' in ext:
						externally_synchronized.add((type, ext.split('.')[1]))
					else: assert False, '%s not parsed correctly' % ext

			params = v.findall('param/type')
			for param in params:
				ptype = param.text
				if ptype in INSTANCE_CHAIN_PARAMETERS:
					is_instance_chain_command = True
					break
				elif ptype in DEVICE_CHAIN_PARAMETERS:
					is_device_chain_command = True
					break

			if name in valid_functions and not name in functions:
				functions.append(name)
				if is_instance_chain_command:
					if name not in instance_chain_commands: instance_chain_commands.append(name)
				elif is_device_chain_command:
					if name not in device_chain_commands: device_chain_commands.append(name)
				else:
					if name not in special_commands: special_commands.append(name)

if __name__ == '__main__':
	init()
