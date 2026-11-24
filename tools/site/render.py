"""Page generators for the AIVERSE website, one call per language (stdlib only).

English pages are written to the site root, other languages (ru) to <lang>/ with the same
file names. Shared assets (style.css, favicon.svg, media/) stay at the root; `B` is
the relative prefix to them. Data fields are localised by suffix: `title` (ru) and
`titleEn` (en); a missing translation falls back to the Russian text.
"""
import html
import json
import posixpath
import re

from mdhtml import Converter

_ARM = '-72,-220 0,-262 71,-220 71,-58 0,-100 -72,-58'
# Animated AIVERSE mark for every loading state of the site (copy kept in sync by tools/branding/logo/loader.py).
LOADER_SVG = (
    # aiverse-loader:begin (tools/branding/logo/loader.py)
"<svg class=\"aiv-mark\" viewBox=\"0 60 1000 880\" role=\"img\" aria-label=\"Loading\"><style>.aiv-mark{display:block;overflow:visible}.aiv-mark .aiv-b{fill:var(--aiv-mark-ink,#141210);opacity:0;animation:.55s cubic-bezier(.2,.7,.25,1) forwards}.aiv-mark .aiv-t{animation-name:aiv-t}.aiv-mark .aiv-l{animation-name:aiv-l;animation-delay:.1s}.aiv-mark .aiv-r{animation-name:aiv-r;animation-delay:.2s}.aiv-mark .aiv-d{fill:var(--aiv-mark-accent,#E9C3A4);opacity:0;transform-box:view-box;transform-origin:500px 588px;animation:aiv-d .5s .55s cubic-bezier(.2,.7,.25,1) forwards,aiv-p 1.6s 1.05s ease-in-out infinite}@keyframes aiv-t{from{opacity:0;transform:translate(0,-70px)}to{opacity:1;transform:none}}@keyframes aiv-l{from{opacity:0;transform:translate(-60px,32px)}to{opacity:1;transform:none}}@keyframes aiv-r{from{opacity:0;transform:translate(60px,32px)}to{opacity:1;transform:none}}@keyframes aiv-d{from{opacity:0;transform:rotate(-90deg) scale(.3)}to{opacity:1;transform:none}}@keyframes aiv-p{0%,100%{opacity:1;transform:none}50%{opacity:1;transform:scale(1.07)}}@media (prefers-reduced-motion:reduce){.aiv-mark .aiv-b,.aiv-mark .aiv-d{animation:none;opacity:1}}</style><polygon class=\"aiv-b aiv-t\" points=\"500,94 373,179 373,476 500,397 627,476 627,179\"/><polygon class=\"aiv-b aiv-l\" points=\"433,741 147,907 23,833 23,719 307,561 307,663\"/><polygon class=\"aiv-b aiv-r\" points=\"567,741 853,907 977,833 977,719 693,561 693,663\"/><polygon class=\"aiv-d\" points=\"500,438 350,588 500,738 650,588\"/></svg>"
    # aiverse-loader:end
)


def loader(cls='aiv-loader', label=''):
    """Loading indicator: the animated mark, optionally with a caption under it."""
    cap = f'<span>{label}</span>' if label else ''
    return f'<span class="{cls}" role="status">{LOADER_SVG}{cap}</span>'


MARK_LIGHT = ('<svg width="72" height="68" viewBox="-280 -280 560 530"><g fill="#F3EFE8">'
              f'<polygon points="{_ARM}"/><polygon points="{_ARM}" transform="rotate(120)"/>'
              f'<polygon points="{_ARM}" transform="rotate(240)"/></g>'
              '<polygon points="0,-78 78,0 0,78 -78,0" fill="#E9C3A4"/></svg>')


def esc(text):
    return html.escape(str(text or ''), quote=True)


ROOT_LANG = 'en'  # language served at the site root


class Ctx:
    def __init__(self, lang, strings, langs):
        self.lang = lang
        self.T = strings
        # The site root is English; every other language lives in its own folder (ru/).
        self.B = '' if lang == ROOT_LANG else '../'
        self.other = 'ru' if lang == ROOT_LANG else ROOT_LANG
        self.langs = langs
        self.rel = {'released': set(), 'next': ''}

    def loc(self, obj, key):
        if self.lang != 'ru':
            v = obj.get(key + self.lang.capitalize())
            if v:
                return v
        return obj.get(key)

    def media(self, src):
        return f'{self.B}media/{src}'

    def other_href(self, page):
        return (f'{self.other}/{page}' if self.lang == ROOT_LANG else f'../{page}')

    def fmt_date(self, iso):
        y, m, d = iso.split('-')
        return f'{int(d)}.{m}.{y}' if self.lang == 'ru' else f'{self.T["months"][int(m) - 1][:3]} {int(d)}, {y}'

    def fmt_target(self, target):
        if not target:
            return self.T['noTarget']
        y, m = target.split('-')
        return f'{self.T["months"][int(m) - 1]} {y}'

    def where(self, w):
        return self.T['whereMain'] if w == 'main' else self.T['whereBranch']

    def status(self, st):
        return {'done': self.T['stDone'], 'in-progress': self.T['stProgress'], 'planned': self.T['stPlanned']}[st]


def fill_tokens(text, ctx, page):
    for k, v in ctx.T.items():
        if isinstance(v, str):
            text = text.replace('{{' + k + '}}', v)
    text = text.replace('{{B}}', ctx.B).replace('{{lang}}', ctx.lang).replace('{{otherLang}}', ctx.other)
    return text.replace('{{OTHER_HREF}}', ctx.other_href(page))


def render_page(layout, ctx, page, title, desc, content, nav=None, script='', main_class=''):
    out = fill_tokens(layout, ctx, page)
    out = out.replace('<!--TITLE-->', esc(title)).replace('<!--DESC-->', esc(desc))
    out = out.replace('<!--MAINCLASS-->', main_class)
    out = out.replace('<!--CONTENT-->', content).replace('<!--SCRIPT-->', script)
    if nav:
        out = out.replace(f'data-nav="{nav}"', f'data-nav="{nav}" aria-current="page"')
    return out


# ---------------------------------------------------------------- main page blocks

def showcase_html(frame, systems, ctx):
    more = ''
    if frame.get('entry'):
        more = f'<a href="devlog.html#{esc(frame["entry"])}">{ctx.T["showcaseMore"]}</a>'
    title = ctx.loc(frame, 'title')
    return (
        '<section class="showcase" aria-labelledby="showcase-title">\n'
        f'<figure><img src="{esc(ctx.media(frame["src"]))}" alt="{esc(title)}">\n'
        '<figcaption><div class="wrap"><div>'
        f'<p class="eyebrow">{ctx.T["showcaseEyebrow"]}</p>'
        f'<h2 id="showcase-title">{esc(title)}</h2><p>{esc(ctx.loc(frame, "caption"))}</p></div>'
        f'<div class="meta"><span>{esc(ctx.loc(systems[frame["system"]], "name"))} · {ctx.fmt_date(frame["date"])}</span>{more}</div>'
        '</div></figcaption></figure>\n</section>'
    )


def cover_visual(c, ctx, lazy=True):
    cap = esc(ctx.loc(c, 'caption'))
    if c.get('video'):
        return (f'<video src="{esc(ctx.media(c["video"]))}" poster="{esc(ctx.media(c["src"]))}" autoplay muted loop '
                f'playsinline preload="none" aria-label="{cap}"></video>')
    return f'<img src="{esc(ctx.media(c["src"]))}" alt="{cap}"{" loading=\"lazy\"" if lazy else ""}>'


def version_badge(s, ctx):
    """Release badge of a flagship system: shipped version, the next one, or in development."""
    r = s.get('release')
    if r and r in ctx.rel['released']:
        return (f'<a class="badge badge-ink" href="releases.html#v{esc(r)}" title="{esc(ctx.T["verSinceTitle"].format(v=r))}">'
                f'{esc(ctx.T["verSince"].format(v=r))}</a>')
    nxt = r or ctx.rel['next']
    if s.get('where') == 'main' and nxt:
        return (f'<a class="badge badge-accent" href="releases.html#next" title="{esc(ctx.T["verNextTitle"].format(v=nxt))}">'
                f'{esc(ctx.T["verNext"].format(v=nxt))}</a>')
    return f'<span class="badge" title="{esc(ctx.T["verBranchTitle"])}">{ctx.T["verBranch"]}</span>'


