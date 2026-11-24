#!/usr/bin/env python3
"""Preview and submit minimal, consented AIVERSE capability requests."""

import argparse
import json
import re
import subprocess
from pathlib import Path

REPO = "chromealex/AIVERSE"
FIELDS = {"title": 120, "need": 500, "gap": 500, "existing": 500, "evidence": 500, "area": 80}
REQUIRED = ("title", "need", "gap", "existing", "evidence")


def load_draft(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict) or set(data) - set(FIELDS):
        raise ValueError("draft must be an object with only supported fields")
    for key in REQUIRED:
        if key not in data:
            raise ValueError(f"missing {key}")
    for key, limit in FIELDS.items():
        if key not in data:
            continue
        value = data[key]
        if not isinstance(value, str) or not value.strip() or len(value) > limit:
            raise ValueError(f"{key} must be nonempty text of at most {limit} characters")
        if any(ord(char) < 32 and char not in "\n\t" for char in value):
            raise ValueError(f"{key} contains control characters")
        data[key] = value.strip()
    if "\n" in data["title"]:
        raise ValueError("title must be one line")
    return data


def body(data):
    sections = [
        ("Needed capability", data["need"]),
        ("Current gap", data["gap"]),
        ("What exists today", data["existing"]),
        ("Engine evidence", data["evidence"]),
    ]
    if data.get("area"):
        sections.append(("Area", data["area"]))
    return "\n\n".join(f"### {heading}\n{value}" for heading, value in sections) + "\n"


def run_gh(*args):
    return subprocess.run(["gh", *args], text=True, capture_output=True, check=True).stdout.strip()


def submit(data):
    # Exact normalized titles avoid accidentally merging distinct requests.
    existing = json.loads(run_gh("issue", "list", "--repo", REPO, "--state", "open", "--limit", "500", "--json", "title,url"))
    normalized = re.sub(r"\s+", " ", data["title"]).casefold()
    for issue in existing:
        if re.sub(r"\s+", " ", issue["title"]).casefold() == normalized:
            return "Existing issue: " + issue["url"]
    url = run_gh("issue", "create", "--repo", REPO, "--title", data["title"], "--body", body(data))
    return "Created issue: " + url


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("preview", "submit"))
    parser.add_argument("draft", type=Path)
    parser.add_argument("--consent", action="store_true", help="user explicitly approved publishing the previewed text")
    args = parser.parse_args()
    try:
        data = load_draft(args.draft)
        if args.action == "preview":
            print(f"Public destination: https://github.com/{REPO}/issues\n\nTitle: {data['title']}\n\n{body(data)}")
        elif not args.consent:
            parser.error("submit requires explicit user consent and --consent")
        else:
            print(submit(data))
    except (ValueError, json.JSONDecodeError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Capability request failed: {exc}\n")


if __name__ == "__main__":
    main()
