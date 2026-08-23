#!/usr/bin/env python3

"""Create a Chameleon OpenCL profile from an opencl.gpuinfo.org report."""

import argparse
import os
import re
import sys
import tempfile
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path

# This repository has a generator named scripts/json.py.  When this file is
# executed directly, that directory is first on sys.path and would shadow the
# standard-library json module.
_script_path = None
if sys.path and Path(sys.path[0]).resolve() == Path(__file__).resolve().parent:
    _script_path = sys.path.pop(0)
import json
if _script_path is not None:
    sys.path.insert(0, _script_path)

GPUINFO_HOST = "opencl.gpuinfo.org"
GPUINFO_PATH = "/displayreport.php"
TABLE_IDS = {
    "table_deviceinfo",
    "table_deviceextensions",
    "table_deviceimageformats",
    "table_deviceplatforminfo",
    "table_deviceplatformextensions",
}
ACCESS_FLAGS = (
    "CL_MEM_READ_WRITE",
    "CL_MEM_WRITE_ONLY",
    "CL_MEM_READ_ONLY",
    "CL_MEM_KERNEL_READ_AND_WRITE",
)

class ScrapeError(RuntimeError):
    pass

@dataclass
class Cell:
    segments: list[tuple[str, frozenset[str]]] = field(default_factory=list)
    images: list[str] = field(default_factory=list)

    def add_text(self, text, classes):
        if text:
            self.segments.append((text, frozenset(classes)))

    def add_break(self, classes):
        self.segments.append(("\n", frozenset(classes)))

class ReportHTMLParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.tables = {}
        self.page_title = ""
        self._in_title = False
        self._table_id = None
        self._in_tbody = False
        self._row = None
        self._cell = None
        self._span_classes = []
        self._abbr_depth = 0

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "title":
            self._in_title = True
        elif tag == "table":
            table_id = attributes.get("id")
            if table_id in TABLE_IDS:
                self._table_id = table_id
                self.tables.setdefault(table_id, [])
        elif self._table_id and tag == "tbody":
            self._in_tbody = True
        elif self._table_id and self._in_tbody and tag == "tr":
            self._row = []
        elif self._row is not None and tag in ("td", "th"):
            self._cell = Cell()
        elif self._cell is not None and tag == "span":
            self._span_classes.append(set(attributes.get("class", "").split()))
        elif self._cell is not None and tag == "abbr":
            self._abbr_depth += 1
            title = attributes.get("title", "").strip()
            if title:
                self._cell.add_text(title, self._current_classes())
        elif self._cell is not None and tag == "br":
            self._cell.add_break(self._current_classes())
        elif self._cell is not None and tag == "img":
            self._cell.images.append(attributes.get("src", ""))

    def handle_endtag(self, tag):
        if tag == "title":
            self._in_title = False
        elif self._cell is not None and tag == "abbr":
            self._abbr_depth -= 1
        elif self._cell is not None and tag == "span":
            if self._span_classes:
                self._span_classes.pop()
        elif self._cell is not None and tag in ("td", "th"):
            self._row.append(self._cell)
            self._cell = None
            self._span_classes.clear()
            self._abbr_depth = 0
        elif self._row is not None and tag == "tr":
            if self._row:
                self.tables[self._table_id].append(self._row)
            self._row = None
        elif self._table_id and tag == "tbody":
            self._in_tbody = False
        elif self._table_id and tag == "table":
            self._table_id = None

    def handle_data(self, data):
        if self._in_title:
            self.page_title += data
        elif self._cell is not None and self._abbr_depth == 0:
            self._cell.add_text(data, self._current_classes())

    def _current_classes(self):
        result = set()
        for classes in self._span_classes:
            result.update(classes)
        return result

def clean_text(text):
    return re.sub(r"\s+", " ", text).strip()

def cell_groups(cell):
    groups = [[]]
    for text, classes in cell.segments:
        if text == "\n":
            if groups[-1]:
                groups.append([])
            continue
        text = clean_text(text)
        if text:
            groups[-1].append((text, classes))
    return [group for group in groups if group]

