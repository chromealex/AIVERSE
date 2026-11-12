# Importer un projet d’un autre moteur

L’implémentation se trouve dans `agent-engine-import` ; la spécification d’origine est
[engine-import-spec.md](../engine-import-spec.md). Les sommes de contrôle de la simulation et des modèles sont inchangées.
C’est une base d’import fonctionnelle, **et non l’achèvement de toutes les étapes de la spécification**. La couverture et le
travail restant ci-dessous font partie de la passation à MAIN.

## CLI et données intermédiaires

Python 3.8+ ; aucun paquet Python n’est requis. Le lanceur exécutable est `tools/importers/port/aiverse_port` (ou la forme
Python explicite ci-dessous) :

```sh
python3 tools/importers/port/aiverse_port.py scan /path/to/UnityProject
python3 tools/importers/port/aiverse_port.py extract /path/to/UnityProject --out /tmp/port.json
python3 tools/importers/port/aiverse_port.py emit /tmp/port.json --out /path/to/Imported
python3 tools/importers/port/aiverse_port.py /path/to/UnityProject --out /path/to/Imported --scale 1 --mode auto
python3 tools/importers/port/aiverse_port.py report /path/to/Imported
python3 tools/importers/port/aiverse_port.py item /path/to/Imported --id ITEM_ID
python3 tools/importers/port/aiverse_port.py resolve /path/to/Imported --id ITEM_ID --how lua --reason 'Verified by scenario…'
```

Chaque commande affiche du JSON ; en cas d’échec elle sort avec le statut 2. `scan` n’écrit rien. Les dossiers d’entrée et de
sortie ne doivent pas se chevaucher. Ne lancez pas d’imports simultanés vers la même destination. `extract` enregistre les
chemins source, les composants sérialisés complets, les champs, le code source des scripts et leur utilisation. `emit` écrit
un manifeste de jeu, des règles, des cartes, un `.rtsproj`, les ressources copiées, éventuellement sprites/tuiles/audio/effets,
`port.json` et des rapports JSON/Markdown. `sourceRoot` doit encore exister lors de l’écriture des ressources copiées. Le
`.rpakignore` généré exclut des paquets d’exécution l’index intermédiaire des scripts et les rapports. Le manifeste marque le
contenu importé `external: true`, ce qu’aiversebuild propage dans les paquets de ressources.

`--scale` signifie cellules par mètre ; `--mode auto|2d|3d` choisit le plan de scène et la caméra. Unity 3D inverse Z ;
Unity 2D projette XY sur le plan du sol. Le Vector2 de Godot est projeté sur XZ. Les origines sont décalées pour garder des
positions positives sur la carte. Aucun propriétaire n’est inventé : seul un champ Unity explicite `owner` est repris, avec un
avertissement d’approximation. La numérotation des joueurs de la source doit être vérifiée.

`lib/port/mapping.json` définit des alias de champs. `--mapping FILE` ajoute ou remplace des alias pour les cibles prises en
charge. `fireRate` est interprété en tirs par seconde (inverse du temps de recharge), explicitement marqué comme approché.
Les temps de recharge et de construction du JSON du moteur sont en **secondes**, pas en ticks de simulation. Chaque champ
déduit a une provenance, un niveau de confiance et un chemin cible.

### Imports répétés

`.port-state.json` stocke les hachages des fichiers/types générés. Les fichiers modifiés ou déjà présents sont conservés, y
compris quand un éditeur a gardé le marqueur `ported`. Les règles sont fusionnées par type ; les types modifiés perdent ce
marqueur. De nouveaux types peuvent toujours être ajoutés. Les modifications de l’utilisateur aux réglages de niveau règles
sont conservées. Cartes et fichiers binaires sont protégés en entier. Les fichiers générés obsolètes ne sont pas supprimés. Le
manifeste d’état appartient à l’importateur ; ne le modifiez pas.

Les annotations de résolution survivent aux imports répétés tant que l’élément et l’empreinte de l’entité/du script source
concerné ne changent pas. Une source modifiée rouvre l’élément pour vérification.

## Éditeur et MCP

Ouvrez **Projet → Importer un projet de moteur** ou `editor_window {id:"project_import", open:true}`. Analysez la source,
choisissez destination/échelle/2D/3D, puis importez. L’interface exécute l’importateur hors du thread de rendu et peut ouvrir
le résultat. **Copier la passation IA** copie une tâche explicite et ouvre la fenêtre Agent ; elle ne prétend pas qu’un agent a
été lancé.

