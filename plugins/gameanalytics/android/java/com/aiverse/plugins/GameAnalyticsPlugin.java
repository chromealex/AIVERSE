// GameAnalytics Android SDK → the engine's "analytics" protocol (docs/plugins.md).
// Config (game.json "plugins" → "gameanalytics"): gameKey, secretKey (Android values of the game in the
// GameAnalytics dashboard), build, currencies / itemTypes (resource events), dimensions01..03 (allowed
// custom dimension values), infoLog, verboseLog, errorReporting, customEvents (false: "event" calls are
// not turned into design events). configure* must run before initialize(), so they all happen in init.
package com.aiverse.plugins;

import com.aiverse.platform.AiversePlugin;
import com.gameanalytics.sdk.GAProgressionStatus;
import com.gameanalytics.sdk.GAResourceFlowType;
import com.gameanalytics.sdk.GameAnalytics;
import com.gameanalytics.sdk.events.ReceiptInfo;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

public class GameAnalyticsPlugin extends AiversePlugin {
    private boolean started = false;
    private boolean customEvents = true;
    private final Map<String, Object> globalFields = new HashMap<>();

    @Override
    public void call(String method, JSONObject args, Reply reply) {
        switch (method) {
            case "init": init(args, reply); return;
            case "lifecycle": reply.ok(); return;
            case "consent":
                // no consent: nothing leaves the device and nothing is queued for later
                GameAnalytics.setEnabledEventSubmission(args.optBoolean("analytics", true));
                reply.ok();
                return;
            case "user": user(args); reply.ok(); return;
            case "flush": reply.ok(); return; // the SDK sends on its own schedule; no public flush
            default: break;
        }
        if (!started) { reply.fail("gameanalytics: not initialized (gameKey / secretKey missing?)"); return; }
        Map<String, Object> fields = fields(args.optJSONObject("fields"));
        switch (method) {
            case "event":
                if (customEvents) GameAnalytics.addDesignEvent(args.optString("name"), fields);
                break;
            case "design":
                if (args.has("value")) GameAnalytics.addDesignEvent(args.optString("id"), args.optDouble("value", 0), fields);
                else GameAnalytics.addDesignEvent(args.optString("id"), fields);
                break;
            case "progression": progression(args, fields); break;
            case "resource":
                GameAnalytics.addResourceEvent("sink".equals(args.optString("flow")) ? GAResourceFlowType.Sink : GAResourceFlowType.Source,
                    args.optString("currency"), (float) args.optDouble("amount", 0), args.optString("itemType"), args.optString("itemId"), fields);
                break;
            case "purchase": purchase(args, fields); break;
            default: reply.fail("gameanalytics: unknown method " + method); return;
        }
        reply.ok();
    }

    private void init(JSONObject cfg, Reply reply) {
        String gameKey = cfg.optString("gameKey", ""), secretKey = cfg.optString("secretKey", "");
        if (gameKey.isEmpty() || secretKey.isEmpty()) { reply.fail("gameanalytics: gameKey and secretKey are required"); return; }
        customEvents = cfg.optBoolean("customEvents", true);
        GameAnalytics.setEnabledInfoLog(cfg.optBoolean("infoLog", false));
        GameAnalytics.setEnabledVerboseLog(cfg.optBoolean("verboseLog", false));
        GameAnalytics.setEnabledErrorReporting(cfg.optBoolean("errorReporting", true));
        if (cfg.has("build")) GameAnalytics.configureBuild(cfg.optString("build"));
        String[] v;
        if ((v = list(cfg.optJSONArray("currencies"))).length > 0) GameAnalytics.configureAvailableResourceCurrencies(v);
        if ((v = list(cfg.optJSONArray("itemTypes"))).length > 0) GameAnalytics.configureAvailableResourceItemTypes(v);
        if ((v = list(cfg.optJSONArray("dimensions01"))).length > 0) GameAnalytics.configureAvailableCustomDimensions01(v);
        if ((v = list(cfg.optJSONArray("dimensions02"))).length > 0) GameAnalytics.configureAvailableCustomDimensions02(v);
        if ((v = list(cfg.optJSONArray("dimensions03"))).length > 0) GameAnalytics.configureAvailableCustomDimensions03(v);
        if (cfg.has("userId")) GameAnalytics.configureUserId(cfg.optString("userId"));
        GameAnalytics.initialize(activity, gameKey, secretKey);
        started = true;
        reply.ok();
    }

