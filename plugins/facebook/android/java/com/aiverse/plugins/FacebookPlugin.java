// Facebook / Meta Android SDK (facebook-core, facebook-applinks 18) → the engine's "analytics" protocol
// (docs/plugins.md). App id, client token and the auto-logging switches are manifest meta-data written by
// aiversebuild (the SDK initializes itself through its ContentProvider); install attribution comes from
// activateApp + the advertising id, which the SDK sends on its own. This plugin adds custom events,
// purchases, the user id, consent and the deferred app link of the install.
package com.aiverse.plugins;

import android.net.Uri;
import android.os.Bundle;

import com.aiverse.platform.AiversePlugin;
import com.facebook.FacebookSdk;
import com.facebook.appevents.AppEventsLogger;
import com.facebook.applinks.AppLinkData;

import org.json.JSONObject;

import java.math.BigDecimal;
import java.util.Currency;
import java.util.Iterator;

public class FacebookPlugin extends AiversePlugin {
    private AppEventsLogger logger;
    private boolean consent = true;

    @Override
    @SuppressWarnings("deprecation") // sdkInitialize: only if the SDK's own ContentProvider did not run
    public void call(String method, JSONObject args, Reply reply) {
        switch (method) {
            case "init":
                if (!FacebookSdk.isInitialized()) FacebookSdk.sdkInitialize(activity.getApplicationContext());
                logger = AppEventsLogger.newLogger(activity);
                // the plugin starts after the activity resumed: count this launch (later ones the SDK sees itself)
                if (FacebookSdk.getAutoLogAppEventsEnabled()) AppEventsLogger.activateApp(activity.getApplication());
                reply.ok();
                return;
            case "lifecycle": reply.ok(); return;
            case "consent":
                consent = args.optBoolean("analytics", true);
                boolean tracking = args.optBoolean("tracking", true);
                FacebookSdk.setAutoLogAppEventsEnabled(consent);
                FacebookSdk.setAdvertiserIDCollectionEnabled(consent && tracking);
                // Limited Data Use (US state privacy laws): restrict processing without consent
                FacebookSdk.setDataProcessingOptions(consent ? new String[] {} : new String[] {"LDU"}, 0, 0);
                reply.ok();
                return;
            case "user":
                if (args.has("id")) AppEventsLogger.setUserID(args.optString("id").isEmpty() ? null : args.optString("id"));
                reply.ok();
                return;
            case "flush": if (logger != null) logger.flush(); reply.ok(); return;
            case "deferredLink": deferredLink(reply); return;
            case "design": case "progression": case "resource": reply.ok(); return; // no App Events equivalent
            default: break;
        }
        if (logger == null) { reply.fail("facebook: not initialized"); return; }
        if (!consent) { reply.ok(); return; }
        Bundle params = params(args.optJSONObject("fields"));
        switch (method) {
            case "event":
                if (params.containsKey("_valueToSum")) {
                    double v = params.getDouble("_valueToSum");
                    params.remove("_valueToSum");
                    logger.logEvent(args.optString("name"), v, params);
                } else {
                    logger.logEvent(args.optString("name"), params);
                }
                break;
            case "purchase": {
                Currency cur;
                try { cur = Currency.getInstance(args.optString("currency", "USD")); }
                catch (IllegalArgumentException e) { reply.fail("facebook: unknown currency " + args.optString("currency")); return; }
                if (!args.optString("itemId").isEmpty()) params.putString("fb_content_id", args.optString("itemId"));
                if (!args.optString("itemType").isEmpty()) params.putString("fb_content_type", args.optString("itemType"));
                logger.logPurchase(BigDecimal.valueOf(args.optLong("priceMicros", 0)).movePointLeft(6), cur, params);
                break;
            }
            default: reply.fail("facebook: unknown method " + method); return;
        }
        reply.ok();
    }

    // The app link the player clicked in an ad before installing (deep link into the game), or {}.
    private void deferredLink(Reply reply) {
        AppLinkData.fetchDeferredAppLinkData(activity.getApplicationContext(), data -> {
            JSONObject o = new JSONObject();
            try {
                Uri target = data == null ? null : data.getTargetUri();
                if (target != null) o.put("url", target.toString());
                if (data != null && data.getPromotionCode() != null) o.put("promotionCode", data.getPromotionCode());
                if (data != null && data.getRef() != null) o.put("ref", data.getRef());
            } catch (Exception ignored) {}
            reply.ok(o);
        });
    }

    // Fields → event parameters; "_valueToSum" in the fields becomes the event's value to sum.
    private static Bundle params(JSONObject o) {
        Bundle b = new Bundle();
        if (o == null) return b;
        for (Iterator<String> it = o.keys(); it.hasNext();) {
            String k = it.next();
            Object v = o.opt(k);
            if ("_valueToSum".equals(k) && v instanceof Number) b.putDouble(k, ((Number) v).doubleValue());
            else if (v instanceof Integer || v instanceof Long) b.putLong(k, ((Number) v).longValue());
            else if (v instanceof Number) b.putDouble(k, ((Number) v).doubleValue());
            else if (v instanceof String) b.putString(k, (String) v);
        }
        return b;
    }
}