def group_text(group):
    return clean_text(" ".join(text for text, _classes in group))

def parse_scalar(text):
    text = clean_text(text)
    lower = text.lower()
    if lower == "true":
        return True
    if lower == "false":
        return False
    if re.fullmatch(r"0x[0-9a-fA-F]+", text):
        return int(text, 16)
    match = re.fullmatch(r"([+-]?[0-9][0-9,]*)(?: bytes?)?", text)
    if match:
        return int(match.group(1).replace(",", ""))
    if text.startswith("[") and text.endswith("]"):
        values = [value.strip() for value in text[1:-1].split(",")]
        return [parse_scalar(value) for value in values if value]
    php_array = re.fullmatch(
        r"a:([0-9]+):\{((?:i:-?[0-9]+;i:-?[0-9]+;)*)\}", text
    )
    if php_array:
        pairs = [
            (int(key), int(value))
            for key, value in re.findall(r"i:(-?[0-9]+);i:(-?[0-9]+);", php_array.group(2))
        ]
        sequential_keys = [key for key, _value in pairs] == list(range(len(pairs)))
        if len(pairs) == int(php_array.group(1)) and sequential_keys:
            return [value for _key, value in pairs]
    return text

def parse_cell(cell):
    groups = cell_groups(cell)
    classified = [
        (text, classes)
        for group in groups
        for text, classes in group
        if "supported" in classes or "unsupported" in classes or "na" in classes
    ]
    if classified:
        all_text = [text for text, _classes in classified]
        if len(all_text) == 1 and all_text[0].lower() in ("true", "false"):
            return all_text[0].lower() == "true"
        supported = [
            text
            for text, classes in classified
            if "supported" in classes and text.lower() not in ("true", "false")
        ]
        if supported:
            return supported
        if any("na" in classes for _text, classes in classified):
            return []

    values = [parse_scalar(group_text(group)) for group in groups]
    if not values:
        return ""
    if len(values) == 1:
        return values[0]
    return values

def insert_unique(mapping, key, value, source):
    if key in mapping and mapping[key] != value:
        raise ScrapeError(
            f"Conflicting duplicate {source} value for {key}: "
            f"{mapping[key]!r} versus {value!r}"
        )
    mapping[key] = value

def rows_to_properties(rows, source):
    result = {}
    for row in rows:
        if len(row) != 2:
            raise ScrapeError(f"Expected two columns in {source}, found {len(row)}")
        key = group_text(cell_groups(row[0])[0]) if cell_groups(row[0]) else ""
        if not key:
            continue
        insert_unique(result, key, parse_cell(row[1]), source)
    return result

def rows_to_extensions(rows, source):
    result = {}
    for row in rows:
        if len(row) != 2:
            raise ScrapeError(f"Expected two columns in {source}, found {len(row)}")
        name_groups = cell_groups(row[0])
        if not name_groups:
            continue
        name = group_text(name_groups[0])
        version = parse_cell(row[1])
        if version == "":
            version = None
        insert_unique(result, name, version, source)
    return result

def rows_to_image_formats(rows):
    result = []
    for row in rows:
        if len(row) != 7:
            raise ScrapeError(
                f"Expected seven columns in image formats, found {len(row)}"
            )
        values = [parse_cell(cell) for cell in row[:3]]
        access = []
        for flag, cell in zip(ACCESS_FLAGS, row[3:]):
            if any(image.endswith("check.png") for image in cell.images):
                access.append(flag)
        result.append(
            {
                "imageType": values[0],
                "channelOrder": values[1],
                "channelType": values[2],
                "access": access,
            }
        )
    result.sort(
        key=lambda item: (
            str(item["imageType"]),
            str(item["channelOrder"]),
            str(item["channelType"]),
        )
    )
    return result

