# QA2 — integrated S6 verification gates (2026-11-24)

Owner Studio. Branch feature/studio-html5. Starting S6i.2 handoff c8d6c083dbaeeb20e0013fb69a420ab91d227e7c. Main 2be780abdf5fa93844472f34e07e4c6dabae8935 merged into feature only at 7696c53d36924abd1a59f122dc4f1792ff6fe2b6; both main-changed site blobs matched the merge base on the feature side and were taken unchanged from main. Post-merge comparison confirmed behind=0. QA wiring b4de7500b8d5303f666739d427b7a58b98dee7af; CI quoting correction 77afa4e4376c314bf037f803aac8c881e1e7d942; complete linked audit baseline b0ed143125c44de88ee5044d27920fed2552816c.

## QA wiring
- apps/studio-native/CMakeLists.txt: ordinary CTest owner-history target and built MCP registry audit when MCP and Python interpreter are configured.
- tools/studio/verify_mac.sh: selectable isolated KINGSTON output root, module cache on KINGSTON, tools enabled, all 30 declared Mac Studio test executable targets explicitly built and CTest JUnit output. Existing web/Python gates retained.
- .github/workflows/studio-html5-web.yml: MCP changes trigger CI; Mac and Windows compile MCP, owner history and session/asset contracts; selected CTest includes registry audit. Initial workflow change b4de7500 produced invalid quoting/duplicated Windows job; corrected in 77afa4e4.
- tools/mcp/audit_registry.py: add metadata patch and folder creation, covering all 19 explicitly registered linked-only tools.

## Actual evidence
- GitHub source readback at b0ed1431: compared declared Mac Studio test target names to verify_mac.sh: 30/30 present; compared addLinked registrations with audit names: 19/19 present; one Windows job definition. Static coverage check only, not execution.
- GitHub Actions run 45376754636 on b0ed1431 was QUEUED at inspection, no success evidence. Run 45376665359 on b4de7500 failed (invalid workflow fixed); older run 45374791805 cancelled. Do not report cancelled/queued builds as passed.
- Container has no checkout, no /Volumes/KINGSTON mount; git ls-remote failed DNS resolution. Native build, CTest, TS build, real MCP audit, Mac/Windows UI and quick gate NOT RUN in this session.

## Local Mac gate
Use isolated checkout at b0ed1431 or subsequent documentation-only handoff. Verify mounted writable KINGSTON before creating output. From checkout root:
```bash
export AIVERSE_STUDIO_VERIFY_ROOT=/Volumes/KINGSTON/AIVERSE-builds/studio-s6-qa
export STUDIO_BUILD_TYPE=Debug
bash tools/studio/verify_mac.sh
```
Retain terminal build log, studio-ctest.xml and native/Testing/Temporary/LastTest.log. After passing focused Studio gates, run the unified merged-tree quick gate in the same isolated native build directory (configured Studio option retained). Review warnings/errors and native interactive GPU/viewport behavior. No main merge, no production readiness claim. Existing S6 limitations remain tracked; don't silently discard data or bypass native owner checks to make tests pass.