def systems_html(systems, ctx):
    cards = []
    for s in systems:
        if not s.get('flagship'):
            continue
        sid = esc(s['id'])
        link = f'<a class="sys-link" href="system-{sid}.html">{ctx.T["cardMore"]}</a>'
        badge = version_badge(s, ctx)
        head = f'<div class="sys-head"><h3><a href="system-{sid}.html">{esc(s["name"])}</a></h3>{badge}</div>'
        text = f'<p class="sys-tag">{esc(ctx.loc(s, "tagline"))}</p><p>{esc(ctx.loc(s, "essence"))}</p>'
        if s.get('cover'):
            c = s['cover']
            cards.append(f'<article class="sys-card" id="sys-{sid}"><figure>{cover_visual(c, ctx)}'
                         f'<figcaption>{esc(ctx.loc(c, "caption"))}</figcaption></figure>'
                         f'<div class="sys-body">{head}{text}{link}</div></article>')
        else:
            cards.append(f'<article class="sys-card sys-pending" id="sys-{sid}">'
                         f'<div class="sys-placeholder" aria-hidden="true">{MARK_LIGHT}<span>{esc(s["name"])}</span></div>'
                         f'<div class="sys-body">{head}{text}<p class="sys-wait">{ctx.T["coverPending"]}</p>{link}</div></article>')
    return '\n'.join(cards)


def core_html(systems, ctx):
    """Compact cards of the other big engine systems (no cover, no roadmap line)."""
    cards = []
    core = sorted((s for s in systems if s.get('core')), key=lambda s: not s.get('cover'))
    for s in core:
        sid = esc(s['id'])
        c = s.get('cover')
        thumb = (f'<span class="core-thumb"><img src="{esc(ctx.media(c["src"]))}" alt="{esc(ctx.loc(c, "caption"))}" loading="lazy"></span>'
                 if c else '')
        tags = ''.join(f'<span>{esc(ctx.loc(it, "name"))}</span>' for it in s['includes'][:4])
        cards.append(f'<a class="core-card{" has-thumb" if c else ""}" href="system-{sid}.html" id="sys-{sid}">{thumb}<h3>{esc(ctx.loc(s, "name"))}</h3>'
                     f'<p class="core-tag">{esc(ctx.loc(s, "tagline"))}</p><p>{esc(ctx.loc(s, "essence"))}</p>'
                     f'<div class="core-inc">{tags}</div><span class="sys-link">{ctx.T["coreMore"]}</span></a>')
    return '\n'.join(cards)


# ---------------------------------------------------------------- system pages and roadmap

def sorted_roadmap(items):
    order = {'done': 0, 'in-progress': 1, 'planned': 2}
    return sorted(items, key=lambda it: (order[it['status']], it.get('date') or it.get('target') or '9999'))


def rm_track(items, ctx):
    """Horizontal roadmap timeline: done items on the left, the current one marked, plans to the right."""
    items = sorted_roadmap(items)
    now = next((i for i, it in enumerate(items) if it['status'] != 'done'), None)
    lis = []
    for i, it in enumerate(items):
        st = it['status']
        when = ctx.fmt_date(it['date']) if st == 'done' else ctx.fmt_target(it.get('target'))
        text = ctx.loc(it, 'text')
        text = f'<p>{esc(text)}</p>' if text else ''
        flag = f'<span class="rmt-now">{ctx.T["rmNow"]}</span>' if i == now else ''
        lis.append(f'<li class="rmt-item rm-{st}{" is-now" if i == now else ""}">{flag}<span class="rmt-dot" aria-hidden="true"></span>'
                   f'<div class="rmt-card"><div class="rm-meta"><span class="st st-{st}">{ctx.status(st)}</span>'
                   f'<time>{esc(when)}</time></div><h3>{esc(ctx.loc(it, "title"))}</h3>{text}</div></li>')
    done = sum(1 for it in items if it['status'] == 'done')
    pct = round(100 * done / len(items)) if items else 0
    return (f'<div class="rmt"><div class="rmt-bar"><div class="rmt-progress" role="img" '
            f'aria-label="{esc(ctx.T["rmProgress"].format(d=done, n=len(items)))}"><span style="width:{pct}%"></span></div>'
            f'<span class="rmt-count mono">{esc(ctx.T["rmProgress"].format(d=done, n=len(items)))}</span>'
            f'<span class="rmt-nav"><button type="button" class="rmt-btn" data-dir="-1" aria-label="{ctx.T["rmPrev"]}">←</button>'
            f'<button type="button" class="rmt-btn" data-dir="1" aria-label="{ctx.T["rmNext"]}">→</button></span></div>'
            f'<div class="rmt-scroll" tabindex="0"><ol class="rmt-list">{"".join(lis)}</ol></div></div>')


def feature_has_page(it):
    return bool(it.get('id') and (it.get('article') or it.get('media')))


def include_card(s, it, ctx):
    name = esc(ctx.loc(it, 'name'))
    text = esc(ctx.loc(it, 'text'))
    if not feature_has_page(it):
        return f'<article class="card"><h3>{name}</h3><p>{text}</p></article>'
    m = (it.get('media') or [None])[0]
    thumb = (f'<span class="inc-thumb"><img src="{esc(ctx.media(m["src"]))}" alt="" loading="lazy"></span>' if m else '')
    return (f'<a class="card inc-link{" has-thumb" if m else ""}" href="system-{esc(s["id"])}-{esc(it["id"])}.html">{thumb}'
            f'<h3>{name}</h3><p>{text}</p><span class="inc-more">{ctx.T["incMore"]}</span></a>')


def blog_posts(entries, ctx, names=None):
    """Fully expanded posts on a vertical date rail; names: system id -> name to tag each post with its topic."""
    status = {'done': ctx.T['stDone'], 'in-progress': ctx.T['stProgress'], 'experimental': ctx.T['stExperimental']}
    posts = []
    for e in entries:
        figs = []
        for m in e['media']:
            cap = esc(m['caption'])
            if re.search(r'\.(mp4|webm)$', m['src'], re.I):
                figs.append(f'<figure><video src="{esc(m["src"])}" controls muted playsinline preload="metadata" aria-label="{cap}"></video>'
                            f'<figcaption>{cap}</figcaption></figure>')
            else:
                figs.append(f'<figure><a href="{esc(m["src"])}" data-cap="{cap}"><img src="{esc(m["src"])}" alt="{cap}" loading="lazy"></a>'
                            f'<figcaption>{cap}</figcaption></figure>')
        gal = f'<div class="blog-media n{min(len(figs), 3)}">{"".join(figs)}</div>' if figs else ''
        body = ''.join(f'<p>{esc(p)}</p>' for p in (e.get('body') or []))
        facts = f'<div><dt>{ctx.T["blogVerified"]}</dt><dd>{esc(e["verified"])}</dd></div>'
        if e.get('limits'):
            facts += f'<div><dt>{ctx.T["blogLimits"]}</dt><dd>{esc(e["limits"])}</dd></div>'
        src = esc(e['branch'] or '') + (f' · {esc(e["commit"])}' if e.get('commit') else '')
        topic = (f'<a class="entry-sys" href="devblog-{esc(e["system"])}.html">{esc(names[e["system"]])}</a>'
                 if names else '')
        links = ''.join(f'<a href="{esc(l["href"])}">{esc(l["title"])}</a>' for l in e.get('links') or [])
        posts.append(f'<li class="blog-post" id="{esc(e["id"])}" data-sys="{esc(e["system"])}">'
                     f'<time class="blog-date mono" datetime="{esc(e["date"])}">{ctx.fmt_date(e["date"])}</time>'
                     f'<span class="blog-dot" aria-hidden="true"></span><article class="blog-card">'
                     f'<div class="entry-meta">{topic}<span class="st st-{esc(e["status"])}">{esc(status.get(e["status"], e["status"]))}</span>'
                     f'{f"<span>{src}</span>" if src else ""}</div>'
                     f'<h2>{esc(e["title"])}</h2><p class="summary">{esc(e["summary"])}</p>'
                     f'{gal}{body}'
                     f'<dl class="facts-list">{facts}</dl>{f"<div class=entry-links>{links}</div>" if links else ""}</article></li>')
    return f'<ol class="blog-list">{"".join(posts)}</ol>'


LIGHTBOX = ('<div class="lightbox" id="lightbox" role="dialog" aria-modal="true"><button type="button" id="lb-close" aria-label="×">×</button>'
            '<img id="lb-img" alt=""><p id="lb-cap"></p></div>')


