#!/usr/bin/env python3
"""AIVERSE terminal build/CTest dashboard. Python 3.8+, standard library only.

Runs real CMake/CTest commands, streams full output to timestamped logs and
renders a heartbeat while silent tests such as place are still running.
"""
import argparse
import collections
import datetime
import json
import os
import pathlib
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
START_RE = re.compile(r"^\s*Start\s+\d+:\s+(\S+)")
RESULT_RE = re.compile(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.{2,}\s+(.+)$")
BUILD_RE = re.compile(r"^\s*\[\s*(\d+)%\]")
# GCC/Clang, MSVC, CMake and failing CTest assertions. These counts are
# diagnostic OUTPUT LINES, not unique compiler diagnostics.
ERROR_RE = re.compile(r"(?:\bfatal error:|\berror:|\bCMake Error\b|\berror C\d+\b|\*\*\*Failed|^\s*FAIL(?:\s|:)|^\s*FAILED(?:\s|:)|^\s*The following tests FAILED:|\bundefined reference to\b)", re.I)
WARNING_RE = re.compile(r"(?:\bwarning:|\bwarning C\d+\b|\bCMake Warning\b)", re.I)
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
SOURCE_RE = re.compile(r"(\S+\.(?:c|cc|cpp|cxx|m|mm|h|hpp|hxx)):(\d+)(?::\d+)?:\s*(?:fatal )?(?:warning|error):\s*(.*)", re.I)


def short_issue(text):
    """Preserve location + message in the UI; full source path stays in log."""
    match = SOURCE_RE.search(text)
    if match:
        return "%s:%s: %s" % (pathlib.Path(match.group(1)).name, match.group(2), match.group(3))
    return text.strip()


def elapsed(seconds):
    n = int(max(0, seconds))
    return "%02d:%02d:%02d" % (n // 3600, (n // 60) % 60, n % 60)


def trim(value, width):
    return value if len(value) <= width else value[:max(0, width - 1)] + "…"


def options(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mode", choices=("all", "configure", "build", "test"), default="all")
    p.add_argument("--source", default=str(ROOT), help="CMake source directory")
    p.add_argument("--build-dir", default="build/aiverse-progress", help="relative to --source")
    p.add_argument("--jobs", type=int, default=4, help="parallel build/test jobs")
    p.add_argument("--build-type", choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"))
    p.add_argument("--define", action="append", default=[], metavar="NAME=VALUE")
    p.add_argument("--target", action="append", default=[], help="CMake target (repeatable)")
    p.add_argument("--test-regex", help="CTest -R filter")
    p.add_argument("--test-exclude", help="CTest -E filter")
    p.add_argument("--verbose-tests", action="store_true", help="CTest -V: stream per-test output")
    p.add_argument("--plain", action="store_true", help="no ANSI dashboard; heartbeat in logs/CI")
    p.add_argument("--dry-run", action="store_true", help="show commands without running")
    a = p.parse_args(argv)
    if a.jobs < 1:
        p.error("--jobs must be >= 1")
    for definition in a.define:
        if "=" not in definition or not definition.split("=", 1)[0]:
            p.error("--define must be NAME=VALUE")
    a.source = pathlib.Path(a.source).expanduser().resolve()
    a.build_dir = pathlib.Path(a.build_dir).expanduser()
    if not a.build_dir.is_absolute():
        a.build_dir = a.source / a.build_dir
    a.build_dir = a.build_dir.resolve()
    return a


class Dashboard:
    def __init__(self, args, report):
        self.args, self.report = args, report
        self.tty = sys.stdout.isatty() and not args.plain and os.environ.get("TERM", "dumb") != "dumb"
        self.start = time.monotonic()
        self.stage_start = self.start
        self.stage_name = "initializing"
        self.total_tests = None
        self.completed = 0
        self.failed_tests = 0
        self.active = {}
        self.tail = collections.deque(maxlen=9)
        self.warning_count = 0
        self.error_count = 0
        self.issues = collections.deque(maxlen=6)
        self.recent_errors = collections.deque(maxlen=6)
        self.recent_warnings = collections.deque(maxlen=6)
        self.diagnostics_path = report / "diagnostics.log"
        self.diagnostic_stream = self.diagnostics_path.open("w", encoding="utf-8")
        self.build_progress = None
        self.last_output = self.start
        self.proc = None
        self._screen = False
        self._last_draw = 0.0
        if self.tty:
            sys.stdout.write("\033[?1049h\033[?25l")  # alternate buffer, cursor hidden
            sys.stdout.flush()
            self._screen = True

    def close(self):
        self.diagnostic_stream.flush()
        self.diagnostic_stream.close()
        if self._screen:
            sys.stdout.write("\033[?25h\033[?1049l")
            sys.stdout.flush()
            self._screen = False

    def consume(self, line):
        now = time.monotonic()
        self.last_output = now
        line = line.rstrip("\r\n")
        if not line:
            return
        self.tail.append(line)
        m = START_RE.match(line)
        if self.stage_name == "ctest" and m:
            self.active[m.group(1)] = now
        m = RESULT_RE.match(line)
        if self.stage_name == "ctest" and m:
            name, result = m.groups()
            self.active.pop(name, None)
            self.completed += 1
            if "Passed" not in result:
                self.failed_tests += 1
        m = BUILD_RE.match(line)
        if self.stage_name == "build" and m:
            self.build_progress = int(m.group(1))
        # The dashboard retains the latest issues across configure/build/test;
        # a warning must not disappear merely because the next stage starts.
        plain = ANSI_RE.sub("", line)
        severity = "ERROR" if ERROR_RE.search(plain) else (
            "WARN" if WARNING_RE.search(plain) else None)
        if severity:
            if severity == "ERROR":
                self.error_count += 1
            else:
                self.warning_count += 1
            issue = short_issue(plain)
            self.issues.append((severity, issue))
            (self.recent_errors if severity == "ERROR" else self.recent_warnings).append(issue)
            stamp = datetime.datetime.now().strftime("%H:%M:%S")
            self.diagnostic_stream.write("[%s] %s [%s] %s\n" %
                                         (severity, stamp, self.stage_name, plain))
            self.diagnostic_stream.flush()

    def draw(self, force=False):
        now = time.monotonic()
        if not self.tty or (not force and now - self._last_draw < .3):
            return
        self._last_draw = now
        cols = max(64, shutil.get_terminal_size((100, 24)).columns)
        width = min(cols - 2, 106)
        rules = "─" * width
        lines = ["AIVERSE  //  BUILD & TEST MONITOR", rules,
                 "Stage: %-10s  elapsed %s   total %s" %
                 (self.stage_name, elapsed(now - self.stage_start), elapsed(now - self.start)),
                 "Build: %s" % str(self.args.build_dir),
                 "Reports: %s" % str(self.report), rules]
        if self.stage_name == "ctest":
            max_tests = str(self.total_tests) if self.total_tests is not None else "?"
            lines.append("Tests: %s/%s finished, %s failed | running: %s" %
                         (self.completed, max_tests, self.failed_tests, len(self.active)))
            if self.total_tests:
                fill = int(min(1, self.completed / self.total_tests) * 28)
                lines.append("[" + "█" * fill + "░" * (28 - fill) + "] %3d%%" %
                             (100 * self.completed // self.total_tests))
            for name, started in sorted(self.active.items(), key=lambda x: x[1])[:5]:
                lines.append("  ● %s  active %s" % (name, elapsed(now - started)))
        elif self.stage_name == "build":
            lines.append("Latest CMake target progress: %s (per target, not whole build)" %
                         ((str(self.build_progress) + "%") if self.build_progress is not None else "waiting"))
        else:
            lines.append("Configuring CMake / preparing source and cache")
        lines.append("Warnings: %s   Errors: %s   Quiet for: %s" %
                     (self.warning_count, self.error_count, elapsed(now - self.last_output)))
        lines += [rules, "WARNINGS / ERRORS   (most recent, full list in diagnostics.log)"]
        terminal_rows = shutil.get_terminal_size((100, 24)).lines
        available = max(12, terminal_rows - 1)
        issue_slots = max(1, min(5, available - len(lines) - 4))
        # Errors stay visible even if many unrelated warnings arrive later.
        # Reserve room for both severities whenever the terminal allows it.
        errors = list(self.recent_errors)[-min(3, issue_slots):]
        warnings = list(self.recent_warnings)[-max(0, issue_slots - len(errors)):] if issue_slots > len(errors) else []
        if not warnings and len(errors) < issue_slots:
            errors = list(self.recent_errors)[-issue_slots:]
        for issue in errors:
            lines.append("! ERROR  " + issue)
        for issue in warnings:
            lines.append("! WARN   " + issue)
        if not (errors or warnings):
            lines.append("  No warnings or errors detected so far")
        lines += [rules, "Recent output:"]
        # Keep the warning/error panel visible on short terminal windows;
        # only the less important recent-output list can be truncated.
        recent_slots = max(0, available - len(lines) - 2)
        for item in list(self.tail)[-recent_slots:] if recent_slots else []:
            lines.append("  " + item)
        lines += [rules, "Ctrl+C stops work; all diagnostics and raw logs are saved."]
        visible = []
        for line in lines[:available]:
            clipped = trim(line, width)
            if line.startswith("! ERROR"):
                clipped = "\033[31m" + clipped + "\033[0m"
            elif line.startswith("! WARN"):
                clipped = "\033[33m" + clipped + "\033[0m"
            visible.append(clipped)
        sys.stdout.write("\033[H\033[2J" + "\n".join(visible) + "\n")
        sys.stdout.flush()

    def run(self, stage, command):
        self.stage_name, self.stage_start = stage, time.monotonic()
        self.active.clear()
        self.completed = 0
        self.failed_tests = 0
        self.build_progress = None
        self.tail.clear()
        # Keep cross-stage diagnostic counts and the pinned list intact.
        self.last_output = self.stage_start
        path = self.report / (stage + ".log")
        if self.args.dry_run:
            print("[dry-run]", subprocess.list2cmdline(command))
            return 0
        received = queue.Queue()
        with path.open("w", encoding="utf-8") as log:
            self.proc = subprocess.Popen(command, cwd=str(self.args.source),
                                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         text=True, encoding="utf-8", errors="replace",
                                         bufsize=1, start_new_session=(os.name == "posix"))

            def reader():
                try:
                    for line in self.proc.stdout:
                        received.put(line)
                finally:
                    received.put(None)

            thread = threading.Thread(target=reader, daemon=True)
            thread.start()
            done = False
            last_heartbeat = time.monotonic()
            while not done:
                try:
                    line = received.get(timeout=.2)
                except queue.Empty:
                    line = ""  # heartbeat, not end-of-stream
                if line is None:
                    done = True
                elif line:
                    log.write(line)
                    log.flush()
                    self.consume(line)
                    if not self.tty:
                        print(line, end="", flush=True)
                self.draw()
                if not self.tty and time.monotonic() - last_heartbeat >= 15:
                    active = ", ".join("%s (%s)" % (name, elapsed(time.monotonic()-start))
                                       for name, start in self.active.items())
                    print("[alive] %s elapsed %s; %s" %
                          (stage, elapsed(time.monotonic()-self.stage_start), active or "still running"),
                          flush=True)
                    last_heartbeat = time.monotonic()
            thread.join(timeout=2)
            result = self.proc.wait()
            self.proc = None
        self.draw(force=True)
        return result

    def interrupt(self):
        if self.proc is None or self.proc.poll() is not None:
            return
        try:
            if os.name == "posix":
                os.killpg(self.proc.pid, signal.SIGTERM)
            else:
                self.proc.terminate()
        except ProcessLookupError:
            pass


def main(argv=None):
    args = options(argv)
    if not (args.source / "CMakeLists.txt").is_file():
        print("Cannot find CMakeLists.txt in", args.source, file=sys.stderr)
        return 2
    for exe in ("cmake", "ctest"):
        if not shutil.which(exe):
            print("Missing executable:", exe, file=sys.stderr)
            return 2
    report = args.build_dir / "aiverse-reports" / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-%s" % os.getpid())
    report.mkdir(parents=True, exist_ok=True)
    stages = []
    if args.mode in ("all", "configure"):
        cmd = ["cmake", "-S", str(args.source), "-B", str(args.build_dir), "-DAIVERSE_BUILD_TESTS=ON"]
        if args.build_type:
            cmd.append("-DCMAKE_BUILD_TYPE=" + args.build_type)
        cmd += ["-D" + x for x in args.define]
        stages.append(("configure", cmd))
    if args.mode in ("all", "build"):
        cmd = ["cmake", "--build", str(args.build_dir), "--parallel", str(args.jobs)]
        if args.target:
            cmd += ["--target"] + args.target
        stages.append(("build", cmd))
    if args.mode in ("all", "test"):
        cmd = ["ctest", "--test-dir", str(args.build_dir), "--output-on-failure", "--no-tests=error",
               "--parallel", str(args.jobs)]
        if args.test_regex:
            cmd += ["-R", args.test_regex]
        if args.test_exclude:
            cmd += ["-E", args.test_exclude]
        if args.verbose_tests:
            cmd += ["--verbose"]
        stages.append(("ctest", cmd))
    summary = {"build_dir": str(args.build_dir), "mode": args.mode, "commands": stages,
               "started_at": datetime.datetime.now().isoformat()}
    dashboard = Dashboard(args, report)
    rc = 0
    try:
        for stage, command in stages:
            warnings_before, errors_before = dashboard.warning_count, dashboard.error_count
            if stage == "ctest" and not args.dry_run:
                probe = ["ctest", "--test-dir", str(args.build_dir), "--show-only=json-v1"]
                if args.test_regex:
                    probe += ["-R", args.test_regex]
                if args.test_exclude:
                    probe += ["-E", args.test_exclude]
                result = subprocess.run(probe, capture_output=True, text=True, check=False)
                if result.returncode == 0:
                    try:
                        dashboard.total_tests = len(json.loads(result.stdout).get("tests", []))
                    except (ValueError, TypeError):
                        dashboard.total_tests = None
            rc = dashboard.run(stage, command)
            summary[stage] = {"exit_code": rc, "completed": dashboard.completed,
                              "test_failures": dashboard.failed_tests,
                              "warnings": dashboard.warning_count - warnings_before,
                              "errors": dashboard.error_count - errors_before}
            if rc:
                break
    except KeyboardInterrupt:
        dashboard.interrupt()
        rc = 130
    finally:
        dashboard.close()
        summary["exit_code"] = rc
        summary["diagnostics"] = {"warnings": dashboard.warning_count,
                                  "errors": dashboard.error_count,
                                  "file": str(dashboard.diagnostics_path)}
        summary["elapsed_seconds"] = round(time.monotonic() - dashboard.start, 2)
        (report / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print("AIVERSE:", "PASS" if rc == 0 else "FAIL (exit %s)" % rc)
    print("Elapsed:", elapsed(time.monotonic() - dashboard.start))
    print("Diagnostics: %s warnings, %s error lines" %
          (dashboard.warning_count, dashboard.error_count))
    if dashboard.issues:
        for level, issue in list(dashboard.issues)[-5:]:
            print("  [%s] %s" % (level, issue))
    print("Diagnostic log:", dashboard.diagnostics_path)
    print("Logs:", report)
    return rc


if __name__ == "__main__":
    sys.exit(main())
