# Client-side engine support for the Bunker Wars port (agent-bw-client)

Generic engine features the Bunker Wars (Mushroom Wars 2 codebase) client needs, plus the importers that
bring BW's own data in. Original BW assets are never committed: the importers read the user's copy of the
Unity project (`Client/Assets`) and write into a game folder.

| Part | Where | Status |
|---|---|---|
| Spine 2D skeletal animation | `engine/spine`, `apps/client/ClientSpine.*`, `tools/importers/spine/skel2json.py` | runtime + UI views; checked on BW heroes / emotions |
| "FMOD" audio banks | `tools/importers/fmod/import_audio.py` | BW bank (UI.Windows FMODChannels) -> audio.json |
| Steam (store + social) | `plugins/steam` (`SteamSocial.h`), `engine/services/Social.*` | tested against a fake Steam client |
| SDK plugins | `plugins/{appsflyer,push,deeplinks,ump,xsolla}`, `engine/services/{Push,Links,Consent}.*` | services + desktop mocks tested; native code unverified on devices |
| Tutorials | `tools/importers/bunkerwars/tutorial.py` + `kit/` | battle tutorials run in matches; meta tutorials exported as data |
| Gamepads | `engine/platform/Gamepad.h`, `engine/platform/gamepad/*`, `apps/client/ClientGamepad*` | Linux / Windows backends built; cursor logic and BW chords tested |
| Battle screens + HUD | `tools/importers/bunkerwars/{client,ui_screens,bwloc}.py`, `kit/ui/logic/bw_hud.lua`, `kit/scripts/{bw/hud,view/bw/hud_view}.lua` | BW HUD + 86 tutorial windows converted and running in the client (screenshots) |

## Spine

`engine/spine` is an own implementation of the Spine 4.x runtime (presentation only, floats): skeleton JSON
and libGDX atlases (both layouts, rotated / trimmed regions), every bone transform mode, skins, region / mesh /
linked-mesh attachments (weighted or not), IK (1 and 2 bones, softness, stretch, compress), transform
constraints, path constraints (fixed / percent position; length / fixed / percent / proportional spacing;
tangent / chain / chain-scale rotation; constant-speed and per-curve-length paths, ported from spine-runtimes
4.0 `PathConstraint`), clipping attachments (the clip polygon is ear-clipped into convex pieces, every following
triangle up to the end slot is cut against them, uvs from barycentric coordinates), timelines for bones, slots
(rgba / rgb / alpha / two-color light part), attachments, IK, transform and path constraints (position, spacing,
mix), deform (meshes, clipping and path vertices), draw order and events with stepped / linear / bezier curves,
and an `AnimationState` with crossfades (`defaultMix`, per pair), queues and events. Not drawn (a warning on
load): sequences, dark colors. Rendering goes through the RHI with premultiplied alpha (GLSL + MSL in
`SpineShaders.h`): normal, additive (alpha written 0), multiply (`dst * (src + 1 - a)`) and screen
(`gfx::BlendMode::Screen`: ONE, 1 - srcColor, as the Spine runtimes; GL and Metal). BW's own 9 skeletons
(6 heroes, 3 emotions) use only region / mesh attachments and normal / additive slots.

Binary skeletons (`.skel` / `.skel.bytes`, Spine 4.0) are converted to JSON first — exactly, the whole file is
consumed or it stops:

```sh
python3 tools/importers/spine/skel2json.py hero1.skel.bytes spine/hero1.json
cp hero1.atlas.txt spine/hero1.atlas; cp hero1.png spine/
```

UI views (`<img src="#spine:NAME"/>` in any RmlUi document), declared in game.json or created by UI scripts:

```jsonc
"spine": { "hero1": { "skeleton": "spine/hero1.json", "atlas": "spine/hero1.atlas",   // atlas: default next to it
                      "width": 512, "height": 512, "scale": 1, "skin": "default", "pad": 8,
                      "animation": "hero1_idle1", "loop": true, "mix": 0.2,
                      "idle": ["hero1_idle1", "hero1_idle2", "hero1_idle3"] } }       // random idle after each loop
```

Lua (UI scripts): `spine.create(name, opts)`, `spine.play(name, anim, loop[, track])`,
`spine.add(name, anim, loop[, delay[, track]])`, `spine.skin(name, skin)`, `spine.animations(name)`,
`spine.remove(name)`; Spine events arrive through `game.on("spine", fn)` as `{view, type, animation, name, int,
float, string}`. Each view renders into its own render target every frame (skeleton framed by its first posed
frame). Tests: `aiverse_tests spine_`.

## Audio: BW's "FMOD"

