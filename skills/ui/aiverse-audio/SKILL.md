---
name: aiverse-audio
description: Set up AIVERSE game sound - audio.json sounds, buses, groups, spatial settings, music sets, event bindings, synth sounds, verified without hardware by recording the mix.
---

# Audio

Format: `docs/game-format.md` "audio.json" (presentation only - never reaches the simulation).

1. Import files with `asset_import` (ogg); `game_file_read` / `game_file_write` the game's `audio.json`.
2. Structure: `volume` per bus (master, sfx, ui, voice, music, ambient), `spatial` (distances, air absorption, reverb), `groups` (max voices, steal), `sounds` (files with pitch range, maxInstances, cooldown, priority, near/far variants, `synth` for placeholder blips), `music` sets (menu, match, battle; shuffle, loop).
3. Bind sounds to game events (per type, weapon, ability) as the format describes; unknown names only warn, so re-read warnings after every edit.
4. Verify without speakers: run the client with `AIVERSE_AUDIO=null` (real audio thread, no hardware) and `AIVERSE_AUDIO_CAPTURE=mix.wav`, play a short match (`AIVERSE_FAST_FORWARD`, `AIVERSE_EXIT_AFTER_FRAMES`), check the recorded mix is not silent and not clipped.
5. Mix: voices and alerts above effects (`priority`, buses); big battles limited by `groups` so they stay readable.

Runtime library: none. Workflow over the game's audio data.

## Ordered repeated selection voices

Set `types.<id>.repeatSelect` in audio.json, for example
`{"sound":"annoyed","count":3,"order":"sequential","cycle":true,"window":0}`.
The named sound's files play in order starting at the threshold; with cycle
enabled, the next click after the last variant returns to the normal selection
voice. Zero or omitted window disables timeout for sequential mode; a positive
window limits the sequence. Existing random mode remains the default.

Use the existing game_file_read/write operations, then read the saved JSON
back. Verify repeated selection of the same entity, switching/clearing
selection, and an issued order. Record audio through the null-device capture
workflow above; inspecting JSON alone does not prove the audible sequence.
