# Vroom 0.33 — course 3D sous Unreal Engine 5.8

## État de la version 0.33

La 0.33 corrige le redémarrage dans l’herbe, le comptage des tours invalidés et les blocages des IA aux stands. Le joueur peut maintenant demander le service dans une large zone de l’allée. Le dérapage récompense le contre-braquage dosé ; un petit frottement de roues peut se rattraper à la réception. Le cockpit et ses gants blancs de la **0.30** sont conservés, avec une prise resserrée d’environ **3 mm**. Le décor reçoit des matériaux photographiques, des rochers scannés et deux modèles de feuillus avec textures photographiques. Les 30 suites portables et les 45 contrôles visibles du package passent, dont huit mesures de cadence et six tours complets. Le [rapport de finalisation](docs/FINALISATION_033.md) distingue ces étapes.

Dépôt : [vroom-unreal](https://git.estrasse.fr/david/vroom-unreal).

Prototype personnel consacré au Vroom de Lankhor de 1991 : une monoplace, six silhouettes de circuits issues des petites cartes Atari ST, une conduite jouable et un habillage 3D Unreal. L'objectif est de disposer d'une base mesurable pour retrouver progressivement le jeu d'époque. **La fidélité intégrale et le photoréalisme ne sont pas encore réalisés.**

Le jeu propose **Arcade, Grand Prix avec qualification et Chrono**, douze adversaires capables de freiner derrière un véhicule et de choisir une voie de dépassement, trois difficultés et un enchaînement des six étapes. Le Grand Prix conserve un classement général des treize pilotes et désigne le champion ; les adversaires encore en course sont classés à l’arrivée du joueur selon leur avancement. Les stands imposent une immobilisation pour changer les pneus ou ravitailler. Les records par circuit/mode, les derniers tours, le delta et un fantôme enregistré donnent un objectif au Chrono.

Le bandeau translucide de 84 pixels conserve les informations de course et ajoute les objectifs, le classement et les alertes. Les menus sont accessibles à la souris, au clavier et via les commandes manette prévues par Unreal. Les préférences de conduite et de son ainsi que les touches clavier peuvent être sauvegardées. Les décors reçoivent des reliefs artistiques et une végétation différenciée ; traces de pneus, fumée et sons stéréo des concurrents complètent les retours de conduite. Le cockpit rapproche les mains neutres de la composition Amiga et affine les roues du joueur. Le sens des faces des pneus et jantes est corrigé pour fermer leur surface extérieure ; les petits rétroviseurs restent bas devant les pneus. Le cadrage bas et la plage de braquage sont conservés ; les captures normales neutre/gauche/droite du package final ont été examinées.

Les mesures historiques de la 0.30 restent documentées dans [VALIDATION.md](docs/VALIDATION.md). Elles ne certifient pas automatiquement la 0.33 : son [rapport](docs/FINALISATION_033.md) consigne l’état des contrôles du nouveau package. Les scénarios se suivent à l’écran avec `python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak`.


Le [mode d'emploi](docs/PLAYER.md) décrit les commandes, les règles exactes et les sauvegardes. Les fonctionnalités annoncées ici sont celles du code de cette branche ; [VALIDATION.md](docs/VALIDATION.md) distingue les essais réellement réalisés et leurs limites. **Windows, DLSS et l'essai d'une manette physique restent à valider.**
## Jouer sur le Mac

La version compilée et testée ici cible **Unreal Engine 5.8, Mac Apple Silicon**. Des profils **RTX 5090 sous Linux/Vulkan et Windows/DirectX 12** sont également préparés ; leurs builds et leur rendu sur le PC restent à vérifier. Voir [docs/RTX5090.md](docs/RTX5090.md). L'installation Mac utilisée se trouve dans `/Users/Shared/Epic Games/UE_5.8`. Une compilation du code nécessite Xcode avec les outils de développement et la configuration acceptée par cette installation Unreal. Les lanceurs n'installent aucun logiciel.

Pour jouer à la version installée, ouvrir **`~/Applications/Vroom.app`**, double-cliquer **`~/Desktop/Vroom.command`** ou utiliser **`Tools/play_latest.command`**. Le lanceur lit le manifeste de la version explicitement validée et vérifie son binaire. **Compiler un package ne change plus la version active.** Le choix de mode, de difficulté et de circuit précède le départ ; le menu accepte les clics, les flèches et la croix directionnelle.

**`Tools/run_mac.command`** lance quant à lui le jeu dans le module Editor déjà compilé, en fenêtre **1440 × 900**, sans reconstruire les binaires. La première ouverture peut compiler des shaders.

Pour préparer un clone ne contenant que les sources, fermer l'éditeur et le jeu, puis lancer **`Tools/build_mac.command`**. Il compile `VroomEditor`, puis prépare les matériaux, les meshes des monoplaces et de l’herbe, ainsi que la carte `Circuit`, via `Tools/bootstrap_assets.py` et ses générateurs associés. Le générateur conserve la carte lorsqu'elle existe déjà ; il régénère uniquement ses matériaux identifiés par métadonnées et refuse d'écraser un matériau étranger. Les matériaux et la carte dans `Content` sont versionnés avec le projet. Les binaires, `Intermediate`, `Saved` et les caches Unreal restent des résultats locaux régénérables.

Depuis un terminal ouvert à la racine du projet :

```sh
Tools/build_mac.command
Tools/run_mac.command
```

Si Unreal est installé ailleurs :

```sh
Tools/build_mac.command --engine "/chemin/vers/UE_5.8"
Tools/run_mac.command --engine "/chemin/vers/UE_5.8"
```

Les deux scripts acceptent également la variable `VROOM_UE_ROOT`. `--help` donne les options sans lancer Unreal. Les scripts supposent un chemin de projet Mac accessible et n'effectuent aucun téléchargement. Ne pas lancer plusieurs compilations ou générations d'assets à la fois.

## Commandes

| Commande initiale | Action |
|---|---|
| Haut, Z/W ; bas, S/Espace ; gauche/droite, QD/AD | Accélérer, freiner, braquer |
| Maj gauche/droite ou LB/RB | Entrer en glisse avec le braquage, doser le contre-braquage, puis relâcher pour le bonus gagné |
| E/X ou Y/X sur manette | Monter / descendre d'un rapport |
| G ; C ou Back/Select | Boîte automatique/manuelle ; cockpit/poursuite |
| M | Conduite souris : déplacement horizontal, clic gauche accélérateur, clic droit frein |
| P, Start ou bouton central de souris | Pause / reprise |
| O/F7 ; F8 | Graphismes ; commandes et son |
| Entrée/A ; Échap/B | Valider au menu ; revenir |
| T/F, à l'arrêt dans l’allée des stands | Pneus / essence |
| Entrée/A ou clic gauche en conduite souris, dans l’allée des stands | Service complet pneus et essence |
| R | Recommencer la séance |
| F1…F6 | Choisir le circuit |
| F10 | FOV préféré, puis variantes −12°/+12° |

Les touches clavier se réattribuent dans **Commandes et son** ; sensibilité, zone morte, FOV, vue, boîte, volumes et fantôme y sont enregistrés. Appliquer conserve les modifications, Annuler les abandonne. Les préférences et les records restent locaux.

Le stick gauche et les gâchettes commandent direction et pédales ; la croix directionnelle et A/B parcourent les menus. Les noms de boutons suivent la disposition Xbox. La détection et les correspondances de chaque manette nécessitent un essai matériel. Le [mode d'emploi complet](docs/PLAYER.md) détaille les trois modes, la souris, les pages de réglages, les stands et le fantôme.

## Circuits et conduite

| Touche | Circuit du fichier ST | Longueur `CLAP` originale |
|---|---|---:|
| F1 | Autriche | 5 911 m |
| F2 | France | 3 813 m |
| F3 | USA | 6 405 m |
| F4 | Suède | 4 018 m |
| F5 | Japon | 4 359 m |
| F6 | Royaume-Uni | 4 778 m |

Les touches suivent l’ordre des fichiers de sources. Les modes de progression enchaînent Japon, Autriche, France, Royaume-Uni, Suède et USA ; choisir le Japon permet de commencer par la première étape. Le JSON conserve séparément les événements longitudinaux d'origine et les coordonnées des petites cartes. Le tracé en plan est une **prévisualisation dérivée de ces cartes**, lissée puis mise à l'échelle pour retrouver la longueur `CLAP`. Les rayons et le rapport d'aspect des pixels restent à valider ; ce lissage ne reconstitue pas les tables de projection historiques. Voir [docs/REFERENCE.md](docs/REFERENCE.md) pour les sources, l'extraction et les preuves.

L'Autriche possède maintenant un relief issu de ses **47 événements de pente** dans les sources ST. Le profil est continu en hauteur et en pente, y compris au raccord du tour. Son calibrage métrique et sa correction de fermeture sont des adaptations : l'écart entre son point bas et son point haut atteint environ **25,90 m**, sans constituer un relevé du vrai circuit ni un dénivelé cumulé. Les cinq autres circuits restent plats, car aucun événement de relief explicite n'apparaît dans leurs fichiers de cette archive. [docs/ELEVATION.md](docs/ELEVATION.md) détaille l'intégration, les unités retrouvées et les choix de calibrage.

Le moteur portable reprend les six coefficients de rapport, les seuils automatiques, une courbe de couple échantillonnée, une résistance de l'air et un freinage quadratique relevés dans le code ST. Les passages supérieurs se font vers **80, 130, 180, 230 et 270 km/h**. Le régime maximal de table est **13 312 tr/min**. Le joueur, les adversaires, les contacts et le chronomètre avancent ensemble par pas de **1/120 s**. Le rendu interpole les positions précédentes et courantes du joueur et des adversaires entre ces pas. Le temps accumulé pendant une image excessive est borné pour éviter les grands sauts ; dans ce cas, le temps de simulation peut avancer moins vite que le temps réel.

La conduite reprend le déplacement latéral arcade : le volant agit sur le déport et la courbe entraîne une dérive à compenser. La caméra suit la tangente lissée de la route avec une inclinaison du châssis limitée. Les filtres de commande avancent au pas physique ; la direction et la hauteur des petits sauts sont interpolées pour l’affichage. La rotation du châssis en glisse possède une vitesse continue et une accélération plafonnée à 160°/s² pour adoucir l'entrée et la sortie du dérapage. Les calibrages restent propres au prototype. [docs/DRIVING.md](docs/DRIVING.md) détaille les équations et les limites.

Pour déraper, appuyer sur **Maj** ou LB/RB en braquant, à plus d’environ **44 km/h** sur la route, puis maintenir le bouton en **contre-braquant de façon dosée**. Au clavier, de brèves pressions sur la direction opposée entrecoupées de relâchements permettent de régler la glisse. La charge dépend de ce contrôle : conserver le braquage initial ou tenir indéfiniment le contre-braquage à fond ne suffit pas. Relâcher le bouton avec une charge acquise donne une poussée de niveau 1 ou 2. Le frein ne déclenche pas automatiquement de dérapage ; il l’annule. Contact, herbe, saut physique, panne sèche ou vitesse insuffisante effacent aussi la charge. Ces règles sont une adaptation ludique, sans équivalence démontrée dans Vroom original.

Un petit frottement entre roues peut provoquer un bond bref : corriger la poussée latérale pendant le bond préserve la vitesse à la réception. Une bonne correction conserve **100 %** de la vitesse d’arrivée, une commande neutre **97 %**, une mauvaise correction **94 %**. La réception n’ajoute jamais de vitesse ni de bonus gratuit ; un choc sévère conserve sa pénalité de casse.

La route fait 9,5 mètres de large et ses accotements sont ouverts : les glissières et la butée latérale à **±9 m** ont été supprimées. Le hors-piste augmente le roulement et réduit l'adhérence ; la voiture ralentit et peut revenir sur la route en braquant. Les contacts entre voitures sont recherchés par balayage entre deux états de simulation, avec une seule réponse par rencontre. Un petit choc retire de la vitesse ; un choc central important entraîne une casse, tandis qu'un contact extérieur rapide peut provoquer un saut au niveau des pneus. La vitesse relative et la zone du contact déterminent la réponse.

Pendant une casse ou la récupération, les commandes sont suspendues et le HUD affiche le point mort. Une roue se détache et est projetée avec une rotation visible pendant la casse. La voiture revient progressivement sur le bitume en 2,8 à 7 secondes selon son éloignement, puis repart à l’arrêt, avec une courte protection contre un nouveau choc. Le tour continue et reste invalidé pour le meilleur temps. Ces réponses visuelles, la trajectoire du débris et les règles de remise en piste sont des adaptations du prototype, pas une simulation physique complète ni une reproduction intégrale des routines originales. Les éléments solides du décor sont testés par balayage 3D ; les feuilles, l’herbe et les marquages ne constituent pas des obstacles. [docs/COLLISIONS.md](docs/COLLISIONS.md) distingue les règles retrouvées dans les sources, les adaptations et leurs tests.

## Image et son

Les options graphiques proposent quatre préréglages et des réglages séparés de résolution interne, anticrénelage, ombres, éclairage global, reflets, végétation, vent, effets, flou, plafond d’images/s et synchronisation verticale. Les choix appliqués sont conservés dans les réglages utilisateur. Les valeurs ci-dessous sont les **défauts sans préférence enregistrée**, pas des mesures de performance :

| Réglage | Mac | Profils RTX 5090 — Linux et Windows |
|---|---|---|
| Résolution interne / anticrénelage | 70 % / TAA | 100 % / TSR |
| Éclairage global et reflets | Préréglage léger, Lumen désactivé | Préréglage Ultra, Lumen activé |
| Ombres | Cascades | Ombres virtuelles demandées |
| Herbe / vent | Faible / activé | Dense / activé |
| Synchronisation / plafond | VSync / 60 images/s | VSync / 120 images/s |

Les définitions 1440 × 900 et 2560 × 1664 utilisées pour les essais Mac désignent la sortie affichée. Avec le réglage interne de 70 %, elles ne correspondent pas à un rendu natif à ces définitions.

Sur UE 5.8, **TAA correspond à `r.AntiAliasingMethod=2`** ; la valeur `3` correspond à MSAA. Les choix TAA et TSR du menu utilisent les constantes du moteur. Les préréglages Élevé et Ultra activent Lumen ; le coût dépend de la machine et de la définition. Ces plafonds restent des cibles. Les profils RTX 5090 ne disposent pas encore de mesures sur le PC pour cette version.

L’atmosphère et les teintes du décor varient avec le circuit, sans recapture permanente du ciel. L’exposition est fixe et le flou de mouvement est désactivé par défaut. En plein écran fenêtré, la définition de sortie suit celle du bureau ; une sortie 4K ne garantit pas une cadence donnée.

Le cockpit et les bras sont de vrais volumes 3D. Les gants utilisent le modèle de main gauche OpenXR de **Valve Corporation**, sous sa licence jointe : **3 028 sommets et 4 899 triangles par main**, la droite étant obtenue par symétrie. La prise des doigts est calculée une fois sur les articulations du modèle. Les deux gants sont ensuite des composants distincts, dont la prise est fixée dans le repère du volant : sa rotation déplace et oriente ensemble la jante et les mains, sans recalcul squelettique à chaque image. `M_RacingGlove` utilise deux textures de couleur et de normales ; leurs sources sont en **4K**, avec une taille maximale de **2 048 pixels dans le package Mac**. Elles apportent le grain du cuir, les renforts et les coutures. [docs/ASSETS.md](docs/ASSETS.md) précise la préparation et l'attribution.

Les mains partagent la prise et la rotation de la jante pendant le braquage. Les gants blancs, l’échelle **1,05**, l’inclinaison locale des poignets **80°** et la hauteur de prise de la 0.30 sont conservés. `grip_depth_cm=-2.8` dans `Content/Data/gloves.json` resserre la prise d’environ **3,1 mm** vers le centre. Le recalage local du maillage reste limité à **4,3 mm supplémentaires** par rapport au gant 0.30, sans retirer de faces ni modifier les UV. Les tests vérifient le dégagement des sommets ; ils ne garantissent pas l’absence d’intersection à l’intérieur de chaque triangle. Les vues normales neutre/gauche/droite du package 0.33 ont été inspectées dans la série visible validée. Le cadrage conserve le volant bas de la référence Amiga : **la main gauche monte dans un virage à droite, et la droite dans un virage à gauche**, tandis que l’autre peut passer sous le cadre. Les manches suivent les poignets. Les sept dessins Amiga guident la composition ; ils ne pilotent plus le placement des mains par interpolation de coordonnées d’écran. Cette prise 3D constitue une adaptation, sans correspondance pixel par pixel revendiquée. Les onze composants du cockpit et les rétroviseurs partagent le clignotement après remise en piste. [docs/COCKPIT_AMIGA.md](docs/COCKPIT_AMIGA.md) décrit ce placement et ses limites de validation ; [docs/AMIGA_ART.md](docs/AMIGA_ART.md) documente les dessins extraits.

Le bandeau de course est une interface **Canvas moderne, compacte et translucide**, avec contours arrondis, fond bleu nuit et accents cyan. Il conserve les informations inspirées de l’original : carte du circuit, trois voyants, Grand Prix, score, stage, tour, 27 repères de dépassement et jauge d’essence. Sa hauteur de référence est **84 pixels sur une fenêtre de 900 pixels de haut**, soit environ 9,3 %, avec une marge supérieure. Les objectifs, le classement, les alertes pneus/essence et les indications de service complètent cette information. Le grand boîtier 3D de 34 % n’est plus affiché. Les chronos et le retour de dérapage restent séparés ; les règles originales ne sont pas toutes reproduites.

Les matériaux de la scène et les primitives du HUD sont préparés sous l'écran **PRÉPARATION DU CIRCUIT**, chrono arrêté. Les lignes épaisses Canvas utilisent des quadrilatères en triangles et un état graphique statique pour éviter de recréer leurs configurations de rendu à chaque image. [PERFORMANCE.md](docs/PERFORMANCE.md) conserve le diagnostic et les mesures après correction.

Les abords comprennent quatre publicités rétro en 2048 × 512, des panneaux séparés sur poteaux et jambes de force, douze stands détaillés et des portiques métalliques de départ, d’arrivée, de stands et de freinage. Le portique de départ et le HUD suivent le signal rouge, orange puis vert, synchronisé avec les bips et la libération de la course. La ligne chronométrée conserve son origine. Les publicités sont des créations typographiques du prototype. Les poteaux, troncs, murs et structures ont des volumes de collision ; l’herbe, les feuillages, les peintures au sol et les images publicitaires sont exclus. Une sortie de piste ralentit progressivement. Un choc sévère contre le décor peut casser les pneus et impose un retour lent sur le bitume, commandes bloquées, puis une reprise à l’arrêt ; un contact à 60 km/h ou moins rebondit sans cette remise en piste forcée. Les règles et leurs adaptations sont décrites dans [docs/COLLISIONS.md](docs/COLLISIONS.md).

Les accotements portent des touffes d’herbe 3D à brins masqués, dont les pointes ondulent sous le vent. Le menu permet de couper l’herbe ou de choisir trois densités ; les distances de visibilité sont respectivement 30, 45 et 60 m. Les touffes sont instanciées et le vent est calculé par le matériau, sans déplacer les instances sur le CPU à chaque image. Les monoplaces utilisent désormais des meshes dédiés : nez effilé, pontons, cockpit ouvert, casque, suspensions, ailerons profilés et roues détaillées. `Tools/bootstrap_car_assets.py` génère **3 182 triangles par concurrent** ; les douze adversaires restent répartis dans quatre groupes instanciés, avec six livrées. Les roues du joueur utilisent 40 segments, les supports de rétroviseurs rejoignent la coque hors du passage des pneus et le pourtour du cockpit reçoit carbone, rembourrage et fixations métalliques. Deux feuillus utilisent désormais des modèles existants, trois niveaux de détail et des textures photographiques de branches et d’écorce ; les conifères et arbres parasols conservent leurs silhouettes précédentes. [docs/ENVIRONMENT_ASSETS.md](docs/ENVIRONMENT_ASSETS.md) décrit ces ressources CC0, leur import et les validations réalisées sur le package. Les terrains et horizons reçoivent des profils artistiques propres aux circuits ; les bâtiments restent stylisés. La mini-carte reste en cache et masque les adversaires en solo. Ce travail améliore les formes sans établir un rendu photoréaliste ni une copie exacte des voitures originales.

[docs/PERFORMANCE.md](docs/PERFORMANCE.md) et [docs/VALIDATION.md](docs/VALIDATION.md) consignent les mesures Mac datées, le build testé et leurs limites. Les anciennes mesures ne valident pas automatiquement un nouveau package ou un autre réglage graphique.

Le lancement peut demander une sortie 4K, sans garantie de débit d'images sur cette machine :

```sh
Tools/run_mac.command -ResX=3840 -ResY=2160
```

Le menu prend en charge **NVIDIA DLSS 4.5 optionnel sur Windows**, avec le plugin compatible UE 5.8 : il détecte sa disponibilité et n’active que les modes pris en charge (DLAA, Qualité, Équilibré, Performance). Sans plugin utilisable, le rendu natif reste disponible. **DLSS n’est pas disponible sur ce Mac**, et aucune génération d’images ni DLSS 5 n’est activée. Les profils NVIDIA et leur installation sont détaillés dans [docs/RTX5090.md](docs/RTX5090.md).

Le son utilise la banque `Content/Data/amiga_sound.bin` : moteur rééchantillonné selon le régime, crissement en glisse et impacts, mélangés en mono à 48 kHz. Une synthèse de secours prend le relais si la banque manque. Une couche stéréo ajoute les moteurs adverses, leur position gauche/droite, un décalage de hauteur lié au rapprochement, le bruit de surface et des retours de rapports et de charge de dérapage. Les volumes général, moteur et effets sont séparés. Les pneus laissent des traces temporaires et de la fumée selon la qualité des effets.


## Reconstruire et vérifier

Les scripts proposent trois modes :

```sh
Tools/build_mac.command --editor
Tools/build_mac.command --assets
Tools/build_mac.command --package
```

`--editor` compile le module Editor et prépare les assets ; c'est le défaut. `--assets` ne recompile pas le C++, mais demande un module déjà utilisable. `--package` effectue ces deux étapes puis appelle `BuildCookRun` pour compiler, cuire, préparer et archiver une application **Mac Development** dans `Saved/Packages`. `--archive /chemin` change cette destination. Un package créé est un résultat local de développement ; ces commandes n'ajoutent pas de signature ou de notarisation de distribution. La carte, les matériaux, le mesh des touffes d’herbe et les données sous `Content/Data` sont inclus par la configuration de packaging.

Tests portables, puis vérification du package Mac et installation explicite :

```sh
python3 Tools/test_portable.py
python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak
python3 Tools/install_mac.py --validation "CHEMIN-ANNONCE-PAR-LE-VERIFICATEUR/results.json"
```

Remplacer le chemin de `results.json` par celui annoncé par le vérificateur. `--visible` affiche et active chaque scénario ; `--graphics-preset performance` fixe temporairement le profil sans modifier les préférences sauvegardées. La vérification contrôle cockpit, interface, voitures, menus, persistance, service au stand, dérapage et scénarios de collision. Elle mesure deux définitions et, avec `--soak`, les six circuits pendant au moins deux minutes chacun, avec des seuils de cadence et de continuité du mouvement. Les rapports restent dans `Saved/Validation`. `--editor` limite les captures au module Editor et ne constitue pas une validation installable.

L’installateur exige la série réussie complète et les empreintes du binaire et des données du package. Il conserve les versions sous `~/Library/Application Support/Vroom/Releases/`, écrit le manifeste `active.json` et prépare le lanceur stable `~/Applications/Vroom.app` ainsi que le raccourci du Bureau. Les builds seuls ne remplacent plus ce manifeste. Ces opérations locales ne constituent pas une signature ou une notarisation de distribution.

La suite de conduite contrôle accélération, freinage jusqu'à l'arrêt, rapports, symétrie du volant, sens d'un virage, hors-piste, borne de vitesse, comportement à 60/120 Hz et entrées non finies. La suite de contacts contrôle notamment les seuils de vitesse relative, la réponse unique, le balayage entre états, les petits chocs, la casse centrale, le saut, l'atterrissage, la remise en piste et la protection temporaire. Leur réussite vérifie la cohérence des modèles, sans constituer une comparaison avec Vroom exécuté sur ST ou Amiga.

Les scénarios Unreal reproductibles `-VroomCollisionTest=slow`, `crash` et `tyre`, leurs compteurs attendus et leurs commandes de capture sont décrits dans [docs/COLLISIONS.md](docs/COLLISIONS.md). Ils vérifient les événements de contact et de retour ; l'apparence et la fluidité du rendu demandent une observation séparée.

Les options de capture et de démonstration suivantes servent aux vérifications visuelles :

```sh
Tools/run_mac.command -VroomAutoDemo -VroomChaseCamera \
  -VroomCapture=/tmp/vroom-preview.png -VroomCaptureAfter=6 -VroomExitAfter=10
```

`-VroomAutoDemo` démarre automatiquement une conduite assistée de test ; elle n'est pas un mode démonstration original ni un pilote garanti sans sortie de route. `-VroomChaseCamera` choisit la caméra arrière. La capture est demandée après le nombre de secondes de temps d'application indiqué, HUD compris ; la sortie automatique est facultative. Les options n'apportent aucune validation par elles-mêmes : vérifier l'image et les journaux produits. Sur ce Mac, les journaux de jeu sont dans `~/Library/Logs/Vroom`. Le script de génération écrit `Saved/Logs/VroomBootstrap.log`.

## Suite du travail

L'édition de référence choisie est **Vroom Amiga**. Il reste à enregistrer des séquences de conduite reproductibles dans l'original, valider les courbes et le calibrage du relief en espace de conduite, puis comparer les commandes, temps, vitesses et trajectoires. Les règles de course, le comportement réactif des adversaires, les stands et les nouveaux décors restent des adaptations à comparer à l’original. La suite concerne notamment le calibrage de la difficulté, la fidélité des trajectoires, la présentation, les périphériques matériels et les validations Windows/NVIDIA. L'usage demandé est personnel ; aucune distribution ni vente n'est préparée.
