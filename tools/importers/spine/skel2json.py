#!/usr/bin/env python3
"""Spine 4.0 binary skeleton (.skel / .skel.bytes) -> Spine JSON (the format engine/spine reads).

Reads the binary layout of the official runtimes' SkeletonBinary (4.0.x) and writes the same data
the Spine editor's JSON export would: bones, slots, IK / transform / path constraints, skins with
region / mesh / linked mesh / bounding box / path / point / clipping attachments, events and
animations (bone, slot, constraint, deform, draw order and event timelines with stepped / bezier
curves). The conversion is exact: the whole file must be consumed, otherwise it stops with an error.

    python3 skel2json.py hero.skel.bytes hero.json
"""
import json
import struct
import sys

TRANSFORM_MODES = ["normal", "onlyTranslation", "noRotationOrReflection", "noScale", "noScaleOrReflection"]
BLEND_MODES = ["normal", "additive", "multiply", "screen"]
ATTACHMENT_TYPES = ["region", "boundingbox", "mesh", "linkedmesh", "path", "point", "clipping"]
POSITION_MODES = ["fixed", "percent"]
SPACING_MODES = ["length", "fixed", "percent", "proportional"]
ROTATE_MODES = ["tangent", "chain", "chainScale"]

SLOT_TIMELINES = {0: "attachment", 1: "rgba", 2: "rgb", 3: "rgba2", 4: "rgb2", 5: "alpha"}
BONE_TIMELINES = {0: ("rotate", ["value"]), 1: ("translate", ["x", "y"]), 2: ("translatex", ["value"]),
                  3: ("translatey", ["value"]), 4: ("scale", ["x", "y"]), 5: ("scalex", ["value"]),
                  6: ("scaley", ["value"]), 7: ("shear", ["x", "y"]), 8: ("shearx", ["value"]),
                  9: ("sheary", ["value"])}
CURVE_LINEAR, CURVE_STEPPED, CURVE_BEZIER = 0, 1, 2


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0
        self.strings = []

    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b

    def sbyte(self):
        b = self.byte()
        return b - 256 if b > 127 else b

    def boolean(self):
        return self.byte() != 0

    def int32(self):
        v = struct.unpack_from(">i", self.d, self.p)[0]
        self.p += 4
        return v

    def uint32(self):
        v = struct.unpack_from(">I", self.d, self.p)[0]
        self.p += 4
        return v

    def short(self):
        v = struct.unpack_from(">h", self.d, self.p)[0]
        self.p += 2
        return v

    def float(self):
        v = struct.unpack_from(">f", self.d, self.p)[0]
        self.p += 4
        return v

    def varint(self, optimize_positive=True):
        result = 0
        shift = 0
        while True:
            b = self.byte()
            result |= (b & 0x7F) << shift
            if not (b & 0x80) or shift >= 28:
                break
            shift += 7
        if not optimize_positive:
            result = (result >> 1) ^ -(result & 1)
        return result

    def string(self):
        n = self.varint()
        if n == 0:
            return None
        if n == 1:
            return ""
        s = self.d[self.p:self.p + n - 1].decode("utf-8")
        self.p += n - 1
        return s

    def string_ref(self):
        i = self.varint()
        return None if i == 0 else self.strings[i - 1]

    def long(self):
        v = struct.unpack_from(">q", self.d, self.p)[0]
        self.p += 8
        return v


def num(v):
    """Float32 -> the shortest decimal that reads back as the same float32 (JSON stays small)."""
    if isinstance(v, int):
        return v
    for digits in range(1, 10):
        s = "%.*g" % (digits, v)
        if struct.unpack(">f", struct.pack(">f", float(s)))[0] == struct.unpack(">f", struct.pack(">f", v))[0]:
            f = float(s)
            return int(f) if f.is_integer() and abs(f) < 1e9 else f
    return v


def rgba_hex(c):
    return "%08x" % (c & 0xFFFFFFFF)


def rgb_hex(c):
    return "%06x" % (c & 0xFFFFFF)


def bytes_color(r, n):
    return "".join("%02x" % r.byte() for _ in range(n))