def parse_report(html):
    parser = ReportHTMLParser()
    parser.feed(html)
    missing = TABLE_IDS.difference(parser.tables)
    if missing:
        raise ScrapeError(
            "The report page is missing expected tables: " + ", ".join(sorted(missing))
        )

    raw_device = rows_to_properties(
        parser.tables["table_deviceinfo"], "device properties"
    )
    device_properties = {}
    metadata = {}
    for key, value in raw_device.items():
        if key.startswith("CL_"):
            device_properties[key] = value
        elif key != "Submitted by":
            metadata[key] = value

    return {
        "page_title": clean_text(parser.page_title),
        "metadata": metadata,
        "platform_properties": rows_to_properties(
            parser.tables["table_deviceplatforminfo"], "platform properties"
        ),
        "platform_extensions": rows_to_extensions(
            parser.tables["table_deviceplatformextensions"], "platform extensions"
        ),
        "device_properties": device_properties,
        "device_extensions": rows_to_extensions(
            parser.tables["table_deviceextensions"], "device extensions"
        ),
        "image_formats": rows_to_image_formats(
            parser.tables["table_deviceimageformats"]
        ),
    }

def validate_url(url):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme not in ("http", "https"):
        raise ScrapeError("The report URL must use http or https")
    if parsed.hostname != GPUINFO_HOST or parsed.port is not None:
        raise ScrapeError(f"The report URL must use the host {GPUINFO_HOST}")
    if parsed.path != GPUINFO_PATH:
        raise ScrapeError(f"The report URL path must be {GPUINFO_PATH}")
    report_ids = urllib.parse.parse_qs(parsed.query).get("id", [])
    if len(report_ids) != 1 or not report_ids[0].isdigit():
        raise ScrapeError("The report URL must contain one numeric id parameter")
    report_id = report_ids[0]
    canonical_url = f"https://{GPUINFO_HOST}{GPUINFO_PATH}?id={report_id}"
    return report_id, canonical_url

def download_report(url):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "tracetooltests-opencl-profile-scraper/1.0"},
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            charset = response.headers.get_content_charset() or "ISO-8859-1"
            return response.read().decode(charset)
    except (urllib.error.URLError, UnicodeError) as error:
        raise ScrapeError(f"Failed to download {url}: {error}") from error

def api_version(properties):
    numeric = properties.get("CL_DEVICE_NUMERIC_VERSION")
    if isinstance(numeric, str) and re.fullmatch(
        r"[0-9]+\.[0-9]+\.[0-9]+", numeric
    ):
        return numeric
    version = properties.get("CL_DEVICE_VERSION", "")
    if isinstance(version, str):
        match = re.search(r"OpenCL ([0-9]+)\.([0-9]+)(?:\.([0-9]+))?", version)
        if match:
            return f"{match.group(1)}.{match.group(2)}.{match.group(3) or '0'}"
    raise ScrapeError("Could not determine the OpenCL API version from the report")

REQUIRED_PLATFORM_PROPERTIES = (
    "CL_PLATFORM_PROFILE",
    "CL_PLATFORM_VERSION",
    "CL_PLATFORM_NAME",
    "CL_PLATFORM_VENDOR",
    "CL_PLATFORM_HOST_TIMER_RESOLUTION",
)

REQUIRED_DEVICE_PROPERTIES = (
    "CL_DEVICE_NAME",
    "CL_DEVICE_VENDOR",
    "CL_DEVICE_VENDOR_ID",
    "CL_DEVICE_VERSION",
    "CL_DEVICE_NUMERIC_VERSION",
    "CL_DEVICE_AVAILABLE",
    "CL_DEVICE_MAX_COMPUTE_UNITS",
    "CL_DEVICE_MAX_WORK_GROUP_SIZE",
)

def require_properties(properties, required, description):
    missing = sorted(set(required).difference(properties))
    if missing:
        raise ScrapeError(
            f"The report is missing required {description}: {', '.join(missing)}"
        )

