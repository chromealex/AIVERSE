---
name: aiverse-snapshot-delta
description: Cut server-to-client snapshot bandwidth in any authoritative AIVERSE game (actor hosts, city worlds, vehicles, custom native hosts) with delta compression against acknowledged baselines - record sets, per-peer sender history, client acks and bounded decoding over lossy latest-only channels.
---

# snapshot-delta

Use when an authoritative host sends full world snapshots to clients and most of each snapshot repeats the previous
one. The FPS authority session already uses it (`engine/net/authority/Session.cpp`, protocol 5); follow these steps
for another host (a native module's server, a custom battle server).

1. Read `engine/net/SnapshotDelta.h` (namespace `aiverse::lib::delta`).
2. Snapshot -> `RecordSet`: one record per stable thing, keyed by a 32-bit ID that stays the same between snapshots
   (0 = globals such as tick / score / phase; `kind << 28 | id` for entities, as `snapshotRecords` in
   `engine/net/authority/Session.cpp` does). Record bytes come from the owner's `Archive` `io`, so equal state gives
   equal bytes and a moved entity changes only a few bytes.
3. Server, per peer: `DeltaSender sender;` -> `auto packet = sender.encode(sequence, records)` with a strictly
   increasing sequence (the tick); an empty result means "send nothing". Prefix your own header (magic, protocol,
   message type) and send it on the unreliable latest channel.
4. Acks: the client reports the newest sequence it decoded (FPS: `Input.viewTick`); call
   `sender.acknowledge(seq)` for well-formed packets only. Unknown or old sequences are ignored, so a client can only
   hurt itself. Without acks the sender keeps diffing against the last ack and falls back to a full snapshot once
   that baseline leaves its history (32 by default).
5. Client: `DeltaReceiver receiver;` -> `auto records = receiver.decode(bytes, &seq)`; nullopt = drop the packet
   (malformed, replayed, or its baseline is gone). Rebuild the snapshot from the records and validate it exactly
   like a full one (`recordsSnapshot` re-encodes and runs `decodeSnapshot`).
6. Reconnect / new session: `reset()` both sides so the next packet is full.
7. Check: `tests/libraries/delta_tests.cpp` (CTest `library_snapshot_delta`: record diffs, size changes, removals,
   truncated and corrupted input, 600 snapshots over 20 % loss); FPS stress in `tests/fps/tests.cpp` asserts the
   100-client loopback match sends under half of the full bytes (measured about one seventh). Not implied:
   encryption, reliability, congestion control or per-field priority.

Build selection: `AIVERSE::lib_snapshot_delta` (no dependencies); native OBJECT modules call
`aiverse_native_use_libraries(target snapshot-delta)`.
