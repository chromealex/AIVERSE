// Google UMP 3 → the engine's "consent" protocol (docs/plugins.md, engine/services/Consent.h).
// request: requestConsentInfoUpdate, then loadAndShowConsentFormIfRequired; personalised ads =
// TCF purposes 1, 3 and 4 granted (IABTCF_PurposeConsents written by the form) or no consent needed.
// options: showPrivacyOptionsForm (the "privacy settings" button the regulations require).
package com.aiverse.plugins;

import android.content.SharedPreferences;
import android.preference.PreferenceManager;

import com.aiverse.platform.AiversePlugin;
import com.google.android.ump.ConsentDebugSettings;
import com.google.android.ump.ConsentInformation;
import com.google.android.ump.ConsentRequestParameters;
import com.google.android.ump.UserMessagingPlatform;

import org.json.JSONArray;
import org.json.JSONObject;

public class UmpPlugin extends AiversePlugin {
    private JSONObject config = new JSONObject();

    @Override
    public void call(String method, JSONObject args, Reply reply) throws Exception {
        switch (method) {
            case "init": config = args; reply.ok(); return;
            case "request": request(args, reply); return;
            case "options":
                UserMessagingPlatform.showPrivacyOptionsForm(activity, e -> answer(reply, e == null ? null : e.getMessage()));
                return;
            case "lifecycle": reply.ok(); return;
            default: reply.fail("ump: unknown method " + method);
        }
    }

    private void request(JSONObject args, Reply reply) {
        ConsentRequestParameters.Builder p = new ConsentRequestParameters.Builder().setTagForUnderAgeOfConsent(config.optBoolean("underAge", false));
        JSONArray test = config.optJSONArray("testDevices");
        if ("eea".equals(args.optString("debugGeography")) || test != null) {
            ConsentDebugSettings.Builder d = new ConsentDebugSettings.Builder(activity);
            if ("eea".equals(args.optString("debugGeography"))) d.setDebugGeography(ConsentDebugSettings.DebugGeography.DEBUG_GEOGRAPHY_EEA);
            if (test != null) for (int i = 0; i < test.length(); i++) d.addTestDeviceHashedId(test.optString(i));
            p.setConsentDebugSettings(d.build());
        }
        ConsentInformation info = UserMessagingPlatform.getConsentInformation(activity);
        emit("app.pause", null);
        info.requestConsentInfoUpdate(activity, p.build(),
                () -> UserMessagingPlatform.loadAndShowConsentFormIfRequired(activity, e -> { emit("app.resume", null); answer(reply, e == null ? null : e.getMessage()); }),
                e -> { emit("app.resume", null); answer(reply, e.getMessage()); });
    }

    private void answer(Reply reply, String error) {
        ConsentInformation info = UserMessagingPlatform.getConsentInformation(activity);
        int s = info.getConsentStatus();
        String status = s == ConsentInformation.ConsentStatus.NOT_REQUIRED ? "notRequired"
                : s == ConsentInformation.ConsentStatus.REQUIRED ? "required"
                : s == ConsentInformation.ConsentStatus.OBTAINED ? "obtained" : "unknown";
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(activity);
        String purposes = prefs.getString("IABTCF_PurposeConsents", "");
        boolean personalized = "notRequired".equals(status) || (purposes.length() >= 4 && purposes.charAt(0) == '1' && purposes.charAt(2) == '1' && purposes.charAt(3) == '1');
        try {
            JSONObject r = new JSONObject();
            r.put("status", status);
            r.put("canRequestAds", info.canRequestAds());
            r.put("personalized", personalized && info.canRequestAds());
            r.put("optionsRequired", info.getPrivacyOptionsRequirementStatus() == ConsentInformation.PrivacyOptionsRequirementStatus.REQUIRED);
            if (error != null) r.put("error", error);
            reply.ok(r);
        } catch (Exception e) {
            reply.fail(e.toString());
        }
    }
}
