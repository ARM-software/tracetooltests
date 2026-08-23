#!/usr/bin/env python3

import os
import re
import xml.etree.ElementTree as ET

ROOT_PATH = os.path.dirname(os.path.abspath(__file__))
REGISTRY_PATH = os.path.join(ROOT_PATH, "..", "external", "OpenCL-Docs", "xml", "cl.xml")

def command_names():
    root = ET.parse(REGISTRY_PATH).getroot()
    names = []
    for feature in root.findall("feature"):
        if feature.attrib.get("api") != "opencl":
            continue
        number = tuple(int(part) for part in feature.attrib["number"].split("."))
        if number > (3, 0):
            continue
        for command in feature.findall("require/command"):
            name = command.attrib["name"]
            if name not in names:
                names.append(name)
    return names

def command_details():
    root = ET.parse(REGISTRY_PATH).getroot()
    elements = {
        command.findtext("proto/name"): command
        for command in root.findall("commands/command")
        if command.find("proto/name") is not None
    }
    result = {}
    for name in command_names():
        command = elements[name]
        prototype = " ".join("".join(command.find("proto").itertext()).split())
        parameters = []
        for parameter in command.findall("param"):
            declaration = " ".join("".join(parameter.itertext()).split())
            parameters.append((declaration, parameter.findtext("name")))
        result[name] = {
            "return_type": prototype[: prototype.rfind(name)].strip(),
            "parameters": parameters,
            "deprecated": "DEPRECATED" in command.attrib.get("prefix", "")
            or "DEPRECATED" in command.attrib.get("suffix", ""),
        }
    return result

def icd_dispatch_commands():
    header_path = os.path.join(
        ROOT_PATH, "..", "external", "OpenCL-Headers", "CL", "cl_icd.h"
    )
    with open(header_path, encoding="utf-8") as header:
        return set(re.findall(r"\b(cl[A-Za-z0-9_]+)\s*;", header.read()))

def extension_commands():
    root = ET.parse(REGISTRY_PATH).getroot()
    result = {}
    for extension in root.findall("extensions/extension"):
        if "opencl" not in extension.attrib.get("supported", "").split(","):
            continue
        commands = set()
        for command in extension.findall("require/command"):
            commands.add(command.attrib["name"])
        result[extension.attrib["name"]] = commands
    return result

def extension_dependencies():
    root = ET.parse(REGISTRY_PATH).getroot()
    result = {}
    for extension in root.findall("extensions/extension"):
        if "opencl" not in extension.attrib.get("supported", "").split(","):
            continue
        result[extension.attrib["name"]] = set(
            re.findall(r"\bcl_[A-Za-z0-9_]+\b", extension.attrib.get("depends", ""))
        )
    return result