Bunker Wars has no FMOD Studio project or banks: its "FMOD" is the UI.Windows audio system — an
`FMODChannels` asset (groups → items: key, clips, SFX / Music, volume, play chance, max count, music channel,
loop, fades, `triggerOnBegin` / `triggerOnEnd` chains) and ~800 `FMODEventReference` assets (key to play,
volume events, haptics).

```sh
python3 tools/importers/fmod/import_audio.py --unity <bunker-wars>/Client/Assets --out <game>
```

writes `audio/` (clips as they are; aiversebuild compresses), merges `audio.json` (SFX → `sounds` with `bus`
ui / sfx, `maxInstances` + `steal: none`; Music → `music` playlists: a `triggerOnBegin` chain becomes one
adaptive playlist with `layers`, layers raised by volume events get an `intensity`; a `triggerOnEnd` chain
becomes consecutive entries; `menu` / `match` and the `click` / `victory` / `defeat` cues are filled), and
`audio_events.json` — the event map (reference guid / asset → key, volume events, haptics; per key the
values audio.json has no key for: play chance, channel, fades, stop / mute other channels). On the real BW
project: 198 sounds, 14 playlists, 217 references. A real FMOD Studio project needs File > Export Audio and a
`--map` JSON. Test: `tools/importers/fmod/test_import_audio.py`.

## Platform services and plugins

Protocols (details in docs/plugins.md): `social` (Steam: user, achievements, stats, friends, rich presence,
overlay, invites, join), `push` (permission, token, local notifications with categories / tags / repeat,
received / opened), `links` (launch link, links while running, from every provider, deduplicated), `consent`
(CMP form: Google UMP). C++: `engine/services/{Social,Push,Links,Consent}.h`; UI Lua tables `social`, `push`,
`links`, `consent` and the generic `plugins.call(name, method, args, fn)`; events through `game.on("social" |
"push" | "link")`. Desktop mocks: `mock-social`, `mock-push`, `mock-links`, `mock-consent`.

| BW SDK | Engine |
|---|---|
| Steamworks.NET (achievements, friends, overlay, presence) | `steam` plugin, protocols store + social |
| AppsFlyer | `appsflyer` (analytics + links: events, revenue, level, user id, GDPR consent, OneLink / deferred links) |
| AppLovin MAX (rewarded / interstitial) | `applovin` (existing) |
| Xsolla web shop (`v/market/initXsolla`) | `xsolla` (web: Pay Station with the backend's token, catalog prices) |
| AndroidPushNotificationsPlugin, Firebase messaging | `push` (FCM + local on Android, UNUserNotificationCenter + APNs on iOS) |
| deep links (OneLink, invites) | `deeplinks` (+ appsflyer) |
| GoogleUMP / UserConsent | `ump` (consent); ATT: the `att` plugin of branch agent-sdk-plugins |

Not written by aiversebuild yet (add in Xcode): the iOS Push Notifications and Associated Domains entitlements.

## Tutorials

```sh
python3 tools/importers/bunkerwars/tutorial.py --unity <bunker-wars>/Client/Assets \
    --maps ME.TexturesGenerator/_Common/Maps --out <game> --kit --rules <game>/rules.json
```

Battle tutorials (BW `MapTutorialModule`: before-battle chapters + tutorial chapters of `TutorialActions`) of
every map become `scripts/bw/tutorial_data/<map>.lua` and a map script `scripts/bw/tut_<map>.lua`: list it in the
map's `"scripts"` and `scripts/view/bw/tutorial_view.lua` in `"viewScripts"`. The kit runtime
(`scripts/bw/tutorial.lua`, deterministic, in the simulation) reproduces BW's chapter machine (GOTO pass first,
parallel chapters) and implements the actions on engine primitives:

