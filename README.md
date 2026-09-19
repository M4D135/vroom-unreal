# Vroom 0.34 — course 3D sous Unreal Engine 5.8

## État de la version 0.34

La 0.34 reprend désormais la **photo de cockpit F1 fournie par David** : yeux plus bas, carrosserie rouge et vrais pneus visibles, volant classique compact à trois branches, sans halo, prise à neuf heures et trois heures et gants blancs conservés. Les rétroviseurs allongés reposent sur des bras rouges, derrière les pneus dans la géométrie de la voiture. Cette nouvelle référence remplace la contrainte précédente de cadrage strictement Amiga.

Les adversaires anticipent leur freinage et s’engagent dans un dépassement avant de se rabattre. Le drift compte trois niveaux de turbo, un bonus de lâcher précis et des étincelles bleues, ambre puis violettes. Le décor reçoit des bosquets plus variés, des panneaux rythmés et des portiques supplémentaires. Un cinquième profil **Photoréalisme** privilégie éclairage et ombres avec une cible de 30 images/s sur Mac ; Performance conserve sa cible de 60 images/s.

**Version installée : `0.34.0-c8a4441fbfd4`.** David a approuvé visuellement la prise du pouce autour de la jante circulaire. Les **31 suites portables**, dont les 10 contrôles des gants, et les **47 contrôles de la série visible `20260919-231125-package` passent**. En Photoréalisme, les huit mesures couvrent deux définitions et les six circuits complets : **29,996–30,000 images/s**, avec un maximum de **39,573 ms par image**, après préparation. Les empreintes du binaire, des données et du rapport installé sont vérifiées ; la version 0.33 est conservée pour retour arrière. Les captures [neutre](docs/images/cockpit-034-neutral.png), [droite](docs/images/cockpit-034-right.png) et [gauche](docs/images/cockpit-034-left.png) montrent ce package avec la pose approuvée. Le [rapport](docs/validation-results.json) et le [bilan 0.34](docs/FINALISATION_034.md) détaillent ces preuves et leurs limites. L’état Git est consultable dans le dépôt ci-dessous.

