// GameAnalytics JavaScript SDK (UMD build, global `gameanalytics`) → the engine's "analytics" protocol
// (docs/plugins.md). Same config as the native plugins; use the web game's own gameKey / secretKey.
(function () {
  "use strict";
  var NAME = "gameanalytics";
  var started = false, customEvents = true, globalFields = {};

  function GA() {
    var ns = window.gameanalytics;
    if (!ns || !ns.GameAnalytics) throw new Error("GameAnalytics SDK is not loaded (GameAnalytics.min.js)");
    return ns;
  }
  function fields(f) {
    var r = {};
    if (f && typeof f === "object") Object.keys(f).forEach(function (k) {
      var v = f[k];
      if (typeof v === "string" || typeof v === "number") r[k] = v;
    });
    return r;
  }
  function list(v) { return Array.isArray(v) ? v.map(String) : []; }
  function need() { if (!started) throw new Error("gameanalytics: not initialized (gameKey / secretKey missing?)"); return GA(); }

  window.AiversePlugins.register(NAME, {
    init: function (cfg) {
      cfg = cfg || {};
      if (!cfg.gameKey || !cfg.secretKey) throw new Error("gameanalytics: gameKey and secretKey are required");
      var ns = GA(), ga = ns.GameAnalytics;
      customEvents = cfg.customEvents !== false;
      ga.setEnabledInfoLog(!!cfg.infoLog);
      ga.setEnabledVerboseLog(!!cfg.verboseLog);
      if (cfg.build !== undefined) ga.configureBuild(String(cfg.build));
      if (list(cfg.currencies).length) ga.configureAvailableResourceCurrencies(list(cfg.currencies));
      if (list(cfg.itemTypes).length) ga.configureAvailableResourceItemTypes(list(cfg.itemTypes));
      if (list(cfg.dimensions01).length) ga.configureAvailableCustomDimensions01(list(cfg.dimensions01));
      if (list(cfg.dimensions02).length) ga.configureAvailableCustomDimensions02(list(cfg.dimensions02));
      if (list(cfg.dimensions03).length) ga.configureAvailableCustomDimensions03(list(cfg.dimensions03));
      if (cfg.userId) ga.configureUserId(String(cfg.userId));
      ga.initialize(String(cfg.gameKey), String(cfg.secretKey));
      started = true;
      return {};
    },
    lifecycle: function () { return null; },
    flush: function () { return null; },
    consent: function (a) { GA().GameAnalytics.setEnabledEventSubmission(a.analytics !== false); return null; },
    user: function (a) {
      var ga = GA().GameAnalytics, props = a.properties || {}, global = false;
      if (a.id !== undefined) ga.configureUserId(String(a.id));
      Object.keys(props).forEach(function (k) {
        var v = String(props[k]);
        if (k === "dimension01") ga.setCustomDimension01(v);
        else if (k === "dimension02") ga.setCustomDimension02(v);
        else if (k === "dimension03") ga.setCustomDimension03(v);
        else { globalFields[k] = v; global = true; }
      });
      if (global) ga.setGlobalCustomEventFields(Object.assign({}, globalFields));
      return null;
    },
    event: function (a) { var ns = need(); if (customEvents) ns.GameAnalytics.addDesignEvent(a.name, undefined, fields(a.fields)); return null; },
    design: function (a) { need().GameAnalytics.addDesignEvent(a.id, typeof a.value === "number" ? a.value : undefined, fields(a.fields)); return null; },
    progression: function (a) {
      var ns = need(), p = a.path || [];
      if (!p.length) return null;
      var st = a.status === "complete" ? ns.EGAProgressionStatus.Complete : a.status === "fail" ? ns.EGAProgressionStatus.Fail : ns.EGAProgressionStatus.Start;
      ns.GameAnalytics.addProgressionEvent(st, p[0], p[1], p[2], typeof a.score === "number" ? a.score : undefined, fields(a.fields));
      return null;
    },
    resource: function (a) {
      var ns = need();
      var flow = a.flow === "sink" ? ns.EGAResourceFlowType.Sink : ns.EGAResourceFlowType.Source;
      ns.GameAnalytics.addResourceEvent(flow, a.currency, Number(a.amount) || 0, a.itemType, a.itemId, fields(a.fields));
      return null;
    },
    purchase: function (a) {
      var ns = need();
      ns.GameAnalytics.addBusinessEvent(a.currency, Math.round((Number(a.priceMicros) || 0) / 10000), a.itemType, a.itemId, a.cartType || "shop", fields(a.fields));
      return null;
    },
  });
})();
