# Unreal animation source investigation — 2026-11-20

User clarification: “Как в анриале”. Working interpretation is MetaHuman Creator
preview behavior: independent body/face animation selection, body/facial poses,
smooth transitions. The existing procedural idle is not equivalent in quality.

Official reference:
https://dev.epicgames.com/documentation/metahuman/navigating-metahuman-creator-in-unreal-engine
describes separate head/body AnimSequence assignments and template retargeting.

Read-only local discovery found MetaHumanSample 5.7 at
`/Users/Shared/UnrealEngine/Launcher/VaultCache/MetaHumanSample_5.7/data`:
- `Content/Animation/Locomotion/MM_Idle.uasset`
- `Content/Animation/f_sample_anim.uasset` and `m_sample_anim.uasset`
- MetaHuman retargeting and corrective pose assets.

These are Unreal packages, not directly loadable glTF animation clips. No FBX,
GLB or glTF files were found in this sample with `rg --files` extension filters.
The launcher path `/Users/alex/Applications/UE_5.7` exists but contains plugin
remnants; tested standard Mac UnrealEditor executable paths are absent. Spotlight
also returned no UnrealEditor.app/UnrealEditor-Cmd. This does not prove there is
no engine at an unindexed custom location. A plugin dylib is not an editor.

No sample files were modified, no editor was launched, no animation was exported
or applied, and no new screenshots were produced. Builds/tests NOT RUN: this was
source discovery only. Existing native preview remains unchanged, including
known eyelash alpha defects and T-pose shirt intersections.

Next: locate a usable Unreal installation or obtain a skeletal-animation export
with its reference skeleton. Use a KINGSTON project copy and redirect caches/logs
there before export. Validate skeleton compatibility and garment retargeting,
then add independent body/face clip selection using the existing animation owner.
Do not bundle sample assets into the repository or claim procedural poses are
MetaHuman corrective deformation. Installation of a large engine is not yet done.
