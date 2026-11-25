# S6h.1 — recovery storage quota (2026-11-24)

Owner Studio; branch feature/studio-html5. Base 562abfa2f365a31dcae41e4fbd076b768f8fdbfb; roadmap 6e3b0a7c78bd11bed2ffb923c6f77afd7ee2433b; implementation a4fc5cd923178179535910d80a8fc92c8176353e.

## Implementation
- apps/studio-native/StudioAssetTransfers.h/.cpp: bounded logical-byte accounting of asset recovery tree including folder contents and unlisted slots; separate asset checkpoint/journal totals and available filesystem bytes. Versioned project-local recovery quota policy (integer MiB, 0 unlimited for legacy compatibility, max 1 TiB). Compare-and-set policy update under existing asset-history lock, durable temp/rename. Positive net retained growth checked before quarantine/replacement journal and coordinated batch journal; net-reducing restore and explicit purge remain available. Both pending move and coordinated journal block manual trash review/purge.
- apps/studio-native/StudioSession.cpp: typed project-bound quota setter; existing Trash listing returns storage/error diagnostics.
- apps/studio-web/src/types.ts, components/AssetTrashDialog.tsx, mock.ts: display usage, checkpoint/journal sizes, available disk, warning and over-quota state, explicit quota edit. Mock does not pretend native recovery is available.
- apps/studio-native/tests/StudioAssetTransferTests.cpp: default policy, persistence, rejected growth, over-budget restore and stale quota refusal.
- docs/devlog/systems.json, docs/mcp-agent-link.md, skills/editor/studio-ui/SKILL.md: scope and owner guidance.

## Evidence
Source changes committed and read back; native API, Studio and existing recovery owner integration implemented. No new dependency, simulation owner or authored gameplay schema. Local C++ compile, CTest, web typecheck/build, MCP registry audit, Mac/Windows UI and screenshots, performance measurements, and KINGSTON quick gate NOT RUN. Roadmap remains in progress; no main merge or full parity claim. New policy persistence and fixtures are implemented but not runtime-verified. Runtime ECS/headless and graph editing are N/A.

## Limits and next
Quota is for net retained trash growth, not physical allocated blocks, staging peak, record-only growth, project/map history or all project caches. Existing over-budget data is never automatically deleted; reducing transfers remain allowed. Rejected operations may leave their pre-existing native staging/recovery manifests; report accounting failures rather than inventing usage. No interprocess lock beyond existing cooperating Studio publication paths; standalone native callers must serialize mutation. Default warning is 4 GiB without configured quota. Next S6h.2: per-record folder accounting, safe retention review and explicit batch cleanup with stale-plan protection, and bounded staging/record-overhead policy; S6i linked MCP readback and authoring coverage; final QA.
