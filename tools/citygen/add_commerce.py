#!/usr/bin/env python3
"""Adds the peaceful economy to a city project (rules "commerce" + map "premises", docs/commerce.md).

    add_commerce.py <project> [--map maps/city.json]

Additive and repeatable: buildings already on the map become premises (a door on the nearest pavement, a price
by kind and by how central the spot is), the rules get the commerce block with the business types. Entries that
exist are kept as they are (prices somebody tuned by hand survive); premises whose building is gone are dropped.
"""
import argparse, json, math, os, sys

# building type name fragment -> premises kind
KINDS = [
    ("corner_shops", "shop"), ("neighbourhood_market", "shop"), ("arcade_shops", "shop"), ("department_store", "shop"),
    ("covered_market", "shop"), ("corner_diner", "cafe"), ("barrel_diner", "cafe"), ("roadhouse", "cafe"),
    ("office_tower", "office"), ("business_centre", "office"), ("twin_towers", "office"), ("telephone_exchange", "office"),
    ("loading_warehouse", "warehouse"), ("rooflight_warehouse", "warehouse"), ("small_hangar", "warehouse"),
    ("service_depot", "dealer"), ("column_bank", "bank"), ("precinct", "precinct"), ("clinic", "clinic"), ("fire_station", "fire_station"), ("maintenance_depot", "garage"), ("workshop_yard", "garage"),
    ("apartments", "flat"), ("brownstone", "flat"), ("townhouses", "flat"), ("mansard_house", "flat"),
    ("residence", "flat"), ("tenement", "flat"), ("garden_court", "flat"), ("lofts", "flat"), ("plaster_courtyard", "flat"),
]
BASE_PRICE = {"shop": 18000, "cafe": 24000, "office": 40000, "warehouse": 30000, "garage": 22000, "flat": 9000,
              "dealer": 1, "bank": 1, "gunshop": 1, "precinct": 1, "clinic": 1, "fire_station": 1}
RENT_SHARE = 0.0175          # weekly rent as a share of the price
PUBLIC = {"dealer", "bank", "gunshop", "precinct", "clinic", "fire_station"}   # run by the city: not for rent or sale
GUNSHOP_FROM, GUNSHOP_SPACING = "corner_shops", 70   # every so many cells one corner shop sells guns
COMMERCE = {
    "currency": "cash", "dayMinutes": 24, "weekDays": 7, "range": 3, "sellShare": 0.8,
    "managerWageWeek": 450, "unmanagedShare": 0.5, "storage": ["flat"],
    "bank": {"rateWeek": 0.02, "unsecured": 2000, "collateralShare": 0.6, "termWeeks": 20, "graceWeeks": 2},
    "businesses": {
        "newsstand": {"name": "Newsstand", "kinds": ["shop"], "setup": 1500, "incomeDay": 260, "costDay": 110, "ticket": 8},
        "grocery": {"name": "Grocery", "kinds": ["shop"], "setup": 4000, "incomeDay": 700, "costDay": 300, "staff": 2, "wageWeek": 180, "ticket": 20},
        "diner": {"name": "Diner", "kinds": ["cafe"], "setup": 9000, "incomeDay": 1300, "costDay": 560, "staff": 3, "wageWeek": 200, "ticket": 30},
        "agency": {"name": "Agency", "kinds": ["office"], "setup": 6000, "incomeDay": 1000, "costDay": 420, "staff": 2, "wageWeek": 260, "ticket": 0},
        "repair_shop": {"name": "Repair shop", "kinds": ["garage"], "setup": 12000, "incomeDay": 1700, "costDay": 700, "staff": 3, "wageWeek": 240, "ticket": 0},
        "delivery": {"name": "Delivery depot", "kinds": ["warehouse"], "setup": 8000, "incomeDay": 1150, "costDay": 520, "staff": 2, "wageWeek": 220, "ticket": 0},
    },
    "offers": {
        "gun_1_pistol": {"name": "Pistol, 24 rounds", "kinds": ["gunshop"], "price": 300, "weapon": "pistol", "ammo": 24},
        "gun_2_smg": {"name": "Machine gun, 60 rounds", "kinds": ["gunshop"], "price": 900, "weapon": "smg", "ammo": 60},
        "gun_3_shotgun": {"name": "Shotgun, 10 shells", "kinds": ["gunshop"], "price": 700, "weapon": "shotgun", "ammo": 10},
        "gun_4_rocket": {"name": "Rockets, 3", "kinds": ["gunshop"], "price": 2500, "weapon": "rocket", "ammo": 3},
        "car_1_hatchback": {"name": "Hatchback", "kinds": ["dealer"], "price": 4000, "vehicle": "hatchback"},
        "car_2_sedan": {"name": "Sedan", "kinds": ["dealer"], "price": 6000, "vehicle": "sedan"},
        "car_3_wagon": {"name": "Wagon", "kinds": ["dealer"], "price": 7000, "vehicle": "wagon"},
        "car_4_van": {"name": "Van", "kinds": ["dealer"], "price": 9000, "vehicle": "van"},
        "car_5_muscle": {"name": "Muscle car", "kinds": ["dealer"], "price": 15000, "vehicle": "muscle"},
        "car_6_limousine": {"name": "Limousine", "kinds": ["dealer"], "price": 30000, "vehicle": "limousine"},
    },
}