def blog_page(entries, systems, ctx):
    """The engine blog: everything outside the flagship systems, newest first, filterable by topic."""
    names = {s['id']: s['name'] for s in systems}
    topics = [s for s in systems if any(e['system'] == s['id'] for e in entries)]
    chips = [f'<button class="filter" type="button" data-sys="" aria-pressed="true">{ctx.T["rmAll"]}</button>']
    chips += [f'<button class="filter" type="button" data-sys="{esc(s["id"])}" aria-pressed="false">{esc(s["name"])}</button>'
              for s in topics]
    empty = f'<p class="blog-empty">{ctx.T["blogEmpty"]}</p>' if not entries else ''
    return (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["navBlog"]}</p><h1>{ctx.T["blogHubTitle"]}</h1>'
            f'<p>{ctx.T["blogHubLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong>{len(entries)}</strong><span>{ctx.T["blogPosts"]}</span></div>'
            f'<div><strong>{len(topics)}</strong><span>{ctx.T["blogTopics"]}</span></div></div></section>'
            f'<div class="dl-filters blog-filters" role="group" aria-label="{ctx.T["rmFilter"]}">{"".join(chips)}</div>'
            f'{empty}{blog_posts(entries, ctx, names)}<div class="blog-end"></div>{LIGHTBOX}')


BLOG_JS = """<script>
(function(){var b=document.querySelectorAll('.blog-filters .filter');function set(id){b.forEach(function(x){x.setAttribute('aria-pressed',String(x.dataset.sys===id));});
document.querySelectorAll('.blog-post').forEach(function(p){p.hidden=!!id&&p.dataset.sys!==id;});history.replaceState(null,'',id?'#topic='+id:location.pathname);}
b.forEach(function(x){x.addEventListener('click',function(){set(x.dataset.sys);});});
var m=location.hash.match(/^#topic=(.+)$/);if(m&&document.querySelector('.blog-filters [data-sys="'+m[1]+'"]'))set(m[1]);})();
</script>"""


def devblog_page(s, mine, ctx, flagship):
    """All stages of one system, newest first, fully expanded: the system's own development diary."""
    sid = esc(s['id'])
    name = esc(s['name'])
    sys_href = f'system-{sid}.html' if flagship else f'blog.html#topic={sid}'
    first = ctx.fmt_date(mine[-1]['date']) if mine else ''
    last = ctx.fmt_date(mine[0]['date']) if mine else ''
    return (f'<section class="dl-hero"><div><p class="crumbs"><a href="./#systems">{ctx.T["crumbSystems"]}</a> / '
            f'<a href="{sys_href}">{name}</a> / {ctx.T["blogNav"]}</p>'
            f'<h1>{ctx.T["blogTitle"]}: {name}</h1><p>{esc(ctx.T["blogLead"].format(s=s["name"]))}</p></div>'
            f'<div class="dl-stats"><div><strong>{len(mine)}</strong><span>{ctx.T["blogStages"]}</span></div>'
            f'<div><strong class="blog-stat-date">{last}</strong><span>{ctx.T["blogLast"]}</span></div></div></section>'
            f'{blog_posts(mine, ctx)}'
            f'<p class="blog-first mono">{ctx.T["blogFirst"]}: {first}</p>{LIGHTBOX}')


def feature_page(s, it, body_html, ctx):
    """A mini-article about one part of a flagship system: lead, screenshots, long text."""
    sid = esc(s['id'])
    gal = ''
    if it.get('media') and not it.get('article'):  # an article places its own images
        figs = ''.join(f'<figure><a href="{esc(ctx.media(m["src"]))}" data-cap="{esc(ctx.loc(m, "caption"))}">'
                       f'<img src="{esc(ctx.media(m["src"]))}" alt="{esc(ctx.loc(m, "caption"))}"{" loading=\"lazy\"" if i else ""}></a>'
                       f'<figcaption>{esc(ctx.loc(m, "caption"))}</figcaption></figure>'
                       for i, m in enumerate(it['media']))
        gal = f'<div class="fp-gallery{" fp-one" if len(it["media"]) == 1 else ""}">{figs}</div>'
    def sib_href(o):
        return f'system-{sid}-{esc(o["id"])}.html' if feature_has_page(o) else f'system-{sid}.html#inc'
    sib = ''.join(f'<a class="chip{" chip-ink" if o is it else ""}" href="{sib_href(o)}">{esc(ctx.loc(o, "name"))}</a>'
                  for o in s['includes'])
    return (f'<section class="sp-hero fp-hero"><p class="crumbs"><a href="./#systems">{ctx.T["crumbSystems"]}</a> / '
            f'<a href="system-{sid}.html">{esc(s["name"])}</a> / {esc(ctx.loc(it, "name"))}</p>'
            f'<h1>{esc(ctx.loc(it, "name"))}</h1><p class="sp-lead">{esc(ctx.loc(it, "text"))}</p></section>'
            f'{gal}<article class="doc fp-body">{body_html}</article>'
            f'<section class="sp-sec sp-others"><p class="eyebrow">{ctx.T["incOther"].format(s=esc(s["name"]))}</p>'
            f'<div class="chips">{sib}</div><p><a class="btn btn-line" href="system-{sid}.html">← {esc(s["name"])}</a></p></section>'
            '<div class="lightbox" id="lightbox" role="dialog" aria-modal="true"><button type="button" id="lb-close" aria-label="×">×</button>'
            '<img id="lb-img" alt=""><p id="lb-cap"></p></div>')


FEATURE_JS = """<script>
(function(){var lb=document.getElementById('lightbox');if(!lb)return;var im=document.getElementById('lb-img'),cp=document.getElementById('lb-cap');
function close(){lb.classList.remove('open');im.removeAttribute('src');}
document.querySelectorAll('.fp-gallery a, .blog-media a, .gal-grid a.gal-img, .rel-hl a.rel-hl-img').forEach(function(a){a.addEventListener('click',function(e){e.preventDefault();
im.src=a.getAttribute('href');cp.textContent=a.dataset.cap||'';lb.classList.add('open');});});
document.getElementById('lb-close').addEventListener('click',close);lb.addEventListener('click',function(e){if(e.target===lb)close();});
document.addEventListener('keydown',function(e){if(e.key==='Escape')close();});})();
</script>"""


