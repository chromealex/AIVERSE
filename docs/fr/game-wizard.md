# Assistant « Créer un jeu »

Une fenêtre (hub de projets → **Créer un jeu**, ou **Projet → Créer un jeu…**) ou un appel MCP (`game_make`) produit un projet
de jeu complet avec son `.aiverse` : le jeu d’un modèle choisi d’après l’idée, les illustrations / modèles / musiques / sons de
l’utilisateur, une carte de tutoriel guidé, des conseils discrets en partie, des niveaux générés, le méta-jeu (menus,
améliorations, boutique, récompense quotidienne, profil, réglages, résultat), achats et publicité sur demande, langues,
identité visuelle et une vérification par des bots. Tout ce qu’il écrit, ce sont des données de jeu ordinaires, modifiables
ensuite.

Code : `apps/editor/core/GameWizard*.cpp` (sans UI), `apps/editor/core/WizardOps.cpp` (MCP), `apps/editor/EditorGameWizard.cpp`
(la page du hub). Kit d’exécution copié dans chaque jeu : `templates/_wizard/` (Lua + RmlUi, voir son README).

## Réponses (`WizardSpec`, toutes facultatives)

| Groupe | Clés |
|---|---|
| Contenu | `artDir` (dossier : png/jpg/tga, glb/gltf/obj/fbx, wav/ogg/mp3/flac, ttf/otf, md/txt), `artUrl` (un fichier ou un .zip / .tar.gz ; téléchargé avec le curl du système dans `~/.aiverse/wizard/<heure>`) |
| Idée | `idea` (n’importe quelle langue ; les .md/.txt du dossier sont lus aussi), `genre` (id de modèle ou `auto`), `features` (economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait scifi modern) |
| Projet | `title`, `location`, `studio` (bundle id `com.<studio>.<slug>`), `version`, `orientation`, `languages`, `platforms`, `seed` |
| Tutoriel | `tutorial` (`both` / `hard` / `soft` / `none`), `tutorialForced`, `tutorialSkippable`, `tutorialDim`, `hints` |
| Niveaux | `levels` (0..40), `mapPreset` (`auto`, `none` = copies de la carte de base, ou un preset du générateur), `mapSize`, `bossEvery`, `difficultyCurve`, `startResourcesPercent`, `keepTemplateMaps`, `verifyLevels` |
| Méta | `metaIdea` (texte libre : ce que la méta doit contenir ; va dans la tâche de l’agent), `currencies` (soft, hard), `upgrades`, `shop`, `daily`, `profileScreen`, `xpPerLevel` |
| Argent | `ads`, `interstitialEvery`, `purchases`, `stores` |
| Apparence | `brandingFromArt`, `paletteFromArt` |

`game_make_options` liste les choix, `game_make_plan` montre ce qui serait créé (sans fichiers). Mêmes réponses + seed = même jeu.

## Comment il décide

- **Modèle** : les mots de l’idée sont comparés aux mots-clés de chaque modèle (radicaux, ru / en), les fonctionnalités
  voulues à ses mécaniques, l’orientation à son écran ; en cas d’égalité, le seed tranche. Rien de précisé : l’un des
  modèles équilibrés.
- **Contenu** : les modèles et sprites 2D vont aux types dont l’id / le nom (ru / en) figure dans le nom de fichier ; le reste
  est copié et listé dans `WIZARD.md`. L’illustration principale (d’après le nom et la taille) devient l’écran de démarrage, le
  fond du menu et la couleur d’accent de l’interface ; l’image la plus carrée (ou « icon » / « logo ») devient l’icône 1024.
  La musique, d’après son nom, va dans `menu` / `match` / `battle` / `victory` / `defeat`, les sons courts sur des événements
  (`click`, `select`, `move`, `died`, `underAttack`, ...). La première police TrueType devient `fonts/title.ttf`.
- **Tutoriel guidé** (`maps/wiz_tutorial.json` + `scripts/wizard/tutorial_steps.lua`), écrit à partir des règles :
  - *économie* (ouvriers, chantiers de barre latérale, MCV déployables, constructeurs TA) : déployer, récolter, champ de
    distorsion / ravitaillement si besoin, la chaîne de prérequis d’un producteur de type caserne, former deux unités, raser un
    bâtiment ennemi qui ne riposte pas ;
  - *garnison* (Mushroom Wars, State.io) : envoyer vers un bâtiment neutre, en prendre deux autres, prendre un bâtiment ennemi ;
  - *construction auto* (lane battlers) : appuyer deux fois sur Construire, améliorer, tenir ;
  - *générique* : déplacer l’armée, attaquer.
  Seules les commandes de l’étape passent (`before_command`), l’ennemi attend (ses unités se défendent quand même), le panneau
  et la flèche sont dessinés par le script de vue, les ressources de l’étape sont complétées pour qu’on ne reste pas bloqué.
  Pas pour les économies de routes à la Knights and Merchants (forme générique là-bas).
- **Tutoriel discret** : des conseils uniques (caméra, ouvriers inactifs, ressources insuffisantes, limite de
  ravitaillement, armée, garnisons, attaque en cours) depuis `scripts/wizard/hints_config.lua` ; les conseils vus sont
  mémorisés dans le profil, les réglages les désactivent.
