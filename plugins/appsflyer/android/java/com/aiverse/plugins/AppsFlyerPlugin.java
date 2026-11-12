// AppsFlyer SDK 6.15 → the engine's "analytics" and "links" protocols (docs/plugins.md).
// analytics: event → logEvent(name, fields); purchase → af_purchase (af_revenue, af_currency,
// af_content_id, af_content_type, af_order_id); progression complete → af_level_achieved;
// user → setCustomerUserId / setAdditionalData; consent → AppsFlyerConsent (GDPR) + anonymize
// when analytics is off; deferredLink → the deferred deep link (first launch through a OneLink).
// links: subscribeForDeepLink → event "link" {url, deferred, data}; "initial" = the launch link.
package com.aiverse.plugins;

import com.aiverse.platform.AiversePlugin;
import com.appsflyer.AFInAppEventParameterName;
import com.appsflyer.AFInAppEventType;
import com.appsflyer.AppsFlyerConsent;
import com.appsflyer.AppsFlyerLib;
import com.appsflyer.deeplink.DeepLink;
import com.appsflyer.deeplink.DeepLinkResult;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

public class AppsFlyerPlugin extends AiversePlugin {
    private boolean started;
    private String launchUrl = "";
    private boolean launchDeferred;
    private JSONObject launchData = new JSONObject();
    private boolean linkResolved;
    private final java.util.List<Reply> waiting = new java.util.ArrayList<>();

    @Override
    public void call(String method, JSONObject args, Reply reply) throws Exception {
        AppsFlyerLib af = AppsFlyerLib.getInstance();
        switch (method) {
            case "init": init(args); reply.ok(); return;
            case "event": af.logEvent(activity, args.optString("name"), fields(args.optJSONObject("fields"))); reply.ok(); return;
            case "design": {
                Map<String, Object> f = fields(args.optJSONObject("fields"));
                if (args.has("value")) f.put("value", args.optDouble("value"));
                af.logEvent(activity, args.optString("id").replace(':', '_'), f);
                reply.ok();
                return;
            }
            case "progression": {
                if (!"complete".equals(args.optString("status"))) { reply.ok(); return; }
                Map<String, Object> f = fields(args.optJSONObject("fields"));
                f.put(AFInAppEventParameterName.LEVEL, join(args.optJSONArray("path")));
                if (args.has("score")) f.put(AFInAppEventParameterName.SCORE, args.optDouble("score"));
                af.logEvent(activity, AFInAppEventType.LEVEL_ACHIEVED, f);
                reply.ok();
                return;
            }
            case "purchase": {
                Map<String, Object> f = fields(args.optJSONObject("fields"));
                f.put(AFInAppEventParameterName.REVENUE, args.optDouble("priceMicros") / 1e6);
                f.put(AFInAppEventParameterName.CURRENCY, args.optString("currency"));
                f.put(AFInAppEventParameterName.CONTENT_ID, args.optString("itemId"));
                f.put(AFInAppEventParameterName.CONTENT_TYPE, args.optString("itemType"));
                JSONObject receipt = args.optJSONObject("receipt");
                if (receipt != null) f.put("af_order_id", receipt.optString("token", receipt.optString("transactionId")));
                af.logEvent(activity, AFInAppEventType.PURCHASE, f);
                reply.ok();
                return;
            }
            case "user": {
                if (args.has("id")) af.setCustomerUserId(args.optString("id"));
                JSONObject props = args.optJSONObject("properties");
                if (props != null) af.setAdditionalData(fields(props));
                reply.ok();
                return;
            }
            case "consent": {
                boolean analytics = args.optBoolean("analytics", true), tracking = args.optBoolean("tracking", true);
                af.setConsentData(AppsFlyerConsent.forGDPRUser(analytics, tracking));
                af.anonymizeUser(!analytics);
                reply.ok();
                return;
            }
            case "deferredLink":
            case "initial": answerLink(reply); return;
            case "resource": case "flush": case "lifecycle": reply.ok(); return;
            default: reply.fail("appsflyer: unknown method " + method);
        }
    }

    private void init(JSONObject cfg) {
        if (started) return;
        started = true;
        AppsFlyerLib af = AppsFlyerLib.getInstance();
        af.setDebugLog(cfg.optBoolean("debug", false));
        JSONArray domains = cfg.optJSONArray("oneLinkDomains");
        if (domains != null) {
            String[] d = new String[domains.length()];
            for (int i = 0; i < d.length; i++) d[i] = domains.optString(i);
            af.setOneLinkCustomDomain(d);
        }
        af.subscribeForDeepLink(this::onDeepLink);
        af.init(cfg.optString("devKey"), null, activity.getApplicationContext());
        af.start(activity);
        // the SDK answers the deep link callback once per launch; without a link within 10 s there is none
        activity.getWindow().getDecorView().postDelayed(() -> { if (!linkResolved) resolve(); }, 10000);
    }

    private void onDeepLink(DeepLinkResult r) {
        if (r.getStatus() == DeepLinkResult.Status.FOUND && r.getDeepLink() != null) {
            DeepLink d = r.getDeepLink();
            JSONObject data = d.getClickEvent();
            String url = data.optString("link", d.getDeepLinkValue() == null ? "" : d.getDeepLinkValue());
            boolean deferred = Boolean.TRUE.equals(d.isDeferred());
            if (!linkResolved) { launchUrl = url; launchDeferred = deferred; launchData = data; }
            try {
                JSONObject e = new JSONObject();
                e.put("url", url); e.put("deferred", deferred); e.put("data", data);
                emit("link", e);
            } catch (Exception ignored) {}
        }
        resolve();
    }

    private void resolve() {
        linkResolved = true;
        for (Reply r : waiting) answerLink(r);
        waiting.clear();
    }

    private void answerLink(Reply reply) {
        if (!linkResolved) { waiting.add(reply); return; }
        try {
            JSONObject r = new JSONObject();
            r.put("url", launchUrl); r.put("deferred", launchDeferred); r.put("data", launchData);
            reply.ok(r);
        } catch (Exception e) { reply.fail(e.toString()); }
    }

    private static Map<String, Object> fields(JSONObject o) {
        Map<String, Object> m = new HashMap<>();
        if (o == null) return m;
        for (Iterator<String> it = o.keys(); it.hasNext(); ) { String k = it.next(); m.put(k, o.opt(k)); }
        return m;
    }

    private static String join(JSONArray a) {
        if (a == null) return "";
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < a.length(); i++) { if (i > 0) b.append(':'); b.append(a.optString(i)); }
        return b.toString();
    }
}
