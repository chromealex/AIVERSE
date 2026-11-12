// Notifications → the engine's "push" protocol (docs/plugins.md, engine/services/Push.h).
// Local notifications: AlarmManager → AlarmReceiver posts them (kept in SharedPreferences, so
// they survive a reboot). Remote push: Firebase Cloud Messaging, initialised from the plugin config
// (no google-services.json / Gradle plugin needed); MessagingService reports the token and
// shows / forwards messages. Tapping a notification starts the activity with the extra
// "aiverse.push" (its data JSON): "opened" at launch (getIntent) or while running (onNewIntent).
package com.aiverse.plugins;

import android.Manifest;
import android.app.AlarmManager;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;

import com.aiverse.platform.AiversePlugin;
import com.google.firebase.FirebaseApp;
import com.google.firebase.FirebaseOptions;
import com.google.firebase.messaging.FirebaseMessaging;

import android.app.Notification;
import android.content.BroadcastReceiver;
import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;
import java.util.Map;
import org.json.JSONObject;

import java.util.Iterator;

public class PushPlugin extends AiversePlugin {
    static final String PREFS = "aiverse.push";
    static final String EXTRA = "aiverse.push";
    static volatile PushPlugin instance;
    private Reply permissionReply;
    private boolean launchRead;

    @Override
    public void call(String method, JSONObject args, Reply reply) throws Exception {
        switch (method) {
            case "init": init(args); reply.ok(); return;
            case "permission": permission(reply); return;
            case "token": token(reply); return;
            case "schedule": schedule(activity, args); reply.ok(); return;
            case "cancel": cancel(args); reply.ok(); return;
            case "badge": reply.ok(); return;   // launcher badges follow the notifications on Android
            case "lifecycle": reply.ok(); return;
            default: reply.fail("push: unknown method " + method);
        }
    }

