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
    "gpu_icb_material_contract": "aiverse_gpu_icb_material_contract_test",
    "modeling_authoring": "aiverse_modeling_tests",
    "vivid_human_appearance": "aiverse_vivid_human_appearance_tests",
    "studio_model_picking": "aiverse_studio_model_picking_tests",
    "view_live_appearance_material_target": "aiverse_live_appearance_material_target_tests",
    "vulkan_scope_timing": "aiverse_vulkan_scope_timing_tests",
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
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    build = args.build_dir.resolve()
    if sys.platform == "darwin" and Path("/Volumes/KINGSTON").resolve() not in build.parents:
        parser.error("Mac builds must be on /Volumes/KINGSTON")
    if not (build / "CMakeCache.txt").is_file():
        parser.error("configure the native build first with tests, tools, Studio and GPU acceptance enabled")
    tests = dict(TESTS)
    tests["texture_graph_" + args.backend + "_gpu"] = "aiverse_texture_graph_" + args.backend + "_gpu_test"
    prefix = ["ctest", "--test-dir", str(build), "-C", args.config]
    inventory = json.loads(subprocess.check_output(prefix + ["--show-only=json-v1"], text=True))
    available = {entry["name"] for entry in inventory["tests"]}
    missing = sorted(set(tests) - available)
    if missing:
        parser.error("required CTest registrations missing: " + ", ".join(missing))
    subprocess.run(["cmake", "--build", str(build), "--config", args.config,
                    "--parallel", str(args.parallel), "--target", *tests.values()], check=True)
    pattern = "^(" + "|".join(re.escape(name) for name in tests) + ")$"
    env = dict(os.environ, AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS="1")
    # A skipped acceptance is not evidence. CTest may return zero for a skip.
    result = subprocess.run(prefix + ["-R", pattern, "--output-on-failure", "--no-tests=error"],
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end="")
    if result.returncode or re.search(r"\*\*\*Not Run|\*\*\*Skipped|\(Skipped\)|\(Disabled\)", result.stdout):
        return 1
    print("Targeted Stage 38 CTest suite completed; material_batches --unit and live host checks remain separate.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
