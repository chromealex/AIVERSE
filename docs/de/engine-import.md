# Import eines Projekts aus einer anderen Engine

Die Implementierung liegt in `agent-engine-import`; die Ausgangsspezifikation ist
[engine-import-spec.md](../engine-import-spec.md). Die Prüfsummen von Simulation und Vorlagen sind unverändert.
Dies ist eine funktionierende Import-Grundlage, **nicht die Erfüllung aller Stufen der Spezifikation**. Abdeckung und
offene Arbeit unten sind Teil der Übergabe an MAIN.

## CLI und Zwischendaten

Python 3.8+; keine Python-Pakete nötig. Der ausführbare Starter ist `tools/importers/port/aiverse_port` (oder die explizite
Python-Form unten):

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

Jeder Befehl gibt JSON aus; bei einem Fehler endet er mit Status 2. `scan` schreibt nichts. Eingabe- und Ausgabeordner
dürfen sich nicht überschneiden. Keine gleichzeitigen Importe in dasselbe Ziel. `extract` zeichnet Quellpfade, vollständig
serialisierte Komponenten, Felder, Skriptquellcode und deren Verwendung auf. `emit` schreibt ein Spielmanifest, Regeln,
Karten, `.rtsproj`, kopierte Assets, optional Sprites/Tiles/Audio/Effekte, `port.json` sowie JSON-/Markdown-Berichte.
`sourceRoot` muss beim Schreiben kopierter Assets noch existieren. Die erzeugte `.rpakignore` schließt den Zwischenindex der
Skripte und die Berichte aus den Laufzeit-Paketen aus. Das Manifest markiert importierte Inhalte mit `external: true`, und
aiversebuild überträgt das in die Ressourcenpakete.

`--scale` bedeutet Zellen pro Meter; `--mode auto|2d|3d` wählt Szenenebene und Kamera. Unity 3D spiegelt Z; Unity 2D
projiziert XY auf die Bodenebene. Godot-Vector2 wird auf XZ abgebildet. Ursprünge werden verschoben, damit Kartenpositionen
positiv bleiben. Besitzer werden nicht erfunden: Nur ein explizites Unity-Feld `owner` wird abgebildet, mit einer Warnung
zur Näherung. Die Spielernummerierung der Quelle muss geprüft werden.

`lib/port/mapping.json` definiert Feld-Aliasse. `--mapping FILE` ergänzt oder überschreibt Aliasse für unterstützte Ziele.
`fireRate` wird als Schüsse pro Sekunde (Kehrwert des Cooldowns) gedeutet und ausdrücklich als Näherung markiert.
Abklingzeiten und Bauzeiten im Engine-JSON sind **Sekunden**, keine Simulationsticks. Jedes abgeleitete Feld hat Herkunft,
Konfidenz und Zielpfad.

### Wiederholte Importe

`.port-state.json` speichert Hashes der erzeugten Dateien/Typen. Bearbeitete oder bereits vorhandene Dateien bleiben
erhalten, auch wenn ein Editor den Marker `ported` behalten hat. Regeln werden pro Typ zusammengeführt; bearbeitete Typen
verlieren diesen Marker. Neue Typen können weiterhin hinzukommen. Änderungen des Nutzers an Einstellungen auf Regelebene
bleiben erhalten. Karten und Binärdateien sind als ganze Dateien geschützt. Veraltete erzeugte Dateien werden nicht
gelöscht. Das Zustandsmanifest gehört dem Importer; bearbeiten Sie es nicht.

Lösungsvermerke überstehen wiederholte Importe nur, solange sich das Element und der Fingerabdruck der zugehörigen
Quell-Entität/des Skripts nicht ändern. Eine geänderte Quelle öffnet das Element erneut zur Prüfung.

## Editor und MCP

Öffnen Sie **Projekt → Engine-Projekt importieren** oder `editor_window {id:"project_import", open:true}`. Quelle scannen,
Ziel/Maßstab/2D/3D wählen, dann importieren. Die Oberfläche führt den Importer außerhalb des Render-Threads aus und kann das
Ergebnis öffnen. **KI-Übergabe kopieren** kopiert eine explizite Aufgabe und öffnet das Agent-Fenster; es behauptet nicht,
dass ein Agent gestartet wurde.

Der aktuelle Basis-Branch hat keinen Assistenten „Spiel erstellen“. Die Integration mit dem separaten Branch
`agent-gamewizard` bleibt bei MAIN; fremde Branches wurden nicht zusammengeführt.

Registrierte Operationen (Importeinstellungen können auch in `options` verschachtelt werden):

