"""Reviewed per-capability coverage. Catalog hints never become evidence."""
import json
from pathlib import Path
from evidence import validate_report

LEDGER = "docs/evidence/capability-coverage.json"
SURFACES = ("native_api", "mcp", "studio", "skill", "graphs", "tests")
STATUSES = ("pass", "gap", "not_applicable")

def read_coverage(root, known, verify_git=True):
    """Return only structurally valid, current reviewed records and diagnostics."""
    path = root / LEDGER
    if not path.exists():
        return {}, []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or set(data) != {"schema", "records"} or type(data["schema"]) is not int or data["schema"] != 1 or not isinstance(data["records"], list):
            raise ValueError("invalid coverage ledger shape")
    except (OSError, ValueError) as exc:
        return {}, [str(exc)]
    valid, errors, seen, duplicates, reports = {}, [], set(), set(), {}
    for index, row in enumerate(data["records"]):
        label = f"coverage record {index}"
        try:
            if not isinstance(row, dict) or set(row) != {"capability", "surface", "status", "note", "reviewer", "report"}:
                raise ValueError("invalid record fields")
            if any(not isinstance(v, str) or not v.strip() for v in row.values()):
                raise ValueError("all record fields must be nonempty strings")
            key = (row["capability"], row["surface"])
            label = "/".join(key)
            if key in seen:
                duplicates.add(key)
                valid.pop(key, None)
                raise ValueError("duplicate coverage record")
            seen.add(key)
            if key[0] not in known or key[1] not in SURFACES:
                raise ValueError("unknown capability or surface")
            if row["status"] not in STATUSES:
                raise ValueError("unknown coverage status")
            relative = Path(row["report"])
            target = (root / relative).resolve()
            if relative.is_absolute() or ".." in relative.parts or relative.suffix != ".json" or not target.is_relative_to((root / "docs/evidence").resolve()):
                raise ValueError("report must be JSON under docs/evidence")
            if target not in reports:
                reports[target] = validate_report(json.loads(target.read_text(encoding="utf-8")), root, verify_git)
            if reports[target]:
                raise ValueError("; ".join(reports[target]))
            valid[key] = row
        except (OSError, ValueError, TypeError) as exc:
            errors.append(f"{label}: {exc}")
    for key in duplicates:
        valid.pop(key, None)
    return valid, errors

def apply_coverage(rows, investigations, records):
    """Keep unknown surfaces as investigations; emit implementation tasks only for reviewed gaps."""
    tasks = []
    by_id = {task["capability"]: task for task in investigations}
    for row in rows:
        ident = row["id"]
        row["coverage_evidence"] = {}
        for surface in SURFACES:
            record = records.get((ident, surface))
            if record is None:
                continue
            row["coverage"][surface] = record["status"]
            row["coverage_evidence"][surface] = {k: record[k] for k in ("note", "reviewer", "report")}
            if record["status"] == "gap":
                tasks.append({"id": f"implement:{ident}:{surface}", "owner": row["owner"],
                              "capability": ident, "surface": surface, "kind": "implement_gap",
                              "title": f"Complete {surface}: {row['title']}",
                              "evidence": record["report"], "note": record["note"],
                              "acceptance": ["Create or update the bounded feature manifest",
                                             "Implement the missing integration through its existing owner",
                                             "Verify behavior and replace the gap record with current evidence"]})
        unknown = sorted(s for s in SURFACES if row["coverage"][s] == "unknown")
        if unknown:
            tasks.append(dict(by_id[ident], surfaces=unknown))
    return sorted(tasks, key=lambda t: (t["kind"] != "implement_gap", t["id"]))
