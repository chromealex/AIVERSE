"""Small Markdown -> HTML converter for the website docs pages (stdlib only).

Covers what the repository docs use: ATX headings (with stable ids), paragraphs,
fenced code, tables, nested bullet/numbered lists, blockquotes, horizontal rules,
inline code, bold/italic, links, images and autolinks. Raw HTML in the source is
escaped, never passed through.
"""
import html
from highlight import highlight
import re

_INLINE_CODE = re.compile(r'(`+)(.+?)\1', re.S)
_IMAGE = re.compile(r'!\[([^\]]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
_LINK = re.compile(r'\[([^\]]+)\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
_AUTOLINK = re.compile(r'<(https?://[^>\s]+)>')
_BOLD = re.compile(r'\*\*(.+?)\*\*|__(.+?)__')
_ITALIC = re.compile(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])|(?<![\w_])_(?!\s)(.+?)(?<!\s)_(?![\w_])')


def slug(text, used):
    s = re.sub(r'<[^>]+>', '', text).strip().lower()
    s = re.sub(r'[`*_~]', '', s)
    s = re.sub(r'[^\w\- ]+', '', s, flags=re.U)
    s = re.sub(r'[\s]+', '-', s).strip('-') or 'section'
    base, n = s, 1
    while s in used:
        n += 1
        s = f'{base}-{n}'
    used.add(s)
    return s


class Converter:
    """link(href) -> href rewrites relative links; image(src) -> url."""

    def __init__(self, link, image):
        self.link = link
        self.image = image
        self.headings = []  # (level, id, text)

    def inline(self, text):
        codes = []

        def keep_code(m):
            codes.append('<code>' + html.escape(m.group(2).strip()) + '</code>')
            return f'\x00{len(codes) - 1}\x00'

        text = _INLINE_CODE.sub(keep_code, text)
        text = html.escape(text, quote=False)
        stash = []

        def keep(s):
            stash.append(s)
            return f'\x01{len(stash) - 1}\x01'

        text = _IMAGE.sub(lambda m: keep(f'<img src="{html.escape(self.image(html.unescape(m.group(2))))}" '
                                         f'alt="{html.escape(html.unescape(m.group(1)))}" loading="lazy">'), text)
        text = _LINK.sub(lambda m: keep(f'<a href="{html.escape(self.link(html.unescape(m.group(2))))}">') +
                         m.group(1) + keep('</a>'), text)
        text = _AUTOLINK.sub(lambda m: keep(f'<a href="{m.group(1)}">{m.group(1)}</a>'), text)
        text = re.sub(r'&lt;(https?://[^&\s]+)&gt;', lambda m: keep(f'<a href="{m.group(1)}">{m.group(1)}</a>'), text)
        text = _BOLD.sub(lambda m: '<strong>' + (m.group(1) or m.group(2)) + '</strong>', text)
        text = _ITALIC.sub(lambda m: '<em>' + (m.group(1) or m.group(2)) + '</em>', text)
        for _ in range(3):
            text = re.sub('\x01(\\d+)\x01', lambda m: stash[int(m.group(1))], text)
        text = re.sub('\x00(\\d+)\x00', lambda m: codes[int(m.group(1))], text)
        return text

    def convert(self, md):
        lines = md.replace('\r\n', '\n').replace('\t', '    ').split('\n')
        out, used, i, n = [], set(), 0, len(lines)
        para = []

        def flush():
            if para:
                out.append('<p>' + self.inline(' '.join(x.strip() for x in para)) + '</p>')
                para.clear()

        while i < n:
            line = lines[i]
            s = line.strip()
            fence = re.match(r'^(\s*)(```+|~~~+)\s*([\w+-]*)', line)
            if fence:
                flush()
                mark, lang = fence.group(2), fence.group(3)
                i += 1
                code = []
                while i < n and not lines[i].strip().startswith(mark[:3]):
                    code.append(lines[i][len(fence.group(1)):] if lines[i].startswith(fence.group(1)) else lines[i])
                    i += 1
                i += 1
                cls = f' class="lang-{html.escape(lang)}"' if lang else ''
                out.append(f'<pre><code{cls}>' + highlight('\n'.join(code), lang) + '</code></pre>')
                continue
            if not s:
                flush()
                i += 1
                continue
            h = re.match(r'^(#{1,6})\s+(.*?)\s*#*\s*$', s)
            if h:
                flush()
                level = len(h.group(1))
                inner = self.inline(h.group(2))
                hid = slug(h.group(2), used)
                self.headings.append((level, hid, re.sub(r'<[^>]+>', '', inner)))
                out.append(f'<h{level} id="{hid}">{inner}<a class="anchor" href="#{hid}" aria-label="#">#</a></h{level}>')
                i += 1
                continue
            if re.match(r'^(\*\s*\*\s*\*[\s*]*|-\s*-\s*-[\s-]*|_\s*_\s*_[\s_]*)$', s):
                flush()
                out.append('<hr>')
                i += 1
                continue
            if s.startswith('|') and i + 1 < n and re.match(r'^\s*\|?\s*:?-{2,}', lines[i + 1]):
                flush()
                rows = []
                while i < n and lines[i].strip().startswith('|'):
                    rows.append(lines[i].strip())
                    i += 1

                def cells(r):
                    r = r.strip().strip('|')
                    parts, cur, depth = [], '', 0
                    for ch in r:
                        if ch == '`':
                            depth ^= 1
                        if ch == '|' and not depth:
                            parts.append(cur)
                            cur = ''
                        else:
                            cur += ch
                    parts.append(cur)
                    return [c.strip().replace('\\|', '|') for c in parts]

                head = ''.join(f'<th>{self.inline(c)}</th>' for c in cells(rows[0]))
                body = ''.join('<tr>' + ''.join(f'<td>{self.inline(c)}</td>' for c in cells(r)) + '</tr>'
                               for r in rows[2:])
                out.append(f'<div class="table"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')
                continue
            if s.startswith('>'):
                flush()
                quote = []
                while i < n and lines[i].strip().startswith('>'):
                    quote.append(re.sub(r'^\s*>\s?', '', lines[i]))
                    i += 1
                out.append('<blockquote>' + Converter(self.link, self.image).convert('\n'.join(quote)) + '</blockquote>')
                continue
            if re.match(r'^\s*([-*+]|\d+[.)])\s+', line):
                flush()
                block = []
                while i < n and (re.match(r'^\s*([-*+]|\d+[.)])\s+', lines[i]) or
                                 (lines[i].strip() and lines[i].startswith('  ')) or
                                 (not lines[i].strip() and i + 1 < n and re.match(r'^\s+\S', lines[i + 1]) and
                                  block)):
                    block.append(lines[i])
                    i += 1
                out.append(self.list_html(block))
                continue
            para.append(line)
            i += 1
        flush()
        return '\n'.join(out)

    def list_html(self, block):
        def indent(l):
            return len(l) - len(l.lstrip(' '))

        base = indent(block[0])
        ordered = bool(re.match(r'^\s*\d+[.)]', block[0]))
        items, cur = [], None
        for l in block:
            m = re.match(r'^(\s*)([-*+]|\d+[.)])\s+(.*)$', l)
            if m and indent(l) <= base + 1:
                cur = [m.group(3)]
                items.append(cur)
            elif cur is not None:
                cur.append(l[base + 2:] if l.startswith(' ' * (base + 2)) else l.strip())
        tag = 'ol' if ordered else 'ul'
        parts = []
        for it in items:
            first, rest = it[0], it[1:]
            nested = [r for r in rest if r.strip()]
            if nested and any(re.match(r'^\s*([-*+]|\d+[.)])\s+', r) for r in nested):
                k = next(j for j, r in enumerate(rest) if re.match(r'^\s*([-*+]|\d+[.)])\s+', r))
                text = ' '.join([first] + [r.strip() for r in rest[:k]])
                sub = Converter(self.link, self.image)
                parts.append('<li>' + self.inline(text) + sub.convert('\n'.join(rest[k:])) + '</li>')
            else:
                parts.append('<li>' + self.inline(' '.join([first] + [r.strip() for r in rest])) + '</li>')
        return f'<{tag}>' + ''.join(parts) + f'</{tag}>'
