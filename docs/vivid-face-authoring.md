# VIVID facial identity authoring

Studio → VIVID Appearance → Форма лица · скан / фото / импорт.
Select the source character and load mesh/bone names. Select the face meshes
(including details that must follow it), adjust the 18 identity controls,
and Apply to preview. Reset/undo are draft operations. Assemble character
bakes the same deformation before appearance/body/wardrobe assembly.
The original asset is untouched; source.json stores the editable face recipe.
Restore source.json against the ORIGINAL model, never the already baked GLB.

The template must be Y-up, face forward +Z. Coordinates use the selected
meshes' world-bind bounding box. Topology, UVs, skin weights and semantic morph
names survive. Existing morph deltas are transformed by the local deformation
Jacobian. This is not anatomical bone refitting or MetaHuman RigLogic.
Extreme deformation requires pose and eye/teeth/eyelash intersection review.

## Photo reference

Local PNG/JPEG/WebP is a read-only reference, not uploaded or copied into the
project. Crop tightly around the head. Create six template landmarks; choose
an active point and click its target position, or edit XYZ numerically.
Front changes X/Y only, profile changes Z/Y only. Unknown depth stays from the
template. The square reference view normalizes each axis independently.
Markers are generic initial guesses, not detected facial landmarks.
No automatic photogrammetry, photo texture extraction, landmark detection,
camera calibration, or single-photo 3D identity reconstruction is implemented.
The recipe persists geometry targets, not the private reference photo.

## Scan

Select an imported model asset as scan. Both models must already have common
alignment and units. A bounded nearest-vertex search generates up to 128 local
landmark targets; at least four correspondences are required. This is coarse
fitting, not ICP registration or nearest-triangle fitting. Maximum distance
is in model units; strength scales displacement. Review the result before Apply.

## Separate head and body

The selected character is the body. Pick a separate imported head model, an
optional body socket, meshes to hide, and a fresh output name. A new GLB retains
both skins, materials, morphs and imported clips. Head nodes/clips are namespaced.
An inverse-bind compensation chain preserves the imported world bind frame
while following the body socket. Sources must share coordinate scale/alignment.
No neck weld, automatic retarget, or synchronization of independent animation
clips is provided. The head/body operation uses source PBR and does not apply
pending appearance/face drafts; edit the new asset afterward.

Native methods: studio.humanFaceFitScan, studio.humanHeadBodyImport;
both share studio.humanCharacterBuildStatus and its single async operation slot.
Face recipes are optional in studio.humanAppearanceApply and
studio.humanCharacterBuild. No browser project writes are introduced.
Existing binary MetaHuman DNA adapter limitations remain unchanged; arbitrary
modern body DNA and full RigLogic import are not implemented by these tools.