def system_page(s, flagships, entries, ctx, repo_blob, doc_pages):
    sid = esc(s['id'])
    mine = [e for e in entries if e['system'] == s['id']]
    buttons = []
    art = s.get('article')
    if art and art in doc_pages:
        buttons.append(f'<a class="btn btn-ink" href="docs-{doc_pages[art]}.html">{ctx.T["docsPage"]}</a>')
    elif art:
        href = art if art.startswith('https://') else f'{repo_blob}/{art}'
        buttons.append(f'<a class="btn btn-ink" href="{esc(href)}">{ctx.T["article"]}</a>')
    core = bool(s.get('core'))
    if mine or not core:
        buttons.append(f'<a class="btn btn-line" href="{f"devblog-{sid}.html" if mine else f"devlog.html#system={sid}"}">{ctx.T["blogTitle"]} · {len(mine)}</a>')
    badge = '' if core else version_badge(s, ctx)
    since = f'<p class="sp-since mono">{ctx.T["since"]} {ctx.fmt_date(s["started"])}</p>' if s.get('started') else ''
    hero = (f'<section class="sp-hero"><p class="crumbs"><a href="./#{"core" if core else "systems"}">{ctx.T["crumbSystems"]}</a> / {esc(ctx.loc(s, "name"))}</p>'
            f'<div class="sp-title"><h1>{esc(ctx.loc(s, "name"))}</h1>{badge}</div><p class="sp-tag">{esc(ctx.loc(s, "tagline"))}</p>'
            f'<p class="sp-lead">{esc(ctx.loc(s, "essence"))}</p>{since}'
            f'<div class="actions">{"".join(buttons)}</div></section>')
    c = s.get('cover')
    if c:
        cover = f'<figure class="sp-cover">{cover_visual(c, ctx, lazy=False)}<figcaption>{esc(ctx.loc(c, "caption"))}</figcaption></figure>'
    elif core:
        cover = ''
    else:
        cover = (f'<div class="sp-cover sp-pending"><div class="sys-placeholder">{MARK_LIGHT}<span>{esc(s["name"])}</span></div>'
                 f'<p><strong>{ctx.T["pendingTitle"]}</strong> {esc(ctx.loc(s, "coverBrief"))}</p></div>')
    inc = ''.join(include_card(s, i, ctx) for i in s['includes'])
    ena = ''.join(f'<li>{esc(x)}</li>' for x in (ctx.loc(s, 'enables') or []))
    rm = (f'<section class="sp-sec"><div class="sp-sec-head"><h2 class="h2">{ctx.T["secRoadmap"]}</h2>'
          f'<a href="roadmap.html#{sid}">{ctx.T["secRoadmapAll"]}</a></div>{rm_track(s["roadmap"], ctx)}</section>'
          if s.get('roadmap') else '')
    shots = []
    for e in mine[:6]:
        m = next((m for m in e['media'] if not re.search(r'\.(mp4|webm)$', m['src'], re.I)), None)
        img = f'<img src="{esc(m["src"])}" alt="{esc(m["caption"])}" loading="lazy">' if m else ''
        shots.append(f'<a class="sp-entry" href="devblog-{sid}.html#{esc(e["id"])}">{img}<span class="mono">{ctx.fmt_date(e["date"])}</span>'
                     f'<strong>{esc(e["title"])}</strong><span>{esc(e["summary"])}</span></a>')
    blog_link = (f'<a href="devblog-{sid}.html">{esc(ctx.T["blogAll"].format(n=len(mine)))}</a>' if mine else '')
    log = (f'<div class="sp-entries">{"".join(shots)}</div>' if shots else f'<p class="empty">{ctx.T["noEntries"]}</p>')
    others = ''.join(f'<a class="chip{" chip-ink" if o["id"] == s["id"] else ""}" href="system-{esc(o["id"])}.html">{esc(ctx.loc(o, "name"))}</a>'
                     for o in flagships if bool(o.get('core')) == core)
    return (hero + cover +
            f'<section class="sp-sec" id="inc"><h2 class="h2">{ctx.T["secIncludes"]}</h2><div class="grid sp-inc">{inc}</div></section>'
            f'<section class="sp-sec"><h2 class="h2">{ctx.T["secEnables"]}</h2><ul class="sp-ena">{ena}</ul></section>'
            + rm
            + (f'<section class="sp-sec"><div class="sp-sec-head"><h2 class="h2">{ctx.T["secDevlog"]}</h2>'
               f'{blog_link}</div>{log}</section>' if (mine or not core) else '') +
            f'<section class="sp-sec sp-others"><p class="eyebrow">{ctx.T["otherSystems"]}</p><div class="chips">{others}</div></section>')


LINE_COLORS = ('#2F6FB0', '#3F8A55', '#1F8A9E', '#7A4FB0', '#B0562F', '#B0407A', '#C08A1E', '#8A4B26', '#C0392B', '#4B5E8A')


def line_color(s, flagships):
    ids = [f['id'] for f in flagships]
    return s.get('color') or LINE_COLORS[(ids.index(s['id']) if s['id'] in ids else len(ids)) % len(LINE_COLORS)]


def short(text, n):
    return text if len(text) <= n else text[:n - 1].rstrip() + '…'


def roadmap_lines(flagships, ctx):
    """Main-page overview: one coloured line per system, stations for roadmap items."""
    rows = []
    for s in flagships:
        rm = sorted_roadmap(s['roadmap'])
        n = len(rm)
        now = next((i for i, it in enumerate(rm) if it['status'] != 'done'), n)
        col = line_color(s, flagships)
        sts = []
        for i, it in enumerate(rm):
            x = 3 + 94 * i / max(n - 1, 1)
            title = ctx.loc(it, 'title')
            cls = 'done' if it['status'] == 'done' else ('cur' if i == now else 'plan')
            lab = ''
            if i == now:
                lab = f'<span class="ln-l ln-cur">{esc(short(title, 40))}</span>'
            elif it['status'] != 'done' and i == now + 1:
                lab = f'<span class="ln-l ln-plan">{esc(short(title, 30))}</span>'
            elif i == 0:
                lab = f'<span class="ln-l">{esc(short(title, 30))}</span>'
            sts.append(f'<span class="ln-st ln-{cls}" style="left:{x:.2f}%" title="{esc(title)}"><i></i>{lab}</span>')
        xn = 3 + 94 * min(now, n - 1) / max(n - 1, 1) if now < n else 100
        done = sum(1 for it in rm if it['status'] == 'done')
        now_m = f'<span class="ln-now-m">{esc(ctx.loc(rm[now], "title"))}</span>' if now < n else ''
        rows.append(f'<a class="ln" href="roadmap.html#{esc(s["id"])}" style="--c:{col};--x:{xn:.2f}%">'
                    f'<span class="ln-n">{esc(s["name"])}<small>{esc(ctx.T["rmProgress"].format(d=done, n=n))}</small>'
                    f'{now_m}</span>'
                    f'<span class="ln-track"><span class="ln-rail"></span>{"".join(sts)}</span></a>')
    return (f'<section id="roadmap" class="section wrap ln-sec"><div class="ln-head"><div><p class="eyebrow">{ctx.T["navRoadmap"]}</p>'
            f'<h2 class="h2">{ctx.T["lnTitle"]}</h2></div><div class="ln-side"><p>{ctx.T["lnLead"]}</p>'
            f'<div class="ln-legend"><span><i class="lg-done"></i>{ctx.T["lnDone"]}</span><span><i class="lg-cur"></i>{ctx.T["rmNow"]}</span>'
            f'<span><i class="lg-plan"></i>{ctx.T["lnAhead"]}</span></div></div></div>'
            f'<div class="ln-list">{"".join(rows)}</div>'
            f'<p class="ln-more"><a class="btn btn-line" href="roadmap.html">{ctx.T["lnAll"]} →</a></p></section>')


def roadmap_page(flagships, ctx):
    """Now / next / later: one row per system, done items folded, the current one highlighted."""
    chips = [f'<button class="filter" type="button" data-sys="" aria-pressed="true">{ctx.T["rmAll"]}</button>']
    chips += [f'<button class="filter" type="button" data-sys="{esc(s["id"])}" aria-pressed="false">{esc(s["name"])}</button>'
              for s in flagships]

    def item(it, cls):
        text = ctx.loc(it, 'text')
        when = ctx.fmt_date(it['date']) if it['status'] == 'done' else (ctx.fmt_target(it['target']) if it.get('target') else '')
        return (f'<li class="{cls}"><span class="nn-title">{esc(ctx.loc(it, "title"))}</span>'
                f'{f"<span class=nn-text>{esc(text)}</span>" if text else ""}'
                f'{f"<time class=mono>{esc(when)}</time>" if when else ""}</li>')
    rows = []
    for s in flagships:
        rm = sorted_roadmap(s['roadmap'])
        done = [it for it in rm if it['status'] == 'done']
        cur = [it for it in rm if it['status'] == 'in-progress']
        plan = [it for it in rm if it['status'] == 'planned']
        pct = round(100 * len(done) / len(rm)) if rm else 0
        last = ctx.fmt_date(done[-1]['date']) if done else '—'
        recent = done[-2:][::-1]
        older = done[:-2][::-1]
        more = (f'<details class="nn-more"><summary>{ctx.T["nnMore"].format(n=len(older))}</summary>'
                f'<ul class="nn-done">{"".join(item(it, "") for it in older)}</ul></details>') if older else ''
        dash = '<span class="nn-none">—</span>'
        cur_html = f'<ul class="nn-cur">{"".join(item(it, "") for it in cur)}</ul>' if cur else dash
        rows.append(
            f'<section class="nn-row" id="{esc(s["id"])}" data-sys="{esc(s["id"])}" style="--c:{line_color(s, flagships)}">'
            f'<div class="nn-sys"><span class="nn-ring" style="--p:{pct}"><i>{len(done)}/{len(rm)}</i></span>'
            f'<span><a class="nn-name" href="system-{esc(s["id"])}.html">{esc(s["name"])}</a>'
            f'<span class="nn-last">{ctx.T["nnLast"]} {esc(last)}</span></span></div>'
            f'<div class="nn-col" data-label="{ctx.T["nnDone"]}"><ul class="nn-done">{"".join(item(it, "") for it in recent)}</ul>{more}</div>'
            f'<div class="nn-col" data-label="{ctx.T["rmNow"]}">'
            f'{cur_html}</div>'
            f'<div class="nn-col" data-label="{ctx.T["nnNext"]}"><ul class="nn-plan">{"".join(item(it, "") for it in plan)}</ul>'
            f'{"" if plan else dash}</div></section>')
    total = sum(len(s['roadmap']) for s in flagships)
    done_all = sum(1 for s in flagships for it in s['roadmap'] if it['status'] == 'done')
    return (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["rmEyebrow"]}</p><h1>{ctx.T["rmTitle"]}</h1>'
            f'<p>{ctx.T["rmLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong>{len(flagships)}</strong><span>{ctx.T["rmSystems"]}</span></div>'
            f'<div><strong>{done_all}/{total}</strong><span>{ctx.T["rmItems"]}</span></div></div></section>'
            f'<div class="dl-filters rm-filters" role="group" aria-label="{ctx.T["rmFilter"]}">{"".join(chips)}</div>'
            f'<div class="nn-board"><div class="nn-head"><span>{ctx.T["nnSystem"]}</span><span>{ctx.T["nnDone"]}</span>'
            f'<span>{ctx.T["rmNow"]}</span><span>{ctx.T["nnNext"]}</span></div>{"".join(rows)}</div>')


