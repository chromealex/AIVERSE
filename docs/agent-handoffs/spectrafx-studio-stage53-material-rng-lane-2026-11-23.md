# Stage 53 — reserved Metal material RNG lane

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 62ae64c9c3ba8d9df242a29afc32a35d07ee1e0d and 05c130be6606fb7b48c5a756dedbdd2419fb9b1d.

SpectraGpuShaders.h moves birth RNG helpers before VM read and connects materialSource code 2 to birthRandom01(p,71u). SpectraGpuMaterial.h documents reserved lane; existing authored random rejection is preserved because CPU prand(uid) differs. Skill updated. No tests or compilation. Next implement explicit opt-in random semantics across compiler, VM, runtime and replay seed lifecycle; do not claim authored RNG support or CPU/GPU parity yet.
