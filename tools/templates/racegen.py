#!/usr/bin/env python3
"""Generate an oval race track for a metre map with box colliders (docs/races.md: rules "race" + map "race").

Writes into --out (default: templates/racing next to this script):
  rules/race.json   the car (fps.vehicleTypes: one wheeled car with a driver seat), the race knobs (rules "race") and
                    the road type of the track surface (rules "roadTypes", cellMeters 1)
  maps/start.json   ground, inner / outer walls (straights as boxes, bends as rotated boxes), one car per racer, the
                    track surface as a spline road (map "roadNetwork") and the race (map "race": checkpoints, grid, line)
Units: metres in the map, milli-units in the vehicle-controllers JSON. Track: two straights along Z joined by
half circles; racers drive up the right straight (+Z) first; the finish line lies on the right straight.

  python3 tools/templates/racegen.py [--out DIR] [--racers 4] [--bots 3] [--straight 60] [--radius 30]
                                      [--width 14] [--laps 3]
"""
import argparse, json, math, os
import os
# Sample games live in the package catalog: these generators edit the installed copies
# (tools/pkg/fetch_templates.py, AIVERSE_TEMPLATES_DIR); republish the package afterwards.
import sys as _sys
_sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pkg'))
from fetch_templates import default_dir as _templates_dir  # noqa: E402

def milli(v): return int(round(v * 1000))
def yaw_turns(dx, dz): return math.atan2(dx, dz) / (2 * math.pi)

def centerline(L, R, step):
    """Closed loop of points (x, z) along the track centre, starting on the right straight at z=-L."""
    pts = []
    n = max(2, int(round(2 * L / step)))
    for i in range(n): pts.append((R, -L + 2 * L * i / n))                       # right straight, +Z
    k = max(6, int(round(math.pi * R / step)))
    for i in range(k): a = math.pi * i / k; pts.append((R * math.cos(a), L + R * math.sin(a)))      # top bend
    for i in range(n): pts.append((-R, L - 2 * L * i / n))                       # left straight, -Z
    for i in range(k): a = math.pi + math.pi * i / k; pts.append((R * math.cos(a), -L + R * math.sin(a)))  # bottom bend
    return pts

