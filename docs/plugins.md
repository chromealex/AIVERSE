# Native plugins: payments, ads, any platform SDK

Platform SDKs (stores, ad networks, analytics, ...) plug into the engine through one asynchronous
bridge, `engine/platform/Plugins.h`. A plugin is a name plus methods, written in the platform's own
language:

| Platform | Language | Base / registration |
|---|---|---|
| Android | Java (calling Java/Kotlin SDKs) | `extends com.aiverse.platform.AiversePlugin`, manifest `<meta-data android:name="aiverse.plugin.NAME">` |
| iOS, macOS | Objective-C++ (`.mm`) | `<AiversePlugin>` (`engine/platform/apple/AiversePlugin.h`), Info.plist `AiversePlugins` |
| Web | JavaScript | `AiversePlugins.register(name, { method(args) { ... } })` |
| any (incl. desktop) | C++ | `plugins::NativePlugin` + `AIVERSE_PLUGIN("name", Class)` |

aiversebuild writes the registration entries from `plugin.json`; plugin code never touches the engine.

Games normally don't call plugins directly: they use the standard services in `engine/services`,
which pick the provider for the current platform from `game.json`:

- `aiverse::store` (`Store.h`) — products with localized prices, purchase, restore, finish.
- `aiverse::ads` (`Ads.h`) — interstitial, rewarded, sticky banner, consent, "no ads" switch.
- `aiverse::analytics` (`Analytics.h`) — custom, design, progression, resource and purchase events,
  user id / properties, consent, flush, the install's deferred app link. Unlike store / ads, **every**
  analytics provider in the build gets every call (GameAnalytics + Facebook + AppMetrica together).
- `aiverse::privacy` (`Privacy.h`) — the iOS App Tracking Transparency prompt and the player's
  consent, applied to ads and analytics in one place (§ Privacy: ATT and consent).

Desktop dev builds have `mock-store`, `mock-ads`, `mock-analytics` and `mock-att` (C++,
`engine/services/MockPlugins.cpp`) so the whole purchase, ad, analytics and consent flow can be built and
tested without a phone: list them in game.json "plugins" (or run with `AIVERSE_MOCK_PLUGINS=1`), then
`AIVERSE_MOCK_STORE=purchased|pending|cancelled|failed`, `AIVERSE_MOCK_ADS=ok|noreward|fail`,
`AIVERSE_MOCK_ATT=authorized|denied|restricted` (the answer to the prompt), `AIVERSE_MOCK_ANALYTICS_LOG=<file>`
(every analytics call is appended as `method {json}`, exactly what a phone build would hand to the SDKs),
`AIVERSE_MOCK_DEFERRED_LINK=<url>`.

### Only what you use

A build contains exactly the plugins the game selected — game.json `"plugins"`, or `--plugins a,b`
for one build — and nothing else: no Gradle dependency, Maven repository, manifest entry, Java class,
Objective-C source, pod, Info.plist key, SKAdNetwork id or web script of any other plugin. Plugins
without a part for the target platform are skipped (aiversebuild prints why). The desktop mocks are
compiled out of every packaged build unless the game lists `mock-*` (aiversebuild writes
`set(AIVERSE_MOCK_PLUGINS OFF)` into `plugins.cmake`; plain dev CMake builds keep them, but even there
they register only when listed or with `AIVERSE_MOCK_PLUGINS=1`). CI checks this for every plugin and for
"no plugins" (`.github/scripts/check-plugin-isolation.py`).

## Ready plugins (`plugins/`)

