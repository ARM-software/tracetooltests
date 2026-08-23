#!/usr/bin/env python3

import unittest

import opencl_gpuinfo


REPORT_HTML = """
<html><head><title>Example GPU - OpenCL Hardware Database</title></head><body>
<table id="table_deviceinfo"><tbody>
<tr><td>Submitted by</td><td>Someone</td></tr>
<tr><td>Submitted at</td><td>2026-08-12 05:00:13</td></tr>
<tr><td>Operating system</td><td>Example OS</td></tr>
<tr><td>Identifier</td><td>Example identifier</td></tr>
<tr><td>CL_DEVICE_NAME</td><td>Example GPU r0p0</td></tr>
<tr><td>CL_DEVICE_VERSION</td><td><abbr title="OpenCL 3.0 full version">truncated</abbr></td></tr>
<tr><td>CL_DRIVER_VERSION</td><td>3.0</td></tr>
<tr><td>CL_DEVICE_NUMERIC_VERSION</td><td>3.0.0</td></tr>
<tr><td>CL_DEVICE_GLOBAL_MEM_SIZE</td><td>4,194,304 bytes</td></tr>
<tr><td>CL_DEVICE_IMAGE_SUPPORT</td><td><span class="supported">true</span></td></tr>
<tr><td>CL_DEVICE_SINGLE_FP_CONFIG</td><td>
<span class="supported">CL_FP_INF_NAN</span><br>
<span class="na">CL_FP_SOFT_FLOAT</span></td></tr>
<tr><td>CL_DEVICE_MAX_WORK_ITEM_SIZES</td><td>[1024, 512, 64]</td></tr>
<tr><td>CL_DEVICE_SUPPORTED_REGISTER_ALLOCATIONS_ARM</td><td>a:2:{i:0;i:32;i:1;i:64;}</td></tr>
</tbody></table>
<table id="table_deviceextensions"><tbody>
<tr><td>cl_khr_icd</td><td>1.0.0</td></tr>
</tbody></table>
<table id="table_deviceimageformats"><tbody>
<tr><td>CL_MEM_OBJECT_IMAGE2D</td><td>CL_RGBA</td><td>CL_UNORM_INT8</td>
<td><img src="images/icons/check.png"></td><td><img src="images/icons/missing.png"></td>
<td><img src="images/icons/check.png"></td><td><img src="images/icons/missing.png"></td></tr>
</tbody></table>
<table id="table_deviceplatforminfo"><tbody>
<tr><td>CL_PLATFORM_NAME</td><td>Example Platform</td></tr>
</tbody></table>
<table id="table_deviceplatformextensions"><tbody>
<tr><td>cl_khr_icd</td><td>1.0.0</td></tr>
</tbody></table>
</body></html>
"""


class OpenCLGPUInfoTest(unittest.TestCase):
    def test_profile_shape_and_typed_values(self):
        report = opencl_gpuinfo.parse_report(REPORT_HTML)
        profile = opencl_gpuinfo.build_profile(
            report,
            "7217",
            "https://opencl.gpuinfo.org/displayreport.php?id=7217",
        )

        device = profile["capabilities"]["device"]
        self.assertEqual(profile["profiles"]["CL_GPUINFO_7217"]["api-version"], "3.0.0")
        self.assertEqual(device["properties"]["CL_DEVICE_VERSION"], "OpenCL 3.0 full version")
        self.assertEqual(device["properties"]["CL_DRIVER_VERSION"], "3.0")
        self.assertEqual(device["properties"]["CL_DEVICE_GLOBAL_MEM_SIZE"], 4194304)
        self.assertIs(device["properties"]["CL_DEVICE_IMAGE_SUPPORT"], True)
        self.assertEqual(device["properties"]["CL_DEVICE_SINGLE_FP_CONFIG"], ["CL_FP_INF_NAN"])
        self.assertEqual(device["properties"]["CL_DEVICE_MAX_WORK_ITEM_SIZES"], [1024, 512, 64])
        self.assertEqual(
            device["properties"]["CL_DEVICE_SUPPORTED_REGISTER_ALLOCATIONS_ARM"],
            [32, 64],
        )
        self.assertEqual(device["extensions"], {"cl_khr_icd": "1.0.0"})
        self.assertEqual(
            device["imageFormats"][0]["access"],
            ["CL_MEM_READ_WRITE", "CL_MEM_READ_ONLY"],
        )
        self.assertNotIn("Submitted by", profile["source"])

    def test_url_validation_and_default_name(self):
        report_id, canonical = opencl_gpuinfo.validate_url(
            "http://opencl.gpuinfo.org/displayreport.php?id=7217"
        )
        self.assertEqual(report_id, "7217")
        self.assertEqual(
            canonical,
            "https://opencl.gpuinfo.org/displayreport.php?id=7217",
        )
        self.assertEqual(
            opencl_gpuinfo.default_gpu_name("Mali-G715 r0p0"),
            "Mali-G715-r0p0",
        )

    def test_rejects_conflicting_duplicate_property(self):
        duplicate = REPORT_HTML.replace(
            "</tbody></table>\n<table id=\"table_deviceextensions\">",
            "<tr><td>CL_DEVICE_NAME</td><td>Different GPU</td></tr>"
            "</tbody></table>\n<table id=\"table_deviceextensions\">",
            1,
        )
        with self.assertRaises(opencl_gpuinfo.ScrapeError):
            opencl_gpuinfo.parse_report(duplicate)


if __name__ == "__main__":
    unittest.main()