TRACK_JS = """<script>
(function(){document.querySelectorAll('.rmt').forEach(function(t){var sc=t.querySelector('.rmt-scroll'),now=t.querySelector('.is-now');
function edges(){t.classList.toggle('at-start',sc.scrollLeft<4);t.classList.toggle('at-end',sc.scrollLeft+sc.clientWidth>sc.scrollWidth-4);}
if(now)sc.scrollLeft=Math.max(0,now.offsetLeft-sc.clientWidth*0.35);
t.querySelectorAll('.rmt-btn').forEach(function(b){b.addEventListener('click',function(){sc.scrollBy({left:+b.dataset.dir*sc.clientWidth*0.8,behavior:'smooth'});});});
sc.addEventListener('scroll',edges,{passive:true});window.addEventListener('resize',edges);edges();});})();
</script>"""

ROADMAP_JS = """<script>
(function(){var b=document.querySelectorAll('.rm-filters .filter');function set(id){b.forEach(function(x){x.setAttribute('aria-pressed',String(x.dataset.sys===id));});
document.querySelectorAll('.nn-row').forEach(function(l){l.hidden=!!id&&l.dataset.sys!==id;});
history.replaceState(null,'',id?'#'+id:location.pathname);window.dispatchEvent(new Event('resize'));}
b.forEach(function(x){x.addEventListener('click',function(){set(x.dataset.sys);});});
var h=location.hash.slice(1);if(h&&document.querySelector('.rm-filters [data-sys="'+h+'"]'))set(h);})();
</script>"""


# ---------------------------------------------------------------- documentation

def is_russian(text):
    letters = re.findall(r'[A-Za-zА-Яа-яЁё]', text[:20000])
    cyr = sum(1 for ch in letters if re.match(r'[А-Яа-яЁё]', ch))
    return bool(letters) and cyr / len(letters) > 0.3


def docs_nav(manifest, ctx, current=None):
    parts = []
    for sec in manifest['sections']:
        links = ''.join(f'<a href="docs-{esc(p["id"])}.html"{" aria-current=\"page\"" if p["id"] == current else ""}>'
                        f'{esc(ctx.loc(p, "title"))}</a>' for p in sec['pages'])
        parts.append(f'<div class="dn-sec"><span>{esc(ctx.loc(sec, "title"))}</span>{links}</div>')
    search = (f'<div class="ds-box"><input class="ds-input" type="search" placeholder="{esc(ctx.T["docsSearch"])}" '
              f'aria-label="{esc(ctx.T["docsSearch"])}" autocomplete="off" spellcheck="false">'
              f'<div class="ds-results" hidden></div></div>')
    return f'<nav class="docs-nav" aria-label="{ctx.T["navDocs"]}">{search}{"".join(parts)}</nav>'


def doc_sections(page_id, title, body_html):
    """Search records of one doc page: one per heading (h1..h4) with the plain text under it."""
    out = []
    parts = re.split(r'<h([1-4])(?: [^>]*)? id="([^"]+)"[^>]*>(.*?)</h\1>', body_html, flags=re.S)
    def plain(h):
        t = re.sub(r'<a class="anchor"[^>]*>.*?</a>', '', h)
        t = re.sub(r'</?(?:span|code|a|em|strong|b|i|mark|kbd)\b[^>]*>', '', t)   # inline: no gap
        t = re.sub(r'<[^>]+>', ' ', t)
        return re.sub(r'\s+', ' ', html.unescape(t)).strip()
    if plain(parts[0]):
        out.append({'p': page_id, 't': title, 'h': '', 'a': '', 'x': plain(parts[0])[:4000]})
    for i in range(1, len(parts) - 3, 4):
        out.append({'p': page_id, 't': title, 'h': plain(parts[i + 2]), 'a': parts[i + 1],
                    'x': plain(parts[i + 3])[:4000]})
    return out


MENU_SPY_JS = """<script>
(function(){var map=[['systems','systems'],['core','systems'],['genres','genres'],['roadmap','roadmap']],links={};
document.querySelectorAll('.menu a[data-nav]').forEach(function(a){links[a.dataset.nav]=a;});
var secs=map.map(function(m){return [document.getElementById(m[0]),m[1]];}).filter(function(m){return m[0]&&links[m[1]];});if(!secs.length)return;
function spy(){var y=innerHeight*.35,cur=null;secs.forEach(function(m){var r=m[0].getBoundingClientRect();if(r.top<=y&&r.bottom>y)cur=m[1];});
Object.keys(links).forEach(function(k){if(k===cur)links[k].setAttribute('aria-current','page');else if(links[k].getAttribute('aria-current')==='page')links[k].removeAttribute('aria-current');});}
addEventListener('scroll',spy,{passive:true});addEventListener('resize',spy);addEventListener('hashchange',function(){setTimeout(spy,50);});spy();})();
</script>"""


DOCS_SEARCH_JS = """<script>
(function(){var boxes=document.querySelectorAll('.ds-box');if(!boxes.length)return;var idx=null,loading=null,L=window.DS_L||{};
function load(){if(idx)return Promise.resolve(idx);if(!loading)loading=fetch('docs-search.json').then(function(r){return r.json();}).then(function(d){idx=d.map(function(s){s.lt=(s.t+' '+s.h).toLowerCase();s.lx=s.x.toLowerCase();return s;});return idx;});return loading;}
function esc(t){return t.replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function mark(t,ws){var h=esc(t);ws.forEach(function(w){if(w.length<2)return;h=h.replace(new RegExp('('+w.replace(/[.*+?^${}()|[\\]\\\\]/g,'\\\\$&')+')','gi'),'<mark>$1</mark>');});return h;}
function snip(s,ws){var x=s.x,i=-1;ws.some(function(w){i=s.lx.indexOf(w);return i>=0;});if(i<0)return x.slice(0,140);var a=Math.max(0,i-60);return (a?'…':'')+x.slice(a,a+170)+(a+170<x.length?'…':'');}
function search(q){var ws=q.toLowerCase().split(/\\s+/).filter(Boolean);if(!ws.length)return [];var r=[];
idx.forEach(function(s){var sc=0;for(var k=0;k<ws.length;k++){var w=ws[k],inT=s.lt.indexOf(w)>=0,inX=s.lx.indexOf(w)>=0;if(!inT&&!inX)return;sc+=inT?10:1;if(inX)sc+=Math.min(3,s.lx.split(w).length-1)*.2;}
if(!s.h)sc-=.5;r.push([sc,s]);});r.sort(function(a,b){return b[0]-a[0];});return r.slice(0,14).map(function(x){return x[1];});}
boxes.forEach(function(box){var inp=box.querySelector('.ds-input'),res=box.querySelector('.ds-results'),sel=-1;
function render(){var q=inp.value.trim();if(!q){res.hidden=true;return;}load().then(function(){var ws=q.toLowerCase().split(/\\s+/).filter(Boolean),hits=search(q);sel=-1;
res.innerHTML=hits.length?hits.map(function(s){return '<a class="ds-hit" href="docs-'+s.p+'.html'+(s.a?'#'+s.a:'')+'"><strong>'+mark(s.t,ws)+(s.h?' <span>› '+mark(s.h,ws)+'</span>':'')+'</strong><em>'+mark(snip(s,ws),ws)+'</em></a>';}).join(''):'<p class="ds-none">'+esc(L.none||'')+'</p>';res.hidden=false;});}
inp.addEventListener('input',render);inp.addEventListener('focus',function(){load();if(inp.value.trim())render();});
inp.addEventListener('keydown',function(e){var hs=res.querySelectorAll('.ds-hit');if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();if(!hs.length)return;sel=(sel+(e.key==='ArrowDown'?1:-1)+hs.length)%hs.length;hs.forEach(function(h,i){h.classList.toggle('on',i===sel);});hs[sel].scrollIntoView({block:'nearest'});}
else if(e.key==='Enter'){var h=hs[sel>=0?sel:0];if(h){e.preventDefault();location.href=h.href;}}else if(e.key==='Escape'){res.hidden=true;inp.blur();}});
document.addEventListener('click',function(e){if(!box.contains(e.target))res.hidden=true;});});
document.addEventListener('keydown',function(e){if(e.key==='/'&&!/input|textarea/i.test((document.activeElement||{}).tagName||'')){e.preventDefault();boxes[0].querySelector('.ds-input').focus();}});})();
</script>"""


