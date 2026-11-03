#!/usr/bin/env python3
"""Check a manual-install PortMaster ZIP without executing its ARM binaries."""
import argparse
import json
import re
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath


def verify(archive):
    errors = []
    binaries = {}
    with zipfile.ZipFile(archive) as z:
        bad = z.testzip()
        if bad:
            errors.append("CRC failure: " + bad)
        names = z.namelist()
        if len(names) != len(set(names)):
            errors.append("Duplicate archive entries")
        benchmark = "Vurus Benchmark.sh" in names
        required = {
            "Vurus.sh", "vurus/vurus.aarch64", "vurus/README.txt",
            "vurus/engine-README.txt", "vurus/LICENSE.md",
            "vurus/licenses/Vurus.txt", "vurus/licenses/oxideav-indeo.txt",
            "vurus/licenses/zlib.txt", "vurus/licenses/SMAA.txt",
            "vurus/licenses/mame-psgs.txt", "vurus/licenses/MAME-PSGS-NOTICE.txt",
            "vurus/licenses/libc++.txt", "vurus/licenses/libc++abi.txt",
            "vurus/licenses/GCC-runtime.txt",
            "vurus/licenses/rust-licenses/COPYRIGHT-library.html",
        }
        if benchmark:
            required.update({"Vurus Benchmark.sh", "vurus/vurus-bench.aarch64",
                             "vurus/benchmark-README.txt"})
        errors.extend("Missing " + name for name in sorted(required - set(names)))
        scripts = {"Vurus.sh"}
        engines = {"vurus/vurus.aarch64"}
        if benchmark:
            scripts.add("Vurus Benchmark.sh")
            engines.add("vurus/vurus-bench.aarch64")
        top = {"vurus"} | scripts
        runtime = {"vurus.aarch64", "README.txt", "engine-README.txt", "LICENSE.md", "licenses"}
        if benchmark:
            runtime.update({"vurus-bench.aarch64", "benchmark-README.txt"})
        forbidden = {"data", "saves", ".git", ".agents", ".codex", ".aws"}
        for item in z.infolist():
            p = PurePosixPath(item.filename)
            if p.is_absolute() or ".." in p.parts or "\\" in item.filename:
                errors.append("Unsafe archive path: " + item.filename)
                continue
            if not p.parts or p.parts[0] not in top:
                errors.append("Unexpected root entry: " + item.filename)
            elif p.parts[0] == "vurus" and len(p.parts) > 1 and p.parts[1] not in runtime:
                errors.append("Unexpected runtime entry: " + item.filename)
            mode = item.external_attr >> 16
            if stat.S_ISLNK(mode):
                errors.append("Symlink in package: " + item.filename)
            if forbidden.intersection(p.parts) or p.suffix.lower() in {
                    ".iso", ".bin", ".cue", ".rsc", ".gaf", ".wav", ".avi", ".vzs", ".cfg"}:
                errors.append("Original/user data in package: " + item.filename)
            if item.filename in scripts | engines and mode & 0o777 != 0o755:
                errors.append("Expected mode 0755: " + item.filename)
        for name in scripts.intersection(names):
            body = z.read(name)
            if not body.startswith(b"#!/bin/bash\n") or b"\r" in body:
                errors.append("Invalid launcher header/line endings: " + name)
            check = subprocess.run(["bash", "-n"], input=body, capture_output=True)
            if check.returncode:
                errors.append("Shell syntax error: " + name)
        with tempfile.TemporaryDirectory(prefix="vurus-package-check-") as directory:
            for name in sorted(engines.intersection(names)):
                p = Path(directory) / PurePosixPath(name).name
                p.write_bytes(z.read(name))
                header = subprocess.check_output(["readelf", "-h", str(p)], text=True)
                dynamic = subprocess.check_output(["readelf", "-d", str(p)], text=True)
                versions = subprocess.check_output(["readelf", "-W", "--version-info", str(p)], text=True)
                if not re.search(r"Machine:\s+AArch64", header):
                    errors.append("Not AArch64: " + name)
                if re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
                    errors.append("Embedded library search path: " + name)
                needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
                if any(x.startswith(("libc++", "libstdc++", "libunwind")) for x in needed):
                    errors.append("Dynamic C++ runtime: " + name)
                glibc = sorted({tuple(map(int, v.split("."))) for v in
                                re.findall(r"GLIBC_([0-9.]+)", versions)})
                if glibc and glibc[-1] > (2, 31):
                    errors.append("glibc requirement exceeds 2.31: " + name)
                binaries[name] = {"needed": needed, "max_glibc": glibc[-1] if glibc else None}
    if errors:
        raise ValueError("\n".join(errors))
    return {"archive": str(archive), "entries": len(names), "benchmark": benchmark, "binaries": binaries}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(args.archive), indent=2))
    except (ValueError, OSError, zipfile.BadZipFile, subprocess.CalledProcessError) as exc:
        print("PortMaster package validation failed:\n" + str(exc), file=sys.stderr)
        sys.exit(1)
