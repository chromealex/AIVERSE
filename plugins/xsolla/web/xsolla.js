// Xsolla Pay Station → the engine's "store" protocol (docs/plugins.md) for web builds.
// The payment token is created by the game's backend (it knows the player and signs the order), so
// purchase() asks `tokenUrl` (or uses a token handed over with the "token" method), opens Pay Station
// in a window and answers "pending". Xsolla notifies the backend (webhook), which grants the goods;
// with `statusUrl` the plugin also asks the backend after the window closes and reports the result as
// a "purchase" event (state "purchased"). restore() is empty: purchases live on the backend.
(function () {
  "use strict";
  var NAME = "xsolla";
  var cfg = {}, headers = {}, handedToken = null;
  var emit = function (ev, data) { window.AiversePlugins.emit(NAME, ev, data || {}); };
  function sku(id) { return (cfg.items && cfg.items[id]) || id; }
  function post(url, body) {
    var h = { "Content-Type": "application/json" };
    for (var k in headers) h[k] = headers[k];
    return fetch(url, { method: "POST", headers: h, body: JSON.stringify(body), credentials: "include" })
      .then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status); return r.json(); });
  }
  function payStation(token) {
    return (cfg.sandbox ? "https://sandbox-secure.xsolla.com" : "https://secure.xsolla.com") + "/paystation4/?token=" + encodeURIComponent(token);
  }
  function micros(amount) { return Math.round(parseFloat(amount || "0") * 1e6); }

  window.AiversePlugins.register(NAME, {
    init: function (config) {
      cfg = config || {};
      headers = cfg.headers || {};
      if (!cfg.projectId) throw new Error("xsolla: projectId missing in game.json plugins.xsolla");
      return {};
    },
    session: function (a) { headers = (a && a.headers) || {}; return {}; },
    token: function (a) { handedToken = a && a.token; return {}; },
    products: function (a) {
      var url = "https://store.xsolla.com/api/v2/project/" + encodeURIComponent(cfg.projectId) + "/items/virtual_items?limit=100";
      return fetch(url).then(function (r) { if (!r.ok) throw new Error("xsolla catalog: HTTP " + r.status); return r.json(); })
        .then(function (cat) {
          var bySku = {};
          (cat.items || []).forEach(function (it) { bySku[it.sku] = it; });
          var out = [];
          ((a && a.products) || []).forEach(function (p) {
            var it = bySku[sku(p.id)];
            if (!it || !it.price) return;
            out.push({ id: p.id, type: p.type, title: it.name || "", description: it.description || "",
                       price: it.price.amount + " " + it.price.currency, priceMicros: micros(it.price.amount), currency: it.price.currency });
          });
          return out;
        });
    },
    purchase: function (a) {
      var id = a.id;
      var got = handedToken ? Promise.resolve({ token: handedToken })
              : cfg.tokenUrl ? post(cfg.tokenUrl, { productId: id, sku: sku(id) })
              : Promise.reject(new Error("xsolla: no payment token (game.json tokenUrl, or the token method)"));
      handedToken = null;
      return got.then(function (t) {
        var token = t.token || (t.data && t.data.token);
        if (!token) throw new Error("xsolla: the backend returned no token");
        var w = window.open(payStation(token), "xsolla", "width=760,height=760");
        if (!w) throw new Error("xsolla: the payment window was blocked (open it from a click)");
        emit("app.pause");
        var timer = setInterval(function () {
          if (!w.closed) return;
          clearInterval(timer);
          emit("app.resume");
          if (!cfg.statusUrl) return;
          post(cfg.statusUrl, { productId: id, token: token }).then(function (s) {
            if (s && (s.paid || s.status === "done"))
              emit("purchase", { productId: id, state: "purchased", token: token, orderId: String(s.invoiceId || s.invoice_id || ""), receipt: { xsollaToken: token } });
          }).catch(function () {});
        }, 500);
        return { productId: id, state: "pending", token: token, orderId: "", receipt: { xsollaToken: token, sku: sku(id) } };
      });
    },
    restore: function () { return []; },
    finish: function () { return {}; },
    lifecycle: function () { return {}; }
  });
})();