class Converter:
    def __init__(self, data, scale=1.0):
        self.r = Reader(data)
        self.scale = scale
        self.out = {}
        self.bones = []
        self.slots = []
        self.ik = []
        self.transform = []
        self.path = []
        self.skins = []
        self.events = []
        self.nonessential = False
        self.skin_vertex_info = {}   # (skin, slot index, attachment name) -> (weighted, setup vertices)

    def run(self):
        r = self.r
        h = r.long()
        sk = {}
        if h != 0:
            sk["hash"] = str(h)
        v = r.string()
        if v:
            sk["spine"] = v
        sk["x"], sk["y"], sk["width"], sk["height"] = (num(r.float()) for _ in range(4))
        self.nonessential = r.boolean()
        if self.nonessential:
            sk["fps"] = num(r.float())
            images = r.string()
            if images:
                sk["images"] = images
            audio = r.string()
            if audio:
                sk["audio"] = audio
        self.out["skeleton"] = sk
        r.strings = [r.string() for _ in range(r.varint())]
        self.read_bones()
        self.read_slots()
        self.read_ik()
        self.read_transform()
        self.read_path()
        default = self.read_skin(True)
        skins = [default] if default else []
        for _ in range(r.varint()):
            skins.append(self.read_skin(False))
        self.skins = skins
        self.resolve_linked()
        self.read_events()
        animations = {}
        for _ in range(r.varint()):
            name = r.string()
            animations[name] = self.read_animation()
        out = self.out
        out["bones"] = self.bones
        out["slots"] = self.slots
        if self.ik:
            out["ik"] = self.ik
        if self.transform:
            out["transform"] = self.transform
        if self.path:
            out["path"] = self.path
        out["skins"] = [{k: v for k, v in s.items() if not k.startswith("_")} for s in skins]
        if self.events:
            out["events"] = {e["_name"]: {k: v for k, v in e.items() if not k.startswith("_")} for e in self.events}
        if animations:
            out["animations"] = animations
        if r.p != len(r.d):
            raise ValueError("spine binary: %d trailing bytes (not a Spine 4.0 file?)" % (len(r.d) - r.p))
        return out

    # ---- setup pose
    def read_bones(self):
        r = self.r
        for i in range(r.varint()):
            b = {"name": r.string()}
            if i > 0:
                b["parent"] = self.bones[r.varint()]["name"]
            rot, x, y, sx, sy, shx, shy, length = (r.float() for _ in range(8))
            mode = TRANSFORM_MODES[r.varint()]
            skin_required = r.boolean()
            if self.nonessential:
                b["color"] = rgba_hex(r.int32())
            for key, val, dflt in (("length", length * self.scale, 0), ("rotation", rot, 0), ("x", x * self.scale, 0),
                                   ("y", y * self.scale, 0), ("scaleX", sx, 1), ("scaleY", sy, 1),
                                   ("shearX", shx, 0), ("shearY", shy, 0)):
                if val != dflt:
                    b[key] = num(val)
            if mode != "normal":
                b["transform"] = mode
            if skin_required:
                b["skin"] = True
            self.bones.append(b)

    def read_slots(self):
        r = self.r
        for _ in range(r.varint()):
            s = {"name": r.string(), "bone": self.bones[r.varint()]["name"]}
            color = r.int32()
            if color & 0xFFFFFFFF != 0xFFFFFFFF:
                s["color"] = rgba_hex(color)
            dark = r.int32()
            if dark != -1:
                s["dark"] = rgb_hex(dark)
            att = r.string_ref()
            if att is not None:
                s["attachment"] = att
            blend = BLEND_MODES[r.varint()]
            if blend != "normal":
                s["blend"] = blend
            self.slots.append(s)

    def read_ik(self):
        r = self.r
        for _ in range(r.varint()):
            c = {"name": r.string(), "order": r.varint()}
            if r.boolean():
                c["skin"] = True
            c["bones"] = [self.bones[r.varint()]["name"] for _ in range(r.varint())]
            c["target"] = self.bones[r.varint()]["name"]
            c["mix"] = num(r.float())
            c["softness"] = num(r.float() * self.scale)
            c["bendPositive"] = r.sbyte() != -1
            c["compress"] = r.boolean()
            c["stretch"] = r.boolean()
            c["uniform"] = r.boolean()
            self.ik.append(c)

    def read_transform(self):
        r = self.r
        for _ in range(r.varint()):
            c = {"name": r.string(), "order": r.varint()}
            if r.boolean():
                c["skin"] = True
            c["bones"] = [self.bones[r.varint()]["name"] for _ in range(r.varint())]
            c["target"] = self.bones[r.varint()]["name"]
            c["local"] = r.boolean()
            c["relative"] = r.boolean()
            c["rotation"] = num(r.float())
            c["x"] = num(r.float() * self.scale)
            c["y"] = num(r.float() * self.scale)
            c["scaleX"] = num(r.float())
            c["scaleY"] = num(r.float())
            c["shearY"] = num(r.float())
            for k in ("mixRotate", "mixX", "mixY", "mixScaleX", "mixScaleY", "mixShearY"):
                c[k] = num(r.float())
            self.transform.append(c)

    def read_path(self):
        r = self.r
        for _ in range(r.varint()):
            c = {"name": r.string(), "order": r.varint()}
            if r.boolean():
                c["skin"] = True
            c["bones"] = [self.bones[r.varint()]["name"] for _ in range(r.varint())]
            c["target"] = self.slots[r.varint()]["name"]
            c["positionMode"] = POSITION_MODES[r.varint()]
            c["spacingMode"] = SPACING_MODES[r.varint()]
            c["rotateMode"] = ROTATE_MODES[r.varint()]
            c["rotation"] = num(r.float())
            pos = r.float()
            c["position"] = num(pos * self.scale if c["positionMode"] == "fixed" else pos)
            sp = r.float()
            c["spacing"] = num(sp * self.scale if c["spacingMode"] in ("length", "fixed") else sp)
            c["mixRotate"], c["mixX"], c["mixY"] = (num(r.float()) for _ in range(3))
            self.path.append(c)

    # ---- skins and attachments
    def read_skin(self, default):
        r = self.r
        if default:
            slot_count = r.varint()
            if slot_count == 0:
                return None
            skin = {"name": "default"}
        else:
            skin = {"name": r.string_ref()}
            bones = [self.bones[r.varint()]["name"] for _ in range(r.varint())]
            ik = [self.ik[r.varint()]["name"] for _ in range(r.varint())]
            tr = [self.transform[r.varint()]["name"] for _ in range(r.varint())]
            pa = [self.path[r.varint()]["name"] for _ in range(r.varint())]
            for k, v in (("bones", bones), ("ik", ik), ("transform", tr), ("path", pa)):
                if v:
                    skin[k] = v
            slot_count = r.varint()
        atts = {}
        for _ in range(slot_count):
            slot_index = r.varint()
            slot_atts = {}
            for _ in range(r.varint()):
                name = r.string_ref()
                att = self.read_attachment(skin["name"], slot_index, name)
                slot_atts[name] = att
            atts[self.slots[slot_index]["name"]] = slot_atts
        skin["attachments"] = atts
        return skin

    def read_vertices(self, vertex_count):
        r = self.r
        if not r.boolean():
            return False, [num(r.float() * self.scale) for _ in range(vertex_count * 2)]
        out = []
        for _ in range(vertex_count):
            n = r.varint()
            out.append(n)
            for _ in range(n):
                out.append(r.varint())
                out.append(num(r.float() * self.scale))
                out.append(num(r.float() * self.scale))
                out.append(num(r.float()))
        return True, out

    def read_attachment(self, skin_name, slot_index, att_name):
        r = self.r
        name = r.string_ref()
        if name is None:
            name = att_name
        kind = ATTACHMENT_TYPES[r.byte()]
        a = {}
        if name != att_name:
            a["name"] = name
        if kind == "region":
            path = r.string_ref()
            if path is not None and path != name:
                a["path"] = path
            rot, x, y, sx, sy, w, h = (r.float() for _ in range(7))
            color = r.int32()
            for key, val, dflt in (("x", x * self.scale, 0), ("y", y * self.scale, 0), ("scaleX", sx, 1), ("scaleY", sy, 1),
                                   ("rotation", rot, 0)):
                if val != dflt:
                    a[key] = num(val)
            a["width"] = num(w * self.scale)
            a["height"] = num(h * self.scale)
            if color & 0xFFFFFFFF != 0xFFFFFFFF:
                a["color"] = rgba_hex(color)
            return a
        a["type"] = kind
        if kind == "boundingbox":
            n = r.varint()
            weighted, verts = self.read_vertices(n)
            a["vertexCount"] = n
            a["vertices"] = verts
            if self.nonessential:
                a["color"] = rgba_hex(r.int32())
        elif kind == "mesh":
            path = r.string_ref()
            if path is not None and path != name:
                a["path"] = path
            color = r.int32()
            n = r.varint()
            uvs = [num(r.float()) for _ in range(n * 2)]
            tris = [r.short() & 0xFFFF for _ in range(r.varint())]
            weighted, verts = self.read_vertices(n)
            hull = r.varint()
            if color & 0xFFFFFFFF != 0xFFFFFFFF:
                a["color"] = rgba_hex(color)
            a["uvs"] = uvs
            a["triangles"] = tris
            a["vertices"] = verts
            a["hull"] = hull
            if self.nonessential:
                a["edges"] = [r.short() & 0xFFFF for _ in range(r.varint())]
                a["width"] = num(r.float() * self.scale)
                a["height"] = num(r.float() * self.scale)
            self.skin_vertex_info[(skin_name, slot_index, att_name)] = (weighted, verts, n)
        elif kind == "linkedmesh":
            path = r.string_ref()
            if path is not None and path != name:
                a["path"] = path
            color = r.int32()
            skin = r.string_ref()
            parent = r.string_ref()
            inherit = r.boolean()
            if color & 0xFFFFFFFF != 0xFFFFFFFF:
                a["color"] = rgba_hex(color)
            if skin is not None:
                a["skin"] = skin
            a["parent"] = parent
            if not inherit:
                a["deform"] = False
            if self.nonessential:
                a["width"] = num(r.float() * self.scale)
                a["height"] = num(r.float() * self.scale)
            a["_link"] = (skin_name, slot_index, att_name, skin, parent)
        elif kind == "path":
            a["closed"] = r.boolean()
            a["constantSpeed"] = r.boolean()
            n = r.varint()
            weighted, verts = self.read_vertices(n)
            a["vertexCount"] = n
            a["vertices"] = verts
            a["lengths"] = [num(r.float() * self.scale) for _ in range(n // 3)]
            if self.nonessential:
                a["color"] = rgba_hex(r.int32())
        elif kind == "point":
            rot, x, y = r.float(), r.float(), r.float()
            a["x"] = num(x * self.scale)
            a["y"] = num(y * self.scale)
            a["rotation"] = num(rot)
            if self.nonessential:
                a["color"] = rgba_hex(r.int32())
        elif kind == "clipping":
            a["end"] = self.slots[r.varint()]["name"]
            n = r.varint()
            weighted, verts = self.read_vertices(n)
            a["vertexCount"] = n
            a["vertices"] = verts
            if self.nonessential:
                a["color"] = rgba_hex(r.int32())
        return a

    def resolve_linked(self):
        # deform timelines of a linked mesh use the parent's vertices
        for skin in self.skins:
            for slot_name, atts in skin["attachments"].items():
                for att_name, a in atts.items():
                    link = a.pop("_link", None)
                    if not link:
                        continue
                    skin_name, slot_index, _, parent_skin, parent = link
                    key = (parent_skin or "default", slot_index, parent)
                    if key in self.skin_vertex_info:
                        self.skin_vertex_info[(skin_name, slot_index, att_name)] = self.skin_vertex_info[key]

    def read_events(self):
        r = self.r
        for _ in range(r.varint()):
            e = {"_name": r.string_ref()}
            iv = r.varint(False)
            fv = r.float()
            sv = r.string()
            audio = r.string()
            if iv:
                e["int"] = iv
            if fv:
                e["float"] = num(fv)
            if sv:
                e["string"] = sv
            if audio:
                e["audio"] = audio
                e["volume"] = num(r.float())
                e["balance"] = num(r.float())
            self.events.append(e)

    # ---- animations
    def curve(self, key, values1, values2, scales, time1, time2):
        """Reads the curve after a key frame into key["curve"] (JSON 4.0: "stepped" or flat bezier list)."""
        r = self.r
        t = r.byte()
        if t == CURVE_STEPPED:
            key["curve"] = "stepped"
        elif t == CURVE_BEZIER:
            c = []
            for i in range(len(values1)):
                cx1 = r.float()
                cy1 = r.float() * scales[i]
                cx2 = r.float()
                cy2 = r.float() * scales[i]
                c += [num(cx1), num(cy1), num(cx2), num(cy2)]
            key["curve"] = c

    def read_curve_timeline(self, names, scales, defaults, frame_count, color_bytes=None):
        r = self.r
        keys = []
        time = r.float()
        if color_bytes:
            vals = [r.byte() for _ in range(color_bytes)]
        else:
            vals = [r.float() * s for s in scales]
        for frame in range(frame_count):
            key = {}
            if time != 0:
                key["time"] = num(time)
            if color_bytes:
                hexs = "".join("%02x" % v for v in vals)
                if color_bytes in (4, 3):
                    key["color"] = hexs
                elif color_bytes == 7:
                    key["light"] = hexs[:8]
                    key["dark"] = hexs[8:]
                elif color_bytes == 6:
                    key["light"] = hexs[:6]
                    key["dark"] = hexs[6:]
                elif color_bytes == 1:
                    key["value"] = num(vals[0] / 255.0)
            else:
                for n, v, d in zip(names, vals, defaults):
                    if v != d:
                        key[n] = num(v)
            keys.append(key)
            if frame == frame_count - 1:
                break
            time2 = r.float()
            if color_bytes:
                vals2 = [r.byte() for _ in range(color_bytes)]
                channels = color_bytes
                self.curve(key, [0] * channels, [0] * channels, [1] * channels, time, time2)
            else:
                vals2 = [r.float() * s for s in scales]
                self.curve(key, vals, vals2, scales, time, time2)
            time, vals = time2, vals2
        return keys

    def read_animation(self):
        r = self.r
        s = self.scale
        anim = {}
        r.varint()   # number of timelines (a capacity hint)
        # slots
        slots = {}
        for _ in range(r.varint()):
            slot_index = r.varint()
            tl = {}
            for _ in range(r.varint()):
                ttype = r.byte()
                frame_count = r.varint()
                name = SLOT_TIMELINES[ttype]
                if name == "attachment":
                    keys = []
                    for _ in range(frame_count):
                        t = r.float()
                        k = {}
                        if t != 0:
                            k["time"] = num(t)
                        k["name"] = r.string_ref()
                        keys.append(k)
                    tl[name] = keys
                    continue
                r.varint()   # bezier count
                nbytes = {"rgba": 4, "rgb": 3, "rgba2": 7, "rgb2": 6, "alpha": 1}[name]
                tl[name] = self.read_curve_timeline(None, None, None, frame_count, nbytes)
            slots[self.slots[slot_index]["name"]] = tl
        if slots:
            anim["slots"] = slots
        # bones
        bones = {}
        for _ in range(r.varint()):
            bone_index = r.varint()
            tl = {}
            for _ in range(r.varint()):
                ttype = r.byte()
                frame_count = r.varint()
                r.varint()   # bezier count
                name, fields = BONE_TIMELINES[ttype]
                scaled = name.startswith("translate")
                scales = [s if scaled else 1] * len(fields)
                default = 1 if name.startswith("scale") else 0
                tl[name] = self.read_curve_timeline(fields, scales, [default] * len(fields), frame_count)
            bones[self.bones[bone_index]["name"]] = tl
        if bones:
            anim["bones"] = bones
        # ik
        ik = {}
        for _ in range(r.varint()):
            index = r.varint()
            frame_count = r.varint()
            r.varint()
            keys = []
            time, mix, soft = r.float(), r.float(), r.float() * s
            for frame in range(frame_count):
                k = {}
                if time != 0:
                    k["time"] = num(time)
                if mix != 1:
                    k["mix"] = num(mix)
                if soft != 0:
                    k["softness"] = num(soft)
                k["bendPositive"] = r.sbyte() != -1
                k["compress"] = r.boolean()
                k["stretch"] = r.boolean()
                keys.append(k)
                if frame == frame_count - 1:
                    break
                time2, mix2, soft2 = r.float(), r.float(), r.float() * s
                self.curve(k, [mix, soft], [mix2, soft2], [1, s], time, time2)
                time, mix, soft = time2, mix2, soft2
            ik[self.ik[index]["name"]] = keys
        if ik:
            anim["ik"] = ik
        # transform constraints
        tr = {}
        names = ["mixRotate", "mixX", "mixY", "mixScaleX", "mixScaleY", "mixShearY"]
        for _ in range(r.varint()):
            index = r.varint()
            frame_count = r.varint()
            r.varint()
            keys = self.read_curve_timeline(names, [1] * 6, [1] * 6, frame_count)
            tr[self.transform[index]["name"]] = keys
        if tr:
            anim["transform"] = tr
        # path constraints
        paths = {}
        for _ in range(r.varint()):
            index = r.varint()
            c = self.path[index]
            tl = {}
            for _ in range(r.varint()):
                ttype = r.byte()
                frame_count = r.varint()
                r.varint()
                if ttype == 0:
                    sc = s if c["positionMode"] == "fixed" else 1
                    tl["position"] = self.read_curve_timeline(["value"], [sc], [0], frame_count)
                elif ttype == 1:
                    sc = s if c["spacingMode"] in ("length", "fixed") else 1
                    tl["spacing"] = self.read_curve_timeline(["value"], [sc], [0], frame_count)
                else:
                    tl["mix"] = self.read_curve_timeline(["mixRotate", "mixX", "mixY"], [1] * 3, [1] * 3, frame_count)
            paths[c["name"]] = tl
        if paths:
            anim["path"] = paths
        # deform
        deform = {}
        for _ in range(r.varint()):
            skin = self.skins[r.varint()]
            skin_out = {}
            for _ in range(r.varint()):
                slot_index = r.varint()
                slot_out = {}
                for _ in range(r.varint()):
                    att_name = r.string_ref()
                    weighted, setup, nverts = self.skin_vertex_info.get((skin["name"], slot_index, att_name), (False, [], 0))
                    frame_count = r.varint()
                    r.varint()
                    keys = []
                    time = r.float()
                    for frame in range(frame_count):
                        k = {}
                        if time != 0:
                            k["time"] = num(time)
                        end = r.varint()
                        if end != 0:
                            start = r.varint()
                            k["offset"] = start
                            k["vertices"] = [num(r.float() * s) for _ in range(end)]
                        keys.append(k)
                        if frame == frame_count - 1:
                            break
                        time2 = r.float()
                        self.curve(k, [0], [1], [1], time, time2)
                        time = time2
                    slot_out[att_name] = keys
                skin_out[self.slots[slot_index]["name"]] = slot_out
            deform[skin["name"]] = skin_out
        if deform:
            anim["deform"] = deform
        # draw order
        n = r.varint()
        if n:
            keys = []
            for _ in range(n):
                k = {}
                t = r.float()
                if t != 0:
                    k["time"] = num(t)
                offs = []
                for _ in range(r.varint()):
                    offs.append({"slot": self.slots[r.varint()]["name"], "offset": r.varint()})
                if offs:
                    k["offsets"] = offs
                keys.append(k)
            anim["drawOrder"] = keys
        # events
        n = r.varint()
        if n:
            keys = []
            for _ in range(n):
                k = {}
                t = r.float()
                if t != 0:
                    k["time"] = num(t)
                e = self.events[r.varint()]
                k["name"] = e["_name"]
                iv = r.varint(False)
                fv = r.float()
                if iv != e.get("int", 0):
                    k["int"] = iv
                if fv != e.get("float", 0):
                    k["float"] = num(fv)
                if r.boolean():
                    k["string"] = r.string()
                if "audio" in e:
                    k["volume"] = num(r.float())
                    k["balance"] = num(r.float())
                keys.append(k)
            anim["events"] = keys
        return anim


def convert(data, scale=1.0):
    return Converter(data, scale).run()


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    with open(argv[1], "rb") as f:
        out = convert(f.read())
    with open(argv[2], "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, separators=(",", ":"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
