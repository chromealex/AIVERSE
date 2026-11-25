#!/bin/sh
# Bit-exactness guard for the Lua port (scripting agent): every sample game (catalog template packages), seeds 1-3, must print
# the checksum recorded in baseline.txt (fixtures in tests/checksums/games too: recorded with the C++
# implementation before each port). Usage: tests/checksums/check.sh [build dir] [--record]
# the scripted template needs Lua (not in a pre-scripting build): its lines come from the scripting build.
# Prints sim time per match too (perf comparison). Exit 1 on any difference.
set -eu
B=${1:-build/lin}
DIR=$(dirname "$0")
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
export LC_ALL=C
# sample games: the official template packages of the catalog (installed on first use, then cached)
TPL=${AIVERSE_TEMPLATES_DIR:-$(python3 tools/pkg/fetch_templates.py)} || { echo "cannot install the template packages"; exit 1; }
# sample games + fixtures that exercise mechanics the samples don't (tests/checksums/games)
for gd in "$TPL"/* "$DIR"/games/*; do
  [ -f "$gd/game.json" ] || continue
  # This baseline guards the deterministic RTS module; other genres have separate suites.
  module=$(python3 .github/scripts/game-info.py "$gd" | sed -n 's/^MODULE=//p')
  [ "$module" = "game" ] || continue
  g=$(basename "$gd")
  for s in 1 2 3; do
    if ! line=$("$B/aiverse_headless" --project "$gd" --seed "$s" --quiet 2>&1); then
      printf '%s seed %s: headless match failed\n%s\n' "$g" "$s" "$line" >&2
      exit 1
    fi
    if ! sum=$(printf '%s\n' "$line" | awk '/^Checksum: / {
      sub(/\r$/, ""); value=substr($0, 11); count++
    } END {
      if (count != 1 || length(value) != 16 || value ~ /[^0-9a-f]/) exit 1
      print value
    }'); then
      printf '%s seed %s: expected one 16-digit hexadecimal checksum\n' "$g" "$s" >&2
      exit 1
    fi
    ms=$(echo "$line" | sed -n 's/.*sim=\([0-9]*\) ms.*/\1/p')
    win=$(echo "$line" | sed -n 's/^Result: winner=\([^ ]*\).*/\1/p')
    echo "$g $s $sum" >> "$OUT"
    printf '%-14s seed %s  %s  %6s ms  winner=%s\n' "$g" "$s" "$sum" "$ms" "$win"
  done
done
if [ ! -s "$OUT" ]; then echo "no game matches ran" >&2; exit 1; fi
if [ "${2:-}" = "--record" ]; then
  cp "$OUT" "$DIR/baseline.txt"
  echo "recorded $DIR/baseline.txt"
elif ! awk '{ sub(/\r$/, ""); print }' "$DIR/baseline.txt" | diff - "$OUT"; then
  echo "CHECKSUMS DIFFER"
  exit 1
else
  echo "all checksums identical"
fi
