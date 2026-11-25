#!/usr/bin/env python3
"""Create a real AIVERSE Studio project copy with all 10 World Processes.

Uses an existing playable GameCore project as its base, preserving actual
game.json/rules/assets and valid map terrain. Does not invent game rules
or an unknown entity type. Never overwrites source/target data.
Usage:
  python3 examples/world/world_processes_native/create_showcase.py \
    --source-project /path/to/ExistingAiverseGame \
    --destination /path/to/AiverseWorldProcessesShowcase \
    --carrier-type VALID_NON_COLLIDER_TYPE \
    [--source-map maps/duel.json] [--owner 0]

The carrier type must be a real unit/decoration entity type from the existing
rules (not a static collider), so GameCore::mapPlaced retains live ECS Entity
handles for all authored World Processes. Multiple "carrier" entities are
intentional; this is a functional integration map, not finished art assets.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import shutil
from pathlib import Path


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as source:
        data = json.load(source)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as target:
        json.dump(data, target, ensure_ascii=False, indent=2)
        target.write("\n")


def relative_map(manifest: dict, requested: str | None) -> str:
    if requested:
        return requested
    maps = manifest.get("maps", [])
    for item in maps:
        if isinstance(item, dict) and isinstance(item.get("file"), str):
            return item["file"]
        if isinstance(item, str):
            return item
    raise ValueError("game.json has no map. Supply --source-map.")


def safe_child(root: Path, relative: str) -> Path:
    candidate = (root / relative).resolve()
    if root.resolve() not in candidate.parents:
        raise ValueError(f"Path must stay inside game project: {relative}")
    return candidate


def make_showcase(
    source: Path,
    destination: Path,
    relative_source_map: str | None,
    carrier_type: str,
    owner: int,
    fragment_file: Path,
) -> tuple[Path, int]:
    source = source.resolve()
    destination = destination.resolve()
    if not source.is_dir():
        raise ValueError(f"Source game project not found: {source}")
    if destination.exists():
        raise FileExistsError(f"Destination already exists (no overwrites): {destination}")
    if source in destination.parents or destination in source.parents:
        raise ValueError("Source and destination must not contain one another")
    if not carrier_type or len(carrier_type) > 128:
        raise ValueError("Explicit valid non-collider carrier type is required")
    if owner < 0 or owner > 16:
        raise ValueError("Owner must be 0..16 (or a valid player from the source map)")

    game = read_json(source / "game.json")
    if game.get("module", "game") == "scene":
        raise ValueError("Use a GameCore-native playable game, not module=scene")
    source_map = relative_map(game, relative_source_map)
    map_doc = read_json(safe_child(source, source_map))
    if not isinstance(map_doc.get("entities"), list):
        raise ValueError("Source map must include an entities array")
    source_ids = {entity.get("id") for entity in map_doc["entities"]
                  if isinstance(entity, dict)}
    fragments = read_json(fragment_file)
    authored = fragments.get("entities")
    if not isinstance(authored, list) or not authored:
        raise ValueError("Showcase source must contain nonempty entities")
    ids = [item.get("id") for item in authored if isinstance(item, dict)]
    if len(ids) != len(authored) or any(not isinstance(i, str) or not i for i in ids):
        raise ValueError("All showcase entities need stable IDs")
    if len(ids) != len(set(ids)) or set(ids) & source_ids:
        raise ValueError("Showcase IDs duplicate each other or existing map objects")

    size = map_doc.get("size", [64, 48])
    if not isinstance(size, list) or len(size) < 2:
        raise ValueError("Playable source map needs a valid size")
    w, h = int(size[0]), int(size[1])
    if w < 20 or h < 20:
        raise ValueError("Showcase requires a source map at least 20x20 cells")
    columns = min(8, max(2, (w - 4) // 4))
    rows = math.ceil(len(authored) / columns)
    if rows * 4 + 4 >= h:
        raise ValueError("Map is too small for safe spaced-out demo objects")
    if owner > len(map_doc.get("players", [])):
        raise ValueError("Owner exceeds player count of source map")

    # A visual carrier is an ordinary real rules-defined spawned entity.
    # Existing, valid gameplay rule/asset ownership is preserved unchanged.
    entities = copy.deepcopy(map_doc["entities"])
    for i, fragment in enumerate(authored):
        x = 2 + (i % columns) * 4
        z = 2 + (i // columns) * 4
        extra = copy.deepcopy(fragment.get("components", {}))
        if not isinstance(extra, dict) or not extra:
            raise ValueError(f"Showcase entity {fragment['id']} lacks components")
        entity = {
            "id": fragment["id"],
            "type": carrier_type,
            "owner": owner,
            "pos": [x, z],
            "components": extra,
        }
        if "GranularField" in extra:
            entity["worldProcessGizmo"] = {"cellSizeMetres": 1}
        if "EvolveRegion" in extra:
            entity["worldProcessGizmo"] = {"radiusMetres": 8}
        if "CrowdAgent" in extra:
            # Physical world coordinates are metres; CROWD stores centimetres.
            extra["CrowdAgent"]["x"] = x * 100
            extra["CrowdAgent"]["z"] = z * 100
            extra["CrowdAgent"]["goalX"] = x * 100 + 200
            extra["CrowdAgent"]["goalZ"] = z * 100
        if "CrowdSlot" in extra:
            extra["CrowdSlot"]["x"] = x * 100
            extra["CrowdSlot"]["z"] = z * 100
        entities.append(entity)

    map_doc["entities"] = entities
    out_relative = "maps/world_processes_showcase.json"
    # Check all destination manifest/rules schemas BEFORE copying anything.
    if not isinstance(game.get("maps", []), list):
        raise ValueError("Source game maps index must be an array")
    rules_relative = game.get("rules", "rules.json")
    if not isinstance(rules_relative, str):
        raise ValueError("Source rules reference must be a string path")
    source_rules = read_json(safe_child(source, rules_relative))
    configured = source_rules.get("nativeModules", [])
    if not isinstance(configured, list):
        raise ValueError("Source nativeModules must be an array")
    if any(isinstance(m, dict) and m.get("id") == "world_processes"
           for m in configured):
        raise ValueError("Source already activates world_processes; configure explicitly")

    # Only now create the destination; all known inputs were validated first.
    shutil.copytree(source, destination)
    out_map_path = safe_child(destination, out_relative)
    write_json(out_map_path, map_doc)
    maps = game.setdefault("maps", [])
    if not isinstance(maps, list):
        raise ValueError("Existing game maps index is not an array")
    maps.append({"file": out_relative, "title": "World Processes — all ten"})
    write_json(destination / "game.json", game)

    rules_relative = game.get("rules", "rules.json")
    if not isinstance(rules_relative, str):
        raise ValueError("Existing game rules reference is not a path")
    rules_path = safe_child(destination, rules_relative)
    rules = read_json(rules_path)
    modules = rules.setdefault("nativeModules", [])
    if not isinstance(modules, list):
        raise ValueError("Rules nativeModules must be an array")
    if any(isinstance(m, dict) and m.get("id") == "world_processes"
           for m in modules):
        raise ValueError("Source already activates world_processes; configure explicitly")
    modules.append({"id": "world_processes", "config": {
        "morph": True, "crowd": True, "imprint": True,
        "circuit": True, "granular": True, "evolve": True,
        "society": True
    }})
    write_json(rules_path, rules)

    readme = (
        "# AIVERSE World Processes — generated project\n\n"
        "This is an ordinary playable project copy, not a mocked Scene. The\n"
        "authoring map is maps/world_processes_showcase.json. Open this\n"
        "project in HTML5 Studio and select that map.\n\n"
        "The native module named world_processes MUST be compiled and\n"
        "linked by your game's existing native module target. Copy the\n"
        "example from examples/native/world_processes/ into the project\n"
        "native/ folder and merge its CMake lines with existing native\n"
        "modules. This generator deliberately does not overwrite a\n"
        "project's existing native code.\n\n"
        "All placed showcase objects use the explicitly supplied real\n"
        f"carrier type {carrier_type!r}. This type must be a non-collider\n"
        "that GameCore can spawn and keep alive; if it has a gameplay\n"
        "controller, it may move or die normally. The project owner must\n"
        "choose an appropriate inert/neutral type from its own rules.\n\n"
        "Graphics/physics adapters for MORPH and GRANULAR and native\n"
        "device/NPC host callbacks are OPTIONAL and require the concrete\n"
        "game to wire its real Box3D, Renderer3D, NavMesh, TerraWeave,\n"
        "PHYSIS and NPC systems. This generated map alone does not prove\n"
        "the integrated scene renders or runs correctly.\n"
    )
    (destination / "WORLD_PROCESSES_SHOWCASE.md").write_text(readme, encoding="utf-8")
    return out_map_path, len(authored)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-project", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--source-map", help="Relative path under the source game project")
    parser.add_argument("--carrier-type", required=True,
                        help="Rules-defined spawned non-collider entity type")
    parser.add_argument("--owner", type=int, default=0)
    parser.add_argument("--fragment-file", type=Path, default=(
        Path(__file__).resolve().parents[2] / "native/world_processes/authoring-examples.json"))
    args = parser.parse_args()
    path, count = make_showcase(args.source_project, args.destination,
                               args.source_map, args.carrier_type,
                               args.owner, args.fragment_file)
    print(f"Created generated showcase map: {path} ({count} entities).")
    print("Build/link the native world_processes project module before gameplay.")


if __name__ == "__main__":
    main()
