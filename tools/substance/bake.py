#!/usr/bin/env python3
"""Offline SBSAR bake. Requires Adobe Substance Automation Toolkit."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def bake(source, cache, renderer, size, parameters, memory_mb, cores):
    source = Path(source).resolve(strict=True)
    executable = shutil.which(renderer)
    if not executable:
        raise ValueError("sbsrender unavailable: install Adobe SAT or pass --renderer")
    version = subprocess.check_output([executable, "--version"], text=True).strip()
    identity = {"schema": 1, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "renderer": version, "size": size, "parameters": parameters}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    cache = Path(cache).resolve()
    cache.mkdir(parents=True, exist_ok=True)
    destination = cache / key
    manifest_path = destination / "bake.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        if all((destination / name).is_file() and
               hashlib.sha256((destination / name).read_bytes()).hexdigest() == digest
               for name, digest in manifest["outputs"].items()) and manifest["outputs"]:
            return destination
        raise ValueError("Cached bake is damaged; remove its directory and retry")
    if destination.exists():
        raise ValueError("Incomplete cache entry; remove its directory and retry")
    with tempfile.TemporaryDirectory(prefix=".bake-", dir=cache) as temporary:
        staging = Path(temporary)
        command = [executable, "render", "--input", str(source),
                   "--output-path", str(staging), "--output-format", "png",
                   "--output-name", "{inputGraphUrl}_{outputNodeName}",
                   "--memory-budget", str(memory_mb), "--cpu-count", str(cores),
                   "--set-value", f"$outputsize@{size.bit_length()-1},{size.bit_length()-1}"]
        for name, value in sorted(parameters.items()):
            command.extend(["--set-value", f"{name}@{value}"])
        subprocess.run(command, check=True, timeout=600)
        outputs = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in staging.glob("*.png") if p.stat().st_size}
        if not outputs:
            raise ValueError("Substance produced no PNG outputs")
        (staging / "bake.json").write_text(json.dumps(
            {**identity, "outputs": outputs}, indent=2) + "\n")
        staging.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--renderer", default="sbsrender")
    parser.add_argument("--size", type=int, choices=[512, 1024, 2048, 4096], default=1024)
    parser.add_argument("--parameters", type=Path, help="JSON object of numeric input values")
    parser.add_argument("--memory-mb", type=int, default=1024)
    parser.add_argument("--cores", type=int, default=2)
    args = parser.parse_args()
    try:
        parameters = json.loads(args.parameters.read_text()) if args.parameters else {}
        if not isinstance(parameters, dict) or any(
                not isinstance(k, str) or not k or k.startswith("$") or
                not isinstance(v, (int, float)) or isinstance(v, bool)
                for k, v in parameters.items()):
            raise ValueError("Parameters must map input identifiers to numeric values")
        json.dumps(parameters, allow_nan=False)
        if args.memory_mb < 1 or args.cores < 1:
            raise ValueError("Memory and core budgets must be positive")
        print(bake(args.source, args.cache, args.renderer, args.size,
                   parameters, args.memory_mb, args.cores))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Bake failed: {error}\n")


if __name__ == "__main__":
    main()
