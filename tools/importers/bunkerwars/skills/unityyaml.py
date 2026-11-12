"""Minimal reader for Unity's serialized YAML (prefabs, assets): enough for MonoBehaviour fields.

Unity writes a restricted YAML: documents `--- !u!<class> &<fileID>`, block maps, block lists whose
`- ` items sit at the parent key's indent, flow maps / lists ({fileID: 0, guid: x}) that may wrap over
lines, plain or quoted scalars that may wrap. Scalars stay strings; use num() to read numbers.
"""
import re

_DOC = re.compile(r'^--- !u!(\d+) &(-?\d+)(.*)$')


def num(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _scalar(s):
    s = s.strip()
    if len(s) >= 2 and s[0] == s[-1] and s[0] in "'\"":
        body = s[1:-1]
        return body.replace("''", "'") if s[0] == "'" else body.encode().decode('unicode_escape', 'ignore')
    return s


def _flow(s, i=0):
    """Parse a flow value starting at s[i]; returns (value, next index)."""
    while i < len(s) and s[i] == ' ':
        i += 1
    if i < len(s) and s[i] == '{':
        out, i = {}, i + 1
        while True:
            while i < len(s) and s[i] in ' ,':
                i += 1
            if i >= len(s) or s[i] == '}':
                return out, i + 1
            k = i
            while i < len(s) and s[i] != ':':
                i += 1
            key = s[k:i].strip()
            v, i = _flow(s, i + 1)
            out[key] = v
    if i < len(s) and s[i] == '[':
        out, i = [], i + 1
        while True:
            while i < len(s) and s[i] in ' ,':
                i += 1
            if i >= len(s) or s[i] == ']':
                return out, i + 1
            v, i = _flow(s, i)
            out.append(v)
    k, depth = i, 0
    while i < len(s):
        c = s[i]
        if c in ',}]' and depth == 0:
            break
        i += 1
    return _scalar(s[k:i]), i


def _value(text):
    t = text.strip()
    if t.startswith('{') or t.startswith('['):
        return _flow(t)[0]
    return _scalar(t)


def _balanced(t):
    return t.count('{') + t.count('[') <= t.count('}') + t.count(']')


class _Lines:
    def __init__(self, raw):
        self.items = []   # (indent, text)
        for ln in raw:
            if not ln.strip():
                continue
            ind = len(ln) - len(ln.lstrip(' '))
            self.items.append([ind, ln.strip()])
        self.i = 0

    def peek(self):
        return self.items[self.i] if self.i < len(self.items) else None


def _join_continuation(L, ind, text):
    """Flow values / plain scalars wrapped onto deeper-indented lines."""
    t = text
    if t.lstrip().startswith(('{', '[')):
        while not _balanced(t) and L.peek():
            t += ' ' + L.peek()[1]
            L.i += 1
        return t
    while L.peek() and L.peek()[0] > ind and not re.match(r'^[\w\.\-\[\]<> ]+:( |$)', L.peek()[1]) and not L.peek()[1].startswith('- '):
        t += ' ' + L.peek()[1]
        L.i += 1
    return t


def _block(L, ind):
    p = L.peek()
    if not p or p[0] < ind:
        return None
    if p[1].startswith('- ') or p[1] == '-':
        return _list(L, p[0])
    return _map(L, p[0])


def _list(L, ind):
    out = []
    while True:
        p = L.peek()
        if not p or p[0] != ind or not (p[1].startswith('- ') or p[1] == '-'):
            return out
        body = p[1][2:] if p[1] != '-' else ''
        if re.match(r'^[^{\[\'"][^:]*:( |$)', body):   # a map item: re-read the body as a map line at ind + 2
            p[0], p[1] = ind + 2, body
            out.append(_map(L, ind + 2))
        else:
            L.i += 1
            out.append(_value(_join_continuation(L, ind, body)) if body else _block(L, ind + 1))
    return out


def _map(L, ind):
    out = {}
    while True:
        p = L.peek()
        if not p or p[0] != ind or p[1].startswith('- '):
            return out
        m = re.match(r'^([^:]+?):(?: (.*))?$', p[1])
        if not m:
            L.i += 1
            continue
        key, rest = m.group(1).strip(), m.group(2)
        L.i += 1
        if rest is None or rest == '':
            nxt = L.peek()
            if nxt and nxt[0] > ind:
                out[key] = _block(L, nxt[0])
            elif nxt and nxt[0] == ind and nxt[1].startswith('-'):
                out[key] = _list(L, ind)
            else:
                out[key] = None
        else:
            out[key] = _value(_join_continuation(L, ind, rest))


def parse_documents(text):
    """{fileID(int): (unityClassName, body dict)} for every document of a Unity YAML file."""
    docs, cur, head = {}, [], None

    def flush():
        if head is None:
            return
        L = _Lines(cur)
        top = _map(L, 0) if L.peek() else {}
        cls = next(iter(top), '')
        docs[head] = (cls, top.get(cls) or {})

    for ln in text.split('\n'):
        m = _DOC.match(ln)
        if m:
            flush()
            head, cur = int(m.group(2)), []
        elif head is not None:
            cur.append(ln.rstrip('\r'))
    flush()
    return docs


def load(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return parse_documents(f.read())


def ref_id(v):
    """fileID of a {fileID: n, ...} reference (0 / None when empty)."""
    if isinstance(v, dict):
        try:
            return int(v.get('fileID', 0))
        except ValueError:
            return 0
    return 0
