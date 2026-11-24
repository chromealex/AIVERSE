// AIVERSE devlog page: renders devlog.json built by tools/site/build.py. No framework.
(function () {
  'use strict';
  var I18N = {
    ru: { status: { 'done': 'Готово', 'in-progress': 'В работе', 'experimental': 'Эксперимент' },
          months: ['январь', 'февраль', 'март', 'апрель', 'май', 'июнь', 'июль', 'август', 'сентябрь', 'октябрь', 'ноябрь', 'декабрь'],
          open: 'Открыть снимок: ', verified: 'Проверено', limits: 'Ограничения', more: 'Подробнее', all: 'Все системы',
          none: 'Записей с такими условиями нет.', failed: 'Не удалось загрузить devlog.json.', updated: 'MIT License · обновлено ' },
    en: { status: { 'done': 'Done', 'in-progress': 'In progress', 'experimental': 'Experimental' },
          months: ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'],
          open: 'Open frame: ', verified: 'Verified', limits: 'Limitations', more: 'Details', all: 'All systems',
          none: 'No entries match these filters.', failed: 'Could not load devlog.json.', updated: 'MIT License · updated ' }
  };
  var L = I18N[document.documentElement.lang] || I18N.en;
  var STATUS = L.status;
  var MONTHS = L.months;
  var state = { system: '', status: '', q: '' };
  var data = null;

  function el(tag, attrs, children) {
    var n = document.createElement(tag);
    Object.keys(attrs || {}).forEach(function (k) {
      if (k === 'text') n.textContent = attrs[k];
      else if (k === 'class') n.className = attrs[k];
      else n.setAttribute(k, attrs[k]);
    });
    (children || []).forEach(function (c) { if (c) n.appendChild(c); });
    return n;
  }

  function systemName(id) {
    var s = data.systems.filter(function (x) { return x.id === id; })[0];
    return s ? s.name : id;
  }

  function matches(e) {
    if (state.system && e.system !== state.system) return false;
    if (state.status && e.status !== state.status) return false;
    if (state.q) {
      var hay = [e.title, e.summary, (e.body || []).join(' '), systemName(e.system)].join(' ').toLowerCase();
      if (hay.indexOf(state.q) < 0) return false;
    }
    return true;
  }

  function media(e) {
    var list = e.media.slice(0, 4);
    var box = el('div', { class: 'entry-media n' + list.length });
    list.forEach(function (m) {
      var isVideo = /\.(mp4|webm)$/i.test(m.src);
      var inner = isVideo
        ? el('video', { src: m.src, controls: '', muted: '', playsinline: '', preload: 'metadata', 'aria-label': m.caption })
        : el('img', { src: m.src, alt: m.caption, loading: 'lazy' });
      if (isVideo) { box.appendChild(inner); return; }
      var b = el('button', { type: 'button', 'aria-label': L.open + m.caption }, [inner, el('span', { class: 'cap', text: m.caption })]);
      b.addEventListener('click', function () { openLightbox(m); });
      box.appendChild(b);
    });
    return box;
  }

  function entry(e) {
    var meta = el('div', { class: 'entry-meta' }, [
      el('span', { class: 'st st-' + e.status, text: STATUS[e.status] || e.status }),
      el('a', { class: 'entry-sys', href: 'devblog-' + e.system + '.html', text: systemName(e.system) }),
      el('time', { datetime: e.date, text: e.date }),
      e.branch ? el('span', { text: e.branch + (e.commit ? ' · ' + e.commit : '') }) : null
    ]);
    var facts = el('dl', { class: 'facts-list' }, [
      el('div', {}, [el('dt', { text: L.verified }), el('dd', { text: e.verified })]),
      e.limits ? el('div', {}, [el('dt', { text: L.limits }), el('dd', { text: e.limits })]) : null
    ]);
    var details = el('details', {}, [el('summary', { text: L.more })]
      .concat((e.body || []).map(function (p) { return el('p', { text: p }); }))
      .concat([facts]));
    var links = (e.links || []).length ? el('div', { class: 'entry-links' }, e.links.map(function (l) {
      return el('a', { href: l.href, text: l.title });
    })) : null;
    return el('article', { class: 'entry', id: e.id }, [
      media(e),
      el('div', { class: 'entry-body' }, [meta, el('h2', { text: e.title }), el('p', { class: 'summary', text: e.summary }), details, links])
    ]);
  }

  function renderSystems() {
    var host = document.getElementById('systems');
    host.textContent = '';
    var counts = {};
    data.entries.forEach(function (e) { counts[e.system] = (counts[e.system] || 0) + 1; });
    var all = [{ id: '', name: L.all }].concat(data.systems.filter(function (s) { return counts[s.id]; }));
    all.forEach(function (s) {
      var b = el('button', { class: 'sys', type: 'button', 'aria-pressed': String(state.system === s.id) }, [
        el('span', { text: s.name }), el('span', { class: 'count', text: String(s.id ? counts[s.id] : data.entries.length) })
      ]);
      b.addEventListener('click', function () { state.system = s.id; sync(); });
      host.appendChild(b);
    });
  }

  function renderFeed() {
    var feed = document.getElementById('feed');
    feed.textContent = '';
    var list = data.entries.filter(matches);
    if (!list.length) { feed.appendChild(el('p', { class: 'empty', text: L.none })); return; }
    var month = '';
    list.forEach(function (e) {
      var m = e.date.slice(0, 7);
      if (m !== month) {
        month = m;
        feed.appendChild(el('h2', { class: 'month', text: MONTHS[+m.slice(5, 7) - 1] + ' ' + m.slice(0, 4) }));
      }
      feed.appendChild(entry(e));
    });
    var gap = el('div', { style: 'display:flex;flex-direction:column;gap:20px' });
    while (feed.firstChild) gap.appendChild(feed.firstChild);
    feed.appendChild(gap);
  }

  function sync() {
    document.querySelectorAll('.filter').forEach(function (b) { b.setAttribute('aria-pressed', String(b.dataset.status === state.status)); });
    var h = state.system ? '#system=' + state.system : ' ';
    if (location.hash !== h.trim()) history.replaceState(null, '', h === ' ' ? location.pathname : h);
    renderSystems();
    renderFeed();
  }

  function openLightbox(m) {
    var lb = document.getElementById('lightbox');
    document.getElementById('lb-img').src = m.src;
    document.getElementById('lb-img').alt = m.caption;
    document.getElementById('lb-cap').textContent = m.caption;
    lb.classList.add('open');
    document.getElementById('lb-close').focus();
  }
  function closeLightbox() { document.getElementById('lightbox').classList.remove('open'); }
  document.getElementById('lb-close').addEventListener('click', closeLightbox);
  document.getElementById('lightbox').addEventListener('click', function (ev) { if (ev.target.id === 'lightbox') closeLightbox(); });
  document.getElementById('lightbox').addEventListener('keydown', function (ev) { if (ev.key === 'Escape') closeLightbox(); });

  document.querySelectorAll('.filter').forEach(function (b) {
    b.addEventListener('click', function () { state.status = b.dataset.status; sync(); });
  });
  document.getElementById('q').addEventListener('input', function (ev) { state.q = ev.target.value.trim().toLowerCase(); renderFeed(); });

  fetch('devlog.json', { cache: 'no-cache' }).then(function (r) { return r.json(); }).then(function (json) {
    data = json;
    var m = /^#system=([a-z0-9-]+)$/.exec(location.hash);
    if (m) state.system = m[1];
    var used = {};
    data.entries.forEach(function (e) { used[e.system] = 1; });
    document.getElementById('stat-entries').textContent = data.entries.length;
    document.getElementById('stat-systems').textContent = Object.keys(used).length;
    document.getElementById('stat-date').textContent = data.entries.length ? data.entries[0].date.slice(5).split('-').reverse().join('.') : '—';
    document.getElementById('generated').textContent = L.updated + data.generated.slice(0, 10);
    sync();
    if (location.hash.length > 1 && !m) {
      var target = document.getElementById(decodeURIComponent(location.hash.slice(1)));
      if (target) target.scrollIntoView();
    }
  }).catch(function () {
    document.getElementById('feed').textContent = L.failed;
  });
})();
