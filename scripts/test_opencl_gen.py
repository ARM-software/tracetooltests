#!/usr/bin/env python3

import subprocess
import tempfile
import unittest
from pathlib import Path

import opencl_gen
import opencl_spec


class OpenCLGeneratorTest(unittest.TestCase):
    def test_registry_contains_supported_commands(self):
        commands = set(opencl_spec.command_names())
        self.assertGreater(len(commands), 100)
        self.assertTrue(set(opencl_gen.SUPPORTED_COMMANDS).issubset(commands))

    def test_command_signatures_and_dispatch_members(self):
        details = opencl_spec.command_details()
        self.assertEqual(details["clCreateContext"]["return_type"], "cl_context")
        self.assertEqual(details["clSVMFree"]["return_type"], "void")
        self.assertTrue(details["clCreateCommandQueue"]["deprecated"])
        self.assertIn("clCreateImage", opencl_spec.icd_dispatch_commands())

    def test_generated_dispatch_has_modeled_and_stubbed_commands(self):
        script = Path(__file__).with_name("opencl_gen.py")
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([str(script)], cwd=directory, check=True)
            source = Path(directory, "opencl_auto.cpp").read_text(encoding="utf-8")
        self.assertIn("dispatch.clCreateBuffer = clCreateBuffer;", source)
        self.assertIn("dispatch.clCreateImage = clCreateImage;", source)
        self.assertIn("dispatch.clCreateImage2D = clCreateImage2D;", source)
        self.assertIn("dispatch.clEnqueueTask = clEnqueueTask;", source)
        self.assertIn("return CL_INVALID_OPERATION;", source)
        self.assertIn('strcmp(name, "cl_khr_icd")', source)
        self.assertNotIn('strcmp(name, "cl_khr_command_buffer")', source)

    def test_deprecated_dispatch_slots_are_never_left_null(self):
        details = opencl_spec.command_details()
        dispatch_commands = opencl_spec.icd_dispatch_commands()
        script = Path(__file__).with_name("opencl_gen.py")
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([str(script)], cwd=directory, check=True)
            source = Path(directory, "opencl_auto.cpp").read_text(encoding="utf-8")
            for name, command in details.items():
                if command["deprecated"] and name in dispatch_commands:
                    self.assertIn(f"dispatch.{name} = {name};", source)


if __name__ == "__main__":
    unittest.main()
