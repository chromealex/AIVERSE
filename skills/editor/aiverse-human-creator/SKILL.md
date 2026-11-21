---
name: aiverse-human-creator
description: Author VIVID digital humans and Character Creator looks (skin, eyes, hair, face and body proportions, 52 facial rest channels, routes, rig, skin weights; looks of imported models from a text description) through the aiversemcp human_* / human_look_* tools or the Studio Character Creator window.
---

# VIVID Human Creator

Use this skill when a user asks to create, change or review a digital human / character face, skin,
eyes or hair in an AIVERSE game, or asks about the Studio "Human Creator" window.

One document, two editors:
- Studio window "Human Creator" (centre dock, panel id human_creator), native bridge methods studio.humanCreator*.
- MCP tools `human_schema`, `human_list`, `human_get`, `human_create`, `human_edit`, `human_validate`
  (GPU-free; run in aiversemcp without an editor), plus `human_look_schema`, `human_look_list`, `human_look_get`,
  `human_look_save` for Character Creator looks of imported models.
- Both read and write `<project>/humans/<name>.json` (version 1: `dna`, `rigCorrections`, `skinWeights`, `routes`)
  through `engine/character/vivid/HumanCreatorWorkbench.h` (`aiverse::vivid::HumanCreatorWorkbench`, target `aiverse_vivid_human`).
  Saves are atomic and never truncate the last-good file.

## Workflow (MCP)
1. `human_schema` once: section ids, field `key`s (DNA paths), `min`/`max`, presets, 52 channel names,
   reference rig `nodes`, morph `targets`, head `vertices`. Never guess a key or a range.
2. `human_create {name, preset}` (neutral | warm | cool | stylized).
3. `human_edit {name, ...}` - one all-or-nothing transaction, applied in order:
   `preset` -> `set {key: value}` -> `routes` (replaces the list) -> `rig` -> `skin`.
   ```json
   {"name":"ada","set":{"skin.melanin":0.55,"skin.melaninColor":[0.48,0.32,0.24],
     "eyes.irisColor":[0.22,0.36,0.18],"groom.lengthMeters":0.35,"groom.curl":0.4,
     "anatomy.jawWidth":0.95,"expressionRest.24":0.15},
    "rig":[{"node":"head","translation":[0,0.005,0]}]}
   ```
   An invalid edit (out of range, unknown key, duplicate bone, |offset| > 1, weight outside 0..1) rejects the whole
   call and writes nothing; read `error`, fix, retry.
4. `human_get {name}` to confirm the stored document.
5. Visual check: the user opens Studio > Human Creator > Load `<name>` > "Show in viewport". Say plainly that you did not
   see pixels: `aiversemcp` has no Studio screenshot tool yet.

## Project characters (Character Creator, model source)
The same window, source "Модель", edits an imported character (MetaHuman-style categories: Пресеты, Лицо, Скульпт,
Кожа, Глаза, Волосы, Тело, Одежда, Поза и свет). Looks are `<project>/human-looks/<name>.json`
(`{version:1, values:{key:number}, materials:{sourceMaterial: role}}`), with `<name>.png` preset pictures.

Agent draft from a text description ("пожилой рыбак, обветренная кожа"):
1. `human_look_schema` once: field `key`, `label`, `min`, `max`, `initial` (skin.*, eyes.*, hair.*, lashes.*, body.*).
   Never guess a key; colours are separate R/G/B keys (e.g. `eyes.irisR`).
2. `human_look_save {name, values, base?, materials?, overwrite?}` - values merge onto `base` (another look) or the
   defaults; the whole look is validated, nothing is written on error.
3. `human_look_list` / `human_look_get {name}` to confirm. The artist applies it in Studio > Character Creator >
   Пресеты (cards) and sets the face shape with the blend circle; face shape, sculpt and wardrobe have no MCP tools.
4. See the result: once the look is shown in Studio, `studio_character_snapshot {view: face|torso|full}` returns a PNG
   path of the Character Creator viewport (Studio must be running with this game). Open the PNG before you describe
   the result; if Studio does not answer, say that you could not see it.
The Presets tab can copy this request for you ("Запрос агенту"). Makeup is part of the look: `makeup.*` keys
(lips, blush, shadow, stubble with colour, position and size in skin UV).

Studio-only features (describe the steps, do not claim to have run them): face blend circle with per-part handles
picked in the viewport (`studio.humanFacePick`), drag sculpt with a brush circle, before/after (`studio.humanCompare`),
test expressions (preview animation `expression`: idle smile laugh surprise anger sad disgust talk), live wardrobe
try-on (`studio.humanTryOn`), clothing `tint` [r,g,b] in the wardrobe recipe, LOD/game budget after the build and
"В игру" (`studio.humanPlaceInGame` adds an RTS type with `view.model` and places one unit).

## Sections (Studio window = tools/vivid-creator)
Skin, Eyes, Hair/groom, Face and body proportions, Facial morph / 52 channels (baked into the preview through the
routes), Facial morph bindings (channel -> target, gain 0..4), Rig bone names, Rig correction authoring
(presentation-only node overlay), Skin weight authoring (per vertex + named bone), plus Identity.
Viewport brush painting, scalp collision meshes and imported glTF/FBX/OBJ packages stay in `tools/vivid-creator`.

## Rules
- DNA is presentation only: no gameplay authority, no ECS mutation, no MetaHuman/Unreal DNA files.
- Keep the 52 channel order (`engine/character/vivid/Face.h`); address channels as `expressionRest.<index>`.
- Groom budget: `groom.guideCount * groom.segments <= 262144`.
- Imported characters (scans, wardrobes, appearance looks) use the "VIVID Appearance" window and `studio.humanAppearance*`.

## Validation
- `ctest -R '^studio_human_(creator|appearance_session)$'` (native, Mac/KINGSTON build).
- `cd apps/studio-web && npm run test:human && npx tsc --noEmit`.
- After a change: `python3 tools/skills/catalog.py generate` and `python3 tools/skills/audit.py --strict`.

Reference: `docs/vivid.md` (Studio Human Creator), `docs/agent-handoffs/vivid.md`.