def doc_page(page, manifest, ctx, root, repo, doc_pages):
    src = page['source']
    translated = False
    text_path = src
    if src.startswith('docs/'):
        # A translation in docs/<lang>/ replaces the source on that language's site;
        # links and images still resolve against the original's folder, so the translation
        # can keep every link target unchanged.
        alt = f'docs/{ctx.lang}/{src[5:]}'
        if (root / alt).is_file():
            text_path = alt
            translated = True
    text = (root / text_path).read_text(encoding='utf-8')
    base_dir = posixpath.dirname(src)
    raw = repo.replace('https://github.com/', 'https://raw.githubusercontent.com/') + '/main/'

    def link(href):
        if re.match(r'^(https?:|mailto:|#)', href):
            return href
        path, _, anchor = href.partition('#')
        target = posixpath.normpath(posixpath.join(base_dir, path)) if path else src
        if target.startswith(f'docs/{ctx.lang}/') and 'docs/' + target[len(ctx.lang) + 6:] in doc_pages:
            target = 'docs/' + target[len(ctx.lang) + 6:]
        if target in doc_pages:
            return f'docs-{doc_pages[target]}.html' + (f'#{anchor}' if anchor else '')
        kind = 'tree' if not posixpath.splitext(target)[1] else 'blob'
        return f'{repo}/{kind}/main/{target}' + (f'#{anchor}' if anchor else '')

    def image(href):
        if re.match(r'^https?:', href):
            return href
        return raw + posixpath.normpath(posixpath.join(base_dir, href))

    conv = Converter(link, image)
    body = conv.convert(text)
    toc = ''.join(f'<a class="toc-{lvl}" href="#{hid}">{esc(t[:90])}</a>'
                  for lvl, hid, t in conv.headings if lvl in (2, 3))[:20000]
    if translated:
        orig = f'{repo}/blob/main/{src}'
        note_html_tr = f'<p class="doc-note">{ctx.T["docsTranslated"].format(href=esc(orig))}</p>'
    note_key = 'docsLangNoteRu' if is_russian(text) else 'docsLangNote'
    note = ctx.T.get(note_key) or ''
    if note_key == 'docsLangNoteRu' and ctx.lang == 'ru':
        note = ''
    note_html = f'<p class="doc-note">{esc(note)}</p>' if note else ''
    if translated:
        note_html = note_html_tr
    return (f'<div class="docs-layout">{docs_nav(manifest, ctx, page["id"])}'
            f'<article class="doc" lang="{"ru" if is_russian(text) else "en"}">{note_html}{body}'
            f'<p class="doc-src"><a href="{esc(repo)}/blob/main/{esc(src)}">{ctx.T["docsSource"]}: {esc(src)}</a></p></article>'
            f'<aside class="doc-toc" aria-label="{ctx.T["docsOnPage"]}"><span>{ctx.T["docsOnPage"]}</span>{toc}</aside></div>'), conv.headings


def docs_index(manifest, ctx):
    secs = []
    for sec in manifest['sections']:
        cards = ''.join(f'<a class="doc-card" href="docs-{esc(p["id"])}.html"><strong>{esc(ctx.loc(p, "title"))}</strong>'
                        f'<span>{esc(ctx.loc(p, "lead") or "")}</span><code>{esc(p["source"])}</code></a>'
                        for p in sec['pages'])
        secs.append(f'<section class="sp-sec"><h2 class="h2">{esc(ctx.loc(sec, "title"))}</h2><div class="doc-cards">{cards}</div></section>')
    search = (f'<div class="ds-box ds-hero"><input class="ds-input" type="search" placeholder="{esc(ctx.T["docsSearch"])}" '
              f'aria-label="{esc(ctx.T["docsSearch"])}" autocomplete="off" spellcheck="false">'
              f'<div class="ds-results" hidden></div></div>')
    return (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["docsEyebrow"]}</p><h1>{ctx.T["docsTitle"]}</h1>'
            f'<p>{ctx.T["docsLead"]}</p>{search}</div></section>' + ''.join(secs) + '<div style="height:96px"></div>')


def localized_entries(entries, ctx):
    out = []
    for e in entries:
        e2 = json.loads(json.dumps(e))
        for k in ('title', 'summary', 'body', 'verified', 'limits'):
            e2[k] = ctx.loc(e, k)
        for m in e2['media']:
            m['caption'] = ctx.loc(m, 'caption')
            m.pop('captionEn', None)
            m['src'] = ctx.B + m['src']
        for l in e2.get('links') or []:
            l['title'] = ctx.loc(l, 'title')
            l.pop('titleEn', None)
        for k in list(e2):
            if k.endswith('En'):
                e2.pop(k)
        out.append(e2)
    return out


# ---------------------------------------------------------------- releases

def release_chip(releases, ctx):
    if not releases:
        return ''
    r = releases[0]
    return f'<a class="chip chip-link" href="releases.html">{ctx.T["relChip"].format(v=esc(r["version"]))}</a>'


def releases_page(data, entries, systems, ctx, repo):
    rels = data['releases']
    builds = data['builds']
    latest = rels[0] if rels else None
    since = latest['date'] if latest else '0000-00-00'
    names = {s['id']: ctx.loc(s, 'name') for s in systems}
    pages = {s['id'] for s in systems if s.get('flagship')}

    def sys_tag(sid):
        label = esc(names.get(sid, sid))
        if sid in pages:
            return f'<a class="rm-sys" href="system-{esc(sid)}.html">{label}</a>'
        return f'<span class="rm-sys">{label}</span>'
    shipped = data.get('_shipped')
    fresh = [e for e in entries if (e['id'] not in shipped if shipped is not None else e['date'] > since)]
    head = (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["relEyebrow"]}</p><h1>{ctx.T["relTitle"]}</h1>'
            f'<p>{ctx.T["relLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong>{esc(latest["version"]) if latest else "—"}</strong><span>{ctx.T["relLatest"]}</span></div>'
            f'<div><strong>{len(rels)}</strong><span>{ctx.T["relCount"]}</span></div></div></section>')
    nxt = ''
    if latest:
        items = ''.join(
            f'<li><span class="rm-meta"><span>{ctx.fmt_date(e["date"])}</span>'
            f'{sys_tag(e["system"])}</span>'
            f'<a href="devlog.html#{esc(e["id"])}">{esc(e["title"])}</a></li>'
            for e in fresh[:12])
        body = (f'<ul class="rel-next-list">{items}</ul>' if items else f'<p>{ctx.T["relNextEmpty"]}</p>')
        more = (f'<a class="rel-more" href="devlog.html">{ctx.T["relNextAll"].format(n=len(fresh))}</a>'
                if len(fresh) > 12 else '')
        nxt = (f'<section class="rel-next" id="next"><div class="rel-next-head"><h2>'
               f'{ctx.T["relNextTitleV"].format(v=esc(data["next"]["version"])) if data.get("next") else ctx.T["relNextTitle"]}</h2>'
               f'<p>{ctx.T["relNextLead"].format(v=esc(latest["version"]), d=ctx.fmt_date(latest["date"]))}</p></div>'
               f'{body}{more}</section>')
    cards = []
    for i, r in enumerate(rels):
        def hl_img(h):
            m = (h.get('media') or [None])[0]
            if not m:
                return ''
            src, cap = esc(ctx.media(m['src'])), esc(ctx.loc(m, 'caption'))
            return f'<a class="rel-hl-img" href="{src}" data-cap="{cap}"><img src="{src}" alt="{cap}" loading="lazy"></a>'
        hl = ''.join(f'<li{" class=has-img" if h.get("media") else ""}>{hl_img(h)}<div class="rel-hl-body"><h3>{esc(ctx.loc(h, "title"))}</h3>'
                     f'<p>{esc(ctx.loc(h, "text"))}</p></div></li>'
                     for h in r.get('highlights') or [])
        ver = ctx.loc(r, 'verified')
        notes = f'{repo}/releases/tag/{r["tag"]}'
        cards.append(
            f'<article class="rel-card{" rel-latest" if i == 0 else ""}" id="v{esc(r["version"])}">'
            f'<div class="rel-side"><span class="rel-ver">{esc(r["version"])}</span>'
            f'<span class="rel-date">{ctx.fmt_date(r["date"])}</span>'
            + (f'<span class="rel-badge">{ctx.T["relLatestBadge"]}</span>' if i == 0 else '') +
            f'<a class="rel-commit" href="{repo}/commit/{esc(r["commit"])}"><code>{esc(r["commit"][:9])}</code></a></div>'
            f'<div class="rel-main"><h2>{esc(ctx.loc(r, "title"))}</h2><p class="rel-summary">{esc(ctx.loc(r, "summary"))}</p>'
            + (f'<ul class="rel-hl">{hl}</ul>' if hl else '')
            + (f'<p class="rel-verified"><b>{ctx.T["relVerified"]}</b> {esc(ver)}</p>' if ver else '') +
            f'<div class="rel-dl" data-version="{esc(r["version"])}"><span class="rel-dl-title">{ctx.T["relDownloads"]}</span>'
            f'<div class="rel-dl-list">{loader("aiv-loader aiv-loader-inline")}<a class="btn btn-line btn-sm" href="{esc(builds["branch"])}">{ctx.T["relBuildsBranch"]}</a></div></div>'
            f'<a class="rel-notes" href="{notes}">{ctx.T["relNotes"]}</a></div></article>')
    other = (f'<section class="rel-other" hidden><h2>{ctx.T["relOtherTitle"]}</h2><p>{ctx.T["relOtherLead"]}</p>'
             f'<div class="rel-other-list"></div></section>')
    return head + nxt + f'<div class="rel-list">{"".join(cards)}</div>' + other


