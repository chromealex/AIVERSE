# Texture Graph #133 — final Mac runner archive fix

Branch: `fix/texture-133-function-tests-20261124`; preceding code and handoff commit `d09302fe8f599f19e6263f5a3db1d8e0b51b3f2f`.

The exact-commit final runner passed npm install, Studio web typecheck and all web test groups, then failed at Vite build: the isolated `git archive` omitted `engine/assets/branding/mark.png`, imported by `apps/studio-web/src/App.tsx`. The runner now includes that tracked asset in its web archive. This changes only acceptance packaging, not application code.

Failed run: `bash tools/texture-graph-hdr-final-mac.sh`, exit 1 on macOS arm64; log archive `/Volumes/KINGSTON/AIVERSE-builds/texture-graph-hdr-final-d09302fe8f59-arm64/texture-graph-hdr-final-d09302fe8f59-20261124T181220Z-56772.tar.gz`. Native build, GPU and MCP audit did not start. Next: rerun the final runner from this new clean commit, then inspect native failures or successes with exact SHA.