def build_profile(report, report_id, source_url):
    properties = report["device_properties"]
    # A submitted report necessarily came from an available device, but the
    # site does not include this mandatory core query in every report.
    properties.setdefault("CL_DEVICE_AVAILABLE", True)
    require_properties(
        report["platform_properties"],
        REQUIRED_PLATFORM_PROPERTIES,
        "platform properties",
    )
    require_properties(properties, REQUIRED_DEVICE_PROPERTIES, "device properties")
    device_name = properties.get("CL_DEVICE_NAME")
    if not isinstance(device_name, str) or not device_name:
        raise ScrapeError("The report does not contain a usable CL_DEVICE_NAME")
    operating_system = report["metadata"].get("Operating system", "unknown OS")
    submitted_at = report["metadata"].get("Submitted at")

    profile = {
        "version": 1,
        "api-version": api_version(properties),
        "label": f"{device_name} on {operating_system}",
        "description": f"Scraped from {source_url}",
        "capabilities": ["platform", "device"],
    }
    if isinstance(submitted_at, str) and re.match(
        r"[0-9]{4}-[0-9]{2}-[0-9]{2}", submitted_at
    ):
        profile["history"] = [
            {
                "revision": 1,
                "date": submitted_at[:10],
                "author": "gpuinfo.org",
                "comment": "Automated scrape from opencl.gpuinfo.org",
            }
        ]

    source = {
        "url": source_url,
        "report-id": int(report_id),
        "data-license": "CC-BY-4.0",
        "attribution": "OpenCL Hardware Database at gpuinfo.org",
    }
    for source_key, metadata_key in (
        ("submitted-at", "Submitted at"),
        ("operating-system", "Operating system"),
        ("identifier", "Identifier"),
    ):
        if metadata_key in report["metadata"]:
            source[source_key] = report["metadata"][metadata_key]

    return {
        "profiles": {f"CL_GPUINFO_{report_id}": profile},
        "capabilities": {
            "platform": {
                "properties": report["platform_properties"],
                "extensions": report["platform_extensions"],
            },
            "device": {
                "properties": properties,
                "extensions": report["device_extensions"],
                "imageFormats": report["image_formats"],
            },
        },
        "source": source,
    }

def default_gpu_name(device_name):
    ascii_name = (
        unicodedata.normalize("NFKD", device_name)
        .encode("ascii", "ignore")
        .decode()
    )
    name = re.sub(r"[^A-Za-z0-9.+-]+", "-", ascii_name).strip("-.")
    if not name:
        raise ScrapeError("Could not derive a directory name from CL_DEVICE_NAME")
    return name

def validate_gpu_name(name):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+_-]*", name):
        raise ScrapeError(
            "The GPU directory name must be one path component containing only "
            "letters, digits, '.', '+', '_', or '-'"
        )

def write_profile(profile, output_path, force):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists() and not force:
        raise ScrapeError(
            f"Refusing to overwrite {output_path}; pass --force to replace it"
        )
    with tempfile.NamedTemporaryFile(
        mode="w",
        encoding="utf-8",
        dir=output_path.parent,
        prefix=f".{output_path.name}.",
        delete=False,
    ) as output:
        temporary_path = Path(output.name)
        json.dump(profile, output, indent=4, ensure_ascii=False)
        output.write("\n")
    try:
        os.replace(temporary_path, output_path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise

def parse_args(argv):
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", help="OpenCL GPUInfo displayreport.php URL")
    parser.add_argument(
        "--gpu",
        help="device directory name; defaults to a sanitized CL_DEVICE_NAME",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=repository / "share" / "chameleon" / "devices",
        help="parent directory for GPU profiles",
    )
    parser.add_argument(
        "--force", action="store_true", help="replace an existing opencl.json"
    )
    parser.add_argument(
        "--stdout", action="store_true", help="print JSON instead of writing it"
    )
    return parser.parse_args(argv)

def main(argv=None):
    args = parse_args(argv)
    try:
        report_id, canonical_url = validate_url(args.url)
        report = parse_report(download_report(canonical_url))
        profile = build_profile(report, report_id, canonical_url)
        if args.stdout:
            json.dump(profile, sys.stdout, indent=4, ensure_ascii=False)
            sys.stdout.write("\n")
            return 0

        device_name = profile["capabilities"]["device"]["properties"]["CL_DEVICE_NAME"]
        gpu_name = args.gpu or default_gpu_name(device_name)
        validate_gpu_name(gpu_name)
        output_path = args.output_root / gpu_name / "opencl.json"
        write_profile(profile, output_path, args.force)
        print(output_path)
        return 0
    except ScrapeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