def walls(L, R, W, height=1.2, thick=0.6, seg_deg=10):
    out, ident = [], [100]
    def box(cx, cz, hx, hz, yaw=None):
        ident[0] += 1
        if yaw is None:
            out.append({"id": ident[0], "min": [round(cx - hx, 3), 0, round(cz - hz, 3)], "max": [round(cx + hx, 3), height, round(cz + hz, 3)]})
        else:
            out.append({"id": ident[0], "center": [round(cx, 3), height / 2, round(cz, 3)], "half": [thick / 2, height / 2, round(hz, 3)], "yaw": round(yaw, 6)})
    for r in (R - W / 2 - thick / 2, R + W / 2 + thick / 2):
        for side in (1, -1): box(side * r, 0, thick / 2, L)                       # straights
        segs = int(180 / seg_deg)
        for c in (L, -L):
            for i in range(segs):
                a0 = math.radians(i * seg_deg) + (0 if c > 0 else math.pi)
                a1 = a0 + math.radians(seg_deg); am = (a0 + a1) / 2
                cx, cz = r * math.cos(am), c + r * math.sin(am)
                tx, tz = -math.sin(am), math.cos(am)
                half_len = r * math.sin(math.radians(seg_deg) / 2) + thick / 2   # overlap neighbours slightly
                box(cx, cz, thick / 2, half_len, yaw_turns(tx, tz))
    return out

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(str(_templates_dir()), "racing"))
    ap.add_argument("--racers", type=int, default=4); ap.add_argument("--bots", type=int, default=3)
    ap.add_argument("--straight", type=float, default=60.0, help="half length of a straight (m)")
    ap.add_argument("--radius", type=float, default=30.0, help="bend radius of the centre line (m)")
    ap.add_argument("--width", type=float, default=14.0, help="track width (m)")
    ap.add_argument("--laps", type=int, default=3)
    a = ap.parse_args()
    L, R, W, n = a.straight, a.radius, a.width, a.racers
    if not (1 <= n <= 8) or not (0 <= a.bots <= n) or W >= 2 * R - 4 or W < 8: raise SystemExit("racegen: invalid racers / bots / width")
    gate = W / 2 + 1.0
    # Checkpoints (gate centre, crossing direction): right straight, top, left straight, bottom, finish line last.
    cps = [((R, L / 3), (0, 1)), ((0, L + R), (-1, 0)), ((-R, 0), (0, -1)), ((0, -L - R), (1, 0)), ((R, -L / 3), (0, 1))]
    r3 = lambda v: round(v, 3)
    checkpoints = [{"x": r3(x), "z": r3(z), "yaw": r3(yaw_turns(nx, nz)), "width": r3(2 * gate)} for (x, z), (nx, nz) in cps]
    # Grid: two columns just after the finish line, 7 m between rows.
    finish_z = -L / 3
    grid = [{"x": r3(R + (-2.2 if i % 2 == 0 else 2.2)), "z": r3(finish_z + 6 + 7 * (n - 1 - i)), "yaw": 0} for i in range(n)]
    line = [[r3(x), r3(z)] for x, z in centerline(L, R, 5.0)]
    car = {
        "id": 1, "kind": "wheeled",
        "wheeled": {"massMilli": 1100000, "torque": [[900, 190], [4500, 290], [7000, 230]], "idleRpm": 900, "redlineRpm": 7000,
                    "shiftUpRpm": 6300, "shiftDownRpm": 2400, "gearsMilli": [3200, 2200, 1600, 1250, 1000], "finalDriveMilli": 3700,
                    "automatic": True, "shiftTicks": 10, "frictionMilli": 1150, "maxSteerMilli": 100,
                    "lateralGrip": [[0, 0], [20, 1000], [80, 850]], "longitudinalGrip": [[1000, 1000], [2000, 800]],
                    "wheels": [{"offsetMilli": [-800, 0, 1300], "steered": True}, {"offsetMilli": [800, 0, 1300], "steered": True},
                               {"offsetMilli": [-800, 0, -1300], "driven": True, "handbrake": True},
                               {"offsetMilli": [800, 0, -1300], "driven": True, "handbrake": True}]},
        "seats": [{"id": 1, "role": "driver", "entryMilli": [-400, 0, 200], "enterRadiusMilli": 1500, "exitsMilli": [[-2000, 0, 200], [2000, 0, 200]]}],
    }
    lane = round((W - 2) / 2, 3)
    race = {"fps": {"vehicleTypes": {"vehicles": [car]}},
            "roadTypes": {"cellMeters": 1, "oval": {"name": "Oval", "category": "highway", "surface": "asphalt", "speed": 200,
                          "centre": "dashed", "laneLines": False, "crossings": False, "lanes": {"forward": 1, "backward": 1, "width": lane},
                          "props": [], "sides": {"curb": "low", "sidewalk": 0, "shoulder": 1, "shoulderSurface": "grass", "barrier": "none"}}},
            "race": {"countdownTicks": 180, "finishGraceTicks": 900,
                     "vehicles": {"wrongWayTicks": 60, "respawnWrongWayTicks": 240, "stuckSpeedMilli": 500, "stuckTicks": 300,
                                  "botMaxSpeedMilli": 38000, "botCornerAccelMilli": 8400, "botBrakeDecelMilli": 8000,
                                  "botLaneOffsetMilli": 2500, "botOvertakeDistanceMilli": 14000, "botSkillMilli": 1000, "botRubberBandMilli": 40}}}
    ground = {"id": 1, "min": [round(-R - W - 20, 3), -2, round(-L - R - W - 20, 3)], "max": [round(R + W + 20, 3), 0, round(L + R + W + 20, 3)]}
    players = []
    for i in range(n):
        p = {"name": "Bot %d" % (i + 1) if i >= n - a.bots else "Player %d" % (i + 1), "team": i + 1}
        if i >= n - a.bots: p["bot"] = True
        players.append(p)
    spawns = [[round(-6 + 4 * i, 3), 0.01, 0] for i in range(n)]                  # infield; racers start in their cars
    vehicles = [{"pos": [g["x"], 0.05, g["z"]], "type": 1} for g in grid]
    # The track surface: a spline road along the centre line (map "roadNetwork", the shared road view of every
    # client; road type "oval" in rules.json, cellMeters 1 on this metre map). Quarter arcs: cubic Beziers, k = 0.5523.
    k = 0.5523 * R
    pts = [(R, -L), (R, L), (0, L + R), (-R, L), (-R, -L), (0, -L - R)]
    nodes = [{"id": i + 1, "x": r3(x), "z": r3(z)} for i, (x, z) in enumerate(pts)]
    ctrl = [None, ((R, L + k), (k, L + R)), ((-k, L + R), (-R, L + k)), None, ((-R, -L - k), (-k, -L - R)), ((k, -L - R), (R, -L - k))]
    edges = []
    for i in range(6):
        e = {"id": i + 1, "a": i + 1, "b": (i + 1) % 6 + 1, "type": "oval"}
        if ctrl[i]:
            (c1x, c1z), (c2x, c2z) = ctrl[i]
            e["c1"], e["c2"] = [r3(c1x), r3(c1z), 0], [r3(c2x), r3(c2z), 0]
        edges.append(e)
    world = {"players": players, "spawns": spawns, "colliders": [ground] + walls(L, R, W), "vehicles": vehicles, "name": "Oval",
             "roadNetwork": {"nodes": nodes, "edges": edges, "signals": False},
             "race": {"laps": a.laps, "checkpoints": checkpoints, "grid": grid, "line": line}}
    os.makedirs(os.path.join(a.out, "rules"), exist_ok=True); os.makedirs(os.path.join(a.out, "maps"), exist_ok=True)
    with open(os.path.join(a.out, "rules", "race.json"), "w") as f: json.dump(race, f, indent=1); f.write("\n")
    with open(os.path.join(a.out, "maps", "start.json"), "w") as f: json.dump(world, f, indent=1); f.write("\n")
    print("racegen: %d racers (%d bots), %d checkpoints, %d line points, %d walls -> %s" % (n, a.bots, len(checkpoints), len(line), len(world["colliders"]) - 1, a.out))

if __name__ == "__main__":
    main()
