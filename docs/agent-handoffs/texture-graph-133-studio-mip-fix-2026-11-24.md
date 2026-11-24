# Texture Graph #133 — Studio HDR mip metadata compile blocker

Branch: `fix/texture-133-function-tests-20261124`; prior commit `60aa73664202b2282a37970f1bed1f294415c6ed`.

Fresh Mac arm64 Release gate passed Studio web typecheck, tests and production build. `aiversemcp` built and linked. `aiverse_studio` failed in `apps/studio-native/StudioScene.cpp:913,915`: `gfx::TextureInfo` has no `mipLevels` field. Studio now reads the graph owner's exact request `gpuTextureMipmaps` status before ownership transfer and uses the shared `gfx::maxTextureMipLevels(width,height,1)` for the full chain, or one level when generation was disabled. The existing device texture metadata still validates the resident handle before publication.

Failed command: `bash tools/texture-graph-hdr-final-mac.sh`, exit 2, SHA `60aa73664202b2282a37970f1bed1f294415c6ed`; log archive `/Volumes/KINGSTON/AIVERSE-builds/texture-graph-hdr-final-60aa73664202-arm64/texture-graph-hdr-final-60aa73664202-20261124T184214Z-81285.tar.gz`. CTest, Metal and live Studio NOT RUN. Next: run the final gate from this corrected clean commit.
