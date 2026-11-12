"""Images and fonts of a uGUI hierarchy: Unity sprites -> RmlUi @spritesheet entries, TextMeshPro font
assets -> TrueType files with the family name the engine's font loader will register them under."""
import os
import re
import shutil
import struct
import subprocess

from lib.unity.unity_yaml import get, num

SINGLE_SPRITE = 21300000
BUILTIN_GUID = '0000000000000000f000000000000000'   # Unity's built-in extra resources
BUILTIN_SPRITES = {10905: 'UISprite', 10907: 'Background', 10911: 'InputFieldBackground', 10913: 'Knob', 10901: 'Checkmark',
                   10915: 'DropdownArrow', 10917: 'UIMask'}
DIRECT = ('.png', '.jpg', '.jpeg', '.tga', '.bmp')   # stb_image in engine/ui/UiRender.cpp
CONVERT = ('.psd', '.tif', '.tiff', '.gif', '.exr', '.hdr')


def ident(s, prefix=''):
    s = re.sub(r'[^A-Za-z0-9_-]+', '-', str(s)).strip('-').lower()
    if not s or not (s[0].isalpha() or s[0] == '_'):
        s = (prefix or 'x') + '-' + s
    return s


def image_size(path):
    """(w, h) of PNG / JPEG / TGA / BMP / PSD files from their headers; None when unknown."""
    try:
        with open(path, 'rb') as f:
            head = f.read(64 * 1024)
    except OSError:
        return None
    if head[:8] == b'\x89PNG\r\n\x1a\n' and len(head) >= 24:
        return struct.unpack('>II', head[16:24])
    if head[:4] == b'8BPS' and len(head) >= 26:
        h, w = struct.unpack('>II', head[14:22])
        return w, h
    if head[:2] == b'BM' and len(head) >= 26:
        w, h = struct.unpack('<ii', head[18:26])
        return w, abs(h)
    if head[:2] == b'\xff\xd8':
        i = 2
        while i + 9 < len(head):
            if head[i] != 0xFF:
                i += 1
                continue
            marker = head[i + 1]
            if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
                h, w = struct.unpack('>HH', head[i + 5:i + 9])
                return w, h
            seg = struct.unpack('>H', head[i + 2:i + 4])[0]
            i += 2 + seg
        return None
    if path.lower().endswith('.tga') and len(head) >= 18:
        return struct.unpack('<HH', head[12:16])
    return None


def ttf_family(path):
    """Family name as engine/ui/UiFont.cpp familyName() reads it (Windows / Unicode BMP / en-US, name id 1)."""
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError:
        return ''
    try:
        off = 0
        if data[:4] == b'ttcf':
            off = struct.unpack('>I', data[12:16])[0]
        n = struct.unpack('>H', data[off + 4:off + 6])[0]
        for i in range(n):
            tag, _, toff, _ = struct.unpack('>4sIII', data[off + 12 + 16 * i:off + 28 + 16 * i])
            if tag != b'name':
                continue
            _, count, soff = struct.unpack('>HHH', data[toff:toff + 6])
            for j in range(count):
                pid, eid, lid, nid, ln, so = struct.unpack('>HHHHHH', data[toff + 6 + 12 * j:toff + 18 + 12 * j])
                if pid == 3 and eid == 1 and lid == 0x409 and nid == 1:
                    raw = data[toff + soff + so:toff + soff + so + ln]
                    return ''.join(chr(raw[k + 1]) for k in range(0, len(raw) - 1, 2) if raw[k] == 0).lower()
    except (struct.error, IndexError):
        return ''
    return ''