Dépôt : [vroom-unreal](https://git.estrasse.fr/david/vroom-unreal).

Prototype personnel consacré au Vroom de Lankhor de 1991 : une monoplace, six silhouettes de circuits issues des petites cartes Atari ST, une conduite jouable et un habillage 3D Unreal. L'objectif est de disposer d'une base mesurable pour retrouver progressivement le jeu d'époque. **La fidélité intégrale et le photoréalisme ne sont pas encore réalisés.**

Le jeu propose **Arcade, Grand Prix avec qualification et Chrono**, douze adversaires capables de freiner derrière un véhicule et de choisir une voie de dépassement, trois difficultés et un enchaînement des six étapes. Le Grand Prix conserve un classement général des treize pilotes et désigne le champion ; les adversaires encore en course sont classés à l’arrivée du joueur selon leur avancement. Les stands imposent une immobilisation pour changer les pneus ou ravitailler. Les records par circuit/mode, les derniers tours, le delta et un fantôme enregistré donnent un objectif au Chrono.

Le bandeau translucide de 84 pixels conserve les informations de course et ajoute les objectifs, le classement et les alertes. Les menus sont accessibles à la souris, au clavier et via les commandes manette prévues par Unreal. Les préférences de conduite et de son ainsi que les touches clavier peuvent être sauvegardées. Les décors reçoivent des reliefs artistiques et une végétation différenciée ; traces de pneus, fumée et sons stéréo des concurrents complètent les retours de conduite. La composition du cockpit s’appuie sur la photo F1, avec les manches raccordées au centre des bracelets et les rétroviseurs séparés des pneus. La prise du pouce approuvée conserve ce cadrage et le volant circulaire.

Les mesures historiques de la 0.30 restent documentées dans [VALIDATION.md](docs/VALIDATION.md). Les [preuves 0.33](docs/FINALISATION_033.md) et [0.34](docs/FINALISATION_034.md) restent séparées : une ancienne mesure ne certifie pas un nouveau package. Les scénarios se suivent à l’écran avec `python3 Tools/verify_mac.py --visible --graphics-preset photorealism --profile --soak`.


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

Pour déraper, appuyer sur **Maj** ou LB/RB en braquant, à plus d’environ **44 km/h** sur la route, puis maintenir le bouton en **contre-braquant de façon dosée**. Au clavier, de brèves pressions sur la direction opposée entrecoupées de relâchements permettent de régler la glisse. La charge dépend de ce contrôle : conserver le braquage initial ou tenir indéfiniment le contre-braquage à fond ne suffit pas. Relâcher le bouton libère le turbo gagné, de **niveau I, II ou III** ; relâcher pendant le signal de lâcher parfait ajoute **0,20 seconde**. Le frein ne déclenche pas automatiquement de dérapage ; il l’annule. Contact, herbe, saut physique, panne sèche ou vitesse insuffisante effacent aussi la charge. Ces règles sont une adaptation ludique, sans équivalence démontrée dans Vroom original.

Un petit frottement entre roues peut provoquer un bond bref : corriger la poussée latérale pendant le bond préserve la vitesse à la réception. Une bonne correction conserve **100 %** de la vitesse d’arrivée, une commande neutre **97 %**, une mauvaise correction **94 %**. La réception n’ajoute jamais de vitesse ni de bonus gratuit ; un choc sévère conserve sa pénalité de casse.

La route fait 9,5 mètres de large et ses accotements sont ouverts : les glissières et la butée latérale à **±9 m** ont été supprimées. Le hors-piste augmente le roulement et réduit l'adhérence ; la voiture ralentit et peut revenir sur la route en braquant. Les contacts entre voitures sont recherchés par balayage entre deux états de simulation, avec une seule réponse par rencontre. Un petit choc retire de la vitesse ; un choc central important entraîne une casse, tandis qu'un contact extérieur rapide peut provoquer un saut au niveau des pneus. La vitesse relative et la zone du contact déterminent la réponse.

Pendant une casse ou la récupération, les commandes sont suspendues et le HUD affiche le point mort. Une roue se détache et est projetée avec une rotation visible pendant la casse. La voiture revient progressivement sur le bitume en 2,8 à 7 secondes selon son éloignement, puis repart à l’arrêt, avec une courte protection contre un nouveau choc. Le tour continue et reste invalidé pour le meilleur temps. Ces réponses visuelles, la trajectoire du débris et les règles de remise en piste sont des adaptations du prototype, pas une simulation physique complète ni une reproduction intégrale des routines originales. Les éléments solides du décor sont testés par balayage 3D ; les feuilles, l’herbe et les marquages ne constituent pas des obstacles. [docs/COLLISIONS.md](docs/COLLISIONS.md) distingue les règles retrouvées dans les sources, les adaptations et leurs tests.

## Image et son

Les options graphiques proposent cinq préréglages et des réglages séparés de résolution interne, anticrénelage, ombres, éclairage global, reflets, végétation, vent, effets, flou, plafond d’images/s et synchronisation verticale. Les choix appliqués sont conservés dans les réglages utilisateur. Les valeurs ci-dessous sont les **défauts sans préférence enregistrée**, pas des mesures de performance :

| Réglage | Mac | Profils RTX 5090 — Linux et Windows |
|---|---|---|
| Résolution interne / anticrénelage | 70 % / TAA | 100 % / TSR |
| Éclairage global et reflets | Préréglage léger, Lumen désactivé | Préréglage Ultra, Lumen activé |
| Ombres | Cascades | Ombres virtuelles demandées |
| Herbe / vent | Faible / activé | Dense / activé |
| Synchronisation / plafond | VSync / 60 images/s | VSync / 120 images/s |

Les définitions 1440 × 900 et 2560 × 1664 utilisées pour les essais Mac désignent la sortie affichée. Le rendu interne est de 70 % pour Performance et de 80 % pour Photoréalisme ; ces essais ne correspondent donc pas à un rendu natif à ces définitions.

Sur UE 5.8, **TAA correspond à `r.AntiAliasingMethod=2`** ; la valeur `3` correspond à MSAA. Les choix TAA et TSR du menu utilisent les constantes du moteur. Les préréglages Élevé et Ultra activent Lumen ; le coût dépend de la machine et de la définition. Le nouveau **Photoréalisme** (index 4) emploie **TSR à 80 %**, Lumen pour l’éclairage global et les reflets au niveau 2, ombres et végétation au niveau 3, effets au niveau 2, VSync et plafond à **30 images/s** sur Mac. Il active des ombres d’arbres limitées à **160 m**, avec une transition à partir de 120 m. Son nom décrit un objectif de rendu, pas un résultat photoréaliste déjà validé.

Sur Mac, ce profil conserve les filtres TSR Epic avec un historique à la définition de sortie (100 %), afin d’éviter le coût de l’historique suréchantillonné à 200 %. Le rendu interne reste à 80 % ; les autres profils rétablissent leur réglage habituel.

Les quatre profils précédents restent disponibles ; sortir de Photoréalisme rétablit le plafond normal de 60 images/s. La série finale valide le profil Photoréalisme à environ **30 images/s**, sur les deux définitions et les six circuits complets. Ces résultats ne certifient pas la cible de 60 images/s du profil Performance sur ce package. Les profils RTX 5090 ne disposent pas encore de mesures sur le PC pour cette version.

L’atmosphère et les teintes du décor varient avec le circuit, sans recapture permanente du ciel. L’exposition est fixe et le flou de mouvement est désactivé par défaut. En plein écran fenêtré, la définition de sortie suit celle du bureau ; une sortie 4K ne garantit pas une cadence donnée.

Le cockpit et les bras sont de vrais volumes 3D. Les gants utilisent le modèle de main gauche OpenXR de **Valve Corporation**, sous sa licence jointe : **3 028 sommets et 4 899 triangles par main**, la droite étant obtenue par symétrie. La prise des doigts est calculée une fois sur les articulations du modèle. Les deux gants sont ensuite des composants distincts, dont la prise est fixée dans le repère du volant : sa rotation déplace et oriente ensemble la jante et les mains, sans recalcul squelettique à chaque image. `M_RacingGlove` utilise deux textures de couleur et de normales ; leurs sources sont en **4K**, avec une taille maximale de **2 048 pixels dans le package Mac**. Elles apportent le grain du cuir, les renforts et les coutures. [docs/ASSETS.md](docs/ASSETS.md) précise la préparation et l'attribution.

Les mains partagent la prise et la rotation du volant pendant le braquage. La nouvelle composition vise une prise à **neuf heures et trois heures** sur un volant classique compact à trois branches, sans halo, avec les gants blancs et leurs textures existantes. Le cadrage conserve la caméra à **(−65, 0, 82) cm**, un FOV de référence de **68°**, le nez rouge, les côtés de la coque et les vraies roues avant. Le tableau de bord reste fixé au châssis, derrière le volant ; les gants suivent uniquement la rotation de celui-ci. Les manches rejoignent le centre mesuré des bracelets, avec un raccord resserré sous le cuir. Les rétroviseurs sont placés physiquement derrière ces roues et reliés à la coque par des bras rouges coudés ; leur capture arrière 3D reste active à 30/60 Hz dans le cockpit et à 10 Hz en poursuite. La vue extérieure utilise les mêmes mains, volant et rétroviseurs, sans une seconde paire de bras incorporée au pilote.

La pose retenue place le pouce autour de la jante ; David l’a approuvée sur la vue neutre. Son [audit](docs/cockpit-grip-034.json) établit **0,667 mm de séparation minimale avec la jante** dans le réglage courant. Le [contrôle ciblé pouce–index](docs/thumb-grip-034.json) ne détecte aucune traversée stricte, sans certifier toute l’anatomie ni toutes les auto-intersections possibles. Les captures [neutre](docs/images/cockpit-034-neutral.png), [gauche](docs/images/cockpit-034-left.png) et [droite](docs/images/cockpit-034-right.png) montrent cette pose dans le package validé et installé. [docs/COCKPIT_AMIGA.md](docs/COCKPIT_AMIGA.md) détaille les preuves et distingue cette direction des réglages Amiga historiques ; [docs/AMIGA_ART.md](docs/AMIGA_ART.md) conserve les dessins extraits.

Le bandeau de course est une interface **Canvas moderne, compacte et translucide**, avec contours arrondis, fond bleu nuit et accents cyan. Il conserve les informations inspirées de l’original : carte du circuit, trois voyants, Grand Prix, score, stage, tour, 27 repères de dépassement et jauge d’essence. Sa hauteur de référence est **84 pixels sur une fenêtre de 900 pixels de haut**, soit environ 9,3 %, avec une marge supérieure. Les objectifs, le classement, les alertes pneus/essence et les indications de service complètent cette information. Le grand boîtier 3D de 34 % n’est plus affiché. Les chronos et le retour de dérapage restent séparés ; les règles originales ne sont pas toutes reproduites.

Les matériaux de la scène et les primitives du HUD sont préparés sous l'écran **PRÉPARATION DU CIRCUIT**, chrono arrêté. Les lignes épaisses Canvas utilisent des quadrilatères en triangles et un état graphique statique pour éviter de recréer leurs configurations de rendu à chaque image. [PERFORMANCE.md](docs/PERFORMANCE.md) conserve le diagnostic et les mesures après correction.

Les abords réemploient quatre publicités rétro en 2048 × 512 : panneaux 8 × 2 m tous les **180 m** environ, quelques paires décalées et portiques supplémentaires sur des emplacements candidats espacés de **750 m**. Des balises trois/deux/une barres et des chevrons rouges et blancs annoncent les virages retenus. Le placement vérifie les emprises contre la piste la plus proche et les stands, et écarte les positions dangereuses ; la densité réelle dépend donc du circuit. Les pieds des panneaux suivent le terrain. Les limites sont 64 panneaux et huit portiques supplémentaires, avec les matériaux et groupes d’instances existants.

Les douze stands et les portiques de départ, d’arrivée, de stands et de freinage restent présents. Le signal rouge, orange puis vert est synchronisé avec les bips et la libération de la course. Les publicités sont des créations typographiques du prototype. Les poteaux, troncs, murs et structures ont des volumes de collision ; l’herbe, les feuillages, les peintures au sol et les images publicitaires sont exclus. [docs/COLLISIONS.md](docs/COLLISIONS.md) détaille les réponses aux chocs.

Les accotements portent des touffes d’herbe 3D à brins masqués, dont les pointes ondulent sous le vent. Le menu permet de couper l’herbe ou de choisir trois densités ; les distances de visibilité sont respectivement 30, 45 et 60 m. Les touffes, moins hautes et plus variées, disparaissent progressivement sur les 24 derniers mètres de leur portée. Elles sont instanciées et le vent est calculé par le matériau, sans déplacer les instances sur le CPU à chaque image. Les monoplaces utilisent désormais des meshes dédiés : nez effilé, pontons, cockpit ouvert, casque, suspensions, ailerons profilés et roues détaillées. `Tools/bootstrap_car_assets.py` génère **9 328 triangles par concurrent complet**, **4 744 pour la carrosserie du joueur**, **268 pour son aileron avant** et **1 144 pour son pilote extérieur** ; les douze adversaires sont répartis dans cinq groupes instanciés, dont deux groupes pour leurs 48 roues animées, avec six livrées. Chaque roue du joueur compte **1 256 triangles**, avec 40 segments et dix anneaux pour arrondir les épaules du pneu ; les supports de rétroviseurs rejoignent la coque hors du passage des pneus et le pourtour du cockpit reçoit carbone, rembourrage et fixations métalliques. Deux feuillus utilisent désormais des modèles existants, trois niveaux de détail et des textures photographiques de branches et d’écorce ; les conifères et parasols possèdent désormais des troncs et branches effilés, du feuillage ajouré et trois niveaux de détail. Leurs matériaux assurent le fondu entre niveaux et à la limite de visibilité en conservant l’opacité du feuillage proche. [docs/ENVIRONMENT_ASSETS.md](docs/ENVIRONMENT_ASSETS.md) décrit ces ressources CC0, leur import et les validations réalisées sur le package. Les terrains et horizons reçoivent des profils artistiques propres aux circuits ; les bâtiments restent stylisés. La mini-carte reste en cache et masque les adversaires en solo. Ce travail améliore les formes sans établir un rendu photoréaliste ni une copie exacte des voitures originales.

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
python3 Tools/verify_mac.py --visible --graphics-preset photorealism --profile --soak
python3 Tools/install_mac.py --validation "CHEMIN-ANNONCE-PAR-LE-VERIFICATEUR/results.json"
```

Remplacer le chemin de `results.json` par celui annoncé par le vérificateur. `--visible` affiche et active chaque scénario ; `--graphics-preset photorealism` fixe temporairement le profil Photoréalisme à 30 images/s sur Mac, sans modifier les préférences sauvegardées. Pour contrôler séparément la cible de 60 images/s, choisir `--graphics-preset performance`. La vérification contrôle cockpit, interface, voitures, menus, persistance, service au stand, dérapage et scénarios de collision. Elle mesure deux définitions et, avec `--soak`, les six circuits pendant au moins deux minutes chacun, avec des seuils de cadence et de continuité du mouvement. Les rapports restent dans `Saved/Validation`. `--editor` limite les captures au module Editor et ne constitue pas une validation installable.

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

La référence de gameplay reste **Vroom Amiga** ; la composition actuelle du cockpit suit la photo F1 fournie par David. Il reste à enregistrer des séquences de conduite reproductibles dans l'original, valider les courbes et le calibrage du relief en espace de conduite, puis comparer les commandes, temps, vitesses et trajectoires. Les règles de course, le comportement réactif des adversaires, les stands et les nouveaux décors restent des adaptations à comparer à l’original. La suite concerne notamment le calibrage de la difficulté, la fidélité des trajectoires, la présentation, les périphériques matériels et les validations Windows/NVIDIA. L'usage demandé est personnel ; aucune distribution ni vente n'est préparée.
