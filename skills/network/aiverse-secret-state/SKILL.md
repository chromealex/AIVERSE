---
name: aiverse-secret-state
description: Hidden information for authoritative servers: zones with visibility rules, per-seat opaque handles re-keyed on shuffle, reveal events, reconnect snapshots and commit/reveal rounds.
---

# Secret state and commit/reveal

Header: `<aiverselib/mechanics/SecretState.h>`. Library catalog id `secret-state`, CMake target `AIVERSE::lib_secret_state`.
Capability area K2 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Server: `addZone`, `placeObject` (collect RevealEvents), `shuffleZone`; send each seat only `projectForSeat(state, seat)`.
2. Clients refer to hidden objects by handle; the server maps them with `resolveHandle`.
3. Reconnect: send `snapshotForSeat` and continue from its version.
4. Simultaneous decisions: `CommitRound` with `submitCommit`, `submitReveal`, `resolveRound`; pass a cryptographic hash when the server is not trusted.
5. Persist SecretState (kind 222, server only) and CommitRound (kind 223).

## Limits

Transport and encryption belong to engine/net; the default FNV commitment is not cryptographic.
