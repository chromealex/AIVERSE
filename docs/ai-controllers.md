# AI controller ownership

Games with a Lua or native gameplay director can set this optional rules.json block:

```json
"ai": { "controller": "scripted" }
```

The default is `builtin`. `"bw"` selects the Bunker Wars bots ported as code (AI6, Bishop, Ashley, per-skill casters, debuts, bot triggers; keys in `docs/game-format.md` § Bunker Wars computer player). They run inside `RtsAI` (every tick, own Fixed clocks), submit ordinary commands and keep their state in `RtsAI::saveState` (version 4). Unknown controller names (valid: builtin, scripted, bw) and malformed blocks fail loading with an `ai` diagnostic. `scripted` makes `RtsAI::update` return before advancing its private scheduler/RNG or submitting commands. This applies consistently to callers in the client, editor and headless runner, including the human slot that headless normally automates. It does not create a director: the game must provide one.

The setting does not disable simulation systems, automatic unit combat, human/network commands, Lua callbacks, production, or victory/defeat. Do not use player `waves: true` to achieve this: wave players have different defeat semantics. Existing games without this block retain the built-in controller.

Controller ownership is immutable rules data. Replays store the rules and recorded commands; they do not rerun external AI. No mutable simulation state or new save field is introduced. Changing the controller changes the input stream, so old balance measurements involving built-in AI must be repeated for the new controller.

Validation: `aiverse_tests aiverse_scripted_ai_controller` covers default/explicit built-in, invalid configuration, no commands or state difference against an unautomated reference, manual training, and absence of wave-player exemptions. Game-level tests must separately prove that the supplied director still acts.