- **Niveaux** : `maps/wiz_level_N.json` = la carte de base + `generate` (le preset du modèle, un seed par niveau, la carte de
  base comme modèle) ; difficulté 0..2 selon la courbe, un boss tous les N niveaux ; récompenses, récompenses de première
  victoire et temps des étoiles dans `ui/wizard.json`.
- **Méta** : `ui/wizard.json` (configuration), `ui/profile.json`, `ui/shop.json`, `ui/screens.json` (`start: home`,
  `afterMatch: result`), écrans du kit. Les améliorations arrivent dans la partie sous forme d’`initialVars` `wiz_up_<id>`, et
  `scripts/wizard/meta_bonus.lua` les transforme en bonus / ressources de départ.
- **Argent** : `plugins` / `store` / `ads` dans `game.json` par plateforme (blocs publicitaires de démo Yandex et simulations
  de bureau : remplacez les ids).

## Étape de l’agent : le jeu décrit, à l’identique

Le modèle est un point de départ, pas le jeu. Avec `agent` activé (par défaut), l’assistant écrit `WIZARD_TASK.md` dans le
projet — la description / le GDD mot pour mot, plus la façon de garder cohérentes les parties de l’assistant (étapes du
tutoriel, conseils, configuration de la méta, fichiers de langue) — et la confie à un agent IA connecté, qui reconstruit le
projet via les outils MCP de l’éditeur (en direct dans l’éditeur) : types, statistiques, factions, mécaniques (règles ou Lua),
cartes, conditions de victoire, textes ; puis il vérifie avec `rules_lint`, `map_validate`, `match_run` et écrit
« ## Агент: что сделано » dans WIZARD.md.

- Connexion : l’assistant affiche le même panneau « connecter un agent IA » que la fenêtre Agent (`EditorMcpConnect.*`,
  `core/McpInstall.h`) : un clic écrit aiversemcp dans la configuration du client, et cette connexion unique sert aux deux.
- Lancement depuis l’éditeur (`core/WizardAgent.*`, via le shell de connexion de l’utilisateur pour que la CLI soit trouvée) :
  Claude Code `claude -p --output-format stream-json --allowedTools mcp__aiverse` et Codex CLI `codex exec --full-auto`,
  la tâche sur stdin, le dossier du projet comme répertoire de travail. La CLI est cherchée dans le PATH, les dossiers
  d’installation habituels, le shell de l’utilisateur et dans les applications de bureau (Claude Code dans
  `~/Library/Application Support/Claude/claude-code/<version>/…/claude.app`, Codex dans `ChatGPT.app/Contents/Resources/codex-cli`).
  La progression s’affiche dans le fil d’activité (fenêtre Agent), la barre d’état et la page de l’assistant (bouton Arrêter) ;
  le journal complet dans `logs/wizard-agent-<heure>.log`.
- Cursor, Windsurf, Claude Desktop : « Copier la tâche pour l’agent » met la tâche dans le presse-papiers pour la coller dans
  le client.
- Pas de clé d’API dans l’éditeur : l’agent tourne dans le client et sous le compte de l’utilisateur.
- Dictée : « Dicter » sous le champ de l’idée (page Idée) et le champ des idées de méta (page Méta) — reconnaissance vocale de
  macOS (`apps/editor/EditorSpeech_mac.mm` : Speech + AVFoundation, la première langue du jeu comme locale ; le système demande
  une fois l’accès au micro et à la reconnaissance). Le texte est ajouté au champ. Autres plateformes : bouton désactivé.
- `ai` dans la spécification (textes par langue : slogan, introduction, types, ressources, monnaies, améliorations, niveaux)
  permet à un agent qui appelle lui-même `game_make` de transmettre les noms et textes du jeu.

Le tutoriel dans la vue Game de l’éditeur : l’éditeur n’a pas d’interface de jeu pour afficher le panneau d’étapes, donc le
script de vue envoie `wiz_tut_off` et la carte du tutoriel s’y joue comme une partie ordinaire. Panneau, flèche et filtre
fonctionnent dans le client du jeu (AiverseGame, builds). L’éditeur ouvre le premier niveau, pas le tutoriel.

## Vérification

Avec `verifyLevels`, l’assistant joue lui-même le tutoriel (`initialVars {wiz_tutorial_auto: 1}` : l’ordre `auto` de chaque
étape passe par le même filtre que celui du joueur) et un échantillon de niveaux avec des bots, et signale les erreurs de
chargement, les erreurs de script et un tutoriel inachevé. Passé sur tous les modèles : tout se termine sauf ambersands
(construction sur rocher uniquement), planets et underlord (déplacements / creusement sur terrain spécial) — l’assistant le
signale dans ses avertissements.

Tests : `game_wizard_plan_picks_a_template_by_the_idea`, `game_wizard_makes_a_playable_project`,
`script_command_card_false_hides_the_button`.

## Modification du moteur

Les commandes de script acceptent `"card": false` (pas de bouton / raccourci ; envoyées par les scripts de vue, les documents
HUD, MCP) — le Suivant / Passer du tutoriel (`modules/rts/RtsScriptCommands.cpp`, cartes de commandes du client et de
l’éditeur).
