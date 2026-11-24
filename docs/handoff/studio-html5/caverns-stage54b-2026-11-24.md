# Caverns Stage54b — native Scene brush

Date: 2026-11-24. Studio branch feature/studio-html5, no main merge. Backend dependency: Caverns branch a28f708b52179ef8ecdb53f3332482e181358c63 (Workspace cached SDF raycast) plus Stage54a paint operation.

Source commits: 129870c0/9093e613 (camera ray); a3ee5947/03144a7a (3D stroke coordinates, native pick, capped dabs and grouped History); 88c92a5f/462bdef9/3051340b (cursor interface and spherical overlay); 41a5a8b8 (host input); 48034ffb/7769a3fd (typed controls); a8bee3e4/02bc1320/a776d36c (map/history guard); 61b440f1/91a860b5 (pick discovery/read-only scope).

Open Caverns panel, load document, select Carve/Fill, radius 25–6400 cm and surface or Y depth plane. Arm Scene brush; plain left drag paints, Alt orbits. Surface mode picks native SDF, misses do not paint or fall through to object selection. Depth plane is explicit. Each stroke uses existing undo merge. SDF picking uses fixed Q20 direction and bounded 500 m distance, with a cached index rebuilt on document JSON change; no browser SDF. The cursor does not establish actual cave mesh visibility. Standalone Studio lacks backend until integrated. No merge performed.

Remaining: integrate branches in a dedicated integration branch, connect real StudioScene GeometryScene preview owner and safe fence retirement; generator and entrance edits, paging/diagnostics UI; stronger same-path document identity, MCP interleaving, stroke interruption and active brush UI event synchronization. Native build, TS typecheck/build, web tests and real screenshots NOT RUN. No claim of runtime functionality verified.
