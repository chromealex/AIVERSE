"""Record and validate test provenance. A digest is integrity, not trusted attestation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def git(root, *args):
    return subprocess.check_output(["git", *args], cwd=root, text=True).strip()

def validate_report(report, root, verify_git=False):
    if not isinstance(report, dict) or set(report) != {"sha", "argv", "exit_code", "log", "sha256"}:
        return ["invalid test report fields"]
    errors = []
    sha = report["sha"]
    if not isinstance(sha, str) or not re.fullmatch("[0-9a-f]{40}", sha):
        errors.append("invalid tested SHA")
    elif verify_git:
        try:
            if git(root, "rev-parse", sha + "^{commit}") != sha:
                errors.append("tested SHA is not a commit")
            # Evidence-only commits may follow a test, but relevant code may not change.
            changed = git(root, "diff", "--name-only", "--no-renames", sha, "HEAD").splitlines()
            stale = [p for p in changed if not p.startswith(("docs/features/", "docs/agent-handoffs/", "docs/evidence/"))]
            if stale:
                errors.append("tested tree differs from HEAD: " + ", ".join(stale[:10]))
            subprocess.run(["git", "merge-base", "--is-ancestor", sha, "HEAD"], cwd=root, check=True)
        except (OSError, subprocess.CalledProcessError):
            errors.append("tested SHA is not a reachable ancestor")
    argv = report["argv"]
    if not isinstance(argv, list) or not argv or not all(isinstance(x, str) and x.strip() for x in argv):
        errors.append("missing test command")
    if type(report["exit_code"]) is not int or report["exit_code"] != 0:
        errors.append("test did not exit successfully")
    log = report["log"]
    if not isinstance(log, str) or not log.startswith("docs/evidence/") or ".." in Path(log).parts:
        return errors + ["invalid test log path"]
    target = (root / log).resolve()
    if not target.is_relative_to(root.resolve()) or not target.is_file():
        return errors + ["missing test log"]
    if digest(target) != report["sha256"]:
        errors.append("test log digest mismatch")
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id", required=True)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", args.id):
        parser.error("invalid report id")
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("missing command")
    root = args.root.resolve()
    if git(root, "status", "--porcelain"):
        parser.error("test recording requires clean checkout")
    sha = git(root, "rev-parse", "HEAD")
    done = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if git(root, "rev-parse", "HEAD") != sha or git(root, "status", "--porcelain"):
        parser.error("test changed checkout; refusing to record provenance")
    folder = root / "docs/evidence"
    folder.mkdir(parents=True, exist_ok=True)
    log = folder / (args.id + ".log.txt")
    report = folder / (args.id + ".json")
    if log.exists() or report.exists():
        parser.error("report already exists; use a unique id")
    log.write_bytes(done.stdout)
    report.write_text(json.dumps(dict(sha=sha, argv=command, exit_code=done.returncode,
        log=log.relative_to(root).as_posix(), sha256=digest(log)), indent=2) + "\n", encoding="utf-8")
    print(report.relative_to(root))
    return done.returncode if done.returncode >= 0 else 1

if __name__ == "__main__":
    sys.exit(main())