La branche de base actuelle n’a pas d’assistant « Créer un jeu ». L’intégration avec la branche séparée `agent-gamewizard`
reste à la charge de MAIN ; aucune branche étrangère n’a été fusionnée.

Opérations enregistrées (les réglages d’import peuvent aussi être imbriqués dans `options`) :

- `port_scan {path}` : détection de la source, vérification de la sérialisation, listes de ressources/scènes/scripts.
- `port_import {path,out,scale?,mode?,mapping?}` : exécuter l’extraction et l’écriture.
- `port_report {path?}` : lire le rapport de la destination (par défaut le projet ouvert).
- `port_item {path?,id}` : élément source, composants, scripts liés et types générés.
- `port_resolve {path?,id,how,reason}` : annoter un travail vérifié (`rules`, `lua`, `native`, `dropped`).

L’import via MCP est synchrone, comme l’importateur VFX existant ; les gros projets peuvent prendre du temps. `AIVERSE_PYTHON`
choisit l’interpréteur. Les métacaractères du shell Windows `%`, `!`, guillemets et retours à la ligne dans les arguments sont
rejetés, pas interpolés.

Démarche de l’agent : examiner les éléments manquants, consulter `rules_schema`, implémenter les mécaniques adaptées en données,
puis en Lua, puis en module natif si nécessaire. Utiliser `scenario_run` et des parties de bots pour comparer DPS, temps de
production et vitesse de déplacement. Comparer des captures si l’image source est fournie. Ne résoudre que le travail vérifié ;
expliquer le comportement volontairement abandonné. Le C#/GDScript/Blueprint n’est ni traduit mécaniquement ni exécuté.

## Couverture et travail restant sur la spécification

| Domaine | Implémenté | Restant |
|---|---|---|
| Code Unity partagé | YAML, recherche de GUID, prefabs imbriqués et variantes déplacés dans `lib/unity` ; des imports de compatibilité gardent les VFX fonctionnels | Les limites d’imbrication existantes du parseur partagé s’appliquent toujours |
| Scènes/types Unity | Transformations monde, objets de scène, surcharges de prefabs, utilisation des scripts et champs sérialisés, candidats ScriptableObject, ordre des scènes de build | Prise en compte de l’échelle des transformations dans les bornes des modèles, regroupement sémantique robuste des types, déduction des joueurs/départs |
| Ressources/matériaux | Modèles/textures/audio et dépendances voisines gardent leurs chemins ; facteurs PBR et première surcharge de matériau ; métadonnées d’échelle des modèles ; en option PSD/TIFF → PNG via l’adaptateur ImageMagick/ffmpeg existant | Correspondance des slots de matériaux, textures empaquetées, réglages d’axes à l’import, découpe des images d’animation ; l’absence de convertisseur d’images est signalée |
| Unity 2D | Découpes de sprites dans l’atlas, cellules Tilemap explicites cuites dans l’atlas/une couche fixe, caméra orthographique | RuleTiles, tuiles transformées/teintées, dispositions Grid arbitraires, tuiles animées, pivots/tri des sprites |
| Audio/FX | Audio en boucle de caméra/gestionnaire → playlist de partie ; le convertisseur VFX existant écrit via une zone de préparation protégée | Routage du mixeur et liaison sémantique des effets aux événements |
| Terrain | Les données cuites opaques et les composants Terrain sont signalés | La conversion du champ de hauteurs/splat/arbres/détails de TerrainData n’est pas implémentée |
| Godot | Détection des scènes/ressources texte, ressources externes, translations simples, surcharges numériques exportées, index des scripts, ordre de la scène principale | Héritage de scènes, composition complète des transformations, décodage des Variant complexes multilignes, conversion TileMap/GridMap, clips d’animation |
| Unreal | Récepteur JSON et exportateur Python d’éditeur pour les acteurs chargés, propriétés candidates par réflexion, meshes/textures/audio via AssetExportTask | L’exportateur **n’a pas été exécuté dans Unreal** ; paysage, énumération complète des variables Blueprint et export des graphes non implémentés |
| Éditeur | Fenêtre d’import autonome localisée, entrée de menu, MCP et passation IA copiable | Intégration à l’assistant, sélecteurs de catégories de ressources, lancement direct de l’agent |

Des binaires `.asset` natifs inconnus d’Unity peuvent exister même avec Force Text (éclairage/NavMesh cuits). Ils sont signalés
un par un. La sérialisation binaire des scènes/prefabs/matériaux est rejetée avec l’instruction d’activer Force Text.
`.uasset` n’est jamais analysé hors d’Unreal.

