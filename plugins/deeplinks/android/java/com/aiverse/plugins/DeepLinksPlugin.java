// Deep links → the engine's "links" protocol (docs/plugins.md): the intent that started the activity
// ("initial") and every VIEW intent that reaches the running singleTask activity (onNewIntent →
// event "link"). The intent filters come from plugin.json (custom scheme + verified https host).
package com.aiverse.plugins;

import android.content.Intent;
import android.net.Uri;

import com.aiverse.platform.AiversePlugin;

import org.json.JSONObject;

public class DeepLinksPlugin extends AiversePlugin {
    private String launchUrl = "";
    private boolean launchRead;

    @Override
    public void call(String method, JSONObject args, Reply reply) throws Exception {
        switch (method) {
            case "init":
                readLaunch();
                reply.ok();
                return;
            case "initial": {
                readLaunch();
                JSONObject r = new JSONObject();
                r.put("url", launchUrl);
                reply.ok(r);
                return;
            }
            case "lifecycle": reply.ok(); return;
            default: reply.fail("deeplinks: unknown method " + method);
        }
    }

    private void readLaunch() {
        if (launchRead) return;
        launchRead = true;
        Intent i = activity.getIntent();
        Uri u = i != null && Intent.ACTION_VIEW.equals(i.getAction()) ? i.getData() : null;
        if (u != null) launchUrl = u.toString();
    }

    @Override
    public void onNewIntent(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction()) || intent.getData() == null) return;
        try {
            JSONObject e = new JSONObject();
            e.put("url", intent.getData().toString());
            emit("link", e);
        } catch (Exception ignored) {
        }
    }
}
