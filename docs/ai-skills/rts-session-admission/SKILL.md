---
name: rts-session-admission
description: Authenticate AIVERSE Conn transports before attaching a player to an authoritative game, with match-bound identity, expiry and revocation. Use from native host code, never simulation ticks.
---

# Session admission

Read `libraries/include/aiverselib/net/Admission.h` and `docs/local-network.md`.
Reuse the existing Conn, authoritative Server/Client and app/Session retry policy.
This package adds their missing admission boundary; it does not replace transports.

- Wrap the client connection in `ClientAdmission(connection, token)` before constructing
  the authority Client. It exposes Connecting until the gate acknowledges admission.
- Enqueue incoming server connections in `AdmissionGate(matchAudience)`, then poll using
  the same monotonic millisecond clock as networking. A trusted verifier maps the bearer
  to Principal {player, audience, slot, expiresAt}; never take those fields from a client.
  Use a cached verified session store or a project auth adapter, not a blocking HTTP call
  inside the network poll. Reject occupied slots in the verifier before attach.
- The gate checks token shape, audience, player/slot bounds, expiry, 64 pending requests
  and a 10-second handshake timeout. Attach only returned AuthorizedConnections to Server.
  Keep Access handles to revoke on logout/provider events. Access is single-threaded;
  dispatch revocation onto the owning network thread.
- Poll all connections. Expiry/revocation closes the transport. The existing authority
  releases held input on disconnect and resumes sequence from the saved actor on reconnect.
  After a restart restore the simulation first, then authenticate and reattach players.
- Use a protected transport for real bearer credentials. Loopback tests do not add TLS,
  encryption, server identity or anti-replay protection against stolen bearer tokens.
  Plain Internet UDP is not made confidential by this handshake.
- Native OBJECT consumers call `aiverse_native_use_session(aiverse_native_<id>)` after
  registration; it propagates aiverse_net to the final owner. Ordinary hosts link
  `AIVERSE::session` and the actual transport/authority targets. See
  `examples/native/strategy/AdmissionConsumer.cpp` and `SessionProbe.cpp`.
- Do not put Access, credentials, sockets or wall clocks in NativeState or NativeModule
  setup/tick/command. These are host objects. Run `strategy_native.py` for real native and
  relocated-export linking; run `aiverse_session_native_probe --udp` locally for packet loss,
  reconnect, server restore, private inventory fields and revocation.

For a local host, `LocalTickets.h` reads the private atomic table issued by
`tools/meta/local_match.py` from an authenticated ordinary meta session. See the local
network guide for slot retention, rotation, explicit revocation and `--loopback`.
Clear/revoke on failed reload; never trust a client-provided table. Production identity
issuance, distributed allocation, secure WAN transport and deployment limits remain work.
