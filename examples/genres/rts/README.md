# RTS genre acceptance examples

Nine small authored rule/map/Lua projects exercise existing RTS mechanics. These are
acceptance fixtures, not full games or claims of complete genre coverage. Tower Defense
is an RTS subgenre. No external/commercial assets are required.

Regenerate from the repository root with `python3 tools/genres/create_samples.py`.
Verify with `python3 tests/genres/aiverse_samples.py --headless /path/to/aiverse_headless`.
The verifier runs each game to a winner and compares snapshot, serialized and fresh
serialized continuation. Relevant combat/gathering/build events are required.

Verified using an AppleClang headless build of recovery base `40c434d0`: all nine games
passed completion and all three restore paths. No client GUI, Metal, Windows, Linux,
mobile or Web runtime check was performed in this recovery. These fixtures deliberately
use simple geometry and scripts; use current library packages for richer mechanics.
