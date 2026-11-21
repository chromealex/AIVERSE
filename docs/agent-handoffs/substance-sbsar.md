# Substance SBSAR handoff

## 2026-11-21 — stage 1: standalone offline bake tool

Branch: `feature/studio-html5`. Implementation: `fc88afd2c`.
Starting HEAD: `1f3d53fcb0b6e7470fdf5d58e0afb62cabe3ba5a`.
Paths: `tools/substance/bake.py`, `tools/substance/README.md`.

Added an external SAT subprocess wrapper with numeric exposed parameters,
512–4096 output size, requested engine RAM/CPU budgets, timeout, SHA256-keyed
cache and staging publication. Cache outputs carry integrity hashes.
No runtime API, save/ECS format or renderer changes. Existing unrelated dirty
material/editor files were preserved. No Studio controls or PBR mapping yet.

Executed: `python3 tools/substance/bake.py --help` — PASS, CLI displays.
Executed: `python3 tools/substance/bake.py AGENTS.md --cache /tmp/aiverse-substance-check`
— expected exit 1 with explicit missing-sbsrender diagnostic, before rendering.
Real SBSAR bake/cache reuse/failure recovery/GPU/performance: NOT RUN because
Adobe SAT and SBSAR fixtures are unavailable. CI: not triggered, no CI URL.
First ordinary commit attempt failed due sandbox `.git/index.lock`; retry with
authorized escalation succeeded. This is not a full material import stage.

Risks: Adobe CLI/version compatibility unverified; memory budget is not an OS
limit; concurrent writers to the same key can fail publication safely; PNG
outputs are not yet checked for decodability or mapped by semantic usage.
Next: obtain installed SAT plus representative licensed SBSAR fixtures, verify
real CLI and cache behavior, then implement explicit semantic mapping,
normal/color-space handling, ORM packing and existing asset-pipeline compression.
Only after that wire asynchronous Studio import and preview using shared UI.

## 2026-11-21 — stage 2: explicit PBR bundle packing

Branch `feature/studio-html5`; code `796db270c`, starting HEAD `19b27693b`.
Paths: tools/substance/{pack.py,test_pack.py,requirements.txt,README.md}.
Added explicit output semantic mapping, grayscale ORM packing R=AO/G=roughness/
B=metallic, default channels 1/1/0, optional DirectX normal green flip, color-space
metadata and bundle-relative Studio material field object. Same-size 8-bit PNG
inputs only, <=4096; staging avoids publication of partial bundles. No guessing
authored output names, library writes, editor UI, GPU compression or ECS changes.
Existing unrelated dirty material/renderer work preserved.

Verification executed:
- Initial system Python Pillow import FAILED (dependency absent).
- Initial temporary venv pip install FAILED due sandbox DNS; escalated install
  succeeded, Pillow 12.3.0 installed in /tmp/aiverse-substance-venv only.
- `PYTHONDONTWRITEBYTECODE=1 /tmp/aiverse-substance-venv/bin/python tools/substance/test_pack.py`
  PASS, 2 tests: exact channel/default/normal pixels and mismatched-size rejection
  with no published output.
Real SAT/SBSAR generation, native loading, GPU compression/performance, Studio
preview: NOT RUN; SAT/fixtures missing and editor not integrated. CI not run, no URL.

Compatibility: generated material.json is a field object, not the complete
materials.json library; paths require rebasing on project import. bundle.json
schema 1 records intended color spaces; no existing loader consumes this metadata.
Risks: only scalar numeric bake params and 8-bit maps; no opacity inference,
16-bit height/displacement or automatic normal-convention detection.
Next actionable step: validate with real SAT plus a licensed SBSAR fixture,
then connect bundle path rebasing/publication and existing texture compression
through native asset authoring before adding shared-UI background preview.

## 2026-11-21 — stage 3: offline project publication

Branch `feature/studio-html5`; starting HEAD `7ace9b33a75f2e329a24543d72a3941f3b363627`;
implementation `40797a290`. Paths: tools/substance/import_bundle.py,
test_import_bundle.py, README.md. Unrelated dirty work preserved.

Added validated bundle-local PNG import to content-addressed assets/substance,
project-relative texture references and atomic materials.json replacement.
Existing names are refused; unrelated entries/top-level metadata retained.
Uses existing Studio material fields and library shape, no format migration,
runtime API, ECS, renderer or browser-side writes. Explicit offline CLI only.
Color-space metadata is checked but not installed as separate loader metadata;
existing native field semantics must still be verified visually.

Executed `PYTHONDONTWRITEBYTECODE=1 /tmp/aiverse-substance-venv/bin/python -m unittest discover -s tools/substance -p 'test_*.py'`:
PASS, 4 tests, including exact packed pixels, invalid dimensions, library
preservation/duplicate-name refusal and traversal refusal with no project changes.
Actual SAT bake, native Studio reload/preview, GPU compression/performance and
CI: NOT RUN. No CI URL. Adobe SAT and licensed fixture still missing.

Risks: Studio must be closed; exclusive CLI lock is not shared by native Studio.
Final content comparison detects changes but cannot provide a full transaction
with an uncooperating writer. Crash may leave stale lock or unused immutable
texture folder. No overwrite/reimport UI or asset garbage collection yet.
Next: real fixture end-to-end bake/pack/import plus Studio visual acceptance,
then existing build texture compression and native background import integration.

## 2026-11-21 — stage 4: existing ASTC packaging integration

Branch feature/studio-html5, starting HEAD 8dac0a25645bef3f543807d5d503c6f4e7697efc;
code bab62eb53. Paths tools/substance/{packaging.py,test_packaging.py,README.md}.
Inspected existing FileRules/TextureCompress contracts; reused their target-specific
ASTC4x4/KTX2 path through emitted game.json fragments, no duplicate encoder.
Explicit path rules set sRGB colors/emission, linear normals/ORM, normal flag,
full mips and alpha preservation. Shared ORM rule deduplicated. No automatic
game.json writes, native API/save/ECS changes, or Studio UI integration.

Executed PYTHONDONTWRITEBYTECODE=1 /tmp/aiverse-substance-venv/bin/python -m unittest discover -s tools/substance -p 'test_*.py':
PASS 6 tests, including semantics, ORM deduplication, wildcard/conflict refusal.
Executed command -v astcenc: not found (exit 1). Native compression, actual KTX2,
GPU sampling/quality/memory/performance and SAT bake NOT RUN; tools unavailable.
CI not run; no URL. Existing unrelated dirty changes preserved.

Compatibility: existing game.json packaging contract; first matching target rules
win, so emitted exact rules must precede broad rules. ASTC targets require actual
device support (macos does not imply all Intel Macs). No desktop BC output yet.
Risks: existing encoder mip quality is inherited, not certified by Python tests;
missing encoder can leave packaging warnings, not proof of compressed output.
Next: install/provide SAT and astcenc with licensed SBSAR fixture, execute real
bake/pack/import/build and GPU checks, then native background Studio integration.
