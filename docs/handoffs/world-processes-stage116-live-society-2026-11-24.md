# Stage 116 — planetary live SOCIETY and eleven-layer source milestone
Date: 2026-11-24. Branch: feature/world-processes-stage31-bidirectional-handles.
Base: 4772ce6be14fbda7168f3c2a388c54ef0d477e67. Plan: 8a75747973389f202c566d18ee262980b9222901. Implementation: 242f8775155d4d1e3497fdff896b80119dfc9df1. Fixture: 5538cedea3d26febae1cc13c7d78315981dd3028.

## Delivered
- apps/studio-native/StudioPlanetSocietySnapshot.h: bounded read-only actual knowledge, contacts, messages and triggers.
- apps/studio-native/StudioGameSurface.h/.cpp, StudioHost.h/.cpp, StudioScene.cpp: independent validated Live setting and native diagnostic renderer.
- apps/studio-web/src/types.ts, mock.ts, panels/SceneBrushBar.tsx: typed controls and summaries.
- tests/world_processes_gamecore_native_test.cpp: source regression fixture.

## Verification gate
Implementation/architecture: source committed, existing ECS and renderer reused. Native compilation NOT RUN.
Native API: existing SOCIETY contracts unchanged, Studio-only helper.
Skill/docs: existing skills and compatibility guide updated.
Studio/Inspector/MCP: existing transient typed RPC extended; no new authoring mutation or schema. Typecheck, interaction and RPC tests NOT RUN.
Persistence/graphs: no new authority, schema or graph. Headless regression NOT RUN.
Performance/platforms: bounded per-layer sampling; aggregate measurements and platform acceptance NOT RUN.
Dependencies: existing targets only; build NOT RUN.
Examples/tests: existing fixture extended; no command executed.
Publishing: real captures/release acceptance NOT RUN.
Integration: no main sync/merge; shared Studio paths require integration review.

## Stage 116: planetary live SOCIETY knowledge and NPC triggers

Enable Live 3D overlay and the independent SOCIETY knowledge / trust checkbox during local Play current map. Read canonical Person knowledge, credibility, acceptance, enabled state, directed Contact configuration, configured Message topics and actual NpcTrigger output. Person knowledge and trigger active/fired/transition require exact completed-tick stamps. Topic masks are decimal strings to preserve all 64 bits through JavaScript; population count is numeric. No interpretation of topic meaning or social identity.

Current contact eligibility uses enabled endpoints and trust * source credibility >= destination acceptance, the existing solver's threshold. Source-only topics are the difference of current sampled masks, not measured transfers or historical message delivery. Trigger output is the actual ECS result and action ID; fired is not confirmation of external AI/quest execution. No repeated propagation, trigger evaluation, dispatch or persistence.

Native Scene renders real body-local Transform positions, directed contact edges, current trust eligibility, configured message markers and trigger state. Blue people (size based on known-topic count), green eligible contacts, interrupted muted blocked contacts, amber configured messages, green active and purple fired triggers, grey unavailable. Unplaced rows remain numeric; cross-body logical relationships remain intact and numeric, not drawn across bodies. Stable packed-ID order and owner budgets, up to 256 people, 512 contacts, 128 messages and 128 triggers. Counts describe sampled candidates. Existing local Play/map/revision guards and renderer reused; planar ecology unchanged.

Source regression covers one-hop across two ticks, low trust rejection, high-bit topic mask, cross-body logical propagation, one-shot trigger transition, stale output and cap. Build, CTest, browser typecheck, interaction, captures and performance NOT RUN.

## Live planetary diagnostic coverage — source milestone

Stages 106–116 now have source implementations for all eleven planned layers:

| Stage | Layer | Read-only diagnostic |
| --- | --- | --- |
| 106 | IMPRINT | Canonical trace quads |
| 107 | GRANULAR | Explicit diagnostic-scale occupied tops |
| 108 | EVOLVE | SurfaceArea chart boundaries and region state |
| 109 | PHYSIS | Effective gravity samples |
| 110 | AETHER | Gas masses and isolated duct potentials |
| 111 | FRACTURE | Member/bond state and durable events |
| 112 | MORPH | Bound-node displacement and constraint residual |
| 113 | ECHO | Acoustic graph and completed hearing |
| 114 | CROWD | Native route and reservation state, gravity support |
| 115 | CIRCUIT | Node and device outputs, logical topology |
| 116 | SOCIETY | Knowledge, trust, messages and trigger output |

This is 11/11 source coverage of the defined Live planetary diagnostics, NOT complete planetary gameplay, physical/render integration, or verified production readiness. Existing planar workflows were retained, not newly extended with all eleven overlays. No build/tests or main merge in this sequence. Next recommended work: consolidate transient layer options and shared snapshot/render helpers, review match/map lifecycle and global aggregate line/payload budgets, reconcile shared Studio changes with studio-html5, resolve owner integration gaps (planetary terrain mutation, granular physical scale/render/collider binding, ecology vegetation/weather, actual debris delivery, MORPH model stream binding and physical locomotion), then execute focused and merged-tree Mac quick acceptance when user lifts test deferral. Do not report all features complete on the basis of diagnostic coverage alone.
