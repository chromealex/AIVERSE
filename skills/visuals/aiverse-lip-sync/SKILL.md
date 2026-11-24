---
name: aiverse-lip-sync
description: Lip-sync viseme tracks from recorded speech (loudness, zero crossings, band energy, closed-lip gaps) or from text, with attack/release smoothing and sampling for the face rig.
---

# Lip-sync from audio or text

Header: `<aiverselib/animation/LipSync.h>`. Library catalog id `lip-sync`, CMake target `AIVERSE::lib_lip_sync`.
Capability area A3 in `docs/capability-status.json`. Tests: `tests/libraries/policy_mechanics_tests.cpp`.

Deterministic policy: Fixed/integer math, ascending ids, strictly increasing ticks; repeated or invalid input returns nullopt and leaves state untouched. Works in any project: the owner reports facts and applies outputs.

## Use

1. Audio: `analyseSpeech(pcm, sampleRate)`; text: `visemesFromText(line, seconds)`.
2. Each frame `sampleVisemes(keys, time)` returns weights; drive VIVID `Face::setViseme(static_cast<vivid::Viseme>(i), weight)`.
3. Presentation only; keep the keys with the dialogue line asset.

## Limits

Energy/spectral heuristics, not a phoneme recogniser; Latin text only.