    private void progression(JSONObject a, Map<String, Object> fields) {
        GAProgressionStatus st = "complete".equals(a.optString("status")) ? GAProgressionStatus.Complete
            : "fail".equals(a.optString("status")) ? GAProgressionStatus.Fail : GAProgressionStatus.Start;
        JSONArray p = a.optJSONArray("path");
        int n = p == null ? 0 : Math.min(3, p.length());
        if (n == 0) return;
        boolean scored = a.has("score");
        double score = a.optDouble("score", 0);
        if (n == 1) {
            if (scored) GameAnalytics.addProgressionEvent(st, p.optString(0), score, fields);
            else GameAnalytics.addProgressionEvent(st, p.optString(0), fields);
        } else if (n == 2) {
            if (scored) GameAnalytics.addProgressionEvent(st, p.optString(0), p.optString(1), score, fields);
            else GameAnalytics.addProgressionEvent(st, p.optString(0), p.optString(1), fields);
        } else {
            if (scored) GameAnalytics.addProgressionEvent(st, p.optString(0), p.optString(1), p.optString(2), score, fields);
            else GameAnalytics.addProgressionEvent(st, p.optString(0), p.optString(1), p.optString(2), fields);
        }
    }

    // GameAnalytics wants the price in cents; with a Google Play purchase token the event is validated
    // against the store (ReceiptInfo, SDK 7.2).
    private void purchase(JSONObject a, Map<String, Object> fields) {
        int cents = (int) Math.round(a.optLong("priceMicros", 0) / 10000.0);
        JSONObject receipt = a.optJSONObject("receipt");
        String token = receipt == null ? "" : receipt.optString("token", "");
        String product = receipt == null ? "" : receipt.optString("productId", a.optString("itemId"));
        String cart = a.optString("cartType", "");
        if (cart.isEmpty()) cart = "shop";
        if (!token.isEmpty())
            GameAnalytics.addBusinessEvent(a.optString("currency"), cents, a.optString("itemType"), a.optString("itemId"), cart,
                ReceiptInfo.googlePlay(product, token), fields);
        else
            GameAnalytics.addBusinessEvent(a.optString("currency"), cents, a.optString("itemType"), a.optString("itemId"), cart, fields);
    }

    // dimension01..03 → custom dimensions (values must be listed in the config); anything else → a global
    // custom field sent with every event; "id" → the user id (GameAnalytics: external user id).
    private void user(JSONObject a) {
        if (a.has("id")) GameAnalytics.configureExternalUserId(a.optString("id"));
        JSONObject props = a.optJSONObject("properties");
        if (props == null) return;
        boolean global = false;
        for (Iterator<String> it = props.keys(); it.hasNext();) {
            String k = it.next();
            String v = props.optString(k);
            if ("dimension01".equals(k)) GameAnalytics.setCustomDimension01(v);
            else if ("dimension02".equals(k)) GameAnalytics.setCustomDimension02(v);
            else if ("dimension03".equals(k)) GameAnalytics.setCustomDimension03(v);
            else { globalFields.put(k, v); global = true; }
        }
        if (global) GameAnalytics.setGlobalCustomEventFields(new HashMap<>(globalFields));
    }

    private static Map<String, Object> fields(JSONObject o) {
        Map<String, Object> m = new HashMap<>();
        if (o == null) return m;
        for (Iterator<String> it = o.keys(); it.hasNext();) {
            String k = it.next();
            Object v = o.opt(k);
            if (v instanceof Number || v instanceof String) m.put(k, v);
        }
        return m;
    }

    private static String[] list(JSONArray a) {
        if (a == null) return new String[0];
        String[] r = new String[a.length()];
        for (int i = 0; i < r.length; i++) r[i] = a.optString(i);
        return r;
    }
}
