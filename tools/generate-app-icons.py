#!/usr/bin/env python3
"""Render the application's SVG with Inkscape and package PNGs as ICO/ICNS.

Requires Inkscape only when updating the artwork. Generated assets are committed,
so normal application builds need neither Inkscape nor extra Python packages.
"""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def generate(inkscape: str, icns_only: bool = False) -> None:
    """Render SVG sizes and write native containers; ICNS-only preserves a supplied ICO."""
    icons = Path(__file__).resolve().parents[1] / "assets/icons"
    source = icons / "alt-pg-admin.svg"
    sizes = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
    if icns_only:
        sizes = [size for size in sizes if size not in (24, 48)]
    with tempfile.TemporaryDirectory(prefix="alt-pg-admin-icons-") as directory:
        images = {}
        for size in sizes:
            png = Path(directory) / f"{size}.png"
            subprocess.run([inkscape, str(source), "--export-type=png",
                            f"--export-filename={png}", f"--export-width={size}",
                            f"--export-height={size}"], check=True)
            images[size] = png.read_bytes()
        windows_sizes = [size for size in sizes if size <= 256]
        header = struct.pack("<HHH", 0, 1, len(windows_sizes))
        entries = bytearray()
        payload = bytearray()
        offset = 6 + 16 * len(windows_sizes)
        for size in windows_sizes:
            data = images[size]
            entries.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0,
                                       1, 32, len(data), offset))
            payload.extend(data)
            offset += len(data)
        if not icns_only:
            (icons / "alt-pg-admin.ico").write_bytes(header + entries + payload)
        chunks = bytearray()
        for kind, size in [(b"icp4", 16), (b"icp5", 32), (b"icp6", 64),
                           (b"ic07", 128), (b"ic08", 256), (b"ic09", 512),
                           (b"ic10", 1024), (b"ic11", 32), (b"ic12", 64),
                           (b"ic13", 256), (b"ic14", 512)]:
            data = images[size]
            chunks.extend(kind + struct.pack(">I", 8 + len(data)) + data)
        (icons / "alt-pg-admin.icns").write_bytes(b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks)
    print("Generated assets/icons/alt-pg-admin.icns" if icns_only else
          "Generated assets/icons/alt-pg-admin.ico and alt-pg-admin.icns")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    default = shutil.which("inkscape") or "C:/Program Files/Inkscape/bin/inkscape.exe"
    parser.add_argument("--inkscape", default=default)
    parser.add_argument("--icns-only", action="store_true", help="Update the macOS icon without replacing the supplied Windows ICO")
    args = parser.parse_args()
    generate(args.inkscape, args.icns_only)
