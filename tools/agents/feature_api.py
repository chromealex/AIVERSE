"""Compare compiler-parsed public declarations in two explicit C++ checkouts.

This is a source-interface audit, not an ABI proof. Supply the same build flags
for both trees. Missing compiler/failed parse is an error, never an empty API.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from feature_policy import repo_path

FUNCTIONS = {"FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl",
             "CXXDestructorDecl", "CXXConversionDecl"}
RECORDS = {"CXXRecordDecl", "RecordDecl", "ClassTemplateSpecializationDecl"}
DECLARATIONS = FUNCTIONS | RECORDS | {"EnumDecl", "EnumConstantDecl", "FieldDecl",
                                    "TypeAliasDecl", "TypedefDecl", "VarDecl"}

def signatures(ast, header):
    """Extract header-local visible declaration names and types from Clang JSON."""
    rows = set()
    last_file = None
    def visit(node, scope=(), exposed=True):
        nonlocal last_file
        loc = node.get("loc", {})
        if "spellingLoc" in loc:
            loc = loc["spellingLoc"]
        if "file" in loc:
            last_file = Path(loc["file"]).resolve()
        local = last_file == header
        kind, name = node.get("kind"), node.get("name", "")
        implicit = node.get("isImplicit", False)
        if implicit:
            return
        visible = exposed and node.get("access") not in ("private", "protected")
        qual = "::".join((*scope, name))
        typ = node.get("type", {}).get("qualType", "")
        if local and visible and name and kind in DECLARATIONS:
            if kind not in RECORDS or node.get("completeDefinition"):
                extra = ""
                if kind in RECORDS:
                    extra = json.dumps([(b.get("access"), b.get("type", {}).get("qualType"),
                                         b.get("isVirtual", False)) for b in node.get("bases", [])])
                rows.add((kind, qual, typ, extra))
        if kind in FUNCTIONS:
            return
        child_scope = (*scope, name) if name and kind in RECORDS | {"NamespaceDecl", "EnumDecl"} else scope
        access = "private" if node.get("tagUsed") == "class" else "public"
        for child in node.get("inner", []):
            if child.get("kind") == "AccessSpecDecl":
                access = child.get("access", access)
                continue
            visit(child, child_scope, visible and (kind not in RECORDS or access == "public"))
    visit(ast)
    return rows

def extract(root, relative, compiler, flags):
    if not repo_path(relative):
        raise ValueError("header must be an exact relative path")
    header = (root / relative).resolve()
    if not header.is_relative_to(root.resolve()) or not header.is_file():
        raise ValueError(f"header unavailable: {relative}")
    command = [compiler, "-std=c++20", "-I", str(root.resolve()), *flags,
               "-x", "c++-header", "-fsyntax-only", "-Xclang", "-ast-dump=json", str(header)]
    done = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
    if done.returncode:
        raise ValueError(f"Clang failed for {relative}: {done.stderr[-4000:]}")
    result = signatures(json.loads(done.stdout), header)
    if not result:
        raise ValueError(f"no visible header declarations extracted: {relative}")
    return result

def compare(old_root, new_root, headers, compiler, flags):
    rows = []
    for header in sorted(set(headers)):
        old = extract(old_root, header, compiler, flags)
        new = extract(new_root, header, compiler, flags)
        rows.append({"header": header, "removed": sorted(old - new),
                     "added": sorted(new - old), "unchanged": len(old & new)})
    return {"headers": rows, "potential_breaking": any(row["removed"] for row in rows),
            "scope": "compiler-parsed declarations; templates, inline behavior and ABI need separate review"}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, required=True)
    parser.add_argument("--new", type=Path, required=True)
    parser.add_argument("--header", action="append", required=True)
    parser.add_argument("--clang", default="clang++")
    parser.add_argument("--flag", action="append", default=[])
    args = parser.parse_args()
    try:
        report = compare(args.old.resolve(), args.new.resolve(), args.header, args.clang, args.flag)
        print(json.dumps(report, indent=2))
        return int(report["potential_breaking"])
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as exc:
        print(f"API audit unavailable: {exc}", file=sys.stderr)
        return 2

if __name__ == "__main__":
    sys.exit(main())
