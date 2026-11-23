"""Check that mandatory agent documents remain routed and retain baseline sections."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
INDEX = "docs/agents/contracts/index.json"

def validate_contracts(root=ROOT):
    errors = []
    try:
        index = json.loads((root / INDEX).read_text(encoding="utf-8"))
        entry = (root / "AGENTS.md").read_text(encoding="utf-8")
        for name, headings in index["sections"].items():
            if name != "AGENTS.md" and f"]({name})" not in entry:
                errors.append(f"unrouted mandatory contract: {name}")
            file = (root / name).resolve()
            if not file.is_relative_to(root.resolve()) or not file.is_file():
                errors.append(f"missing contract: {name}")
                continue
            lines = set(file.read_text(encoding="utf-8").splitlines())
            errors.extend(f"{name}: missing baseline section {h}" for h in headings if h not in lines)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        errors.append(f"contract index: {exc}")
    return errors

if __name__ == "__main__":
    errors = validate_contracts()
    for error in errors:
        print(error, file=sys.stderr)
    print(f"Agent contract routing: {len(errors)} error(s)")
    sys.exit(int(bool(errors)))