    private void init(JSONObject cfg) {
        instance = this;
        Context c = activity.getApplicationContext();
        JSONObject ch = cfg.optJSONObject("channel");
        String id = ch != null ? ch.optString("id", "main") : "main";
        c.getSharedPreferences(PREFS, 0).edit().putString("channel", id).putString("icon", cfg.optString("icon", "")).apply();
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel nc = new NotificationChannel(id, ch != null ? ch.optString("name", "Game") : "Game", NotificationManager.IMPORTANCE_DEFAULT);
            ((NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE)).createNotificationChannel(nc);
        }
        JSONObject fb = cfg.optJSONObject("firebase");
        if (fb != null && FirebaseApp.getApps(c).isEmpty()) {
            FirebaseApp.initializeApp(c, new FirebaseOptions.Builder()
                    .setApplicationId(fb.optString("applicationId")).setApiKey(fb.optString("apiKey"))
                    .setProjectId(fb.optString("projectId")).setGcmSenderId(fb.optString("senderId")).build());
            FirebaseMessaging.getInstance().getToken().addOnSuccessListener(t -> emitToken(t));
        }
        if (cfg.optBoolean("askOnStart", false)) permission(null);
        if (!launchRead) { launchRead = true; opened(activity.getIntent()); }
    }

    void emitToken(String t) {
        try { JSONObject e = new JSONObject(); e.put("token", t); emit("token", e); } catch (Exception ignored) {}
    }

    void emitReceived(String title, String body, JSONObject data) {
        try {
            JSONObject e = new JSONObject();
            e.put("title", title); e.put("body", body); e.put("data", data); e.put("foreground", true);
            emit("received", e);
        } catch (Exception ignored) {}
    }

    private void permission(Reply reply) {
        if (Build.VERSION.SDK_INT < 33 || activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
            if (reply != null) reply.ok(granted(true));
            return;
        }
        permissionReply = reply;
        activity.requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 7311);
        // the answer is read on the next resume (the activity forwards no permission results to plugins)
    }

    @Override
    public void onResume() {
        if (permissionReply == null) return;
        Reply r = permissionReply;
        permissionReply = null;
        boolean ok = Build.VERSION.SDK_INT < 33 || activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED;
        r.ok(granted(ok));
    }

    private static JSONObject granted(boolean g) {
        JSONObject r = new JSONObject();
        try { r.put("granted", g); } catch (Exception ignored) {}
        return r;
    }

    private void token(Reply reply) {
        if (FirebaseApp.getApps(activity).isEmpty()) { reply.ok(new JSONObject()); return; }
        FirebaseMessaging.getInstance().getToken()
                .addOnSuccessListener(t -> { try { JSONObject r = new JSONObject(); r.put("token", t); reply.ok(r); } catch (Exception e) { reply.fail(e.toString()); } })
                .addOnFailureListener(e -> reply.fail("push: " + e.getMessage()));
    }

    static void schedule(Context c, JSONObject n) throws Exception {
        long at = System.currentTimeMillis() + (long) (n.optDouble("delay", 0) * 1000);
        n.put("at", at);
        c.getSharedPreferences(PREFS, 0).edit().putString("n." + n.getString("id"), n.toString()).apply();
        arm(c, n);
    }

    static PendingIntent alarm(Context c, String id) {
        Intent i = new Intent(c, AlarmReceiver.class).setAction("aiverse.push." + id).putExtra("id", id);
        return PendingIntent.getBroadcast(c, id.hashCode(), i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
    }

    static void arm(Context c, JSONObject n) {
        AlarmManager am = (AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
        // inexact alarms need no SCHEDULE_EXACT_ALARM permission; reminders do not need the second
        am.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, n.optLong("at"), alarm(c, n.optString("id")));
    }

    private void cancel(JSONObject a) {
        Context c = activity.getApplicationContext();
        SharedPreferences p = c.getSharedPreferences(PREFS, 0);
        SharedPreferences.Editor ed = p.edit();
        AlarmManager am = (AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE);
        for (Iterator<String> it = p.getAll().keySet().iterator(); it.hasNext(); ) {
            String k = it.next();
            if (!k.startsWith("n.")) continue;
            try {
                JSONObject n = new JSONObject(p.getString(k, "{}"));
                boolean hit = a.optBoolean("all") || (a.has("id") && a.optString("id").equals(n.optString("id")))
                        || (a.has("category") && a.optString("category").equals(n.optString("category")))
                        || (a.has("tag") && a.optString("tag").equals(n.optString("tag")));
                if (!hit) continue;
                am.cancel(alarm(c, n.optString("id")));
                nm.cancel(n.optString("id").hashCode());
                ed.remove(k);
            } catch (Exception ignored) {}
        }
        ed.apply();
    }

    private void opened(Intent i) {
        if (i == null || !i.hasExtra(EXTRA)) return;
        try {
            JSONObject e = new JSONObject();
            e.put("id", i.getStringExtra("aiverse.push.id"));
            e.put("data", new JSONObject(i.getStringExtra(EXTRA)));
            emit("opened", e);
        } catch (Exception ignored) {}
        i.removeExtra(EXTRA);
    }

    @Override
    public void onNewIntent(Intent intent) { opened(intent); }

    // Posts the local notifications when their alarm fires; re-arms them after a reboot.
    public static class AlarmReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context c, Intent intent) {
            SharedPreferences p = c.getSharedPreferences(PREFS, 0);
            if (Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
                for (String k : p.getAll().keySet())
                    if (k.startsWith("n.")) try { arm(c, new JSONObject(p.getString(k, "{}"))); } catch (Exception ignored) {}
                return;
            }
            String id = intent.getStringExtra("id");
            String raw = id == null ? null : p.getString("n." + id, null);
            if (raw == null) return;
            try {
                JSONObject n = new JSONObject(raw);
                post(c, id, n.optString("title"), n.optString("body"), n.optJSONObject("data"));
                long repeat = n.optLong("repeat", 0);
                if (repeat > 0) {
                    n.put("at", System.currentTimeMillis() + repeat * 1000);
                    p.edit().putString("n." + id, n.toString()).apply();
                    arm(c, n);
                } else {
                    p.edit().remove("n." + id).apply();
                }
            } catch (Exception ignored) {}
        }

        static void post(Context c, String id, String title, String body, JSONObject data) {
            SharedPreferences p = c.getSharedPreferences(PREFS, 0);
            Intent open = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
            if (open == null) return;
            open.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
            open.putExtra(EXTRA, data == null ? "{}" : data.toString()).putExtra("aiverse.push.id", id);
            PendingIntent pi = PendingIntent.getActivity(c, id.hashCode(), open, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
            int icon = c.getResources().getIdentifier(p.getString("icon", ""), "drawable", c.getPackageName());
            if (icon == 0) icon = c.getApplicationInfo().icon;
            Notification.Builder b = android.os.Build.VERSION.SDK_INT >= 26
                    ? new Notification.Builder(c, p.getString("channel", "main")) : new Notification.Builder(c);
            b.setSmallIcon(icon).setContentTitle(title).setContentText(body).setContentIntent(pi).setAutoCancel(true);
            ((NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE)).notify(id.hashCode(), b.build());
        }
    }

    // Firebase Cloud Messaging: new tokens and messages (foreground -> "received", else posted like a local one).
    public static class MessagingService extends FirebaseMessagingService {
        @Override
        public void onNewToken(String token) {
            PushPlugin p = instance;
            if (p != null) p.emitToken(token);
        }

        @Override
        public void onMessageReceived(RemoteMessage m) {
            JSONObject data = new JSONObject();
            for (Map.Entry<String, String> e : m.getData().entrySet()) try { data.put(e.getKey(), e.getValue()); } catch (Exception ignored) {}
            RemoteMessage.Notification n = m.getNotification();
            String title = n != null && n.getTitle() != null ? n.getTitle() : data.optString("title");
            String body = n != null && n.getBody() != null ? n.getBody() : data.optString("body");
            PushPlugin p = instance;
            if (p != null) p.emitReceived(title, body, data);
            else AlarmReceiver.post(this, m.getMessageId() == null ? "fcm" : m.getMessageId(), title, body, data);
        }
    }
}
