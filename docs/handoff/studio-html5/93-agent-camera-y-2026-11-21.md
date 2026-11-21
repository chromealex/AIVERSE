# Stage93 — agent Scene camera height

Code9b3a2c9c024ca02b34bb9abe3aa366732c1009ab, feature/aiv87_terrain_pbr.
Native EditorAgent captures/focus accept optional targetY, with atomic finite
validation. Shared Workspace JSON schemas match; no frontend, GUI style or
bridge allowlist changes. Studio has its independent existing camera controls;
this does not add Studio screenshot transport or claim planet camera parity.

Actual native Editor+Studio builds PASS on KINGSTON. Actual birch Scene capture
atY2.6 vs legacyY0 differs, negative protocol requests preserve geometry, and
map remains dirty:false/1391entities. Byte-identical live PNG assertion initially
failed from small frame drift; silhouette IoU.998274 and visual inspection used
instead. See terrain handoff for exact filenames/commands/results. No synthetic
viewport images. Maclocked prevents Y mouse drag/Undo acceptance; not completed.
Next repair native grass cover masking and inspect ground-close Scene again.
