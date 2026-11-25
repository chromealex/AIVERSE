# Stage 36C — retry 10: graph test lookup and exhausted disk

User quick build excerpt: progress 94%, eight ambiguous require calls (tests/vfx_graph_ops_test.cpp lines 83,84,85,87,90,97,98,101), and four ENOSPC diagnostics while building aiverse_hosted_client. Full native build failed; CTest NOT RUN. Exact tested SHA and complete warning/notes count not supplied.

Repair 18ae49125dd63db69bcbbc380aa4f261b37d3e1d: rename local static assertion helper to requireGraphTest and all 148 uses, preserving every condition and error message. The eight failing calls were inside using namespace render::texture_graph scopes; engine/render/graph/TextureGraph.h defines its own require(bool,const char*). No production code or runtime graph validation changed. Manifest updated. Post-fix native compile and CTest NOT RUN.

Storage is a separate blocker. Inspect physical build path with pwd -P, df -h and du -sh; check /Volumes/KINGSTON is mounted/writable with sufficient free space. Reuse the existing cache if already on KINGSTON after freeing space. If the old cache is internal, configure a new external --build-dir under /Volumes/KINGSTON/AIVERSE-builds/building-stage36c; do not blindly move CMakeCache.txt between paths or delete user data. Set TMPDIR on KINGSTON. Save previous reports. Fast-forward the integration branch and rerun quick gate only after space is available. No main merge.
