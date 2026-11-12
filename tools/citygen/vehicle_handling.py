#!/usr/bin/env python3
"""Per-vehicle handling for the city game: every car, truck and bus drives differently.

    python3 tools/citygen/vehicle_handling.py <project>

Writes, for every type whose model is models/vehicles/<name>_damage.glb:
  * rules "driving.vehicles.<name>.wheeled": mass, engine, top speed, grip, steering, brakes, drive layout — from the
    table below — and the wheels where the model has them (wheel_<axle>_<side> nodes), so the wheelbase is real:
    a bus turns wide, a hatchback on the spot;
  * the type's view "procedural": front wheels turn with the steering ("steer"), all wheels roll ("wheel").
Vehicles that were not drivable yet get their body size from the model. Other keys are left alone.

The library defaults are a road car (docs/driving.md): at 100 km/h it turns 20 degrees a second, reverses at 75 km/h
and loses 0.6 s of drive on every gear change at 20 ticks/s. These numbers are arcade ones for a top-down game.
"""
import json
import os
import struct
import sys

# mass kg | peak torque Nm | top speed km/h | tyre grip (mu) | max steer, 1/1000 turn | steer rate, 1/1000 turn/s |
# brakes, g | drive: f / r / a | handbrake grip 0..1 (lower = longer slides) | gear change, ticks | reverse km/h
H = {
    #               mass  torque top  grip  steer rate brake drive hb   shift rev
    'hatchback':   (1000,  210,  88, 2.8,  120,  900, 1.9, 'f', 0.45, 3, 35),
    'sedan':       (1300,  260,  95, 2.6,  110,  700, 1.8, 'f', 0.40, 3, 38),
    'taxi':        (1400,  270,  98, 2.5,  110,  700, 1.8, 'r', 0.35, 3, 38),
    'wagon':       (1500,  260,  90, 2.5,  105,  650, 1.7, 'f', 0.40, 3, 36),
    'classic':     (1600,  250,  82, 2.1,  100,  550, 1.4, 'r', 0.30, 4, 32),
    'convertible': (1350,  330, 112, 2.7,  110,  800, 1.9, 'r', 0.32, 3, 40),
    'police':      (1500,  380, 118, 2.8,  112,  800, 2.0, 'r', 0.35, 3, 42),
    'muscle':      (1550,  520, 128, 2.2,  105,  700, 1.7, 'r', 0.22, 3, 42),
    'rally':       (1250,  420, 120, 3.1,  118,  950, 2.1, 'a', 0.30, 2, 42),
    'supercar':    (1400,  640, 150, 3.3,  105,  900, 2.3, 'r', 0.35, 2, 45),
    'limousine':   (2100,  330,  92, 2.3,   95,  450, 1.5, 'r', 0.40, 4, 30),
    'minivan':     (1800,  270,  86, 2.3,  105,  600, 1.6, 'f', 0.45, 3, 32),
    'suv':         (2100,  400,  98, 2.4,  105,  600, 1.7, 'a', 0.45, 3, 34),
    'jeep':        (1700,  360,  88, 2.5,  112,  700, 1.7, 'a', 0.45, 3, 34),
    'pickup':      (1900,  400,  92, 2.2,  105,  600, 1.6, 'r', 0.30, 3, 34),
    'van':         (2200,  300,  82, 2.1,  100,  500, 1.5, 'r', 0.45, 4, 30),
    'ambulance':   (2800,  440,  96, 2.2,  100,  500, 1.6, 'r', 0.45, 4, 30),
    'armored':     (4500,  620,  74, 2.0,   95,  400, 1.3, 'r', 0.50, 5, 26),
    'box_truck':   (5500,  700,  76, 1.9,  105,  380, 1.2, 'r', 0.50, 6, 24),
    'tow_truck':   (6500,  900,  78, 2.0,  105,  380, 1.2, 'r', 0.50, 6, 24),
    'fire_engine': (11000, 1500, 84, 1.9,  110,  350, 1.2, 'r', 0.55, 6, 24),
    'city_bus':    (11500, 1100, 64, 1.8,  125,  320, 1.1, 'r', 0.60, 7, 20),
    'coach':       (13500, 1500, 82, 1.8,  120,  300, 1.1, 'r', 0.60, 7, 22),
}
DEFAULT = (1400, 260, 92, 2.5, 108, 650, 1.7, 'f', 0.40, 3, 36)
FINAL, EFF, REDLINE, TOP_GEAR = 3.7, 0.85, 7000, 0.92


