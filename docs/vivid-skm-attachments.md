# SKM attachments and named sockets

Studio → VIVID Appearance → Одежда и сборка персонажа supports imported models
with their own skinning skeleton, including hair, clothing and accessories.
Select a model asset, choose **Отдельный · крепление любого SKM**, select a target
bone or named socket, and enter local translation/rotation/scale. Rotation is in
radians. Choose **Сохранить исходное положение** or **Совместить с костью / сокетом**.
Run **Собрать персонажа**, then **Показать собранного персонажа**. This is the
existing native assembly flow; changing a field does not yet live-update the mesh.

Create a named socket under **Сокеты · точки крепления**: name, parent bone and
local transform. Give attachments different arbitrary slot names to put several
on the same target bone/socket. Socket rename updates references in the draft;
referenced sockets cannot be deleted in the panel. Draft edits have existing undo.
The current limit is 128 sockets / 32 attached assets, matching bounded recipes.

Native entry point remains `vivid::assembleHumanCharacter`. Recipe version1 gains
optional `sockets:[{name,bone,translation,rotation,scale}]`; each item can specify
`socket` instead of `bone`, `independentRig:true` and `attachmentRule` equal to
`keep_world` or `snap`. New readers accept old recipes unchanged; old readers that
reject unknown fields cannot consume new socket recipes. Native source.json saves
the full assembly recipe. There is no direct .uasset parser.

Sockets compile to ordinary `socket/<name>` Model nodes. The common world-matrix
traversal and GLB exporter preserve the hierarchy; existing generic View socket
lookup can resolve these names. This adds no animation clock or genre-specific
attachment renderer. Child joint IDs, inverse binds, morphs, materials and authored
clips are retained/remapped with the existing model merger. Clips are preserved,
not independently autoplayed. Shared-skeleton mode still requires matching joint
names/bind frames and zero socket offsets; it remains suitable for compatible clothes.

Snap attaches the imported asset origin with the authored local offset. Keep-world
compensates the target's full bind TRS chain, including nonuniform parent scale,
then follows the animated parent; explicit offsets are in socket-local axes.
Legacy independentRig records without attachmentRule retain their prior offset
ordering. New duplicate socket names, ambiguous bones, missing parents/targets,
nonfinite transforms and nonpositive scales fail before publication. Sockets can
reference existing named nodes or earlier declared sockets; forward/missing targets
are rejected. Imported child skins do not need matching bone names in the parent.

This supports imported SKMs as attached assets, not automatic reweighting of an
arbitrary submesh already sharing another rig. No vertex-picking attachment gizmo,
cloth collision, grooming simulation or full Unreal editor parity is claimed.

Validation: native wardrobe fixture covers keep-world and snap, local offsets,
parent rotation/nonuniform scale, preserved independent skins, GLB socket/skin
roundtrip, duplicate/missing socket rejection and legacy head attachment. Studio
session regression also passes. Native UI interaction and visual SKM motion in
Studio were NOT RUN (desktop remains unavailable). See stage91 handoff.
