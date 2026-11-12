#!/usr/bin/env python3
"""Adds the career ladders to a city project (rules "careers", docs/careers.md) and the script that feeds them.

    add_careers.py <project>

Additive and repeatable: careers that exist in the rules are kept as they are. The taxi is not a career: it is
the starting job and has no ladder.
"""
import json, os, sys

CAREERS = {
    "business": {"name": "Business", "open": True, "ranks": [
        {"name": "Self-employed", "points": 0},
        {"name": "Shop owner", "points": 30, "perks": ["company_name", "bank_trust"]},
        {"name": "Chain owner", "points": 120, "perks": ["bulk_supply"]},
        {"name": "Magnate", "points": 400, "perks": ["city_tenders"]}]},
    "police": {"name": "Police", "open": True, "office": "precinct", "ranks": [
        {"name": "Cadet", "points": 0},
        {"name": "Patrol officer", "points": 20, "perks": ["siren"]},
        {"name": "Sergeant", "points": 80, "perks": ["service_weapon"]},
        {"name": "Detective", "points": 250, "perks": ["records"]}]},
    "medic": {"name": "Ambulance", "open": True, "office": "clinic", "ranks": [
        {"name": "Orderly", "points": 0},
        {"name": "Paramedic", "points": 20, "perks": ["free_treatment"]},
        {"name": "Doctor", "points": 120, "perks": ["field_surgery"]}]},
    "fire": {"name": "Fire brigade", "open": True, "office": "fire_station", "ranks": [
        {"name": "Recruit", "points": 0},
        {"name": "Firefighter", "points": 20, "perks": ["fireproof"]},
        {"name": "Captain", "points": 120, "perks": ["brigade"]}]},
    "gang": {"name": "Gang", "ranks": [
        {"name": "Newcomer", "points": 0},
        {"name": "Trusted", "points": 15, "perks": ["hotwire"]},
        {"name": "Lieutenant", "points": 60, "perks": ["crew"]},
        {"name": "Boss", "points": 200, "perks": ["gang_name"]}]},
}
SCRIPT = "scripts/careers.lua"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = os.path.join(sys.argv[1], "rules.json")
    raw = open(path).read()
    rules = json.loads(raw)
    have = rules.setdefault("careers", {})
    for cid, c in CAREERS.items():
        have.setdefault(cid, c)
    if os.path.exists(os.path.join(sys.argv[1], SCRIPT)) and SCRIPT not in rules.setdefault("scripts", []):
        rules["scripts"].append(SCRIPT)
    indent = 1 if raw.startswith('{\n "') else 2 if raw.startswith('{\n  "') else None
    with open(path, "w") as f:
        f.write(json.dumps(rules, ensure_ascii=False, indent=indent))
    print("careers:", ", ".join(sorted(have)))


if __name__ == "__main__":
    main()
