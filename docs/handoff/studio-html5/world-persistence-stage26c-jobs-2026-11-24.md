# W1.26c — World Persistence background jobs UI

Base 2a2b8bb8c825a64b11b6400a2d52c47d5809ee1f; branch feature/studio-html5.

## Implementation
- 14e29f2f, de61da32, cbe4fff3: native allowlist and expectedProject guard for job start, typed StudioWorldJob DTO.
- 01c989e8, 97d2a680: WorldPersistencePanel uses start/status/cancel/release, polls actual recovery metrics, disables mutation controls during occupied slot, rediscovers detached jobs and never automatically resubmits an ambiguous operation. Show prior project's result separately.
- 73eca603: bounded terminal result layout.
- 50fd8011: source contract fixture updated.
- a213699a: studio-ui skill updated.

## Verification and limits
Backend dependency feature/aiv20261123_open_world_audit at e13645adcf53552e719f0e7f889abf635c86a07a. Native and web builds, Python source tests, actual MCP stdio and WKWebView screenshots NOT RUN. No main merge. No fabricated progress; checkpoint byte bar measures reading, not total time. Single outstanding slot, cancellation refused at irreversible barrier, not an arbitrary job queue.

## Next
Integrate backend and UI branches, reconcile shared allowlist once; native project guard applies to start, old job status/cancel/release are keyed by ID. Run actual CTest/MCP/Studio tests on integrated tree and record results; no main merge before required native Mac gate.