- `port_scan {path}`: Erkennung der Quelle, Prüfung der Serialisierung, Listen von Assets/Szenen/Skripten.
- `port_import {path,out,scale?,mode?,mapping?}`: Extraktion und Ausgabe ausführen.
- `port_report {path?}`: den Bericht im Ziel lesen (standardmäßig das geöffnete Projekt).
- `port_item {path?,id}`: Quellelement, Komponenten, verknüpfte Skripte und erzeugte Typen.
- `port_resolve {path?,id,how,reason}`: geprüfte Arbeit vermerken (`rules`, `lua`, `native`, `dropped`).

Der Import über MCP ist synchron, wie beim bestehenden VFX-Importer; große Projekte können dauern. `AIVERSE_PYTHON` wählt
den Interpreter. Windows-Shell-Metazeichen `%`, `!`, Anführungszeichen und Zeilenumbrüche in Befehlsargumenten werden
abgelehnt, nicht eingesetzt.

Arbeitsablauf des Agenten: fehlende Elemente untersuchen, `rules_schema` konsultieren, passende Mechaniken als Daten
umsetzen, dann Lua, dann bei Bedarf ein natives Modul. Mit `scenario_run` und Bot-Matches DPS, Produktionszeit und
Bewegungsgeschwindigkeit vergleichen. Screenshots vergleichen, wenn das Quellbild vorliegt. Nur geprüfte Arbeit als
gelöst markieren; bewusst weggelassenes Verhalten erklären. C#/GDScript/Blueprint werden nicht mechanisch übersetzt oder
ausgeführt.

## Abdeckung und verbleibende Arbeit an der Spezifikation

| Bereich | Umgesetzt | Offen |
|---|---|---|
| Gemeinsamer Unity-Code | YAML, GUID-Suche, verschachtelte Prefabs und Varianten nach `lib/unity` verschoben; Kompatibilitäts-Importe halten VFX funktionsfähig | Bestehende Verschachtelungsgrenzen des gemeinsamen Parsers gelten weiter |
| Unity-Szenen/Typen | Welttransformationen, Szenenobjekte, Prefab-Overrides, Skriptverwendung und serialisierte Felder, ScriptableObject-Kandidaten, Reihenfolge der Build-Szenen | Einrechnung der Transform-Skalierung in Modellgrenzen, robuste semantische Typgruppierung, Ableitung von Spielern/Starts |
| Assets/Materialien | Modelle/Texturen/Audio und benachbarte Abhängigkeiten behalten Pfade; PBR-Faktoren und erster Material-Override; Metadaten zur Modellskalierung; optional PSD/TIFF → PNG über vorhandenen ImageMagick/ffmpeg-Adapter | Zuordnung von Materialslots, gepackte Maps, Achsen-Importeinstellungen, Zerlegung von Animationsframes; ein fehlender Bildkonverter wird gemeldet |
| Unity 2D | Sprite-Ausschnitte aus Atlanten, explizite Tilemap-Zellen in Atlas/feste Ebene gebacken, orthografische Kamera | RuleTiles, transformierte/getönte Tiles, beliebige Grid-Layouts, animierte Tiles, Sprite-Pivots/Sortierung |
| Audio/FX | Schleifen-Audio von Kamera/Manager → Match-Playlist; der bestehende VFX-Konverter schreibt über geschütztes Staging | Mixer-Routing und semantische Bindung von Effekten an Ereignisse |
| Terrain | Undurchsichtige gebackene Daten und Terrain-Komponenten werden gemeldet | Konvertierung von TerrainData-Höhenfeld/Splat/Bäumen/Details ist nicht umgesetzt |
| Godot | Erkennung von Text-Szenen/-Ressourcen, externe Assets, einfache Verschiebungen, exportierte numerische Overrides, Skriptindex, Reihenfolge der Hauptszene | Szenenvererbung, vollständige Transform-Komposition, Dekodierung mehrzeiliger komplexer Variants, Konvertierung von TileMap/GridMap, Animationsclips |
| Unreal | JSON-Empfänger und Python-Exporter im Editor für geladene Actors, reflektierte Kandidateneigenschaften, Meshes/Texturen/Audio über AssetExportTask | Der Exporter wurde **nicht in Unreal ausgeführt**; Landscape, vollständige Aufzählung von Blueprint-Variablen und Graph-Export fehlen |
| Editor | Eigenständiges lokalisiertes Importfenster, Menüeintrag, MCP und kopierbare KI-Übergabe | Integration in den Assistenten, Auswahl von Asset-Kategorien, direkter Agentenstart |

Unbekannte native Unity-Binärdateien `.asset` können selbst mit Force Text vorkommen (gebackenes Licht/NavMesh). Sie werden
einzeln gemeldet. Binäre Serialisierung von Szenen/Prefabs/Materialien wird mit dem Hinweis auf Force Text abgelehnt.
`.uasset` wird außerhalb von Unreal nie geparst.

