# Ordered selection voices

Click the same scout repeatedly, leaving at least 0.3 seconds between clicks so tones do not overlap. Expected frequencies: 440, 440, 660, 880, then 440 and a new cycle. The first two clicks play the normal voice; the third and fourth play the two repeat files in authored order.

Pause between the third and fourth clicks for more than two seconds: window=0 must retain the 880Hz next variant. Issue Stop or Move, switch to the other scout, or clear selection: the next click starts again at 440Hz. F6 selects the first scout, but explicit world clicks exercise repeated-selection counting.

All clips are generated sine tones with short fades, not third-party recordings. This scene uses the stock HUD and ordinary audio.json data. Browser audio needs an initial user gesture and may be muted by browser/host settings. Capture native output with AIVERSE_AUDIO=null and AIVERSE_AUDIO_CAPTURE pointing to a WAV on KINGSTON.

Acceptance pending the current audio rebuild; do not treat this fixture alone as a passed listening or mixer test.
