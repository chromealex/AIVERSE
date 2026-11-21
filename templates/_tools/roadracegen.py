#!/usr/bin/env python3
"""Generate a road circuit for a grid map (docs/races.md + docs/roads.md): a closed spline road (map "roadNetwork",
one-way edges in the driving direction), the race on it (map "race": checkpoints, grid, racing line sampled from the
same splines), one car per racer and the player's driver. Writes maps/start.json into --out (default:
templates/road_race next to this script); the rules and models of that sample are hand-written.

  python3 templates/_tools/roadracegen.py [--out DIR] [--racers 4] [--laps 3]

Units: map cells (rules driving.metersPerCell = 4 m). The circuit runs through the control points below in order
(Catmull-Rom, converted to one cubic Bezier edge per pair of points).
"""
import argparse, json, math, os

# Control points (cells): a long straight, a hairpin, sweeping bends and a chicane.
POINTS = [(14, 40), (30, 42), (46, 42), (58, 38), (64, 28), (60, 18), (50, 14), (40, 18), (34, 24),
          (26, 22), (20, 12), (12, 10), (7, 18), (8, 30)]
ROAD_WIDTH = 2.6   # cells: two 4.5 m lanes + edges, centred on the spline (road type "circuit" in rules.json)
SIZE = (72, 52)


def catmull_bezier(p):
    n = len(p)
    edges = []
    for i in range(n):
        p0, p1, p2, p3 = p[i - 1], p[i], p[(i + 1) % n], p[(i + 2) % n]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        edges.append((p1, c1, c2, p2))
    return edges


def bez(e, t):
    a, b, c, d = e
    u = 1 - t
    return tuple(u * u * u * a[k] + 3 * u * u * t * b[k] + 3 * u * t * t * c[k] + t * t * t * d[k] for k in range(2))


def sample(edges, step):
    """Points about `step` cells apart along the closed circuit, starting at the first control point."""
    out = []
    for e in edges:
        fine = [bez(e, i / 200) for i in range(201)]
        length = sum(math.dist(fine[i], fine[i + 1]) for i in range(200))
        n = max(2, int(round(length / step)))
        acc, j, seg = 0.0, 0, [fine[0]]
        target = length / n
        for i in range(200):
            d = math.dist(fine[i], fine[i + 1])
            while acc + d >= target * len(seg) and len(seg) < n:
                t = (target * len(seg) - acc) / d if d else 0
                seg.append((fine[i][0] + (fine[i + 1][0] - fine[i][0]) * t, fine[i][1] + (fine[i + 1][1] - fine[i][1]) * t))
            acc += d
        out.extend(seg)
    return out


def yaw_turns(dx, dz):
    return math.atan2(dx, dz) / (2 * math.pi)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(here, "..", "road_race"))
    ap.add_argument("--racers", type=int, default=4)
    ap.add_argument("--laps", type=int, default=3)
    a = ap.parse_args()
    n = a.racers
    if not 1 <= n <= 4:
        raise SystemExit("roadracegen: 1..4 racers (one car model per colour)")
    edges = catmull_bezier(POINTS)
    line = sample(edges, 1.0)                      # ~4 m apart
    r3 = lambda v: round(v, 3)
    nodes = [{"id": i + 1, "x": r3(p[0]), "z": r3(p[1])} for i, p in enumerate(POINTS)]
    road = [{"id": i + 1, "a": i + 1, "b": (i + 1) % len(POINTS) + 1, "type": "circuit",
             "c1": [r3(e[1][0]), r3(e[1][1]), 0], "c2": [r3(e[2][0]), r3(e[2][1]), 0]} for i, e in enumerate(edges)]

    def tangent(i):
        p, q = line[(i - 1) % len(line)], line[(i + 1) % len(line)]
        return q[0] - p[0], q[1] - p[1]

    # Gates every ~1/8 of the lap, the finish line (index 0, the start of the straight) last.
    gates = [len(line) * k // 8 for k in range(1, 8)] + [0]
    checkpoints = []
    for i in gates:
        dx, dz = tangent(i)
        checkpoints.append({"x": r3(line[i][0]), "z": r3(line[i][1]), "yaw": r3(yaw_turns(dx, dz)), "width": r3(ROAD_WIDTH + 1.5)})
    # Grid just after the finish line: two columns, the pole (racer 1) furthest ahead.
    grid, cars = [], []
    for k in range(n):
        idx = 3 + 2 * (n - 1 - k)                  # samples after the finish line
        dx, dz = tangent(idx)
        L = math.hypot(dx, dz)
        nx, nz = dz / L, -dx / L                   # one side of the road
        side = 0.55 if k % 2 == 0 else -0.55
        x, z = line[idx][0] + nx * side, line[idx][1] + nz * side
        grid.append({"x": r3(x), "z": r3(z), "yaw": r3(yaw_turns(dx, dz))})
        cars.append({"type": ["truck_red", "truck_green", "truck_yellow", "truck_purple"][k], "owner": k + 1, "pos": [r3(x), r3(z)]})
    players = [{"name": "You", "team": 1}] + [{"name": "Bot %d" % k, "team": k + 1, "bot": True} for k in range(1, n)]
    rows = ["." * SIZE[0] for _ in range(SIZE[1])]
    hero = {"type": "driver", "owner": 1, "pos": [r3(grid[0]["x"] - 2.5), r3(grid[0]["z"])]}
    world = {"name": "Road Circuit", "size": list(SIZE), "players": players, "terrain": {"rows": rows},
             "entities": [hero] + cars,
             "roadNetwork": {"nodes": nodes, "edges": road, "signals": False},
             "race": {"laps": a.laps, "checkpoints": checkpoints, "grid": grid, "line": [[r3(p[0]), r3(p[1])] for p in line]}}
    os.makedirs(os.path.join(a.out, "maps"), exist_ok=True)
    with open(os.path.join(a.out, "maps", "start.json"), "w") as f:
        json.dump(world, f, indent=1)
        f.write("\n")
    print("roadracegen: %d racers, %d road edges, %d gates, %d line points -> %s" % (n, len(road), len(checkpoints), len(line), a.out))


if __name__ == "__main__":
    main()