def glb_json(path):
    with open(path, 'rb') as f:
        f.seek(12)
        n, _ = struct.unpack('<I4s', f.read(8))
        return json.loads(f.read(n))


def model_wheels(j):
    """[(axle, side, x, z, radius, node)] in metres, model space (forward = -z)."""
    out = []
    for nd in j['nodes']:
        name = nd.get('name', '')
        if not name.startswith('wheel_') or 'mesh' not in nd:
            continue
        _, axle, side = name.split('_')
        acc = j['accessors'][j['meshes'][nd['mesh']]['primitives'][0]['attributes']['POSITION']]
        t = nd.get('translation', [0, 0, 0])
        out.append((int(axle), side, t[0], t[2], max(acc['max'][1], acc['max'][2]), name))
    return out


def body_size(j):
    """Length and width in metres from the chassis mesh."""
    for nd in j['nodes']:
        if nd.get('name') == 'chassis' and 'mesh' in nd:
            acc = j['accessors'][j['meshes'][nd['mesh']]['primitives'][0]['attributes']['POSITION']]
            return acc['max'][2] - acc['min'][2], acc['max'][0] - acc['min'][0]
    return 4.6, 1.8


def wheeled(name, wheels):
    mass, torque, top, grip, steer, rate, brake, drive, hb, shift, rev = H.get(name, DEFAULT)
    radius = wheels[0][4] if wheels else 0.33
    v = top / 3.6
    # top speed: drag eats the drive force there (x1.5: measured — the car tops out a gear lower than the top one);
    # reverse: the redline in the reverse gear
    force = torque * 0.95 * TOP_GEAR * FINAL / radius * EFF
    rev_gear = REDLINE / 9.549 * radius / (rev / 3.6) / FINAL
    w = {'massMilli': mass * 1000,
         'torque': [[900, round(torque * 0.65)], [4500, torque], [7000, round(torque * 0.77)]],
         'dragMilli': round(force / (v * v) * 1500), 'frictionMilli': round(grip * 1000),
         'maxSteerMilli': steer, 'steerRateMilli': rate, 'steerFalloffSpeedMilli': 30000,
         'brakeForce': round(mass * 9.81 * brake), 'handbrakeForce': round(mass * 9.81 * 0.5), 'handbrakeGripMilli': round(hb * 1000),
         'shiftTicks': shift, 'reverseGearMilli': round(rev_gear * 1000),
         'cgHeightMilli': 450 if mass < 2500 else 900}
    if wheels:
        front = min(w_[0] for w_ in wheels)                # axle 0 is the front one (model forward = -z)
        w['wheels'] = [{'offsetMilli': [round(x * 1000), 0, round(-z * 1000)], 'radiusMilli': round(r * 1000),
                        'steered': a == front, 'driven': drive == 'a' or (drive == 'f') == (a == front),
                        'braked': True, 'handbrake': a != front} for a, side, x, z, r, _ in wheels]
    return w


def procedural(wheels, scale, old):
    proc = [p for p in old if not str(p.get('node', '')).startswith('wheel_')]
    front = min((w_[0] for w_ in wheels), default=0)
    for a, side, x, z, r, node in wheels:
        if a == front:
            proc.append({'type': 'steer', 'node': node, 'limit': 40})     # before the spin: the wheel rolls in its turned frame
        proc.append({'type': 'wheel', 'node': node, 'axis': [-1, 0, 0] if side == 'left' else [1, 0, 0], 'radius': round(r * scale, 4)})
    return proc


def apply(project, cell=4.0):
    path = os.path.join(project, 'rules.json')
    r = json.load(open(path))
    cars = r.setdefault('driving', {}).setdefault('vehicles', {})
    done = []
    for tid, t in r['types'].items():
        view = t.get('view') or {}
        model = view.get('model')
        if not isinstance(model, str) or not model.startswith('models/vehicles/') or not os.path.exists(os.path.join(project, model)):
            continue
        j = glb_json(os.path.join(project, model))
        wheels = model_wheels(j)
        car = cars.setdefault(tid, {})
        if 'length' not in car:
            length, width = body_size(j)
            car['length'], car['width'] = round(length / cell, 3), round(min(width, length) / cell, 3)
        car['wheeled'] = wheeled(tid, wheels)
        view['procedural'] = procedural(wheels, view.get('scale', 1), view.get('procedural', []))
        done.append(tid)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(r, f, ensure_ascii=False, indent=1)
        f.write('\n')
    return done


if __name__ == '__main__':
    print(' '.join(apply(sys.argv[1])))