def dump(path, data, raw):
    indent = 1 if raw.startswith('{\n "') else 2 if raw.startswith('{\n  "') else None
    with open(path, "w") as f:
        f.write(json.dumps(data, ensure_ascii=False, indent=indent))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("--map", default="maps/city.json")
    ap.add_argument("--rebuild", action="store_true", help="recompute every premises entry and overwrite the rule numbers")
    a = ap.parse_args()
    rules_path, map_path = os.path.join(a.project, "rules.json"), os.path.join(a.project, a.map)
    rules_raw, map_raw = open(rules_path).read(), open(map_path).read()
    rules, m = json.loads(rules_raw), json.loads(map_raw)
    if "cash" not in rules.get("resources", {}):
        sys.exit("rules.resources has no 'cash'")

    if a.rebuild:
        rules["commerce"] = json.loads(json.dumps(COMMERCE))
    block = rules.setdefault("commerce", {})
    for k, v in COMMERCE.items():
        if k in ("businesses", "offers"):
            have = block.setdefault(k, {})
            for bid, b in v.items():
                have.setdefault(bid, b)
        else:
            block.setdefault(k, v)
    for o in list(block["offers"].values()):       # offers for things this project does not have are left out
        if "vehicle" in o and o["vehicle"] not in rules["types"]:
            block["offers"] = {k: v for k, v in block["offers"].items() if v is not o}
    weapons = {w.get("weapon") for w in rules.get("arsenal", {}).get("weapons", [])}
    block["offers"] = {k: v for k, v in block["offers"].items() if "weapon" not in v or v["weapon"] in weapons}

    W, H = m["size"]
    tiles, terrain = m["tiles"]["rows"], m["terrain"]["rows"]
    dt = m.get("city", {}).get("downtown")
    cx, cz = (dt["x"] + dt["w"] / 2, dt["z"] + dt["h"] / 2) if dt else (W / 2, H / 2)
    reach = math.hypot(max(cx, W - cx), max(cz, H - cz))

    def door(bx, bz):
        best = None
        for dz in range(-12, 13):
            for dx in range(-12, 13):
                x, z = int(bx) + dx, int(bz) + dz
                if not (1 <= x < W - 1 and 1 <= z < H - 1) or terrain[z][x] != "." or tiles[z][x] not in "pls":
                    continue
                d = dx * dx + dz * dz + {"p": 0, "l": 20, "s": 60}[tiles[z][x]]
                if best is None or d < best[0]:
                    best = (d, x, z)
        return best

    old = {} if a.rebuild else {p["id"]: p for p in m.get("premises", [])}
    out, counts, guns = [], {}, []
    for e in m["entities"]:
        kind = next((k for frag, k in KINDS if frag in e["type"]), None)
        if kind and GUNSHOP_FROM in e["type"] and "pos" in e and all(math.hypot(e["pos"][0] - gx, e["pos"][1] - gz) >= GUNSHOP_SPACING for gx, gz in guns):
            kind = "gunshop"
            guns.append(tuple(e["pos"]))
        if not kind or "id" not in e or "pos" not in e:
            continue
        pid = "prem_" + e["id"]
        if pid in old:
            out.append(old[pid])
            counts[kind] = counts.get(kind, 0) + 1
            continue
        spot = door(*e["pos"])
        if not spot:
            continue
        central = max(0.0, 1.0 - math.hypot(e["pos"][0] - cx, e["pos"][1] - cz) / reach)   # 1 in the centre, 0 at the far corner
        price = int(round(BASE_PRICE[kind] * (0.6 + 1.0 * central) / 100.0)) * 100
        entry = {"id": pid, "kind": kind, "building": e["id"], "at": [spot[1] + 0.5, spot[2] + 0.5], "price": max(1, price),
                 "rentWeek": int(round(price * RENT_SHARE / 5.0)) * 5, "traffic": round(0.6 + 1.0 * central, 2)}
        if kind in PUBLIC:
            entry.update({"public": True, "price": 1, "rentWeek": 0})
        out.append(entry)
        counts[kind] = counts.get(kind, 0) + 1
    m["premises"] = out
    dump(rules_path, rules, rules_raw)
    dump(map_path, m, map_raw)
    print("premises:", len(out), counts)


if __name__ == "__main__":
    main()
