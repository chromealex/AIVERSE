#!/usr/bin/env python3
"""Build/run the accumulated Stage 38 regression set in a configured native build.
Not a substitute for the merged-tree Mac quick gate or live Studio/MCP checks.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

TESTS = {
    "view_appearance_authoring": "aiverse_appearance_authoring_tests",
    "render_model_image_transaction": "aiverse_model_image_transaction_tests",
    "camera_model_snapshot": "aiverse_camera_model_snapshot_tests",
    "camera_client_adapter": "aiverse_camera_client_adapter_tests",
    "gpu_icb_material_contract": "aiverse_gpu_icb_material_contract_test",
    "modeling_authoring": "aiverse_modeling_tests",
    "vivid_human_appearance": "aiverse_vivid_human_appearance_tests",
    "studio_model_picking": "aiverse_studio_model_picking_tests",
    "view_live_appearance_material_target": "aiverse_live_appearance_material_target_tests",
    "vulkan_scope_timing": "aiverse_vulkan_scope_timing_tests",
    "aiverse_deferred_loading_tests": "aiverse_deferred_loading_tests",
    "aiverse_hosted_view_tests": "aiverse_hosted_view_tests",
    "render_geometry_page_owners": "aiverse_geometry_page_owners_tests",
    "render_device_epoch": "aiverse_device_epoch_tests",
    "render_resource_owner_group": "aiverse_render_resource_owner_group_tests",
}
for name in (
    "pipeline_cache", "shared_scratch", "liveness", "pass_schedule",
    "dag_lowering", "output_reuse", "image_pins", "group_profile",
    "request_queue", "kernel_source", "materialized_dispatch",
    "gpu_resource_cache", "fusion", "gpu_texture_transfer", "gpu_texture_budget",
):
    TESTS["texture_graph_" + name] = "aiverse_texture_graph_" + name + "_tests"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--backend", required=True, choices=("metal", "vulkan"))
    parser.add_argument("--config", default="Release")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--full", action="store_true",
                        help="include HDR, native Studio contracts and both GPU frame modes; requires Studio enabled")
    parser.add_argument("--material-batches", action="store_true",
                        help="include the desktop CPU material batch regression (32 tests)")
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    build = args.build_dir.resolve()
    if sys.platform == "darwin" and Path("/Volumes/KINGSTON").resolve() not in build.parents:
        parser.error("Mac builds must be on /Volumes/KINGSTON")
    if not (build / "CMakeCache.txt").is_file():
        parser.error("configure the native build first with tests, tools, Studio and GPU acceptance enabled")
    source = Path(__file__).resolve().parent.parent
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    home = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$", cache, re.MULTILINE)
    if not home or Path(home.group(1)).resolve() != source:
        parser.error("CMake cache belongs to a different source tree; use its own build directory")
    sha = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True)
    print(f"Stage 38 source SHA: {sha}; tracked tree: {'DIRTY' if dirty else 'clean'}", flush=True)
    if args.full and dirty:
        parser.error("full acceptance requires a clean tracked checkout")
    print(f"Build: {build}; config: {args.config}; backend: {args.backend}", flush=True)
    tests = dict(TESTS)
    if args.material_batches or args.full:
        tests["material_batches_unit"] = "aiverse_material_batches"
    if args.full:
        tests.update({
            "hdr_texture": "aiverse_hdr_texture_test",
            "studio_session": "aiverse_studio_session_tests",
            "studio_pbr_adapter": "aiverse_studio_pbr_adapter_tests",
            "studio_material_validation": "aiverse_studio_material_validation_tests",
            "studio_shader_graph_draft": "aiverse_studio_shader_graph_draft_tests",
            "texture_graph_" + args.backend + "_offscreen": "aiverse_texture_graph_" + args.backend + "_gpu_test",
        })
    tests["texture_graph_" + args.backend + "_gpu"] = "aiverse_texture_graph_" + args.backend + "_gpu_test"
    prefix = ["ctest", "--test-dir", str(build), "-C", args.config]
    inventory = json.loads(subprocess.check_output(prefix + ["--show-only=json-v1"], text=True))
    available = {entry["name"] for entry in inventory["tests"]}
    missing = sorted(set(tests) - available)
    if missing:
        parser.error("required CTest registrations missing: " + ", ".join(missing))
    subprocess.run(["cmake", "--build", str(build), "--config", args.config,
                    "--parallel", str(args.parallel), "--target", *dict.fromkeys(tests.values())], check=True)
    pattern = "^(" + "|".join(re.escape(name) for name in tests) + ")$"
    env = dict(os.environ, AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS="1", LC_ALL="C")
    # A skipped acceptance is not evidence. CTest may return zero for a skip.
    result = subprocess.run(prefix + ["-R", pattern, "--verbose", "--output-on-failure", "--no-tests=error"],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end="")
    if result.returncode or re.search(r"\*\*\*Not Run|\*\*\*Skipped|\(Skipped\)|\(Disabled\)", result.stdout):
        return 1
    summary = re.search(r"100% tests passed, 0 tests failed out of (\d+)", result.stdout)
    if not summary or int(summary.group(1)) != len(tests):
        print(f"ERROR: expected exactly {len(tests)} passing tests", file=sys.stderr)
        return 1
    after = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    after_dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True)
    if after != sha or after_dirty != dirty:
        print("ERROR: source HEAD or tracked status changed during acceptance", file=sys.stderr)
        return 1
    print(f"Targeted Stage 38 CTest suite completed: {len(tests)}/{len(tests)}; SHA={sha}; tracked tree={'DIRTY' if dirty else 'clean'}")
    if not args.material_batches and not args.full:
        print("Material batch CPU regression was not selected; use --material-batches.")
    print("Live Studio/MCP and merged-tree Mac gate remain separate.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
