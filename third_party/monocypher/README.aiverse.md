# Monocypher 4.0.2 (vendored)

Source: https://github.com/LoupVaillant/Monocypher, tag 4.0.2, files `src/monocypher.c`,
`src/monocypher.h`, `LICENCE.md` (unmodified). License: BSD-2-Clause OR CC0-1.0 (see LICENCE.md).

SHA-256 at import (2026-11-12):
- monocypher.c `02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d`
- monocypher.h `fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3`
- LICENCE.md   `a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0`

Used by `engine/net/Crypto.*` only (XChaCha20-Poly1305 AEAD, keyed BLAKE2b) for the secure battle
transport (`engine/net/SecureUdp.*`) and match tickets (`engine/net/MatchTicket.*`). Do not call it
from simulation code. Platforms: compiled on Linux (gcc); macOS / iOS / Android / Windows / web
builds are expected to work (portable C99) but were not compiled in this change.