| BW actions | Engine |
|---|---|
| Wait, GOTOChapter, PlayerVictory / Defeat | timers on game time, chapters, `game.victory / defeat` |
| AiStop / AiGo | other players' commands are dropped (`before_command`; their garrisons still defend) |
| SetBattleFlags, SetPointFlags | `before_command` blocks input / upgrades / sends from-to / converts / skills |
| MoveUnits, PointAddUnits, PointSetUnits, SetPointOwner | `send` commands, `game.set_garrison`, `game.set_owner` |
| SetPlayerInvincible | `before_damage` returns 0 |
| WaitMoveUnits / PointCaught / PointUpgradeStarted / Finished / TowerFire / NoUnitsOnTheScreen | the player's commands and `captured` / `upgraded` / `attack` events, unit counts |
| ShowWindow / CloseWindow / CloseAllWindows / WaitForClick / WaitWindowClose / FingerTip / FingerTipUpgrade / ShowHide* / CameraScale / ScreenMove / AnimationTrigger | `state.bwt.ui` for the view kit: windows (`ui/hud/<screen>.rml` from the UI.Windows importer, branch agent-ui-import, else a generic panel with lang key `bw.tut.<screen>`), the finger, rings; clicks come back as hidden commands `bw_tut_*` |
| SetGameplayTimescale, window modules GameplayTimeScale / SetBattleTimeScale, HintsScreen pause | trigger variable `gameplay_scale` (0 freezes every gameplay system, commands / scripts keep running), tweened over `switchScaleDuration`; window values from `scripts/bw/tutorial_time.lua` (client.py) |
| SetEnergy | resource `energy` of the player set (give / take) |
| SetMorale | resource `morale` set to the points of that many stars (BW MoraleInfo levels 0 / 500 / 1000 / 2000 / 4000 / 8000, fractions interpolated) |
| SetSkillCooldown / ApplySkill / WaitSkillBecomeAvailable | the n-th skill card of the player (owned, non-automatic player abilities; `SkillSlotIndex` bit -> slot): `game.set_ability_cooldown`, `game.player_ability(p, id, x, z, free)` at the point or BW position (BW units / 40 around the map centre), `game.player_abilities(p)` usable + ready |
| PointConvert | `game.transform` to the next level (first `upgrades` entry with the same tags) or to the level-1 type of the cast kind (House `bunker`, Tower `tower`, Smithy `smithy`); paid / timed upgrades go through the upgrade command |
| SetSkillForever / ApplySkillEnvironment / SkillWaitUse | variable `bw_skill_forever`; environment skills through the hook only; skill uses counted from `ability` / `player_ability` events |

Every action first asks `bw_tutorial_hooks[action](step, player)` when a game script defines it (a non-nil result
replaces the engine path), so the BW skills / data ports can override any of them.

BW point ids are read from `game.data(h).bw_point` of garrison structures (the BW map importer sets them); without
it the garrison structures in entity order are ids 0, 1, 2, ... Players: `player` = var `bw_human` (default 1),
`bot` = the first hostile player, `neutral` = 0. Test mode (`initialVars {bw_tutorial_auto: 1}`) answers UI
waits and plays the player's moves through the normal command path. On the real BW project: 55 maps, 46 action
types; meta tutorials (`_TutorialMeta` TutorialData, 217) are exported to `ui/bw_tutorials.json` (conditions and
actions as data, for the meta port). Tests: `tools/importers/bunkerwars/test_tutorial.py` (with
`AIVERSE_HEADLESS` it plays a tutorial on templates/mushrooms twice and compares checksums).

Gaps: BW speed-ups above 1 (Tutorial7Event1 SetBattleTimeScale 2) are clamped to 1; morale needs a `morale`
resource with `levels` in the template (data port); the meta tutorial runner (conditions on the BW profile)
belongs to the meta port.

## Gamepads

`engine/platform/Gamepad.h`: up to four pads in the Xbox layout (`PadButton`, `PadAxis`, edges, radial dead
zone). Backends: Linux joystick API, Windows XInput (loaded at run time), web Gamepad API, macOS / iOS
GameController, Android gamepad input events. The game client turns a pad into a mouse-like cursor, so every
game (menus, UI documents, selection, orders) works with it:

```jsonc
"gamepad": { "deadZone": 0.2, "cursorSpeed": 900, "panSpeed": 1, "zoomSpeed": 1,
             "snap": { "targets": "structures", "radius": 70, "flick": true },   // "units", "all", "none"
             "buttons": { "a": "click", "b": "rightClick", "x": "key:Q", "start": "key:Escape", "up": "pan" } }
```

