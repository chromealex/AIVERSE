"""A tiny element tree that prints RML."""
from xml.sax.saxutils import escape, quoteattr

VOID = {'br', 'img', 'input'}


class El:
    def __init__(self, tag='div', **attrs):
        self.tag, self.attrs, self.style, self.children = tag, {}, {}, []
        for k, v in attrs.items():
            self.attrs[k.rstrip('_').replace('_', '-')] = v

    def add(self, child):
        self.children.append(child)
        return child

    def css(self):
        return '; '.join('%s: %s' % kv for kv in self.style.items())

    def render(self, depth=0, out=None):
        out = [] if out is None else out
        pad = '    ' * depth
        attrs = dict(self.attrs)
        if self.style:
            attrs['style'] = self.css() + ';'
        a = ''.join(' %s=%s' % (k, quoteattr(str(v))) for k, v in attrs.items() if v is not None)
        if self.tag in VOID:
            out.append('%s<%s%s/>' % (pad, self.tag, a))
            return out
        if not self.children:
            out.append('%s<%s%s></%s>' % (pad, self.tag, a, self.tag))
            return out
        if all(isinstance(c, Raw) for c in self.children):
            out.append('%s<%s%s>%s</%s>' % (pad, self.tag, a, ''.join(c.text for c in self.children), self.tag))
            return out
        out.append('%s<%s%s>' % (pad, self.tag, a))
        for c in self.children:
            if isinstance(c, Raw):
                out.append(pad + '    ' + c.text)
            else:
                c.render(depth + 1, out)
        out.append('%s</%s>' % (pad, self.tag))
        return out


class Raw:
    """Already escaped RML text."""

    def __init__(self, text):
        self.text = text


def text(s):
    return Raw(escape(s))
