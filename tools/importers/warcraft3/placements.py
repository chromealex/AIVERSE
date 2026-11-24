"""Warcraft III W3do 8_11 map placements and custom object editor overrides.

Read-only conversion to a neutral external-asset representation. Unsupported or
future layout versions fail instead of silently shifting records.
"""
import math
import struct
from formats import Reader

MAX_RECORDS = 250000
MAX_SUBRECORDS = 500000
MAX_TEXT = 16384


def _count(r, label, limit=MAX_RECORDS):
    n = r.u32()
    if n > limit:
        raise ValueError('%s count exceeds limit: %d' % (label, n))
    return n


def _vector(r, amount):
    values = list(r.unpack('f' * amount))
    if not all(math.isfinite(value) for value in values):
        raise ValueError('non-finite Warcraft III transform')
    return values


def _items(r):
    result = []
    for _ in range(_count(r, 'dropped item sets')):
        group = []
        for _ in range(_count(r, 'items in set', MAX_SUBRECORDS)):
            item_id, chance = r.tag(), r.unpack('i')[0]
            group.append({'id': item_id, 'chancePercent': chance})
        result.append(group)
    return result


def _header(r):
    if r.tag() != 'W3do':
        raise ValueError('expected W3do header')
    version, subversion = r.unpack('II')
    if (version, subversion) not in ((8, 11), (13, 11)):
        raise ValueError('unsupported W3do version %d/%d (doodad 8/11 or 13/11)' % (version, subversion))
    return version, subversion


def _read_doodads(data, skin_field):
    r = Reader(data)
    version, subversion = _header(r)
    result = []
    for _ in range(_count(r, 'doodads')):
        type_id, variation = r.tag(), r.unpack('i')[0]
        position = _vector(r, 3)
        angle, = _vector(r, 1)
        scale = _vector(r, 3)
        skin = r.tag() if skin_field else None
        v13_color = r.u32() if version == 13 else None
        flags, life = r.unpack('BB')
        table_ptr = r.unpack('i')[0]
        item_sets = _items(r)
        creation = r.unpack('i')[0]
        v13_tail = list(r.unpack('4i')) if version == 13 else None
        result.append({'typeId': type_id, 'skinId': skin, 'variation': variation,
                       'positionWar3': position, 'angleRadians': angle,
                       'scaleWar3': scale, 'flags': flags, 'lifePercent': life,
                       'itemTablePointer': table_ptr, 'itemSets': item_sets,
                       'creationId': creation, 'v13ColorRaw': v13_color,
                       'v13TailRaw': v13_tail})
    special_format, = r.unpack('i')
    specials = []
    for _ in range(_count(r, 'special doodads')):
        item = {'typeId': r.tag()}
        z, x, y = r.unpack('iii')
        item['tilePositionZXY'] = [z, x, y]
        specials.append(item)
    if r.pos != len(data):
        raise ValueError('trailing W3do doodad bytes')
    return {'format': 'warcraft3.doo', 'version': version, 'subversion': subversion,
            'doodads': result, 'specialFormat': special_format,
            'specialDoodads': specials, 'hasSkinField': skin_field}


def doodads(data):
    # Classic 8/11 maps can omit the 4-byte skin rawcode. Both byte layouts
    # exist, and must be checked by completing all records to EOF.
    errors = []
    for skin_field in ((True,) if data[4:8] == struct.pack('<I', 13) else (True, False)):
        try:
            return _read_doodads(data, skin_field)
        except ValueError as ex:
            errors.append(str(ex))
    raise ValueError('W3do doodad: neither classic nor skin-aware 8/11 layout: ' + '; '.join(errors))


