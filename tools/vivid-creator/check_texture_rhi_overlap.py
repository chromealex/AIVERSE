#!/usr/bin/env python3
"""Read-only integration risk inventory for VIVID and Texture RHI branches.

Examines separate changes since the shared merge-base, not the misleading
net diff between divergent heads. This does NOT attempt or certify a merge.
No checkout, fetch, push, worktree or file mutation is performed.
"""
import argparse
import json
import subprocess
from pathlib import Path

DEFAULT_VIVID = "origin/feature/vivid-digital-humans"
DEFAULT_TEXTURE = "origin/feature/texture-rhi-unification"

SHARED_ENGINE_PREFIXES = (
    "engine/render/",
    "engine/gfx/",
)
CRITICAL_EXACT = {
    "CMakeLists.txt",
    "engine/render/Model.h",
    "engine/render/Model.cpp",
    "engine/render/ModelShaders.h",
    "engine/render/GpuMaterialState.h",
    "engine/render/Renderer3D.h",
    "engine/render/Renderer3D.cpp",
    "engine/render/Renderer3DModels.cpp",
    "engine/render/graph/ShaderGraph.h",
    "engine/render/graph/ShaderGraph.cpp",
}
CRITICAL_PREFIXES = ("engine/render/Renderer3D",)


def git(repo: Path, *args: str, raw: bool = False):
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=True,
        capture_output=True,
        text=not raw,
    )
    return result.stdout if raw else result.stdout.strip()


def ref_head(repo: Path, ref: str) -> str:
    # A commit SHA is accepted by git as well as a named ref, but not a
    # path, branch to be created or an expression supplied to a shell.
    return git(repo, "rev-parse", "--verify", ref + "^{commit}")


def changed_paths(repo: Path, base: str, head: str):
    raw = git(repo, "diff", "--no-ext-diff", "--no-renames",
              "--name-only", "-z", base, head, "--", raw=True)
    return set(name.decode("utf-8", errors="replace")
               for name in raw.split(b"\0") if name)


def classification(path: str):
    if path in CRITICAL_EXACT or path.startswith(CRITICAL_PREFIXES):
        return "critical-shared-abi"
    if path.startswith(SHARED_ENGINE_PREFIXES):
        return "shared-render-or-gfx"
    if path == "CMakeLists.txt" or path.startswith("engine/character/vivid/"):
        return "feature-or-build"
    return "other"


def assess(vivid_paths, texture_paths):
    vivid, texture = set(vivid_paths), set(texture_paths)
    common = sorted(vivid & texture)
    only_vivid = sorted(vivid - texture)
    only_texture = sorted(texture - vivid)
    return {
        "changed_vivid": len(vivid),
        "changed_texture": len(texture),
        "overlap_count": len(common),
        "overlap": [
            {"path": path, "category": classification(path)}
            for path in common
        ],
        "vivid_only": only_vivid,
        "texture_only": only_texture,
        "critical_overlap": [
            path for path in common
            if classification(path) == "critical-shared-abi"
        ],
    }


def inventory(repo: Path, vivid_ref: str, texture_ref: str):
    root = Path(git(repo, "rev-parse", "--show-toplevel"))
    vivid = ref_head(root, vivid_ref)
    texture = ref_head(root, texture_ref)
    base = git(root, "merge-base", vivid, texture)
    report = assess(changed_paths(root, base, vivid),
                    changed_paths(root, base, texture))
    report.update({
        "repo": str(root),
        "vivid_ref": vivid_ref,
        "texture_ref": texture_ref,
        "vivid_sha": vivid,
        "texture_sha": texture,
        "merge_base_sha": base,
        "is_merge_proof": False,
        "tests_executed": False,
        "note": (
            "Path overlap is an integration-risk inventory, not a Git merge "
            "conflict oracle. Semantic conflicts can exist without path overlap."
        ),
    })
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--vivid", default=DEFAULT_VIVID)
    parser.add_argument("--texture", default=DEFAULT_TEXTURE)
    parser.add_argument("--output", type=Path,
                        help="Optional JSON destination; never writes to repository source")
    parser.add_argument("--fail-on-overlap", action="store_true",
                        help="Exit 1 when both refs modified at least one path")
    args = parser.parse_args()
    try:
        report = inventory(args.repo, args.vivid, args.texture)
        result = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True)
        if args.output:
            # Filesystem mutation is opt-in; no git objects are changed.
            args.output.write_text(result + "\n", encoding="utf-8")
        print(result)
        if args.fail_on_overlap and report["overlap_count"]:
            parser.exit(1, "VIVID/Texture RHI shared paths need manual review\n")
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        parser.exit(2, f"VIVID/Texture RHI audit failed: {exc}\n")


if __name__ == "__main__":
    main()
