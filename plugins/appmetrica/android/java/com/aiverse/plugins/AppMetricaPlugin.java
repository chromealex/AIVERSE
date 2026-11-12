// AppMetrica Android SDK (io.appmetrica.analytics 8) → the engine's "analytics" protocol (docs/plugins.md).
// Config (game.json "plugins" → "appmetrica"): apiKey, locationTracking (false), firstActivationAsUpdate
// (false), logs (false), structuredEvents (false). Every "event" goes to reportEvent(name, json). With
// structuredEvents, design calls are reported under their id and progression / resource / purchase as
// events named after the method; by default they are left to the other providers (no double counting
// when a game sends the same moment as a raw event and as a GameAnalytics design event).
package com.aiverse.plugins;

import com.aiverse.platform.AiversePlugin;

import io.appmetrica.analytics.AppMetrica;
import io.appmetrica.analytics.AppMetricaConfig;

import org.json.JSONObject;

public class AppMetricaPlugin extends AiversePlugin {
    private boolean started = false;
    private boolean structured = false;

    @Override
    public void call(String method, JSONObject args, Reply reply) throws Exception {
        switch (method) {
            case "init": {
                String key = args.optString("apiKey", "");
                if (key.isEmpty()) { reply.fail("appmetrica: apiKey is required"); return; }
                AppMetricaConfig.Builder b = AppMetricaConfig.newConfigBuilder(key)
                    .withLocationTracking(args.optBoolean("locationTracking", false))
                    .handleFirstActivationAsUpdate(args.optBoolean("firstActivationAsUpdate", false));
                if (args.optBoolean("logs", false)) b.withLogs();
                AppMetrica.activate(activity.getApplicationContext(), b.build());
                // sessions: the plugin starts after the first onResume, so track the activity from now on
                AppMetrica.enableActivityAutoTracking(activity.getApplication());
                AppMetrica.resumeSession(activity);
                structured = args.optBoolean("structuredEvents", false);
                started = true;
                reply.ok();
                return;
            }
            case "lifecycle": reply.ok(); return;
            case "consent": AppMetrica.setDataSendingEnabled(args.optBoolean("analytics", true)); reply.ok(); return;
            case "user":
                if (args.has("id")) AppMetrica.setUserProfileID(args.optString("id").isEmpty() ? null : args.optString("id"));
                reply.ok();
                return;
            case "flush": if (started) AppMetrica.sendEventsBuffer(); reply.ok(); return;
            default: break;
        }
        if (!started) { reply.fail("appmetrica: not initialized"); return; }
        if (!"event".equals(method) && !structured) { reply.ok(); return; }
        JSONObject fields = args.optJSONObject("fields");
        if (fields == null) fields = new JSONObject();
        switch (method) {
            case "event": AppMetrica.reportEvent(args.optString("name"), fields.toString()); break;
            case "design":
                if (args.has("value")) fields.put("value", args.optDouble("value"));
                AppMetrica.reportEvent(args.optString("id"), fields.toString());
                break;
            case "progression":
                fields.put("status", args.optString("status"));
                fields.put("path", args.optJSONArray("path"));
                if (args.has("score")) fields.put("score", args.optLong("score"));
                AppMetrica.reportEvent("progression", fields.toString());
                break;
            case "resource":
                for (String k : new String[] {"flow", "currency", "amount", "itemType", "itemId"}) fields.put(k, args.opt(k));
                AppMetrica.reportEvent("resource", fields.toString());
                break;
            case "purchase":
                for (String k : new String[] {"currency", "priceMicros", "itemType", "itemId", "cartType"}) fields.put(k, args.opt(k));
                AppMetrica.reportEvent("purchase", fields.toString());
                break;
            default: reply.fail("appmetrica: unknown method " + method); return;
        }
        reply.ok();
    }
}