Left stick moves the cursor (quadratic response); with snapping on (BW / MW2 style) a full flick jumps to the
nearest building in that direction and a resting cursor settles on a building under it; A press / hold /
release is a mouse click / drag (drag from building to building = BW's send), B right-click, right stick and
d-pad pan the camera, triggers zoom; any binding: `click`, `rightClick`, `middleClick`, `key:<platform::Key>`,
`pan`, or a game action. A real mouse hides the pad cursor.

Chords and game actions (`"chords": {"rb+a": "send:50", "a+a": "upgrade"}`: modifier + button, or a double press
within 0.3 s; `lt` / `rt` name the triggers as buttons): `send:N` (N % of the garrison of the structure under the
cursor when the modifier went down, else of the selected ones, to the structure under the cursor; also sets the
HUD percent), `ability:N` (N-th usable player ability, as its card), `upgrade`, `convert:<tag>`, `cancel`,
`event:<name>` (to view scripts as `ui.on("pad", {action})`). A chord swallows its button's own binding; a
send modifier draws the origin -> cursor arrow (BW `ShowArrow`). `"scheme": "bunkerwars"` is BW's
`InputConfig.asset` exactly (JoystickButton BottomRow1 / 2 = A / B, TopRow1 / 2 = X / Y): R1 + B / A / X / Y send
25 / 50 / 75 / 100 % (mode UnitsMove), L1 + A upgrade, L1 + X / Y / B convert to bunker / tower / armory
(UpgradeMenu), A twice upgrade, d-pad right / down / left / up skill slots 1-4 (SelectSlot1..4), L2 + B / A / X / Y
emotions 1-4. UI Lua: `game.sendPercent(p?)`, `game.useAbility(i)`.

## Battle screens, HUD, texts (client.py)

```sh
python3 tools/importers/bunkerwars/import.py --source <bunker-wars> --out <game>     # data (agent-bw-data)
python3 tools/importers/bunkerwars/client.py --source <bunker-wars> --out <game>     # this layer
```

`ui_screens.py` adapts BW's UI.Windows 2.x screens (`layouts.layouts[].layoutResource` / `components[]
.componentResource`, `WindowLayoutElement.tag`) in memory for `tools/importers/unity_ui` (agent-ui-import's
importer, unchanged) and converts the battle screens (Gameplay, GameplayTutorial, BattlePreparing, campaign
Victory / Defeat, multiplayer EndMatch / EndMatchResults, the in-battle PopupMenu, tutorial wait / highlight
windows) plus every window the tutorial data shows (per-map windows under ME.TexturesGenerator). `client.py`:

- HUDScreen -> `ui/hud/bw_hud.rml` (script UI, `<body screens="playing paused">`), pooled PlayersBar rows (8) and
  skill cards (5); `ui/logic/bw_hud.lua` binds energy (HeroMana), skill cards (Ready / NotReady / NoEnergy,
  cooldown, click = `game.useAbility`), army per player, percent chooser (`game.sendPercent`), match clock /
  extra time, sudden death panel, pause button, and hides the console / replay / tooltip parts. Data:
  `scripts/bw/hud.lua` (sim, 4 Hz) -> `state.bwhud` -> `scripts/view/bw/hud_view.lua` -> `ui.set("bw")`.
  `ui/skin.rcss` hides the engine HUD parts BW draws itself (top bar, tools, card, minimap).
- tutorial windows -> `ui/hud/<prefab>.rml`; TapToContinue windows take the click (`bw_tut_click` +
  `bw_tut_window`), others let the battle through; `scripts/view/bw/tutorial_windows.lua` maps window -> document.
- texts: `textLocalizationKey` of the screens' TextComponents -> `@bw.ui.<key>`; ru / en from
  `LocalizationSettings.asset` (`bwloc.py`), else the text authored in the prefab (BW's NewTutorial texts are
  only in the prefabs and the live CDN table).
- also runs tutorial.py (--kit --rules, tutorial maps of the game wired), the FMOD importer, adds the HUD scripts
  to rules.json and `"gamepad": {"scheme": "bunkerwars"}` to game.json.

Results (Victory / Defeat / EndMatch*) and the pause menu stay in `ui/screens` for the meta port's
`screens.json`. Not converted (reported per screen in `ui/ugui/report.md`): UI.Windows show / hide animations,
Animator, particles in UI, mesh effects (UIFlippable, gradients), radial fills, BW logic components
(PlayerMoraleItem, HeroSkillDragHandler, ...), some sprites (`sprite not found`).

## Platform builds (stage 2)

| Target | Compiled | How / why not |
|---|---|---|
| Linux x86_64, Linux arm64 | yes | container + VM builds, `aiverse_tests`, client screenshots (Mesa) |
| Steam plugin | yes, against the generated fake SDK | `plugins/steam/test/run.sh`; the real Steamworks SDK needs a partner login |
| Android (Java plugins, NDK gamepad backend) | no | the Mac has JDK 17 / SDK 35 / NDK 27 / Gradle 8.10 (`toolchain_check`), but packaging needs the shared editor's open project switched (another agent's game was open); the VM is aarch64 (no NDK host) and its proxy blocks Google / Maven |
| Web (JS plugins, Gamepad API backend) | no | emsdk 6.0.13 is on the Mac; same reason; storage.googleapis.com is blocked in the VM |
| macOS / iOS (ObjC plugins, GameController backend, Metal screen blend) | no | `native_build` on the Mac fails at once: the running editor binary predates the AIVERSE rename (asks for target `rts_editor`) |
