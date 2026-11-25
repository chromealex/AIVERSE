#!/usr/bin/env python3
"""Fail-closed native Caverns scenario driver. CTest owns the test inventory."""
import os
import pathlib
import subprocess
import sys

CASES = {
    "chamber": ([], []),
    "portal": (["--portal"], ["ENTERED", "EXITED"]),
    "swim": (["--swim"], []),
    "moving_restore": (["--moving-view", "--restore"], ["REBASE", "SAVED", "RESTORED", "REPLAY_MATCH"]),
    "portal_restore": (["--portal", "--restore"], ["ENTERED", "EXITED", "SAVED", "RESTORED", "REPLAY_MATCH"]),
    "swim_restore": (["--swim", "--restore"], ["SAVED", "RESTORED", "REPLAY_MATCH"]),
}

def main():
    executable, case, report = sys.argv[1:]
    args, extra = CASES[case]
    directory = pathlib.Path(report)
    directory.mkdir(parents=True, exist_ok=True)
    log = directory / (case + ".log")
    env = os.environ.copy()
    # Prevent a global screenshot/auto-exit setting from ending a smoke run
    # before its actual arrival and teardown checks.
    for name in ("AIVERSE_SCREENSHOT", "AIVERSE_EXIT_AFTER_FRAMES"):
        env.pop(name, None)
    command = [executable, *args, "--fixed-step", "--frames", "2400"]
    with log.open("w", encoding="utf-8") as out:
        out.write("COMMAND " + repr(command) + "\n")
        out.flush()
        try:
            result = subprocess.run(command, env=env, stdout=out, stderr=subprocess.STDOUT, timeout=780)
        except subprocess.TimeoutExpired:
            print(f"{case}: timed out; {log}", file=sys.stderr)
            return 1
    text = log.read_text(encoding="utf-8", errors="replace")
    print(text[-24000:])
    required = ["ARRIVED", "SHUTDOWN_OK", *extra]
    missing = [marker for marker in required if f"[caverns-demo] {marker}" not in text]
    if result.returncode or missing or "[caverns-demo] FAIL" in text:
        print(f"{case}: FAIL exit={result.returncode} missing={missing}; {log}", file=sys.stderr)
        return 1
    print(f"{case}: PASS; {log}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
