# Texture Graph #133 — Metal fixture metadata compile blocker

Branch: `fix/texture-133-function-tests-20261124`; prior commit `d4df1fec412bec3ec91b3a2879833b6de97e88ce`.

Fresh Mac arm64 gate passed web typecheck/tests/build and linked native Studio plus MCP. Native acceptance built its CPU/Studio fixtures, then the Metal fixture failed compilation in `tests/render/texture_graph_gpu_acceptance.h:324`: `gfx::TextureInfo` has no `mipLevels`. The fixture now checks the actual available width, height and pixel format. Its immediately following GPU readback of mip 2 (when requested) still verifies the mip chain and dimensions, so the missing API field is not replaced by an inferred success.

Failed command: `bash tools/texture-graph-hdr-final-mac.sh`, exit 2; SHA `d4df1fec412bec3ec91b3a2879833b6de97e88ce`; archive `/Volumes/KINGSTON/AIVERSE-builds/texture-graph-hdr-final-d4df1fec412b-arm64/texture-graph-hdr-final-d4df1fec412b-20261124T190334Z-14474.tar.gz`. CTest, Metal execution, MCP audit and live Studio NOT RUN. Next: rerun the final gate on this corrected clean commit.