def gallery_page(entries, systems, ctx):
    """Every engine frame from the development log, newest first, filterable by system."""
    names = {s['id']: ctx.loc(s, 'name') for s in systems}
    shots = []
    for e in entries:   # already newest first
        for m in e['media']:
            if re.search(r'\.(mp4|webm)$', m['src'], re.I):
                continue
            shots.append((e, m))
    used = [s for s in systems if any(e['system'] == s['id'] for e, _ in shots)]
    chips = [f'<button class="filter" type="button" data-sys="" aria-pressed="true">{ctx.T["rmAll"]}</button>']
    chips += [f'<button class="filter" type="button" data-sys="{esc(s["id"])}" aria-pressed="false">{esc(names[s["id"]])}</button>'
              for s in used]
    items = []
    for e, m in shots:
        cap = esc(m.get('caption') or e['title'])
        src = esc(m['src'])
        items.append(f'<figure class="gal-item" data-sys="{esc(e["system"])}">'
                     f'<a class="gal-img" href="{src}" data-cap="{cap}"><img src="{src}" alt="{cap}" loading="lazy"></a>'
                     f'<figcaption><span class="gal-cap">{cap}</span>'
                     f'<a class="gal-meta mono" href="devblog-{esc(e["system"])}.html#{esc(e["id"])}">'
                     f'{esc(names.get(e["system"], e["system"]))} · {ctx.fmt_date(e["date"])}</a></figcaption></figure>')
    return (f'<section class="dl-hero"><div><p class="eyebrow">{ctx.T["navGallery"]}</p><h1>{ctx.T["galTitle"]}</h1>'
            f'<p>{ctx.T["galLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong>{len(shots)}</strong><span>{ctx.T["galShots"]}</span></div>'
            f'<div><strong>{len(used)}</strong><span>{ctx.T["rmSystems"]}</span></div></div></section>'
            f'<div class="dl-filters gal-filters" role="group" aria-label="{ctx.T["rmFilter"]}">{"".join(chips)}</div>'
            f'<div class="gal-grid">{"".join(items)}</div>{LIGHTBOX}')


GALLERY_JS = """<script>
(function(){var b=document.querySelectorAll('.gal-filters .filter');function set(id){b.forEach(function(x){x.setAttribute('aria-pressed',String(x.dataset.sys===id));});
document.querySelectorAll('.gal-item').forEach(function(p){p.hidden=!!id&&p.dataset.sys!==id;});history.replaceState(null,'',id?'#system='+id:location.pathname);}
b.forEach(function(x){x.addEventListener('click',function(){set(x.dataset.sys);});});
var m=location.hash.match(/^#system=(.+)$/);if(m&&document.querySelector('.gal-filters [data-sys="'+m[1]+'"]'))set(m[1]);})();
</script>"""


RELEASES_JS = """<script>
(function(){var cfg=window.REL_CFG;if(!cfg)return;
function mb(n){return (n/1048576).toFixed(0)+' MB';}
function btn(p){var a=document.createElement('a');a.className='btn btn-ink btn-sm rel-pkg';a.href=p.url;
a.textContent=(cfg.os[p.platform]||p.platform)+' · '+mb(p.size);a.title='sha256 '+p.sha256+' · '+p.commit;return a;}
fetch(cfg.index,{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(d){
var pk=d.packages||[],known={},other=[];
document.querySelectorAll('.rel-dl').forEach(function(el){known[el.dataset.version]=el;});
var used={};pk.forEach(function(p){var el=known[p.version];
if(el&&!used[p.version+p.platform]){used[p.version+p.platform]=1;var l=el.querySelector('.rel-dl-list');
if(!el.dataset.filled){l.textContent='';el.dataset.filled='1';}l.appendChild(btn(p));}else if(!el)other.push(p);});
document.querySelectorAll('.rel-dl:not([data-filled])').forEach(function(el){var s=document.createElement('span');
s.className='rel-dl-none';s.textContent=cfg.none;el.querySelector('.rel-dl-list').prepend(s);});
if(other.length){var box=document.querySelector('.rel-other'),list=box.querySelector('.rel-other-list'),g={};
other.forEach(function(p){var k=p.version+' · '+p.commit;(g[k]=g[k]||[]).push(p);});
Object.keys(g).forEach(function(k){var row=document.createElement('div');row.className='rel-other-row';
var c=document.createElement('code');c.textContent=k;row.appendChild(c);g[k].forEach(function(p){row.appendChild(btn(p));});list.appendChild(row);});
box.hidden=false;}}).catch(function(){}).then(function(){document.querySelectorAll('.rel-dl .aiv-loader').forEach(function(e){e.remove();});});})();
</script>"""


def catalog_page(cfg, ctx):
    """Package catalog: the cards are filled in the browser from AIVERSE-PACKAGES index.json."""
    T = ctx.T
    return (f'<section class="dl-hero"><div><p class="eyebrow">{T["catEyebrow"]}</p><h1>{T["catTitle"]}</h1>'
            f'<p>{T["catLead"]}</p></div>'
            f'<div class="dl-stats"><div><strong class="cat-n">—</strong><span>{T["catCount"]}</span></div>'
            f'<div><strong class="cat-c">—</strong><span>{T["catCategories"]}</span></div></div></section>'
            f'<div class="cat-bar"><div class="dl-filters cat-filters" role="group" aria-label="{T["catFilter"]}">'
            f'<button class="filter" type="button" data-cat="" aria-pressed="true">{T["catAll"]}</button></div>'
            f'<label class="dl-search cat-search"><span aria-hidden="true">⌕</span>'
            f'<input type="search" placeholder="{T["catSearch"]}" aria-label="{T["catSearch"]}"></label></div>'
            f'<p class="cat-status">{loader("aiv-loader", T["catLoading"])}</p>'
            f'<div class="cat-grid"></div>'
            f'<section class="cat-howto"><div><h2>{T["catHowTitle"]}</h2><p>{T["catHowLead"]}</p>'
            f'<pre><code>python3 tools/pkg/aiverse_pkg.py search terrain\n'
            f'python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project ~/Games/MyGame</code></pre></div>'
            f'<div id="add"><h2>{T["catAddTitle"]}</h2><p>{T["catAddLead"]}</p>'
            f'<ol class="cat-steps">{"".join(f"<li>{x}</li>" for x in T["catAddSteps"])}</ol>'
            f'<p><a class="btn btn-ink btn-sm" href="docs-packages.html#{T["catAddAnchor"]}">{T["catAddBtn"]}</a> '
            f'<a class="btn btn-line btn-sm" href="{esc(cfg["repo"])}/blob/main/CONTRIBUTING.md">CONTRIBUTING.md</a> '
            f'<a class="btn btn-line btn-sm" href="{esc(cfg["repo"])}">GitHub</a></p></div></section>')


