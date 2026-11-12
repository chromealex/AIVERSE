# Spiel-Assistent („Spiel erstellen“)

Ein Fenster (Projekt-Hub → **Spiel erstellen** oder **Projekt → Spiel erstellen…**) oder ein MCP-Aufruf (`game_make`) erzeugt
ein ganzes Spielprojekt mit eigener `.aiverse`: das Spiel einer Vorlage, die nach der Idee ausgewählt wird, die Grafiken /
Modelle / Musik / Sounds des Nutzers, eine Karte mit hartem Tutorial, weiche Hinweise in Matches, generierte Level, das
Meta-Spiel (Menüs, Upgrades, Shop, Tagesbelohnung, Profil, Einstellungen, Ergebnis), Käufe und Werbung auf Wunsch, Sprachen,
Branding und eine Prüfung mit Bots. Alles, was er schreibt, sind gewöhnliche Spieldaten, die sich danach bearbeiten lassen.

Code: `apps/editor/core/GameWizard*.cpp` (ohne UI), `apps/editor/core/WizardOps.cpp` (MCP), `apps/editor/EditorGameWizard.cpp`
(die Hub-Seite). Laufzeit-Kit, das in jedes Spiel kopiert wird: `templates/_wizard/` (Lua + RmlUi, siehe dessen README).

## Antworten (`WizardSpec`, alles optional)

| Gruppe | Schlüssel |
|---|---|
| Inhalte | `artDir` (Ordner: png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt), `artUrl` (eine Datei oder ein .zip / .tar.gz; wird mit dem System-curl nach `~/.aiverse/wizard/<Zeit>` geladen) |
| Idee | `idea` (beliebige Sprache; .md/.txt im Ordner werden ebenfalls gelesen), `genre` (Vorlagen-ID oder `auto`), `features` (economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern) |
| Projekt | `title`, `location`, `studio` (Bundle-ID `com.<studio>.<slug>`), `version`, `orientation`, `languages`, `platforms`, `seed` |
| Tutorial | `tutorial` (`both` / `hard` / `soft` / `none`), `tutorialForced`, `tutorialSkippable`, `tutorialDim`, `hints` |
| Level | `levels` (0..40), `mapPreset` (`auto`, `none` = Kopien der Basiskarte, oder ein Generator-Preset), `mapSize`, `bossEvery`, `difficultyCurve`, `startResourcesPercent`, `keepTemplateMaps`, `verifyLevels` |
| Meta | `metaIdea` (freier Text: was die Meta enthalten soll; geht in die Aufgabe des Agenten), `currencies` (soft, hard), `upgrades`, `shop`, `daily`, `profileScreen`, `xpPerLevel` |
| Geld | `ads`, `interstitialEvery`, `purchases`, `stores` |
| Aussehen | `brandingFromArt`, `paletteFromArt` |

`game_make_options` listet die Auswahlmöglichkeiten, `game_make_plan` zeigt, was erzeugt würde (ohne Dateien). Gleiche
Antworten + Seed = gleiches Spiel.

## Wie er entscheidet

- **Vorlage**: Wörter der Idee werden mit den Schlüsselwörtern jeder Vorlage verglichen (Wortstämme, ru / en), gewünschte
  Features mit ihren Mechaniken, die Ausrichtung mit ihrem Bildschirm; bei Gleichstand entscheidet der Seed. Nichts
  angegeben: eine der ausgewogenen Vorlagen.
- **Inhalte**: Modelle und 2D-Sprites gehen an die Typen, deren ID / Name (ru / en) im Dateinamen steckt; der Rest wird
  kopiert und in `WIZARD.md` aufgeführt. Das Key-Art (nach Name und Größe) wird zum Startbildschirm, zum Menühintergrund und
  zur UI-Akzentfarbe; das quadratischste Bild (oder „icon“ / „logo“) zum 1024er-Icon. Musik nach Namen in `menu` / `match` /
  `battle` / `victory` / `defeat`, kurze Sounds auf Ereignisse (`click`, `select`, `move`, `died`, `underAttack`, ...). Die
  erste TrueType-Schrift wird zu `fonts/title.ttf`.
- **Hartes Tutorial** (`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`), aus den Regeln geschrieben:
  - *Wirtschaft* (Arbeiter, Sidebar-Werften, aufbaubare MCVs, TA-Konstrukteure): aufbauen, sammeln, Warp-Feld / Versorgung
    bei Bedarf, die Voraussetzungskette eines kasernenähnlichen Produzenten, zwei Einheiten ausbilden, ein feindliches
    Gebäude zerstören, das nicht zurückschießt;
  - *Garnison* (Mushroom Wars, State.io): in ein neutrales Gebäude schicken, zwei weitere einnehmen, ein feindliches einnehmen;
  - *Autobau* (Lane-Battler): zweimal Bauen drücken, verbessern, halten;
  - *allgemein*: Armee bewegen, angreifen.
  Nur die Befehle des Schritts kommen durch (`before_command`), der Gegner wartet (seine Einheiten verteidigen sich trotzdem),
  Panel und Pfeil zeichnet das View-Skript, die Ressourcen des Schritts werden aufgefüllt, damit man nicht steckenbleibt.
  Nicht für Straßenwirtschaften im Stil von Knights and Merchants (dort die allgemeine Form).
- **Weiches Tutorial**: einmalige Hinweise (Kamera, untätige Arbeiter, zu wenig Ressourcen, Versorgungslimit, Armee,
  Garnisonen, Angriff) aus `scripts/wizard/hints_config.lua`; gesehene Hinweise werden im Profil gespeichert, die
  Einstellungen schalten sie ab.
