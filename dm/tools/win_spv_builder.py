#!/usr/bin/env python3
"""
Translates Maxwell shader packages to standard Vulkan SPIR-V (.spv) modules
for native execution on Windows ARM64 Qualcomm Adreno Vulkan.
"""

import os
import sys
import re
import json
import struct
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
XLATE_DLL = os.path.join(REPO_ROOT, "dm", "gfx", "tools", "dmsh_xlate_bin", "dmsh_xlate.dll")

def translate_shaders(profile_dir, pkg_dir, out_spv_dir, target_keys=None):
    os.makedirs(out_spv_dir, exist_ok=True)
    map_path = os.path.join(profile_dir, "shader_map.tsv")
    facts_path = os.path.join(profile_dir, "shader_facts.tsv")

    # Load facts
    facts = {}
    if os.path.exists(facts_path):
        for line in open(facts_path, "r", encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) >= 2:
                try:
                    facts[parts[0]] = json.loads(parts[1])
                except Exception:
                    pass

    # Index shader map
    map_index = {}
    for line in open(map_path, "r", encoding="utf-8"):
        if line.startswith("#"):
            continue
        parts = line.strip().split("\t")
        if len(parts) >= 4:
            if parts[1] == "index":
                continue
            pkg, idx, stg, key = parts[0], int(parts[1]), parts[2], parts[3]
            map_index[key] = (pkg, idx, stg)

    if target_keys is None:
        target_keys = set(map_index.keys())
    else:
        target_keys = set(target_keys)

    print(f"[spv-builder] Translating {len(target_keys)} shaders to {out_spv_dir}...")

    # Cache opened packages
    packages = {}
    jobs = []
    
    for key in sorted(target_keys):
        spv_target = os.path.join(out_spv_dir, f"{key}.spv")
        c1_target = os.path.join(out_spv_dir, f"{key}.c1")
        if os.path.exists(spv_target) and os.path.getsize(spv_target) > 0 and os.path.exists(c1_target):
            continue
        if key not in map_index:
            continue
        pkg_name, idx, stg = map_index[key]
        if pkg_name not in packages:
            pkg_file = os.path.join(pkg_dir, pkg_name)
            if not os.path.exists(pkg_file):
                continue
            data = open(pkg_file, "rb").read()
            recs = [m.start() for m in re.finditer(b"BTCDBTCD", data)]
            packages[pkg_name] = (data, recs)

        data, recs = packages[pkg_name]
        if idx >= len(recs):
            continue
        start = recs[idx]
        end = recs[idx + 1] if idx + 1 < len(recs) else len(data)
        rec = data[start:end]

        ci = rec.find(bytes.fromhex("34127698"))
        if ci < 0:
            continue
        code = rec[0x38:ci]
        c1o, c1t = struct.unpack_from("<2I", rec, ci + 0x700)
        c1 = rec[0x38 + c1o : 0x38 + c1t]

        if "compute" not in stg:
            code = code[0x30:]
        else:
            if code[:4] == bytes.fromhex("79563412"):
                code = code[0x100:]

        code_path = os.path.join(out_spv_dir, f"{key}.bin")
        with open(code_path, "wb") as f:
            f.write(code)

        if c1:
            c1_path = os.path.join(out_spv_dir, f"{key}.c1")
            with open(c1_path, "wb") as f:
                f.write(c1)

        stage_name = "vs" if "vertex" in stg else ("ps" if "pixel" in stg else "cs")
        job = {
            "key": key,
            "stage": stage_name,
            "code": code_path,
            "tex": {},
            "img": {},
            "attr": {},
            "fout": {},
            "c1": c1.hex(),
            "debug": True,
            "reduced": True,
        }
        if key in facts:
            job.update(facts[key])
        jobs.append(job)

    if not jobs:
        print("[spv-builder] All target shaders already up to date.")
        return 0

    print(f"[spv-builder] Submitting batch of {len(jobs)} shaders to dmsh_xlate.dll...")
    batch_json = os.path.join(out_spv_dir, "batch_job.json")
    with open(batch_json, "w", encoding="utf-8") as f:
        json.dump(jobs, f)

    cmd = ["dotnet", XLATE_DLL, batch_json, out_spv_dir]
    res = subprocess.run(cmd, capture_output=True, text=True)
    print(res.stdout.strip())
    if res.stderr.strip():
        for line in res.stderr.strip().splitlines()[:10]:
            print("  ", line)

    success_cnt = sum(
        1 for j in jobs if os.path.exists(os.path.join(out_spv_dir, f"{j['key']}.spv"))
    )
    print(f"[spv-builder] Successfully built {success_cnt}/{len(jobs)} SPIR-V modules.")
    return 0

if __name__ == "__main__":
    profile_dir = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\nando\Downloads\0100A3D008C5C000_build\profile\0100A3D008C5C000"
    pkg_dir = sys.argv[2] if len(sys.argv) > 2 else r"C:\Users\nando\Downloads\0100A3D008C5C000_build\shader\pkg"
    out_spv_dir = sys.argv[3] if len(sys.argv) > 3 else r"C:\Users\nando\Downloads\0100A3D008C5C000_build\profile\0100A3D008C5C000\spirv"
    
    keys = sys.argv[4:] if len(sys.argv) > 4 else None
    sys.exit(translate_shaders(profile_dir, pkg_dir, out_spv_dir, keys))