def units(data):
    r = Reader(data)
    version, subversion = _header(r)
    if version != 8:
        raise ValueError('v13 unit placement requires populated fixtures; source retained unmodified')
    result = []
    for _ in range(_count(r, 'units')):
        type_id, variation = r.tag(), r.unpack('i')[0]
        position = _vector(r, 3)
        angle, = _vector(r, 1)
        scale = _vector(r, 3)
        flags, = r.unpack('B')
        unknown4 = r.take(4).hex()
        owner, = r.unpack('i')
        unknown2 = r.take(2).hex()
        hp, mana, table_ptr = r.unpack('iii')
        item_sets = _items(r)
        gold, = r.unpack('i')
        acquire, = _vector(r, 1)
        hero_level, strength, agility, intelligence = r.unpack('iiii')
        inventory = [{'slot': r.unpack('i')[0], 'itemId': r.tag()}
                     for _ in range(_count(r, 'inventory entries'))]
        abilities = [{'id': r.tag(), 'autocast': bool(r.unpack('i')[0]),
                      'level': r.unpack('i')[0]}
                     for _ in range(_count(r, 'unit abilities'))]
        random_flag, = r.unpack('i')
        random_selection = None
        if type_id in ('uDNR', 'iDNR'):
            if random_flag == 0:
                random_selection = {'levelFlags': list(r.take(4))}
            elif random_flag == 1:
                random_selection = dict(zip(('groupIndex', 'columnIndex'), r.unpack('ii')))
            elif random_flag == 2:
                random_selection = {'options': [
                    {'typeId': r.tag(), 'chancePercent': r.unpack('i')[0]}
                    for _ in range(_count(r, 'random unit candidates'))]}
            else:
                raise ValueError('invalid random unit discriminator %d' % random_flag)
        player_color, waygate, creation = r.unpack('iii')
        result.append({'typeId': type_id, 'variation': variation,
                       'positionWar3': position, 'angleDegrees': angle, 'scaleWar3': scale,
                       'flags': flags, 'unknown4Hex': unknown4, 'owner': owner,
                       'unknown2Hex': unknown2, 'hitPointsOverride': hp,
                       'manaOverride': mana, 'itemTablePointer': table_ptr,
                       'itemSets': item_sets, 'gold': gold,
                       'targetAcquisition': acquire, 'heroLevel': hero_level,
                       'heroAttributes': [strength, agility, intelligence],
                       'inventory': inventory, 'abilities': abilities,
                       'randomFlag': random_flag, 'randomSelection': random_selection,
                       'playerColor': player_color, 'waygate': waygate,
                       'creationId': creation})
    if r.pos != len(data):
        raise ValueError('trailing W3do unit bytes')
    return {'format': 'warcraft3.units', 'version': version, 'subversion': subversion,
            'units': result}


def overrides(data, extension):
    """Object Editor v1/v2 .w3u/.w3t/.w3b/.w3d/.w3a/.w3h/.w3q.

    Mod IDs are metadata-table field IDs, not resolved user-facing properties.
    This preserves every field and both original/custom tables.
    """
    ext = extension.lower().lstrip('.')
    if ext not in ('w3u', 'w3t', 'w3b', 'w3d', 'w3a', 'w3h', 'w3q'):
        raise ValueError('unsupported object editor table: ' + ext)
    r = Reader(data)
    version = r.u32()
    if version not in (1, 2):
        raise ValueError('unsupported object table version %d' % version)
    extended = ext in ('w3d', 'w3a', 'w3q')
    tables = []
    for table_name in ('original', 'custom'):
        records = []
        for _ in range(_count(r, 'object overrides')):
            base, custom = r.tag(), r.tag()
            mods = []
            for _ in range(_count(r, 'modifications', MAX_SUBRECORDS)):
                field, val_type = r.tag(), r.u32()
                if val_type not in (0, 1, 2, 3):
                    raise ValueError('unsupported object value type %d' % val_type)
                level = column = None
                if extended:
                    level, column = r.unpack('ii')
                if val_type == 0:
                    value, = r.unpack('i')
                elif val_type in (1, 2):
                    value, = _vector(r, 1)
                else:
                    value = r.cstring(MAX_TEXT)
                check_id = r.tag()
                entry = {'fieldId': field, 'valueType': val_type, 'value': value,
                         'objectId': check_id}
                if extended:
                    entry.update({'level': level, 'column': column})
                mods.append(entry)
            records.append({'baseId': base, 'customId': custom, 'modifications': mods})
        tables.append(records)
    if r.pos != len(data):
        raise ValueError('trailing object table bytes')
    return {'format': 'warcraft3.' + ext, 'version': version,
            'extendedFields': extended, 'original': tables[0], 'custom': tables[1]}
