"""TextMeshPro / uGUI rich text -> RML spans (escaped), and plain text for string tables."""
import re
from xml.sax.saxutils import escape

_TAG = re.compile(r'<(/?)([a-zA-Z-]+)(?:\s*=\s*("[^"]*"|\'[^\']*\'|[^>\s]*))?[^>]*>')
NAMED = {'black': '#000000', 'white': '#ffffff', 'red': '#ff0000', 'green': '#00ff00', 'blue': '#0000ff',
         'yellow': '#ffff00', 'orange': '#ffa500', 'purple': '#a020f0'}
STYLE = {'b': 'font-weight: bold', 'i': 'font-style: italic', 'uppercase': 'text-transform: uppercase',
         'allcaps': 'text-transform: uppercase', 'lowercase': 'text-transform: lowercase', 'nobr': 'white-space: nowrap'}
IGNORED = {'u', 's', 'mark', 'font', 'voffset', 'cspace', 'mspace', 'line-height', 'align', 'link', 'alpha',
           'indent', 'pos', 'space', 'width', 'margin', 'rotate', 'style', 'smallcaps', 'sub', 'sup', 'gradient', 'noparse', 'page'}


def colour(v):
    v = v.strip('"\'')
    if v.lower() in NAMED:
        return NAMED[v.lower()]
    if re.fullmatch(r'#[0-9a-fA-F]{3,8}', v):
        if len(v) == 9:   # #rrggbbaa -> rgba()
            r, g, b, a = (int(v[i:i + 2], 16) for i in (1, 3, 5, 7))
            return 'rgba(%d, %d, %d, %d)' % (r, g, b, a)
        return v
    return None


def to_rml(s, font_size=None, dropped=None):
    """Escaped RML for a TMP string. Unknown tags are removed and named in `dropped`."""
    out, stack, pos = [], [], 0
    s = str(s or '')
    for m in _TAG.finditer(s):
        out.append(escape(s[pos:m.start()]).replace('\n', '<br/>'))
        pos = m.end()
        close, tag, val = m.group(1) == '/', m.group(2).lower(), (m.group(3) or '')
        if tag == 'br':
            out.append('<br/>')
            continue
        style = None
        if not close:
            if tag in STYLE:
                style = STYLE[tag]
            elif tag == 'color':
                c = colour(val)
                style = 'color: %s' % c if c else None
            elif tag == 'size' and val:
                v = val.strip('"\'')
                if v.endswith('%'):
                    style = 'font-size: %s%%' % v[:-1]
                elif v.endswith('em'):
                    style = 'font-size: %sem' % v[:-2]
                else:
                    try:
                        style = 'font-size: %gdp' % float(v.rstrip('px'))
                    except ValueError:
                        style = None
            elif tag == 'sprite':
                if dropped is not None:
                    dropped.add('<sprite>')
                continue
            if style:
                out.append('<span style="%s;">' % style)
                stack.append(tag)
            elif dropped is not None and tag not in ('color', 'size'):
                dropped.add('<%s>' % tag)
        else:
            if tag in stack:
                while stack:
                    t = stack.pop()
                    out.append('</span>')
                    if t == tag:
                        break
            elif dropped is not None and tag not in IGNORED and tag not in STYLE:
                dropped.add('</%s>' % tag)
    out.append(escape(s[pos:]).replace('\n', '<br/>'))
    out += ['</span>'] * len(stack)
    return ''.join(out)


def plain(s):
    """Text without rich-text tags (string tables are inserted as plain text)."""
    return _TAG.sub(lambda m: '\n' if m.group(2).lower() == 'br' else '', str(s or ''))


def has_tags(s):
    return bool(_TAG.search(str(s or '')))