L’utilitaire pour Unreal est `tools/importers/unreal_export/export.py` ; appelez `export_project(destination)` dans Unreal
Editor avec le plugin Python activé et un nouveau dossier de sortie. Références de son API :
[AssetExportTask](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AssetExportTask?application_version=5.6),
[EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.6).
Il exporte le niveau actuellement chargé et les ressources `/Game` ; il ne parcourt pas les niveaux non chargés.

## Vérification, 2026-11-03

Les projets synthétiques sous `tests/data/port` sont des données de test originales. Le FBX Unreal est le
`tests/data/models/cube.fbx` CC0 existant, écrit à la main. Aucune ressource de jeu téléchargée n’est commitée.

- 34 tests existants de l’importateur VFX passent après le déplacement du code partagé.
- 15 tests de l’importateur de projets couvrent le rejet de sérialisation, les surcharges de prefabs, les unités, les
  modifications protégées, l’idempotence (y compris la réécriture de `port.json`), l’invalidation sur changement de la source,
  la sûreté des chemins, sprites/tuiles et la résolution par CLI. Avec `AIVERSE_PORT_HEADLESS`, les cinq projets se chargent
  dans le vrai moteur et le projet de combat Unity se termine par la victoire du joueur 1.
- Suite C++ : 5857 vérifications, un échec d’écouteur localhost propre au bac à sable ; ce test est passé séparément hors du
  bac à sable réseau (27 vérifications). Un test supplémentaire du pont MCP est passé (5 vérifications).
- Avertissement de compilateur préexistant hors de ce changement : `fb` inutilisé dans `engine/fx/FxEffect.cpp:1058`.

Deux dépôts RTS publics ont été téléchargés uniquement dans `/tmp` puis importés ; **chaque carte produite a été chargée
individuellement avec `aiverse_headless --max-seconds 0`**, pas seulement la première/par défaut :

| Source | Révision | Résultat | Rapport |
|---|---|---|---|
| [Unity FPS RTS](https://github.com/aydenspringer/Unity-FPS-RTS-System) | `df468f644e6aff145d2eb219dca87f99742ca81d` | 509 ressources copiées, 375 candidats de types, 30 cartes ; 30/30 se chargent | 486 approximatifs, 1913 manquants ; scripts, composants de rendu/physique, données cuites opaques et ressources non prises en charge à revoir |
| [Godot Open RTS](https://github.com/lampe-games/godot-open-rts) | `a628ad3bb6a9a903587a181a7ee3300dbf735e81` | 388 ressources copiées, 552 candidats de types, 84 cartes ; 84/84 se chargent | 638 approximatifs, 2017 manquants ; logique des scripts, héritage de scènes, ressources intégrées et systèmes de tuiles/animation à revoir |

Ce sont des vérifications d’import/de chargement, **pas des portages jouables à l’identique**. Le nombre de candidats ne doit
pas être présenté comme des mécaniques de jeu reconstruites avec succès. L’exécution dans Unreal, sous Windows, Android et sur
le web reste non vérifiée.

Le formulaire d’import autonome a été inspecté dans l’éditeur macOS natif. La saisie automatisée par presse-papiers/frappes
rapides ne remplissait pas toujours le texte ImGui, donc aucun import complet par clics n’est revendiqué ; les mêmes opérations
ont été vérifiées via le pont MCP en C++ et la CLI. L’éditeur macOS/Metal compile.

Après la fusion du master actuel (`82c47120`), deux boucles `std::filesystem::path` de `NativeBuild.cpp` sont passées à
`const auto&` pour compiler avec AppleClang. C’est un correctif de compatibilité minimal dans un fichier partagé. MAIN doit
republier les paquets de l’éditeur après intégration et vérification de cette branche ; cette branche ne publie pas de builds.

Vérification finale après la fusion du master et le correctif de compatibilité :

- Éditeur macOS (Metal), aiversemcp, aiverse_headless et aiverse_tests recompilés avec succès.
- Suite C++ complète hors du bac à sable à sockets restreints : **5930 vérifications, 0 échec**.
- CTest : les cibles existantes des importateurs VFX et de projets passent (34 + 15 tests Python).
- Pont MCP ciblé : 5 vérifications ; utilitaires de build de l’éditeur natif : 42 vérifications, toutes passent.
- L’import de bout en bout via `aiversemcp --no-editor` accepte des `options` imbriquées, applique scale=2 et gère un chemin de
  sortie contenant des espaces et une apostrophe.
- `skirmish --seed 3` : les bleus gagnent à 470 secondes de jeu ; `mushrooms --seed 3` : les rouges gagnent à 92 secondes.