- **Level**: `maps/wiz_level_N.json` = die Basiskarte + `generate` (das Preset der Vorlage, ein Seed pro Level, die
  Basiskarte als Vorlage); Schwierigkeit 0..2 nach der Kurve, jedes N-te Level ein Boss; Belohnungen, Erstsieg-Belohnungen und
  Sternzeiten in `ui/wizard.json`.
- **Meta**: `ui/wizard.json` (Konfiguration), `ui/profile.json`, `ui/shop.json`, `ui/screens.json` (`start: home`,
  `afterMatch: result`), Bildschirme aus dem Kit. Upgrades gelangen als `initialVars` `wiz_up_<id>` ins Match, und
  `scripts/wizard/meta_bonus.lua` macht daraus Buffs / Startressourcen.
- **Geld**: `plugins` / `store` / `ads` in `game.json` je Plattform (Yandex-Demo-Werbeblöcke und Desktop-Mocks: IDs ersetzen).

## Agentenschritt: das beschriebene Spiel, eins zu eins

Die Vorlage ist ein Ausgangspunkt, nicht das Spiel. Mit `agent` an (Standard) schreibt der Assistent `WIZARD_TASK.md` ins
Projekt — die Beschreibung / das GDD wörtlich und dazu, wie die Teile des Assistenten stimmig bleiben (Tutorial-Schritte,
Hinweise, Meta-Konfiguration, Sprachdateien) — und übergibt es einem verbundenen KI-Agenten, der das Projekt über die
MCP-Werkzeuge des Editors (live im Editor) umbaut: Typen, Werte, Fraktionen, Mechaniken (Regeln oder Lua), Karten,
Siegbedingungen, Texte; danach prüft er mit `rules_lint`, `map_validate`, `match_run` und schreibt
„## Агент: что сделано“ in WIZARD.md.

- Verbindung: Der Assistent zeigt dasselbe Panel „KI-Agent verbinden“ wie das Agent-Fenster (`EditorMcpConnect.*`,
  `core/McpInstall.h`): Ein Klick schreibt aiversemcp in die Konfiguration des Clients, und diese eine Verbindung dient beiden.
- Start aus dem Editor (`core/WizardAgent.*`, über die Login-Shell des Nutzers, damit die CLI gefunden wird):
  Claude Code `claude -p --output-format stream-json --allowedTools mcp__aiverse` und Codex CLI `codex exec --full-auto`,
  die Aufgabe auf stdin, der Projektordner als Arbeitsverzeichnis. Die CLI wird im PATH, in den üblichen Installationsordnern,
  in der Shell des Nutzers und in den Desktop-Apps gesucht (Claude Code in
  `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`, Codex in `ChatGPT.app/Contents/Resources/codex-cli`).
  Der Fortschritt erscheint im Aktivitätsfeed (Agent-Fenster), in der Statusleiste und auf der Seite des Assistenten
  (Stopp-Knopf); das vollständige Protokoll in `logs/wizard-agent-<Zeit>.log`.
- Cursor, Windsurf, Claude Desktop: „Aufgabe für den Agenten kopieren“ legt die Aufgabe in die Zwischenablage, um sie im
  Client einzufügen.
- Kein API-Schlüssel im Editor: Der Agent läuft im eigenen Client und Konto des Nutzers.
- Diktat: „Einsprechen“ unter dem Ideenfeld (Seite Idee) und dem Feld für Meta-Ideen (Seite Meta) — macOS-Spracherkennung
  (`apps/editor/EditorSpeech_mac.mm`: Speech + AVFoundation, die erste Spielsprache als Locale; das System fragt einmal nach
  Mikrofon- und Spracherkennungszugriff). Der Text wird ans Feld angehängt. Andere Plattformen: Der Knopf ist deaktiviert.
- `ai` in der Spezifikation (Texte pro Sprache: Slogan, Einleitung, Typen, Ressourcen, Währungen, Upgrades, Level) erlaubt
  einem Agenten, der `game_make` selbst aufruft, die Namen und Texte des Spiels zu übergeben.

Das Tutorial in der Game-Ansicht des Editors: Der Editor hat keine Spiel-UI für das Schritt-Panel, daher sendet das
View-Skript `wiz_tut_off`, und die Tutorial-Karte wird dort als gewöhnliches Match gespielt. Panel, Pfeil und Filter
funktionieren im Spiel-Client (AiverseGame, Builds). Der Editor öffnet das erste Level, nicht das Tutorial.

## Prüfung

Mit `verifyLevels` spielt der Assistent das Tutorial selbst durch (`initialVars {wiz_tutorial_auto: 1}`: der `auto`-Befehl
jedes Schritts läuft durch denselben Filter wie beim Spieler) sowie eine Auswahl von Leveln mit Bots und meldet Ladefehler,
Skriptfehler und ein nicht beendetes Tutorial. Über alle Vorlagen geprüft: Alles endet außer ambersands (Bauen nur auf Fels),
planets und underlord (Bewegung / Graben auf besonderem Gelände) — der Assistent sagt das in seinen Warnungen.

Tests: `game_wizard_plan_picks_a_template_by_the_idea`, `game_wizard_makes_a_playable_project`,
`script_command_card_false_hides_the_button`.

## Änderung an der Engine

Skriptbefehle akzeptieren `"card": false` (kein Knopf / Hotkey; gesendet von View-Skripten, HUD-Dokumenten, MCP) — das
Weiter / Überspringen des Tutorials (`modules/rts/RtsScriptCommands.cpp`, Befehlskarten in Client und Editor).
