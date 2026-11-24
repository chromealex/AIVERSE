"""Offline SpectraFX transfer packages. No catalog/network writes."""
from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import zipfile
from pathlib import Path

MAX_BYTES = 128 * 1024 * 1024
MAX_RECIPE = 65536
FIELDS = ("format", "version", "graphSignature", "camera", "tick", "speed", "loop",
          "background", "grid", "axes", "parameters", "gpuBirthRandom", "seed",
          "gpuShaderSeed", "gpuAllocationSeed", "dependencyVersion", "dependencies")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def name_ok(name):
    require(isinstance(name, str) and re.fullmatch(r"[a-z0-9_]{1,48}", name)
            and name != "fx", "Invalid package/preset name")
    return name


def safe_path(root, relative):
    require(isinstance(relative, str) and len(relative) <= 600 and
            not any(c in relative for c in "\\:%?#") and
            not any(ord(c) < 32 for c in relative), "Unsafe package path")
    parts = relative.split("/")
    require(all(p and p not in (".", "..") and not p.endswith((".", " "))
                for p in parts), "Unsafe package path component")
    path = root
    for i, part in enumerate(parts):
        path = path / part
        require(not path.is_symlink(), "Symbolic link in package path")
        if path.exists() and i < len(parts) - 1:
            require(path.is_dir(), "Package parent is not a directory")
    return path