Das Hilfsskript für Unreal ist `tools/importers/unreal_export/export.py`; rufen Sie `export_project(destination)` im
Unreal Editor mit aktiviertem Python-Plugin und einem neuen Ausgabeordner auf. API-Referenzen:
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6),
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6).
Es exportiert das aktuell geladene Level und die `/Game`-Assets; nicht geladene Level werden nicht durchlaufen.

## Prüfung, 2026-11-03

Die synthetischen Projekte unter `tests/data/port` sind originale Testdaten. Das Unreal-FBX ist das vorhandene,
handgeschriebene CC0-`tests/data/models/cube.fbx`. Heruntergeladene Spiel-Assets wurden nicht committet.

- 34 bestehende Tests des VFX-Importers bestehen nach dem Verschieben des gemeinsamen Codes.
- 15 Tests des Projekt-Importers decken Ablehnung ungeeigneter Serialisierung, Prefab-Overrides, Einheiten, geschützte
  Bearbeitungen, Idempotenz (inklusive erneutem Schreiben von `port.json`), Invalidierung bei geänderter Quelle,
  Pfadsicherheit, Sprites/Tiles und Lösung per CLI ab. Mit `AIVERSE_PORT_HEADLESS` laden alle fünf Projekte in der echten
  Engine, und das Unity-Kampfprojekt endet mit einem Sieg von Spieler 1.
- C++-Suite: 5857 Prüfungen, ein nur in der Sandbox auftretender Fehler eines localhost-Listeners; dieser Test bestand
  separat außerhalb der Netzwerk-Sandbox (27 Prüfungen). Ein zusätzlicher MCP-Brückentest bestand (5 Prüfungen).
- Grundlegende Compiler-Warnung außerhalb dieser Änderung: unbenutztes `fb` in `engine/fx/FxEffect.cpp:1058`.

Zwei öffentliche RTS-Repositories wurden nur nach `/tmp` geladen und importiert; **jede erzeugte Karte wurde einzeln mit
`aiverse_headless --max-seconds 0` geladen**, nicht nur die erste/Standardkarte:

| Quelle | Revision | Ergebnis | Bericht |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 509 kopierte Assets, 375 Typkandidaten, 30 Karten; 30/30 laden | 486 näherungsweise, 1913 fehlend; Skripte, Render-/Physikkomponenten, undurchsichtige gebackene Daten und nicht unterstützte Assets müssen geprüft werden |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 388 kopierte Assets, 552 Typkandidaten, 84 Karten; 84/84 laden | 638 näherungsweise, 2017 fehlend; Skriptlogik, Szenenvererbung, eingebettete Ressourcen und Tile-/Animationssysteme müssen geprüft werden |

Das sind Import-/Ladeprüfungen, **keine spielbaren 1:1-Portierungen**. Die Zahl der Kandidaten darf nicht als erfolgreich
nachgebaute Spielmechanik dargestellt werden. Laufzeit in Unreal sowie Ausführung unter Windows, Android und im Web sind
weiterhin ungeprüft.

Das eigenständige Importformular wurde im nativen macOS-Editor angesehen. Automatisierte Eingabe per Zwischenablage/schnellen
Tastendrücken füllte ImGui-Text nicht zuverlässig, daher wird kein vollständiger Klick-Import behauptet; dieselben
Operationen wurden über die C++-MCP-Brücke und die CLI geprüft. Der macOS-Editor/Metal kompiliert.

Nach dem Zusammenführen des aktuellen master (`82c47120`) wurden zwei `std::filesystem::path`-Schleifen in
`NativeBuild.cpp` auf `const auto&` geändert, damit sie mit AppleClang kompilieren. Das ist ein minimaler
Kompatibilitäts-Fix in einer gemeinsamen Datei. MAIN sollte die Editor-Pakete nach Integration und Prüfung dieses
Branches neu veröffentlichen; dieser Branch veröffentlicht keine Builds.

Abschließende Prüfung nach dem master-Merge und dem Kompatibilitäts-Fix:

- macOS-Editor (Metal), aiversemcp, aiverse_headless und aiverse_tests erfolgreich neu gebaut.
- Vollständige C++-Suite außerhalb der socketbeschränkten Sandbox: **5930 Prüfungen, 0 Fehler**.
- CTest: bestehende Ziele des VFX- und des Projekt-Importers bestehen (34 + 15 Python-Tests).
- Gezielte MCP-Brücke: 5 Prüfungen; Build-Helfer des nativen Editors: 42 Prüfungen, alle bestanden.
- Durchgehender Import über `aiversemcp --no-editor` akzeptiert verschachtelte `options`, wendet scale=2 an und verarbeitet
  einen Ausgabepfad mit Leerzeichen und Apostroph.
- `skirmish --seed 3`: Blau gewinnt bei 470 Spielsekunden; `mushrooms --seed 3`: Rot gewinnt bei 92 Sekunden.
