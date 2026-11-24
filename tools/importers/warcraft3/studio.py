"""AIVERSE MapDoc heightfield preview from decoded Warcraft III W3E.

Uses existing map terrain.corners (signed 1/16 of one map cell), i.e. the
heightfield/Studio/TerraWeave bridge. No new terrain renderer or entities.
"""
import math


def placements(terrain, doodads=None, units=None):
    results = []
    for kind, records in (('doodad', (doodads or {}).get('doodads', [])),
                          ('unit', (units or {}).get('units', []))):
        for index, item in enumerate(records):
            x, y, z = item['positionWar3']
            sx, sy, sz = item['scaleWar3']
            yaw = (item['angleRadians'] * 180.0 / math.pi if kind == 'doodad'
                   else item['angleDegrees'])
            results.append({
                'kind': kind, 'sourceIndex': index,
                'sourceId': item['typeId'], 'creationId': item['creationId'],
                'position': [round((y - terrain['offsetY']) / 128.0, 6),
                             round(z / 128.0, 6),
                             round((x - terrain['offsetX']) / 128.0, 6)],
                'yawDegrees': round(yaw, 6),
                'scale': [sy, sz, sx],
                'owner': item.get('owner'),
                'variation': item['variation'],
                'sourceTransform': item['positionWar3'],
            })
    return {'format': 'aiverse.warcraft3.placements.v1',
            'axisTransform': 'war3.x,y,z -> aiverse.y,z,x; horizontal map origin is W3E offset',
            'warcraftUnitsPerAiverseUnit': 128, 'placements': results}


def mapdoc(terrain, info=None, placement_data=None):
    width = terrain['height'] - 1  # engine +X = WC3 +Y
    height = terrain['width'] - 1  # engine +Z = WC3 +X
    if min(width, height) < 4 or max(width, height) > 2048:
        raise ValueError('MapDoc terrain size must be 4..2048 cells; W3E saved separately')
    src_w = terrain['width']
    vertices = terrain['verticesRowMajorBottomLeft']
    corners = []
    for engine_z in range(height + 1):
        for engine_x in range(width + 1):
            elevation = vertices[engine_x * src_w + engine_z][0]
            cm16 = elevation / 8.0  # 128 WC3 world units = 1 cell, 16 corner subunits/cell
            if not math.isfinite(cm16) or not -32768 <= cm16 <= 32767:
                raise ValueError('W3E elevation exceeds MapDoc signed int16 corner range')
            # Python round is ties-to-even; match lround half-away-from-zero used by C++ editor.
            corners.append(math.floor(cm16 + .5) if cm16 >= 0 else math.ceil(cm16 - .5))
    name = (info or {}).get('name') or 'Warcraft III imported terrain'
    # Avoid inventing a native ECS/object type for rawcodes that have not been mapped.
    return {'name': name, 'size': [width, height],
            'terrain': {'rows': [], 'corners': corners}, 'players': [],
            'entities': [], 'warcraft3': {
                'source': 'war3map.w3e', 'previewOnly': True,
                'terrainSourceVersion': terrain['version'],
                'rawPlacementsSource': 'placements.json' if placement_data else None,
                'placedObjectCount': len((placement_data or {}).get('placements', [])),
                'terrainAxes': 'WC3 Y -> MapDoc X; WC3 X -> MapDoc Z',
                'requires': ['native model mapping before scene entities',
                             'NEREID water binding for water surfaces',
                             'WPM import for navigation path restrictions']}}