| Plugin | Platforms | Protocol | SDK |
|---|---|---|---|
| `appstore` | iOS, macOS | store | StoreKit (original API: deprecated since iOS 18 but working; StoreKit 2 is Swift-only) |
| `googleplay` | Android | store | Google Play Billing 9.1 |
| `rustore` | Android | store | RuStore Pay SDK 11 (BOM 2026.10.01). The old `billingclient` stopped working on 1 Aug 2026 |
| `yandex-games` | Web | store + ads | Yandex Games SDK (`/sdk.js`) |
| `steam` | Windows, Linux, macOS | store + social | Steamworks SDK (Inventory Service purchases, DLC; achievements, stats, friends, rich presence, overlay, invites); C++ |
| `yandex-ads` | Android, iOS | ads | Yandex Mobile Ads 8.6 |
| `admob` | Android, iOS | ads | Google Mobile Ads (Android 25.5, iOS 13.11) |
| `applovin` | Android, iOS | ads | AppLovin MAX 13.6.3 (mediation) |
| `ironsource` | Android, iOS | ads | ironSource / Unity LevelPlay 9.5 (mediation, ad-unit API) |
| `gameanalytics` | Android, iOS, Web | analytics | GameAnalytics (Android 7.2.1, iOS GA-SDK-IOS 5.1, JS 5.0.0) |
| `facebook` | Android, iOS | analytics | Meta / Facebook SDK 18 (app events, install attribution, deferred app links; no login / share) |
| `appmetrica` | Android, iOS | analytics | AppMetrica (Android 8.0.0, iOS 5.16.0 — the versions Drone Fall ships) |
| `att` | iOS | privacy | App Tracking Transparency (prompt, status, `NSUserTrackingUsageDescription`, SKAdNetwork ids) |
| `appsflyer` | Android, iOS | analytics + links | AppsFlyer 6.15.2 (attribution, events, revenue, GDPR consent, OneLink / deferred deep links) |
| `push` | Android, iOS | push | Firebase Cloud Messaging 24.1 + local notifications (Android), UserNotifications + APNs (iOS) |
| `deeplinks` | Android, iOS, Web | links | intent filters / URL schemes + universal links / page URL |
| `ump` | Android, iOS | consent | Google User Messaging Platform 3 (GDPR / US privacy form, TCF personalised-ads flag) |
| `xsolla` | Web | store | Xsolla Pay Station (token from the game's backend) + Xsolla Store catalog prices |

## game.json

```jsonc
"plugins": {                                   // which plugins go into the build, and their config
  "googleplay": {},
  "rustore":    { "consoleAppId": "2063512345", "scheme": "ironandgold" },
  "appstore":   {},
  "admob":      { "appId":    { "android": "ca-app-pub-...~...", "ios": "ca-app-pub-...~..." },
                  "rewarded": { "android": "ca-app-pub-.../...", "ios": "ca-app-pub-.../..." } },
  "yandex-ads": { "interstitial": "R-M-123456-1", "rewarded": "R-M-123456-2", "banner": "R-M-123456-3" },
  "applovin":   { "sdkKey": "…86 chars from the MAX dashboard…",
                  "interstitial": { "android": "a1b2c3d4e5f6a7b8", "ios": "…" }, "rewarded": {…}, "banner": {…},
                  "testDevices": ["<advertising id>"] },
  "ironsource": { "appKey": { "android": "25b63cf85", "ios": "25c43a4a5" },
                  "interstitial": { "android": "h3xw38h9214adgxo", "ios": "obg6ohwts3y690ks" }, "rewarded": {…}, "banner": {…} },
  "yandex-games": { "signed": false },
  "steam":      { "appId": 480, "items": { "gems_100": 100, "no_ads": 200 }, "dlc": { "expansion": 1234560 } },
  "gameanalytics": { "gameKey": { "android": "…32…", "ios": "…32…" }, "secretKey": { "android": "…40…", "ios": "…40…" },
                     "build": "0.1", "currencies": ["coins", "shards"], "itemTypes": ["robot", "tech"],
                     "dimensions01": ["pve", "pvp"], "infoLog": false, "customEvents": true },
  "facebook":   { "appId": "1347344110825377", "clientToken": "…", "displayName": "Drone Fall",
                  "autoLogAppEvents": true, "advertiserIdCollection": true },
  "appmetrica": { "apiKey": "3e0383f6-…", "locationTracking": false, "firstActivationAsUpdate": false },
  "att":        { "usage": "We use your data to provide personalized ads and improve your gameplay experience",
                  "prompt": "ready", "delay": 2, "skAdNetworks": ["4fzdc2evr5", "ludvb6z3bs"] }
},
"store": {
  "providers": { "android": ["rustore", "googleplay"], "ios": ["appstore"], "web": ["yandex-games"], "desktop": ["mock-store"] },
  "products": [ { "id": "gems_100", "type": "consumable" },
                { "id": "no_ads", "type": "nonConsumable", "ids": { "ios": "com.studio.game.noads" } } ]
},
"ads": { "providers": { "android": ["yandex-ads", "admob"], "ios": ["admob"], "web": ["yandex-games"] } },
"analytics": { "providers": { "ios": ["gameanalytics", "facebook", "appmetrica"] },   // optional: all built ones
               "consentRequired": false },                                         // true: nothing before consent
"privacy": { "providers": ["att"] }                                                // optional
```

Keys (gameKey / secretKey, client tokens, API keys) belong to the game's own game.json, never to the
engine repository. GameAnalytics keys are per platform: the dashboard has a separate game for Android,
iOS and web.

- Any value may be per platform: `{"android": …, "ios": …, "web": …, "desktop": …, "default": …}`
  is replaced by the current platform's value everywhere (plugin config, store, ads).
- `providers`: the first one built into the app wins. Without it: store = appstore, googleplay,
  rustore, yandex-games, steam, mock-store; ads = yandex-ads, admob, applovin, ironsource, yandex-games, mock-ads.
- **Store policies**: an app published on Google Play must not contain another payment system, and a
  RuStore build should not need Google Play. Build them separately:
  `aiversebuild --target android --plugins googleplay,admob` and `--plugins rustore,yandex-ads`
  (`--plugins` replaces the game.json list for that build).

Test ids: Yandex `demo-interstitial-yandex`, `demo-rewarded-yandex`, `demo-banner-yandex`;
Google — https://developers.google.com/admob/android/test-ads (Android) and /ios/test-ads.
LevelPlay demo app (above; rewarded `syz3d8ekts22q0or` / iOS `l1quzz1xmmdhw5er`, banner
`4fpetq4lhe5lsw3e` / `xc2bsuntn9ea734t`). AppLovin has no shared test key: use your own SDK key and
add your device's advertising id to `testDevices`, or enable test mode in the MAX dashboard.

Mediation notes: `applovin` and `ironsource` are mediators — adapters for other networks (AdMob,
Unity Ads, Mintegral, …) are extra Gradle/pod dependencies; add them in a copy of the plugin under
`<project>/plugins/` (it overrides the engine one). A LevelPlay rewarded reward may arrive just
after the ad closes; the plugin waits up to 1.5 s for it before replying.

### Steam

`steam` is a C++ plugin for the desktop builds (Windows, Linux, macOS) on the Steamworks SDK.
Purchases go through the **Steam Inventory Service**, the way to sell inside a Steam game without
your own server (MicroTxn needs a backend and a publisher Web API key):

1. Steamworks partner site → your app → Inventory Service: enable it, upload item definitions with
   prices (`"price_category"` or `"price"`) and `"store_tags"`/`"name"`/`"description"`. Purchasable
   consumables must not set `auto_stack`: each purchase has to be its own item instance.
2. game.json: `"steam": { "appId": …, "items": { "<product id>": <item definition id> } }`. DLC are
   sold as non-consumables: `"dlc": { "<product id>": <DLC app id> }` (overlay store page, ownership
   via `BIsDlcInstalled`). Store product `"ids"` may also map a product straight to a definition id.
3. Build with the SDK: download it from partner.steamgames.com and set `STEAMWORKS_SDK=<sdk folder>`
   (or `"sdk"` in the plugin config). Valve does not allow the SDK in the repo; aiversebuild stops with
   a hint when it is missing, links `steam_api`, and puts `steam_api64.dll` / `libsteam_api.so` /
   `libsteam_api.dylib` next to the executable.
4. Development runs need `steam_appid.txt` with the app id next to the executable and Steam
   running. Release builds outside the Steam DRM wrapper: `"restartIfNecessary": true`.

How the protocol maps: `products` = `RequestPrices` + `GetItemPrice` (price in the player's
currency, title/description from the item definition); `purchase` = `StartPurchase`, checkout in the
Steam overlay, then the new item instance is found by re-reading the inventory when the overlay
closes (token = item instance id; if nothing arrives within ~8 s the reply is `cancelled`, and a late
grant still comes as a `purchase` event); `restore` = every item instance in the inventory plus owned
DLC; `finish` consumes one consumable instance. The overlay sends `app.pause`/`app.resume`, so the
match pauses while it is open. Receipts carry `steamId`, `appId` and `itemId` for server checks
(`ISteamInventory` Web API). If Steam is not running, store calls fail with the reason and the game
keeps working.

## Using the services (C++)

```cpp
#include "engine/services/Store.h"
#include "engine/services/Ads.h"

// at startup: grant what was paid but not granted (crash mid-purchase, pending payments)
store::onPurchase([](const store::Purchase& p) { if (p.state == store::PurchaseState::Purchased) grant(p); });
store::restore([](bool ok, const std::vector<store::Purchase>& list, const std::string&) { for (auto& p : list) grant(p); });

store::products([](bool ok, const std::vector<store::Product>& items, const std::string& err) { /* show prices */ });
store::purchase("gems_100", [](const store::Purchase& p) {
    if (p.state == store::PurchaseState::Purchased) grant(p);   // Pending: wait for onPurchase
});
void grant(const store::Purchase& p) {
    if (alreadyGranted(p.token)) return;                         // purchases may arrive twice
    giveItem(p.productId); saveGranted(p.token);
    store::finish(p);                                            // consume / acknowledge only after granting
}

ads::show(ads::Format::Rewarded, [](const ads::ShowResult& r) { if (r.rewarded) doubleTheLoot(); }, "level_end");
ads::showBanner();                    // its height is added to platform::safeAreaInsets()
ads::setEnabled(!ownsNoAds);          // "no ads" purchase: interstitials and banners off, rewarded stays
```

All callbacks run on the game thread. While an ad or a payment sheet covers the game the provider
sends `app.pause`/`app.resume`, which arrive as `FocusLost`/`FocusGained` — the same events as
switching apps, so the game pauses and mutes in one place.

### Analytics

```cpp
#include "engine/services/Analytics.h"

analytics::progression(analytics::Progression::Start, "pve:stage_3");                 // world:level[:phase]
analytics::progression(analytics::Progression::Complete, "pve:stage_3", R"({"kills": 41})", /*score*/ 120);
analytics::design("match_end:pve:stage_3:win", R"({"kills": 41})", /*value*/ 93.5);   // GameAnalytics design id
analytics::event("robot_unlocked", R"({"robot_id": "r2", "first": true})");           // raw named event
analytics::resource(analytics::Flow::Sink, "coins", 250, "tech", "armor_3");
analytics::purchase(p.currency, p.priceMicros, "gems", "gems_100", "shop", R"({"token": "…", "productId": "gems_100"})");
analytics::setUserId(profileId);  analytics::setUserProperty("dimension01", "pvp");  analytics::flush();
```

Never call analytics from simulation code or sim scripts (a replay would send everything again): the
UI Lua / host code reports what the player saw. What each provider does with a call:

| Call | gameanalytics | facebook | appmetrica | mock-analytics |
|---|---|---|---|---|
| `event(name, fields)` | design event `name` (off with `"customEvents": false`) | `logEvent(name, params)`; field `_valueToSum` = value | `reportEvent(name, json)` | logged |
| `design(id, fields, value)` | design event | — | — (`"structuredEvents": true`: event `id`) | logged |
| `progression` | progression event (score) | — | — (structured: event `progression`) | logged |
| `resource` | resource event (currency / itemType must be configured) | — | — (structured: event `resource`) | logged |
| `purchase` | business event, cents; Google token → `ReceiptInfo`, Apple `transactionId` → server validation | `logPurchase` (`fb_content_id`, `fb_content_type`) | — (structured: event `purchase`) | logged |
| `setUserId` / `setUserProperty` | external user id; `dimension01..03` → custom dimensions, other keys → global custom fields | `AppEvents.userID` | `userProfileID` | logged |
| consent | event submission on / off | auto-logging, advertiser id, Limited Data Use | data sending on / off | logged |
| `flush` | — (SDK schedules itself) | `flush` | `sendEventsBuffer` | logged |

Without analytics consent the service itself drops event calls; providers also switch off their own
automatic events. GameAnalytics web: the JS SDK rejects events until its session started (about one
round-trip after init) — send the first events after the first screen, not during loading.

Install attribution: Facebook counts installs / activations from `activateApp` (automatic with
`autoLogAppEvents`) plus the advertising id (IDFA only after ATT "authorized"); AppMetrica tracks
installs by itself. `analytics::deferredLink(cb)` returns the Facebook deferred app link of the ad that
brought the install (`""` if none) — ask once on the first launch.

### Privacy: ATT and consent

```cpp
#include "engine/services/Privacy.h"

privacy::requestTracking([](privacy::Tracking t) { /* "authorized" / "denied" / ... — already applied */ });
privacy::setConsent({.analytics = gdprYes, .personalizedAds = gdprYes, .ageRestricted = false});
```

- The `att` plugin (iOS) writes `NSUserTrackingUsageDescription` from `"usage"` (XML-escaped; App Review
  rejects builds without it) and shows the system prompt — on `requestTracking()`, or by itself `delay`
  seconds after the first interactive frame with `"prompt": "ready"` (what Drone Fall's `IosAttUtils`
  does: 2 s after the root screen). A request made while the app is not active waits until it is (since
  iOS 17.4 a background request is silently "denied"). The prompt is shown once per install; later
  calls report the stored answer.
- Effective settings, pushed to every provider whenever the answer or the consent changes:
  ads `consent {"personalized": consent.personalizedAds && trackingAllowed, "ageRestricted"}`,
  analytics `consent {"analytics", "tracking": trackingAllowed}`. `trackingAllowed` = authorized, or
  no prompt on this platform (Android, web, desktop: tracking is "unavailable").
- SKAdNetwork: every plugin's `skAdNetworks` and every plugin config's `"skAdNetworks"` (the game's ad
  networks / attribution partners: put the full list in the `att` config) go into one deduplicated
  `SKAdNetworkItems`; ids may be written with or without `.skadnetwork`, case does not matter.
- Facebook SDK 17+ reads the ATT status itself; ad SDKs (AdMob, AppLovin, LevelPlay, Yandex) do too.

### Lua (client UI state)

`analytics.*` and `privacy.*` are open in the UI Lua next to `store.*` / `ads.*`
(`engine/services/ServicesLua.h`):

```lua
analytics.progression("start", "pve:stage_3")
analytics.progression("complete", "pve:stage_3", { kills = 41 }, 120)
analytics.design("match_end:pve:win", 93.5, { kills = 41 })
analytics.event("robot_unlocked", { robot_id = "r2", first = true })
analytics.resource("sink", "coins", 250, "tech", "armor_3")
analytics.purchase{ currency = "USD", priceMicros = 990000, itemType = "gems", itemId = "gems_100", receipt = { token = t } }
analytics.user(id); analytics.property("dimension01", "pvp"); analytics.flush()
analytics.deferredLink(function(url) ... end)
privacy.requestTracking(function(status) ... end)       -- status: "authorized", "denied", ...
privacy.consent{ analytics = true, personalizedAds = false } -- returns {analytics, personalizedAds, ageRestricted, set}
privacy.tracking(); privacy.trackingAllowed(); privacy.provider()
```

### Porting Drone Fall (Ext-Client) analytics

| Unity | AIVERSE |
|---|---|
| `GameAnalyticsManager` (`GameAnalytics.NewProgressionEvent` / `NewDesignEvent`, `Settings.asset` keys) | `gameanalytics` plugin; its `Project()` table becomes the game's Lua: `analytics.progression` for `level_start` / `match_end`, `analytics.design(path, value, fields)` for the rest |
| `AppMetricaManager` (`ReportEvent(name, json)`, `SendEventsBuffer`) | `appmetrica` plugin: `analytics.event(name, payload)` (with `"customEvents": false` on gameanalytics so raw events stay out of it), `analytics.flush()` |
| Facebook SDK (`FacebookSettings.asset`, no code calls: auto-logged app events) | `facebook` plugin with `autoLogAppEvents` — nothing to call; login / share / gaming services are not used by the game and not wrapped |
| `IosAttUtils.RequestATT` + `XcodeBuildPostProcess` (usage string) | `att` plugin: `"prompt": "ready", "delay": 2, "usage": "…"` |
| `AnalyticsManager` queue, event ids, interruption recovery | game logic: keep it in Lua on `storage.*` (the services send immediately; the SDKs queue offline) |

## Protocols

All messages are JSON. Errors are failed replies; `"cancelled"` means the user backed out.

**store** (`appstore`, `googleplay`, `rustore`, `yandex-games`, `steam`, `mock-store`)

| Method | Args | Reply |
|---|---|---|
| `init` | the plugin's game.json config | `{}` |
| `products` | `{"products": [{"id", "type"}]}` | `[{"id", "title", "description", "price", "priceMicros", "currency", "type"}]` |
| `purchase` | `{"id", "type"}` | purchase |
| `restore` | `{}` | `[purchase]`: owned non-consumables / subscriptions + never-finished purchases |
| `finish` | `{"id", "token", "consumable"}` | `{}`: consume (consumable) or acknowledge |
| event `purchase` | purchase | a purchase finished outside `purchase()` |

purchase = `{"productId", "state": "purchased|pending|restored", "token", "orderId", "receipt": {...}}`;
`token` is unique per purchase; `receipt` carries provider data for server-side validation
(Google `originalJson` + `signature`, Apple `appStoreReceipt`, RuStore `invoiceId`, Yandex `signature`).

**ads** (`yandex-ads`, `admob`, `applovin`, `ironsource`, `yandex-games`, `mock-ads`)

| Method | Args | Reply |
|---|---|---|
| `init` | config | `{}` (starts preloading) |
| `load` | `{"format": "interstitial|rewarded"}` | `{}` |
| `show` | `{"format", "placement"}` | `{"shown", "rewarded", "error"?}` when the ad is closed |
| `banner` | `{"show", "position": "top|bottom"}` | `{}` |
| `consent` | `{"personalized", "ageRestricted"}` | `{}` |
| events | `loaded`, `failed`, `opened`, `closed`, `rewarded` with `{"format"}` | |

**analytics** (`gameanalytics`, `facebook`, `appmetrica`, `mock-analytics`) — sent to every provider,
no reply data; a provider answers `{}` for calls it has no equivalent for.

| Method | Args |
|---|---|
| `init` | config |
| `event` | `{"fields", "name"}` |
| `design` | `{"fields", "id", "value"?}` |
| `progression` | `{"fields", "status": "start|complete|fail", "path": [1..3 strings], "score"?}` |
| `resource` | `{"fields", "flow": "source|sink", "currency", "amount" (≥ 0), "itemType", "itemId"}` |
| `purchase` | `{"fields", "currency", "priceMicros", "itemType", "itemId", "cartType", "receipt": {"token", "productId"} \| {"transactionId"}}` |
| `user` | `{"id"}` or `{"properties": {key: string}}` |
| `consent` | `{"analytics", "tracking"}` |
| `flush` | `{}` |
| `deferredLink` | `{}` → `{"url"?}` (facebook) |

fields = flat object of strings and numbers (the service turns booleans into 1 / 0 and drops the rest).

**privacy** (`att`, `mock-att`)

| Method | Args | Reply |
|---|---|---|
| `init` | config (`prompt`, `delay`) | `{}` |
| `status` | `{}` | `{"status": "notDetermined|restricted|denied|authorized|unavailable"}` |
| `request` | `{}` | `{"status"}` after the prompt (at once when already answered) |
| event `status` | `{"status"}` | the plugin asked by itself (`"prompt": "ready"`) |
**social** (`steam`, `mock-social`) — engine/services/Social.h; achievement / stat ids map to API names in the
plugin config (`"achievements": {"first_win": "ACH_WIN_ONE_GAME"}`, `"stats"`, `"floatStats"`)

| `user` | `{}` | `{"id", "name", "language", "country"}` |
| `unlock` | `{"id"}` | `{}` (event `achievement` `{"id"}` once stored) |
| `progress` | `{"id", "current", "max"}` | `{}` (progress popup) |
| `achievements` | `{}` | `[{"id", "name", "description", "unlocked", "unlockTime"}]` |
| `stat` | `{"id"}` / `{"id", "set"}` / `{"id", "add"}` | `{"value"}` (stores are batched) |
| `friends` | `{}` | `[{"id", "name", "state", "playingThisGame", "status"}]` |
| `presence` | `{"key", "value"}` / `{"clear": true}` | `{}` |
| `overlay` | `{"page"}`: a dialog name, an https URL or `store` | `{}` |
| `invite` | `{"connect", "friend"?}` | `{}` |
| events | `join` `{"connect", "friend"}`, `achievement` `{"id"}`, `presence` `{"friend"}` | |

**push** (`push`, `mock-push`) — engine/services/Push.h

| `permission` | `{}` | `{"granted"}` |
| `token` | `{}` | `{"token"}` (also event `token`) |
| `schedule` | `{"id", "title", "body", "delay", "category", "tag", "data", "repeat"}` | `{}` |
| `cancel` | `{"id"}` / `{"category"}` / `{"tag"}` / `{"all": true}` | `{}` |
| `badge` | `{"count"}` | `{}` |
| events | `token`, `received` `{"title", "body", "data", "foreground"}`, `opened` `{"id", "data"}` | |

**links** (`deeplinks`, `appsflyer`, `mock-links`; every provider reports) — engine/services/Links.h:
`initial` `{}` → `{"url", "deferred"?, "data"?}`; event `link` `{"url", "deferred"?, "data"?}`.

**consent** (`ump`, `mock-consent`) — engine/services/Consent.h: `request` `{"debugGeography"?}` and `options` `{}`
→ `{"status": "notRequired|required|obtained|unknown", "canRequestAds", "personalized", "optionsRequired"}`.
Without a provider the service answers "not required".

**xsolla** (store): `purchase` gets a Pay Station token from config `tokenUrl` (the game's backend, POST
`{productId, sku}` → `{token}`) or from a preceding `token` `{"token"}` call (`plugins.call("xsolla", "token", ...)`),
opens Pay Station and answers `pending`; with `statusUrl` the result after the window closes comes as a
`purchase` event. The backend grants (Xsolla webhook); `restore` is empty.

**app events** any plugin may send: `app.pause`, `app.resume`, `app.banner` `{"height", "position"}`
(logical points, added to the safe area), `app.log` (text to the engine log). The engine calls
`lifecycle` `{"event": "ready"}` on every configured plugin once the first screen is interactive
(Yandex Games moderation requires `LoadingAPI.ready()` then).

## Writing a plugin

Folder `plugins/<name>/` (engine-wide) or `<project>/plugins/<name>/` (one game), with
`plugin.json` and the sources:

```jsonc
{
  "name": "my-sdk", "provides": ["ads"],
  "cpp":     { "sources": ["src/*.cpp"] },                                    // every platform
  "android": { "java": "android/java",                                        // copied to app/src/main/java
               "classes": { "my-sdk": "com.studio.plugins.MySdkPlugin" },
               "dependencies": ["com.example:sdk:1.2.3"], "platformDependencies": ["com.example:bom:1.0"],
               "repositories": ["https://maven.example.com"],
               "manifest": { "root": ["<uses-permission .../>"], "application": ["<meta-data ... android:value=\"${appKey}\"/>"],
                             "activity": ["<intent-filter>...</intent-filter>"] } },
  "ios":     { "sources": ["ios/*.mm"], "classes": { "my-sdk": "AiverseMySdkPlugin" },
               "pods": ["pod 'MySdk', '~> 2.0'"], "frameworks": ["StoreKit"],
               "plist": ["<key>MySdkKey</key><string>${appKey}</string>"],
               "skAdNetworks": ["cstr6suwn9"] },        // ad networks: merged into one SKAdNetworkItems
  "macos":   { same keys as ios },
  "windows": { "sources": ["src/*.cpp"], "includeDirs": ["${sdk}/include"],   // also "linux", "macos":
               "libraries": ["${sdk}/lib/win64/my.lib"],                     // desktop SDKs installed
               "runtime": ["${sdk}/bin/win64/my.dll"] },                     // by the developer
  "defaults": { "sdk": "${env.MY_SDK}" },               // config fallbacks; ${env.X} = environment variable
  "sdkHint": "Download the SDK from … and set MY_SDK",  // printed when a path above is missing
  "web":     { "scripts": ["https://cdn.example.com/sdk.js"], "js": ["web/my-sdk.js"] }
}
```

`${key}` / `${a.b}` in manifest, plist, pod and script strings are filled from the plugin's
game.json config for that platform (aiversebuild warns about missing keys); in manifest and plist
entries the values are XML-escaped, so user text (an ATT usage string with `&` or quotes) is safe. A
plugin config may add `"skAdNetworks": [...]` of its own (merged with the plugin's). aiversebuild then:

- Android — Java sources, Gradle dependencies/repositories, manifest entries (incl. the
  `aiverse.plugin.*` registry); `AiverseActivity` forwards `onResume/onPause/onDestroy/onNewIntent/
  onActivityResult` to plugins (the activity is `singleTask`, payment deep links come back through
  `onNewIntent`).
- iOS — sources and frameworks via `plugins.cmake`, Info.plist keys (a key two plugins both set is
  written once, first plugin wins, with a warning; `skAdNetworks` of all plugins are merged into one
  deduplicated `SKAdNetworkItems`), a Podfile for `aiverse_client` and `pod install` (open
  `AIVERSE.xcworkspace` afterwards).
- Web — copies the scripts and inserts `<script>` tags at `<meta name="aiverse-plugin-scripts">` in the
  shell, plus `AiversePlugins.config[name]`.
- C++ sources are compiled into the game executable itself (not a static library), so
  `AIVERSE_PLUGIN(...)` self-registration works. Desktop `includeDirs`/`libraries` go to the game
  target, `runtime` files are copied next to the executable (rpath `$ORIGIN` / `@loader_path`).
  A C++ plugin can override `update()`: it runs every frame on the game thread (SDK callbacks).

Rules for plugin code: answer every call exactly once (the reply objects ignore a second answer);
call SDKs on their required thread (Java plugins run on the UI thread, Objective-C on the main
thread); never block; send `app.pause`/`app.resume` around anything that covers the game.

### Steam social

The same `steam` plugin answers the `social` protocol (`plugins/steam/src/SteamSocial.h`): ISteamUserStats
(SetAchievement + StoreStats for the unlock popup, IndicateAchievementProgress, GetAchievementAndUnlockTime,
int / float stats with StoreStats batched to once a second), ISteamFriends (immediate friends, persona state,
playing this game, their rich presence "status"; SetRichPresence / ClearRichPresence; ActivateGameOverlay /
ToWebPage / ToStore; InviteUserToGame / ActivateGameOverlayInviteDialogConnectString), join requests
(GameRichPresenceJoinRequested_t and the `+connect` launch command line) as `join` events. Since Steamworks
1.61 the client loads the current stats itself. Set `"connect"` in the rich presence to let friends join.

## Verified / not verified

- ✅ Bridge, services, mocks: unit tests (`tests/tests.cpp`), Linux and Windows (MinGW/Wine).
- ✅ Web bridge in Chromium (calls, config, events, errors, `LoadingAPI.ready`); `yandex-games`
  protocol test against a fake SDK (`node plugins/yandex-games/web/test.mjs`).
- ✅ aiversebuild output: Android manifest/Gradle/Java, web script tags, iOS plugins.cmake/Podfile/
  SKAdNetworkItems; isolation check for every plugin (`check-plugin-isolation.py`).
- ✅ `steam`: builds and links a packaged Linux game with the real SDK redistributables; purchase
  flow (prices, restore, overlay checkout, consume, DLC, cancel) tested against a fake Steam client
  (`plugins/steam/test/run.sh`, CI job `plugins-steam`); without Steam the game starts and store
  calls report why. Not yet run against a real Steam account.
- ✅ Java plugins compile (javac against API stubs written from the SDKs' bytecode / official samples).
- ✅ `steam` social: `plugins/steam/test/run.sh` against the fake Steam client (user, achievements with events,
  stats, friends, presence, overlay, invite, join). `social` / `push` / `links` / `consent` services: unit tests
  with the desktop mocks. Isolation check passes with the new plugins; the generated Android manifest was read.
- ⏳ `appsflyer`, `push`, `deeplinks`, `ump` (Java / Objective-C) and `xsolla` (JS): written from the SDKs'
  documented APIs, not compiled or run here. iOS entitlements (Push Notifications, Associated Domains) are not
  written by aiversebuild: add them in Xcode.
- ⏳ Real SDKs: Gradle/CocoaPods builds and device runs — CI job `plugins`; Objective-C plugins not
  compiled here (no Xcode). `applovin`/`ironsource`: APIs taken from the official integration
  guides and demo apps, not yet run on a device. `yandex-ads` iOS: some Objective-C selectors are derived from the Swift
  API (marked in the source).
