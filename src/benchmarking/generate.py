#!/usr/bin/env python 3

import io
import json
import os
import sys


def generate_header(registry: dict, out: io.TextIOWrapper) -> None:

    sys.stdout, out = out, sys.stdout

    print('#pragma once')
    print('')
    print('#include <optional>')
    print('')
    print('#include "external/json.hpp"')
    print('')
    print('namespace bench')
    print('{')
    print('    template<typename T>')
    print('    struct Capability')
    print('    {')
    print('        T value;')
    print('        bool modifiable;')
    print('    };')
    print('')
    print('    struct Capabilities')
    print('    {')

    for cap_name, capability in registry['capabilities'].items():
        cap_type = capability['type']

        print(f'        std::optional<Capability<{cap_type}>> {cap_name};')

    print('')
    print('        void loadFromCapabilitiesFile(const nlohmann::json& root);')
    print('        void loadFromEnableFile(const Capabilities& capabilities, const nlohmann::json& root, bool addCapabilitiesFileDefaults);')
    print('    };')
    print('}')

    sys.stdout = out


def generate_source(registry: dict, out: io.TextIOWrapper) -> None:

    sys.stdout, out = out, sys.stdout

    print('#include "generated_benchmarking.hpp"')
    print('')
    print('#include <cassert>')
    print('')
    print('namespace bench')
    print('{')
    print('    void Capabilities::loadFromCapabilitiesFile(const nlohmann::json& root)')
    print('    {')

    for cap_name in registry['capabilities']:
        print(f'        {cap_name}.reset();')
    
    print('')
    print('        if (root.contains("capabilities"))')
    print('        {')
    print('            const nlohmann::json& node = root.at("capabilities");')

    for cap_name in registry['capabilities']:
        print('')
        print(f'            if (node.contains("{cap_name}"))')
        print('            {')
        print(f'                {cap_name}.emplace();')
        print(f'                node.at("{cap_name}").at("default").get_to({cap_name}->value);')
        print(f'                node.at("{cap_name}").at("modifiable").get_to({cap_name}->modifiable);')
        print('            }')

    print('        }')
    print('    }')
    print('')
    print('    void Capabilities::loadFromEnableFile(const Capabilities& capabilities, const nlohmann::json& root, bool addCapabilitiesFileDefaults)')
    print('    {')

    for cap_name in registry['capabilities']:
        print(f'        {cap_name}.reset();')

    print('')
    print('        if (root.contains("capabilities"))')
    print('        {')
    print('            const nlohmann::json& node = root.at("capabilities");')

    for cap_name, capability in registry['capabilities'].items():
        print('')
        print(f'            if (node.contains("{cap_name}"))')
        print('            {')
        print(f'                assert(capabilities.{cap_name}.has_value() && capabilities.{cap_name}->modifiable);')
        print(f'                {cap_name} = capabilities.{cap_name};')
        print(f'                node.at("{cap_name}").at("default").get_to({cap_name}->value);')
        if 'min' in capability:
            print(f"                assert({cap_name}->value >= {capability['min']});")
        if 'max' in capability:
            print(f"                assert({cap_name}->value <= {capability['max']});")
        print('            }')
        print(f'            else if (capabilities.{cap_name}.has_value() && addCapabilitiesFileDefaults)')
        print('            {')
        print(f'                {cap_name} = capabilities.{cap_name};')
        print('            }')

    print('        }')
    print('    }')
    print('}')

    sys.stdout = out


def main() -> int:

    project_dir = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
    registry_path = os.path.join(project_dir, 'doc', 'benchmarking_registry.json')
    header_path = os.path.join(project_dir, 'src', 'benchmarking', 'generated_benchmarking.hpp')
    source_path = os.path.join(project_dir, 'src', 'benchmarking', 'generated_benchmarking.cpp')

    with open(registry_path, 'r') as file:
        registry = json.load(file)
    
    with open(header_path, 'w') as file:
        generate_header(registry, file)
    
    with open(source_path, 'w') as file:
        generate_source(registry, file)

    return 0


if __name__ == '__main__':
    sys.exit(main())