CATALOG_JS = """<script>
(function(){var cfg=window.CAT_CFG;if(!cfg)return;var T=cfg.t,lang=cfg.lang;
function loc(o,k){return (lang!=='en'&&o[k+'-'+lang])||o[k]||'';}
function el(tag,cls,text){var e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e;}
function mb(n){return n>=1048576?(n/1048576).toFixed(n>=104857600?0:1)+' MB':Math.max(1,Math.round(n/1024))+' KB';}
var grid=document.querySelector('.cat-grid'),status=document.querySelector('.cat-status'),bar=document.querySelector('.cat-filters'),
search=document.querySelector('.cat-search input'),cur='',cards=[];
function apply(){var q=(search.value||'').toLowerCase().trim(),n=0;cards.forEach(function(c){var ok=(!cur||c.cat===cur)&&(!q||c.hay.indexOf(q)>=0);
c.el.hidden=!ok;if(ok)n++;});status.textContent=n?'':T.none;status.hidden=!!n;}
function card(p){var v=p.versions[p.latest],a=el('article','cat-card');a.id='pkg-'+p.id;
var media=el('div','cat-media'),img=el('img');img.loading='lazy';img.alt=loc(p,'title');img.src=(p.previews||[])[0]||'';media.appendChild(img);
if((p.previews||[]).length>1){var th=el('div','cat-thumbs');p.previews.forEach(function(src,i){var b=el('button','cat-thumb');b.type='button';
b.setAttribute('aria-label',(i+1)+'/'+p.previews.length);var t=el('img');t.src=src;t.alt='';t.loading='lazy';b.appendChild(t);
b.onclick=function(){img.src=src;};th.appendChild(b);});media.appendChild(th);}a.appendChild(media);
var body=el('div','cat-body'),meta=el('div','cat-meta');meta.appendChild(el('span','chip',cfg.cats[p.category]||p.category));
meta.appendChild(el('span','cat-ver','v'+p.latest));body.appendChild(meta);
body.appendChild(el('h2',null,loc(p,'title')));body.appendChild(el('p','cat-summary',loc(p,'summary')));
var d=loc(p,'description');if(d){var det=el('details','cat-more');det.appendChild(el('summary',null,T.more));det.appendChild(el('p',null,d));body.appendChild(det);}
var facts=el('dl','cat-facts');[[T.engine,(v.minEngine||'—')+'+'],[T.size,mb(v.size)],[T.license,p.license||'—'],[T.kind,cfg.kinds[p.kind]||p.kind]].forEach(function(f){
var w=el('div');w.appendChild(el('dt',null,f[0]));w.appendChild(el('dd',null,f[1]));facts.appendChild(w);});body.appendChild(facts);
var cmd=(p.kind==='content'?'add '+p.id+' --project <project>':p.kind==='template'?'create '+p.id+' <folder>':'fetch '+p.id);
var pre=el('div','cat-cmd'),code=el('code',null,'python3 tools/pkg/aiverse_pkg.py '+cmd),cp=el('button','cat-copy',T.copy);cp.type='button';
cp.onclick=function(){navigator.clipboard&&navigator.clipboard.writeText(code.textContent).then(function(){cp.textContent=T.copied;setTimeout(function(){cp.textContent=T.copy;},1500);});};
pre.appendChild(code);pre.appendChild(cp);body.appendChild(pre);
var act=el('div','cat-actions'),dl=el('a','btn btn-ink btn-sm',T.download+' · '+mb(v.size));dl.href=v.url;dl.title='sha256 '+v.sha256;act.appendChild(dl);
if(p.readme){var r=el('a','btn btn-line btn-sm',T.readme);r.href=p.readme;act.appendChild(r);}
a.item=0;a.link=function(){return location.origin+location.pathname+'#pkg-'+p.id+(a.classList.contains('is-open')&&a.item?'/'+a.item:'');};
if(p.viewer){var v3=el('button','btn btn-accent btn-sm cat-3d',T.view3d);v3.type='button';
a.open3d=function(item){var f=media.querySelector('iframe');if(f){f.remove();img.hidden=false;v3.textContent=T.view3d;a.classList.remove('is-open');
history.replaceState(null,'','#pkg-'+p.id);a.scrollIntoView({block:'nearest'});return;}
cards.forEach(function(c){if(c.el!==a&&c.el.querySelector('.cat-frame'))c.el.open3d();});
a.item=item||0;a.classList.add('is-open');setTimeout(function(){a.scrollIntoView({behavior:'smooth',block:'start'});},30);
f=el('iframe','cat-frame');f.src=p.viewer+(p.viewer.indexOf('?')<0?'?':'&')+'lang='+lang+(a.item?'&item='+a.item:'');f.title=loc(p,'title')+' — 3D';f.setAttribute('allow','fullscreen');f.loading='lazy';
img.hidden=true;media.appendChild(f);v3.textContent=T.view3dClose;history.replaceState(null,'','#pkg-'+p.id+(a.item?'/'+a.item:''));};
v3.onclick=function(){a.open3d(0);};act.insertBefore(v3,act.firstChild);}
var ln=el('button','btn btn-line btn-sm',T.link);ln.type='button';ln.title=T.link;
ln.onclick=function(){var u=a.link();history.replaceState(null,'','#'+u.split('#')[1]);navigator.clipboard&&navigator.clipboard.writeText(u).then(function(){ln.textContent=T.linkCopied;setTimeout(function(){ln.textContent=T.link;},1500);});};
act.appendChild(ln);
body.appendChild(act);a.appendChild(body);
cards.push({el:a,cat:p.category,hay:[p.id].concat(Object.keys(p).filter(function(k){return /^(title|summary)(-|$)/.test(k);}).map(function(k){return p[k];}),
(p.tags||[]).join(' '),JSON.stringify(p.provides||{})).join(' ').toLowerCase()});
return a;}
function json(u){return fetch(u,{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json();});}
var base=cfg.index.replace(/[^/]*$/,'');
// index.json lists {id, category, path}; each package folder has aiverse.package.json + versions.json
json(cfg.index).then(function(d){return Promise.all((d.packages||[]).map(function(it){
return Promise.all([json(base+it.path+'/aiverse.package.json'),json(base+it.path+'/versions.json')]).then(function(r){
var p=r[0];['latest','versions','previews','readme','viewer'].forEach(function(k){if(r[1][k]!=null)p[k]=r[1][k];});p.category=it.category;return p;},
function(){return null;});}));}).then(function(list){
var pk=list.filter(function(p){return p&&p.versions&&p.versions[p.latest];}),used={};pk.forEach(function(p){used[p.category]=1;grid.appendChild(card(p));});
Object.keys(cfg.cats).forEach(function(c){if(!used[c])return;var b=el('button','filter',cfg.cats[c]);b.type='button';b.dataset.cat=c;b.setAttribute('aria-pressed','false');bar.appendChild(b);});
bar.querySelectorAll('.filter').forEach(function(b){b.onclick=function(){cur=b.dataset.cat;bar.querySelectorAll('.filter').forEach(function(x){x.setAttribute('aria-pressed',String(x===b));});
history.replaceState(null,'',cur?'#cat='+cur:location.pathname);apply();};});
document.querySelector('.cat-n').textContent=pk.length;document.querySelector('.cat-c').textContent=Object.keys(used).length;
var h=location.hash.match(/^#cat=([a-z]+)/);if(h){var b=bar.querySelector('[data-cat="'+h[1]+'"]');if(b)b.click();}
status.textContent='';apply();
var m=location.hash.match(/^#pkg-([a-z0-9-]+)(?:[/]([0-9]+))?/);if(m){var t=document.getElementById('pkg-'+m[1]);
if(t){if(t.open3d)t.open3d(parseInt(m[2]||'0',10));else t.scrollIntoView();}}
// the 3D viewer tells which model it shows: the address points to it (#pkg-<id>/<model>)
window.addEventListener('message',function(e){var d=e.data;if(!d||d.cv!=='item')return;cards.forEach(function(c){
var f=c.el.querySelector('.cat-frame');if(f&&f.contentWindow===e.source){c.el.item=d.item;history.replaceState(null,'','#'+c.el.link().split('#')[1]);}});});
}).catch(function(){status.textContent=T.error;});
search.addEventListener('input',apply);})();
</script>"""