class Media:
    """Copies textures / fonts into <out>/ui/{img,fonts} and collects the sprite sheet of every texture."""

    def __init__(self, assets, out, report, copy_files=True):
        self.assets, self.out, self.rep, self.copy_files = assets, out, report, copy_files
        self.textures = {}    # source path -> {'file': rel to ui/, 'size': (w, h), 'sprites': {name: rect}}
        self.sprite_names = {}  # (texture, fileID) -> sprite info
        self.used_names = set()
        self.fonts = {}       # font asset guid -> {'family', 'file', 'padding', 'point', 'material'}
        self.font_files = {}  # ttf source -> rel file

    # ---------------------------------------------------------------- textures
    def texture(self, src):
        """Registers a texture file; returns its entry (or None when it cannot be used)."""
        if src in self.textures:
            return self.textures[src]
        ext = os.path.splitext(src)[1].lower()
        base = ident(os.path.splitext(os.path.basename(src))[0], 'img')
        name, k = base, 1
        taken = {t['file'] for t in self.textures.values() if t}
        while 'img/%s.png' % name in taken or 'img/%s%s' % (name, ext) in taken:
            k += 1
            name = '%s-%d' % (base, k)
        entry = None
        size = image_size(src)
        if ext in DIRECT:
            rel = 'img/%s%s' % (name, '.jpg' if ext == '.jpeg' else ext)
            if self.copy_files:
                os.makedirs(os.path.join(self.out, 'ui', 'img'), exist_ok=True)
                shutil.copyfile(src, os.path.join(self.out, 'ui', rel))
            entry = {'file': rel, 'size': size, 'sprites': {}, 'source': src}
        elif ext in CONVERT:
            rel = 'img/%s.png' % name
            ok = False
            if self.copy_files and shutil.which('convert'):
                os.makedirs(os.path.join(self.out, 'ui', 'img'), exist_ok=True)
                r = subprocess.run(['convert', src + '[0]', os.path.join(self.out, 'ui', rel)], capture_output=True)
                ok = r.returncode == 0
                if ok and not size:
                    size = image_size(os.path.join(self.out, 'ui', rel))
            if ok or not self.copy_files:
                entry = {'file': rel, 'size': size, 'sprites': {}, 'source': src}
                self.rep.approx.append('texture %s converted to PNG' % os.path.basename(src))
            else:
                self.rep.warnings.append('texture %s: %s needs ImageMagick (convert) to become PNG' % (os.path.basename(src), ext))
        else:
            self.rep.warnings.append('texture %s: unsupported format %s' % (os.path.basename(src), ext))
        if entry and not entry['size']:
            self.rep.warnings.append('texture %s: size unknown' % os.path.basename(src))
            entry = None
        self.textures[src] = entry
        return entry

    def sprite(self, ref):
        """{'sheet' sprite name, 'inner' (9-slice centre sprite or None), 'border' (l, b, r, t) px, 'ppu', 'w', 'h',
        'file'} for an Image.m_Sprite reference, or None."""
        if not isinstance(ref, dict) or not get(ref, 'guid'):
            return None
        if str(get(ref, 'guid')).lower() == BUILTIN_GUID:
            return {'builtin': BUILTIN_SPRITES.get(get(ref, 'fileID'), 'builtin')}
        path = self.assets.path(ref)
        if not path:
            return None
        fid = get(ref, 'fileID')
        key = (path, fid)
        if key in self.sprite_names:
            return self.sprite_names[key]
        tex = self.texture(path)
        if not tex:
            self.sprite_names[key] = None
            return None
        meta = self.assets.meta(path).get('TextureImporter') or {}
        tw, th = tex['size']
        ppu = num(meta.get('spritePixelsToUnits'), 100.0) or 100.0
        rect, border, sname = (0.0, 0.0, float(tw), float(th)), meta.get('spriteBorder'), os.path.splitext(os.path.basename(path))[0]
        sheet = get(meta, 'spriteSheet', 'sprites') or []
        if int(num(meta.get('spriteMode'), 1)) == 2 or (fid != SINGLE_SPRITE and sheet):
            names = {}
            for row in get(meta, 'internalIDToNameTable') or []:
                if isinstance(row, dict) and isinstance(row.get('first'), dict):
                    names[next(iter(row['first'].values()), None)] = row.get('second')
            hit = None
            for s in sheet:
                if not isinstance(s, dict):
                    continue
                if get(s, 'internalID') == fid or names.get(fid) == s.get('name'):
                    hit = s
                    break
            if hit is None:
                self.rep.warnings.append('sprite %s#%s not found in its sprite sheet' % (os.path.basename(path), fid))
                self.sprite_names[key] = None
                return None
            r = hit.get('rect') or {}
            rect = (num(r.get('x')), num(r.get('y')), num(r.get('width')), num(r.get('height')))
            border, sname = hit.get('border'), str(hit.get('name') or sname)
        # Unity sprite rects have a bottom-left origin; spritesheet rects are top-left.
        x, y, w, h = rect
        top = th - (y + h)
        b = tuple(num(get(border or {}, k)) for k in ('x', 'y', 'z', 'w'))  # left, bottom, right, top
        base = ident(sname, 's')
        nm, k = base, 1
        while nm in self.used_names:
            k += 1
            nm = '%s-%d' % (base, k)
        self.used_names.add(nm)
        tex['sprites'][nm] = (x, top, w, h)
        inner = None
        if any(b) and b[0] + b[2] < w and b[1] + b[3] < h:
            inner = nm + '-in'
            tex['sprites'][inner] = (x + b[0], top + b[3], w - b[0] - b[2], h - b[1] - b[3])
        info = {'sheet': nm, 'inner': inner, 'border': b, 'ppu': ppu, 'w': w, 'h': h, 'file': tex['file']}
        self.sprite_names[key] = info
        return info

    def raw(self, ref):
        """Texture file (relative to ui/) for RawImage.m_Texture."""
        path = self.assets.path(ref) if isinstance(ref, dict) else None
        if not path:
            return None
        tex = self.texture(path)
        return tex['file'] if tex else None

    def spritesheets(self):
        out = []
        for src, t in sorted(self.textures.items()):
            if not t or not t['sprites']:
                continue
            rows = ['    %s: %gpx %gpx %gpx %gpx;' % (n, *r) for n, r in sorted(t['sprites'].items())]
            out.append('@spritesheet sheet-%s {\n    src: ../%s;\n    resolution: 1x;\n%s\n}\n' % (
                ident(os.path.splitext(os.path.basename(t['file']))[0]), t['file'], '\n'.join(rows)))
        return '\n'.join(out)

    # ---------------------------------------------------------------- fonts
    def font(self, ref):
        """TMP_FontAsset reference -> {'family', 'file', 'padding', 'point', 'material'} (family '' if missing)."""
        g = str(get(ref, 'guid', default='') or '').lower() if isinstance(ref, dict) else ''
        if not g:
            return None
        if g in self.fonts:
            return self.fonts[g]
        info = {'family': '', 'file': None, 'padding': 5.0, 'point': 50.0, 'name': g, 'docs': []}
        path = self.assets.path(ref)
        if path:
            docs = self.assets.docs(path)
            info['docs'] = docs
            fa = next((d.data for d in docs if 'm_FaceInfo' in (d.data or {})), {})
            info['name'] = str(fa.get('m_Name') or os.path.basename(path))
            info['padding'] = num(fa.get('m_AtlasPadding'), num(get(fa, 'm_CreationSettings', 'padding'), 5.0)) or 5.0
            info['point'] = num(get(fa, 'm_FaceInfo', 'm_PointSize'), 50.0) or 50.0
            src = self.assets.path(fa.get('m_SourceFontFileGUID')) or self.assets.path(fa.get('m_SourceFontFile'))
            if not (src and os.path.exists(src)):
                src = self.font_by_family(str(get(fa, 'm_FaceInfo', 'm_FamilyName', default='') or ''))
                if src:
                    self.rep.approx.append('font asset %s: source font missing, using %s (same family)' % (info['name'], os.path.basename(src)))
            if src and os.path.splitext(src)[1].lower() in ('.ttf', '.otf', '.ttc'):
                if src not in self.font_files:
                    rel = 'fonts/%s%s' % (ident(os.path.splitext(os.path.basename(src))[0], 'f'), os.path.splitext(src)[1].lower())
                    if self.copy_files:
                        os.makedirs(os.path.join(self.out, 'ui', 'fonts'), exist_ok=True)
                        shutil.copyfile(src, os.path.join(self.out, 'ui', rel))
                    self.font_files[src] = rel
                info['file'] = self.font_files[src]
                info['family'] = ttf_family(src)
            else:
                self.rep.warnings.append('font asset %s: source font file is not in the project (text uses the default UI font)' % info['name'])
        else:
            self.rep.warnings.append('font asset %s not found' % g)
        self.fonts[g] = info
        return info

    def font_by_family(self, family):
        """A font file of the project whose family name starts like the TMP asset's face family."""
        if not family:
            return None
        if not hasattr(self, '_font_index'):
            self._font_index = sorted((ttf_family(p), p) for p in self.assets.by_guid.values()
                                      if os.path.splitext(p)[1].lower() in ('.ttf', '.otf', '.ttc'))
        f = family.lower()
        hit = [p for fam, p in self._font_index if fam == f] or [p for fam, p in self._font_index if fam.startswith(f)]
        return hit[0] if hit else None

    def material(self, font, ref):
        """Float/colour properties of a TMP material (font asset sub-object or separate .mat)."""
        if not isinstance(ref, dict) or not get(ref, 'fileID'):
            return {}
        doc = None
        if get(ref, 'guid'):
            doc, _ = self.assets.object(ref)
        if doc is None and font:
            doc = next((d for d in font.get('docs') or [] if d.file_id == get(ref, 'fileID')), None)
        if doc is None:
            return {}
        props = {}
        saved = get(doc.data, 'm_SavedProperties') or {}
        for group in ('m_Floats', 'm_Colors'):
            for row in saved.get(group) or []:
                if isinstance(row, dict):
                    props.update(row)
        kw = ' '.join(str(x) for x in (get(doc.data, 'm_ValidKeywords') or [])) + ' ' + str(get(doc.data, 'm_ShaderKeywords', default='') or '')
        props['_keywords'] = kw
        return props