def read_bounded(path, limit):
    require(path.is_file() and not path.is_symlink(), "Missing file: " + path.name)
    require(path.stat().st_size <= limit, "File exceeds byte budget")
    with path.open("rb") as f:
        data = f.read(limit + 1)
    require(len(data) <= limit, "File exceeds byte budget")
    return data


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def fnv(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xffffffffffffffff
    return f"{h}:{len(data)}"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def recipe_files(recipe):
    require(isinstance(recipe, dict) and recipe.get("format") == "aiverse.spectrafx.replay"
            and recipe.get("version") == 1 and recipe.get("dependencyVersion") == 3,
            "Package requires replay manifest v3")
    require("projectPath" not in recipe and len(compact(recipe)) <= MAX_RECIPE,
            "Invalid portable recipe")
    deps = recipe.get("dependencies")
    require(isinstance(deps, list) and len(deps) <= 64, "Invalid dependencies")
    files, guids = {}, {}
    for dep in deps:
        require(isinstance(dep, dict), "Invalid dependency")
        path = dep.get("path")
        require(isinstance(path, str) and path.startswith("content/") and
                not path.endswith(".meta") and
                not path.startswith("content/spectrafx/replays/"), "Invalid asset path")
        # Reject scripts/executables; this is a data-only transfer.
        require(Path(path).suffix.lower() in {
            ".json", ".png", ".jpg", ".jpeg", ".tga", ".webp", ".hdr", ".exr",
            ".dds", ".ktx", ".ktx2", ".rtex", ".glb", ".gltf", ".obj", ".mtl",
            ".bin", ".bmp", ".wav", ".ogg"}, "Unsupported package asset type")
        require(path not in files, "Duplicate package asset")
        files[path] = dep.get("content")
        if "meta" in dep or "guid" in dep:
            guid = dep.get("guid")
            require(isinstance(guid, str) and re.fullmatch(r"[0-9a-f]{32}", guid)
                    and guid not in guids, "Invalid or duplicate dependency GUID")
            guids[guid] = path
            files[path + ".meta"] = dep.get("meta")
    require(len({p.casefold() for p in files}) == len(files), "Case-colliding paths")
    for fingerprint in files.values():
        require(isinstance(fingerprint, str) and re.fullmatch(r"[0-9]+:[0-9]+", fingerprint),
                "Invalid dependency fingerprint")
    return files, guids


def package_path(root, name):
    return safe_path(root, "packages/" + name_ok(name) + ".aivfx")


def root_dir(project):
    root = Path(project).resolve(strict=True)
    require(root.is_dir(), "Invalid project")
    return root


def export_package(project, preset, package):
    root = root_dir(project)
    source = safe_path(root, "content/spectrafx/replays/" + name_ok(preset) + ".spectra-replay.json")
    saved = json.loads(read_bounded(source, MAX_RECIPE))
    recipe = {key: saved[key] for key in FIELDS if key in saved}
    files, guids = recipe_files(recipe)
    out = package_path(root, package)
    require(not out.exists(), "Package already exists")
    out.parent.mkdir(exist_ok=True)
    portable = {"format": "aiverse.spectrafx.replay.portable", "version": 1, "recipe": recipe}
    require(len(compact(portable)) <= MAX_RECIPE, "Portable recipe too large")
    payloads, budget = {}, MAX_BYTES
    for path, fingerprint in files.items():
        data = read_bounded(safe_path(root, path), min(budget, 65536) if path.endswith(".meta") else budget)
        require(fnv(data) == fingerprint, "Source asset changed: " + path)
        if path.endswith(".meta"):
            guid = json.loads(data).get("guid")
            require(guids.get(guid) == path[:-5], "Source GUID mismatch")
        payloads[path] = data
        budget -= len(data)
    manifest = {"format": "aiverse.spectrafx.package", "version": 1,
                "portable": portable, "sha256": {p: sha(b) for p, b in payloads.items()}}
    # exclusive publication; on failure remove only the file created here
    with out.open("xb") as stream:
        try:
            with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr("manifest.json", compact(manifest))
                for path, data in sorted(payloads.items()):
                    z.writestr(path, data)
        except BaseException:
            stream.close()
            out.unlink(missing_ok=True)
            raise
    return {"path": "packages/" + out.name, "files": len(files), "bytes": out.stat().st_size}


def load_package(root, name):
    path = package_path(root, name)
    require(path.is_file() and path.stat().st_size <= MAX_BYTES + 2 * 1024 * 1024,
            "Package missing or oversized")
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        require(len(entries) <= 129, "Too many package entries")
        by_name = {e.filename: e for e in entries}
        require(len(by_name) == len(entries) and "manifest.json" in by_name, "Duplicate or missing entry")
        require(sum(e.file_size for e in entries) <= MAX_BYTES + 256 * 1024, "Package expansion exceeds budget")
        for e in entries:
            require(not e.is_dir() and not stat.S_ISLNK(e.external_attr >> 16)
                    and not e.flag_bits & 1, "Invalid ZIP entry")
        require(by_name["manifest.json"].file_size <= 256 * 1024, "Manifest too large")
        manifest = json.loads(z.read("manifest.json"))
        require(manifest.get("format") == "aiverse.spectrafx.package" and manifest.get("version") == 1,
                "Unsupported package format")
        portable = manifest["portable"]
        require(portable.get("format") == "aiverse.spectrafx.replay.portable"
                and portable.get("version") == 1 and len(compact(portable)) <= MAX_RECIPE, "Invalid portable recipe")
        files, guids = recipe_files(portable["recipe"])
        require(set(by_name) == {"manifest.json", *files} and set(manifest["sha256"]) == set(files),
                "Unexpected or missing payload entry")
        payloads = {}
        for p, fp in files.items():
            safe_path(root, p)
            require(not p.endswith(".meta") or by_name[p].file_size <= 65536, "Oversized metadata")
            data = z.read(p)
            require(sha(data) == manifest["sha256"][p] and fnv(data) == fp, "Payload checksum mismatch")
            if p.endswith(".meta"):
                require(guids.get(json.loads(data).get("guid")) == p[:-5], "Payload GUID mismatch")
            payloads[p] = data
        return portable, payloads, guids


def guid_locations(root, wanted):
    found, count, budget = {}, 0, 32 * 1024 * 1024
    content = safe_path(root, "content")
    if not content.exists() or not wanted:
        return found
    for folder, dirs, files in os.walk(content, followlinks=False):
        dirs[:] = [d for d in dirs if not (Path(folder) / d).is_symlink()]
        count += len(dirs) + len(files)
        require(count <= 20000, "GUID scan entry limit exceeded")
        for filename in files:
            meta = Path(folder) / filename
            if not filename.endswith(".meta") or meta.is_symlink():
                continue
            raw = read_bounded(meta, min(65536, budget))
            budget -= len(raw)
            guid = json.loads(raw).get("guid")
            if guid in wanted:
                found.setdefault(guid, []).append(meta.relative_to(root).as_posix()[:-5])
    return found


def plan_package(project, package):
    root = root_dir(project)
    portable, payloads, guids = load_package(root, package)
    found = guid_locations(root, guids)
    conflicts = {p for g, p in guids.items() if any(q != p for q in found.get(g, []))}
    rows = []
    for p, data in sorted(payloads.items()):
        dest = safe_path(root, p)
        if p in conflicts or p.endswith(".meta") and p[:-5] in conflicts:
            state = "guid_conflict"
        elif dest.exists():
            state = "reuse" if dest.is_file() and dest.stat().st_size == len(data) and read_bounded(dest, len(data)) == data else "path_conflict"
        else:
            # An existing, different sidecar must never be adopted silently.
            meta = safe_path(root, p + ".meta") if not p.endswith(".meta") else None
            state = "path_conflict" if meta is not None and meta.exists() and p + ".meta" not in payloads else "add"
        rows.append({"path": p, "state": state, "sha256": sha(data)})
    state = "ready" if all(r["state"] in ("add", "reuse") for r in rows) else "conflict"
    revision = sha(compact({"project": str(root), "portable": portable, "rows": rows}))
    return {"state": state, "planRevision": revision, "files": rows, "portable": portable}, payloads


def apply_package(project, package, revision):
    root = root_dir(project)
    plan, payloads = plan_package(root, package)
    require(plan["state"] == "ready" and revision == plan["planRevision"], "Stale or conflicting package plan")
    created = []
    try:
        for row in plan["files"]:
            p = row["path"]
            dest = safe_path(root, p)
            if row["state"] == "reuse":
                require(read_bounded(dest, len(payloads[p])) == payloads[p], "Reused asset changed")
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest = safe_path(root, p)
            with dest.open("xb") as f:
                created.append(dest)
                f.write(payloads[p])
        return {"installed": len(created), "portable": plan["portable"]}
    except BaseException:
        for p in reversed(created):
            p.unlink(missing_ok=True)
        raise
