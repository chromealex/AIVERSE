// Web demos open in a popup over the page instead of a separate tab.
// Any link to <site>/demos/<id>/ is a demo link; Cmd/Ctrl/Shift/middle click still opens it normally.
(function () {
  'use strict';
  var ru = (document.documentElement.lang || '').indexOf('ru') === 0;
  var L = ru ? { close: 'Закрыть', full: 'На весь экран', tab: 'Открыть в новой вкладке', play: 'Играть в браузере' }
             : { close: 'Close', full: 'Full screen', tab: 'Open in a new tab', play: 'Play in the browser' };
  var DEMO = /\/demos\/[^/]+\/?$/;

  function demoUrl(a) {
    if (!a || !a.href || a.closest('.demo-modal')) return null;
    var u;
    try { u = new URL(a.href, location.href); } catch (e) { return null; }
    if (!/^https?:$/.test(u.protocol) || !DEMO.test(u.pathname)) return null;
    // Only the site's own demos: same host, or the published GitHub Pages host of this site.
    if (u.host !== location.host && !/\.github\.io$/.test(u.host)) return null;
    return u;
  }

  function mark(root) {
    var links = (root || document).querySelectorAll('a[href]');
    for (var i = 0; i < links.length; i++) {
      var a = links[i];
      if (!a.classList.contains('demo-link') && demoUrl(a)) {
        a.classList.add('demo-link');
        if (!a.title) a.title = L.play;
      }
    }
  }

  var modal = null, frame = null, heading = null, opener = null, tabLink = null;

  function build() {
    modal = document.createElement('div');
    modal.className = 'demo-modal';
    modal.setAttribute('role', 'dialog');
    modal.setAttribute('aria-modal', 'true');
    modal.innerHTML =
      '<div class="demo-shell">' +
      '<div class="demo-bar"><span class="demo-title"></span>' +
      '<div class="demo-actions">' +
      '<a class="demo-btn demo-tab" target="_blank" rel="noopener"></a>' +
      '<button type="button" class="demo-btn demo-full"></button>' +
      '<button type="button" class="demo-btn demo-close"></button>' +
      '</div></div>' +
      '<div class="demo-stage"></div></div>';
    heading = modal.querySelector('.demo-title');
    tabLink = modal.querySelector('.demo-tab');
    tabLink.textContent = L.tab;
    var full = modal.querySelector('.demo-full'), close = modal.querySelector('.demo-close');
    full.textContent = L.full;
    close.setAttribute('aria-label', L.close);
    close.innerHTML = '<span aria-hidden="true">✕</span>';
    full.addEventListener('click', function () {
      var stage = modal.querySelector('.demo-stage');
      var req = stage.requestFullscreen || stage.webkitRequestFullscreen;
      if (req) req.call(stage);
    });
    close.addEventListener('click', hide);
    modal.addEventListener('click', function (e) { if (e.target === modal) hide(); });
    document.body.appendChild(modal);
  }

  function show(url, title, from) {
    if (!modal) build();
    opener = from || null;
    heading.textContent = title || '';
    tabLink.href = url.href;
    frame = document.createElement('iframe');
    frame.src = url.href;
    frame.title = title || L.play;
    frame.setAttribute('allow', 'fullscreen; autoplay; gamepad; xr-spatial-tracking');
    frame.setAttribute('allowfullscreen', '');
    modal.querySelector('.demo-stage').appendChild(frame);
    document.documentElement.classList.add('demo-open');
    modal.classList.add('is-open');
    setTimeout(function () { try { frame.focus(); } catch (e) {} }, 50);
  }

  function hide() {
    if (!modal || !modal.classList.contains('is-open')) return;
    if (document.fullscreenElement && document.exitFullscreen) document.exitFullscreen();
    modal.classList.remove('is-open');
    document.documentElement.classList.remove('demo-open');
    if (frame) { frame.src = 'about:blank'; frame.remove(); frame = null; }   // stops audio and frees the GPU
    if (opener && opener.focus) opener.focus();
  }

  function titleFor(a) {
    var card = a.closest('article, .blog-card, li');
    var h = card && card.querySelector('h2, h3');
    return (h && h.textContent.trim()) || a.textContent.trim();
  }

  document.addEventListener('click', function (e) {
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    var a = e.target.closest && e.target.closest('a[href]');
    var url = demoUrl(a);
    if (!url) return;
    e.preventDefault();
    show(url, titleFor(a), a);
  });
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') hide(); });

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { mark(); });
  else mark();
  // The diary renders its entries after loading devlog.json.
  var pending = false;
  if (window.MutationObserver) new MutationObserver(function () {
    if (pending) return;
    pending = true;
    requestAnimationFrame(function () { pending = false; mark(); });
  }).observe(document.documentElement, { childList: true, subtree: true });
})();
