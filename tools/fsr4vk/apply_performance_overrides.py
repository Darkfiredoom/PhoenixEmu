#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
"""Experiment: build fsr4-vk with the exact INT8 shader rewrites of
github.com/zgauthier2000/fsr4-shader-performance-overrides.

Those rewrites replace shaders of AMD's DX12 FSR 4.1.1 DLL (4.1.1.2740), the same DLL fsr4-vk
extracts and translates to SPIR-V. Run after fsr4-vk's own `tools/setup.py` has produced verified
stock assets:

    python3 apply_performance_overrides.py --fsr4vk fsr4-vk \
        --overrides fsr4-shader-performance-overrides/windows/prebuilt/fsr4-overrides

1. Copies the extraction and puts each replacement in place of the AMD shader whose DXBC header
   hash names it.
2. Translates both extractions with fsr4-vk's own conversion scripts and keeps the stock SPIR-V
   that changed.
3. Points the locked catalog's entries for those shaders at the new bytes and repacks the assets
   with fsr4-vk's packer, so its patched variants are re-encoded against the new stock shaders.

fsr4-vk's own patched variants (Windows dot-accumulate passes, exact ultra performance permutations)
stay derived from AMD's shaders. FP8 (RX 9000) shaders are untouched: the rewrites are INT8 only.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def swap_shaders(stock: Path, out: Path, overrides: Path) -> Counter:
    replacements = {path.stem.lower(): path for path in overrides.glob("*.dxil")}
    if not replacements:
        sys.exit(f"no *.dxil replacements in {overrides}")
    shutil.copytree(stock, out)
    manifest_path = out / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    swapped = Counter()
    used = set()
    for path in sorted(out.rglob("*.dxbc")):
        data = path.read_bytes()
        if data[:4] != b"DXBC":
            continue
        key = data[4:20].hex()
        replacement = replacements.get(key)
        if replacement is None:
            continue
        new = replacement.read_bytes()
        if new[:4] != b"DXBC":
            sys.exit(f"{replacement} is not a DXBC container")
        path.write_bytes(new)
        name = path.relative_to(out).as_posix()
        manifest["files"][name] = sha256(new)
        swapped[path.relative_to(out).parts[0]] += 1
        used.add(key)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Replaced {sum(swapped.values())} extracted shaders ({dict(swapped)}) with "
          f"{len(used)} of {len(replacements)} distinct replacements")
    return swapped


def convert(root: Path, extraction: Path, raw: Path):
    sys.path.insert(0, str(root / "tools/convert"))
    from toolchain import find_library, find_tool  # fsr4-vk's own tool lookup

    translator = find_tool("dxil-spirv", None, "DXIL_SPIRV")
    library = find_library(None, translator)
    here = root / "tools/convert"
    run = lambda *args: subprocess.run([sys.executable, *map(str, args)], check=True)
    run(here / "fp8.py", extraction / "fp8", raw / "fp8", "--translator", translator,
        "--library", library)
    run(here / "int8.py", extraction / "int8", raw / "int8", "--dxil-spirv", translator,
        "--auto-exposure", raw / "fp8/auto-exposure.spv")
    run(here / "endpoints.py", extraction / "endpoints", raw / "endpoints", translator)


def materialize(root: Path, assets: Path, out: Path):
    """Every payload of the current assets as plain bytes, for the packer to find by hash."""
    sys.path.insert(0, str(root / "tools/assets"))
    import storage

    manifest = json.loads((assets / "manifest.json").read_text())
    rows = manifest["payloads"]
    cache = {}

    def payload(index):
        if index not in cache:
            row = rows[index]
            data = (assets / row["file"]).read_bytes()
            if row["kind"] == "delta":
                data = storage.reconstruct(payload(row["base"]), data)
            if sha256(data) != row["sha256"]:
                raise RuntimeError("payload changed: " + row["file"])
            cache[index] = data
        return cache[index]

    out.mkdir(parents=True)
    for row in rows:
        suffix = Path(row["file"]).suffix
        if suffix == ".delta":
            suffix = ".spv"
        (out / f"{row['id']:05}{suffix}").write_bytes(payload(row["id"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fsr4vk", type=Path, required=True)
    parser.add_argument("--overrides", type=Path, required=True)
    parser.add_argument("--report", type=Path, help="write a JSON report of what changed")
    args = parser.parse_args()

    root = args.fsr4vk.resolve()
    assets = root / "assets"
    work = root / "build/performance-overrides"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    swapped = swap_shaders(assets / "extracted", work / "extracted", args.overrides.resolve())
    convert(root, assets / "extracted", work / "raw-stock")
    convert(root, work / "extracted", work / "raw-overrides")

    changed = {}  # stock SPIR-V hash -> rewritten bytes
    changed_files = []
    for path in sorted((work / "raw-overrides").rglob("*.spv")):
        stock_path = work / "raw-stock" / path.relative_to(work / "raw-overrides")
        new, old = path.read_bytes(), stock_path.read_bytes()
        if new == old:
            continue
        old_hash = sha256(old)
        if changed.get(old_hash, new) != new:
            sys.exit(f"one stock shader became two different shaders: {path}")
        changed[old_hash] = new
        changed_files.append(path.relative_to(work / "raw-overrides").as_posix())
    print(f"{len(changed_files)} translated files changed ({len(changed)} distinct shaders)")
    if not changed:
        sys.exit("nothing changed: the replacements did not match fsr4-vk's extraction")

    reference = work / "reference"
    shutil.copytree(root / "tools/assets/reference", reference)
    catalog = json.loads((reference / "manifest.json").read_text())
    retargeted = []
    for row in catalog["payloads"]:
        new = changed.get(row["sha256"])
        if new is not None:
            row["sha256"], row["size"] = sha256(new), len(new)
            retargeted.append(row["file"])
    (reference / "manifest.json").write_text(json.dumps(catalog, indent=2) + "\n")
    print(f"{len(retargeted)} catalog payloads now point at rewritten shaders")
    if not retargeted:
        sys.exit("no catalog payload matched a changed shader")

    generated = work / "generated"
    generated.mkdir()
    for index, new in enumerate(changed.values()):
        (generated / f"rewritten-{index:04}.spv").write_bytes(new)
    materialize(root, assets, work / "current")

    pack = work / "pack"
    manage = root / "tools/assets/manage.py"
    subprocess.run([sys.executable, str(manage), "pack", str(reference), str(pack),
                    "--generated", str(generated), "--generated", str(work / "current"),
                    "--omit-legacy-unused"], check=True)
    subprocess.run([sys.executable, str(manage), "verify", str(pack)], check=True)

    # The extraction stays: the build reads only the packed shaders and models.
    shutil.move(str(assets / "extracted"), str(pack / "extracted"))
    shutil.move(str(assets), str(work / "stock-assets"))
    shutil.move(str(pack), str(assets))
    print("Assets now carry the rewritten shaders:", assets)

    if args.report:
        args.report.write_text(json.dumps({
            "swapped_extracted_shaders": dict(swapped),
            "changed_translated_files": changed_files,
            "retargeted_payloads": retargeted,
        }, indent=2) + "\n")


if __name__ == "__main__":
    main()
