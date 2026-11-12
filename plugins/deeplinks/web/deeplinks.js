// Deep links → the engine's "links" protocol (docs/plugins.md) on the web: the page URL is the launch
// link (a portal or a share link with ?invite=..), hash changes and history navigation are links
// while the game runs.
(function () {
  "use strict";
  var NAME = "deeplinks";
  var launch = String(window.location.href || "");
  function changed() { window.AiversePlugins.emit(NAME, "link", { url: String(window.location.href) }); }
  window.AiversePlugins.register(NAME, {
    init: function () {
      window.addEventListener("hashchange", changed);
      window.addEventListener("popstate", changed);
      return {};
    },
    initial: function () { return { url: launch.indexOf("?") >= 0 || launch.indexOf("#") >= 0 ? launch : "" }; },
    lifecycle: function () { return {}; }
  });
})();
