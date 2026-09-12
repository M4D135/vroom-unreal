# Vroom 0.22 — prototype 3D sous Unreal Engine 5.8

## Rôle et état — 8 septembre 2026

Prototype personnel de course sous Unreal Engine, inspiré du Vroom de Lankhor. Les sources de conduite, les six tracés et les outils de build Mac sont conservés pour poursuivre le développement. Les fonctionnalités et limites ci-dessous concernent la version de cette branche ; des travaux locaux plus récents peuvent exister sans avoir encore été poussés.

Dépôt : [vroom-unreal](https://git.estrasse.fr/david/vroom-unreal).

Prototype personnel consacré au Vroom de Lankhor de 1991 : une monoplace, six silhouettes de circuits issues des petites cartes Atari ST, une conduite jouable et un habillage 3D Unreal. L'objectif est de disposer d'une base mesurable pour retrouver progressivement le jeu d'époque. **La fidélité intégrale et le photoréalisme ne sont pas encore réalisés.**

Le prototype contient une séance libre, un départ après trois secondes, douze voitures adverses, un chronomètre par tour, un meilleur tour propre, une mini-carte, deux caméras et six rapports automatiques ou manuels. La vue cockpit montre un volant, des avant-bras en 3D et des gants issus du maillage Valve, avec cuir et coutures texturés. Les rétroviseurs montrent la piste derrière la voiture. Les contacts entre voitures peuvent produire un petit choc, une casse ou un saut, avec projection d'une roue pendant la casse et remise en piste après récupération. Le moteur, les crissements et les impacts utilisent la banque sonore Amiga extraite, avec une synthèse de secours si cette banque manque. Les adversaires suivent des voies prédéfinies et adaptent leur vitesse à la courbure ; ils ne reproduisent pas l'intelligence artificielle originale.

## Jouer sur le Mac

La version compilée et testée ici cible **Unreal Engine 5.8, Mac Apple Silicon**. Des profils **RTX 5090 sous Linux/Vulkan et Windows/DirectX 12** sont également préparés ; leurs builds et leur rendu sur le PC restent à vérifier. Voir [docs/RTX5090.md](docs/RTX5090.md). L'installation Mac utilisée se trouve dans `/Users/Shared/Epic Games/UE_5.8`. Une compilation du code nécessite Xcode avec les outils de développement et la configuration acceptée par cette installation Unreal. Les lanceurs n'installent aucun logiciel.

Pour jouer au package préparé, double-cliquer **`Desktop/Vroom.command`** ou **`Tools/play_latest.command`**. Le lanceur choisit la dernière application compilée et applique le contournement du suivi mémoire UE 5.8 décrit dans [VALIDATION.md](docs/VALIDATION.md). Entrée lance la séance ; F1 à F6 choisissent le circuit.

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

| Commande | Action |
|---|---|
| Entrée | Repartir sur le circuit sélectionné, avec décompte de trois secondes |
| F1…F6 | Choisir Autriche, France, USA, Suède, Japon ou Royaume-Uni ; revenir au menu |
| Flèche haut, Z ou W | Accélérer |
| Flèche bas, S ou Espace | Freiner |
| Flèches gauche/droite, Q/D ou A/D | Braquer |
| Maj gauche/droite + braquage | Maintenir un dérapage à vitesse suffisante ; relâcher Maj sur la route pour déclencher le bonus chargé |
| G | Basculer entre boîte automatique et manuelle |
| E / X | Monter / descendre d'un rapport en boîte manuelle |
| C | Basculer entre cockpit et caméra de poursuite |
| M | Activer ou désactiver le braquage à la souris |
| F10 | Alterner les champs de vision 80°, 68° et 92° |
| P | Mettre la séance en pause ou reprendre |
| O ou F7 | Ouvrir les options graphiques ; la course est suspendue pendant le réglage |
| T / F près du départ, à faible vitesse | Changer les pneus / refaire le plein |
| R | Remettre la voiture et les adversaires au départ ; la séance courante reste dans son état de marche/pause |
| Échap | Revenir au menu |

Une manette reconnue par Unreal peut commander le volant avec son stick gauche et les pédales avec ses gâchettes droite/gauche. La détection matérielle et les éventuelles correspondances propres à chaque manette restent à essayer. Les boutons d’épaule gauche ou droite maintiennent le dérapage. Le changement de rapport, le menu et les vues utilisent les touches ci-dessus. Sur certains claviers Mac, les touches de fonction demandent la touche `fn`.

Dans les options, les flèches haut/bas sélectionnent une ligne et gauche/droite changent sa valeur. **Entrée applique et enregistre ; Échap annule les modifications.** Le bouton **Options graphiques** du menu principal et les commandes du panneau disposent aussi de zones cliquables ; leur validation manuelle à la souris reste à compléter. La course retrouve son état de pause précédent à la fermeture.

La souris agit sur une position de volant conservée : revenir avec la souris recentre le volant ; désactiver puis réactiver M le remet à zéro. Ce mode ne reprend pas encore le contrôle complet accélération/freinage de la souris originale. Le meilleur temps est conservé pour le circuit pendant cette session et réinitialisé lorsque le circuit change. Il n'est pas enregistré sur disque. Sortir de la largeur de route ou toucher un adversaire invalide le tour pour le meilleur temps, tout en laissant le chronomètre continuer.

## Circuits et conduite

| Touche | Circuit du fichier ST | Longueur `CLAP` originale |
|---|---|---:|
| F1 | Autriche | 5 911 m |
| F2 | France | 3 813 m |
| F3 | USA | 6 405 m |
| F4 | Suède | 4 018 m |
| F5 | Japon | 4 359 m |
| F6 | Royaume-Uni | 4 778 m |

Cet ordre suit les fichiers de sources, pas le championnat original. Le JSON conserve séparément les événements longitudinaux d'origine et les coordonnées des petites cartes. Le tracé en plan est une **prévisualisation dérivée de ces cartes**, lissée puis mise à l'échelle pour retrouver la longueur `CLAP`. Les rayons et le rapport d'aspect des pixels restent à valider ; ce lissage ne reconstitue pas les tables de projection historiques. Voir [docs/REFERENCE.md](docs/REFERENCE.md) pour les sources, l'extraction et les preuves.

L'Autriche possède maintenant un relief issu de ses **47 événements de pente** dans les sources ST. Le profil est continu en hauteur et en pente, y compris au raccord du tour. Son calibrage métrique et sa correction de fermeture sont des adaptations : l'écart entre son point bas et son point haut atteint environ **25,90 m**, sans constituer un relevé du vrai circuit ni un dénivelé cumulé. Les cinq autres circuits restent plats, car aucun événement de relief explicite n'apparaît dans leurs fichiers de cette archive. [docs/ELEVATION.md](docs/ELEVATION.md) détaille l'intégration, les unités retrouvées et les choix de calibrage.

Le moteur portable reprend les six coefficients de rapport, les seuils automatiques, une courbe de couple échantillonnée, une résistance de l'air et un freinage quadratique relevés dans le code ST. Les passages supérieurs se font vers **80, 130, 180, 230 et 270 km/h**. Le régime maximal de table est **13 312 tr/min**. Le joueur, les adversaires, les contacts et le chronomètre avancent ensemble par pas de **1/120 s**. Le rendu interpole les positions précédentes et courantes du joueur et des adversaires entre ces pas. Le temps accumulé pendant une image excessive est borné pour éviter les grands sauts ; dans ce cas, le temps de simulation peut avancer moins vite que le temps réel.

La conduite reprend le déplacement latéral arcade : le volant agit sur le déport et la courbe entraîne une dérive à compenser. La caméra suit la tangente lissée de la route avec une inclinaison du châssis limitée. Les filtres de commande avancent au pas physique ; la direction et la hauteur des petits sauts sont interpolées pour l’affichage. Les calibrages restent propres au prototype. [docs/DRIVING.md](docs/DRIVING.md) détaille les équations et les limites.

Pour déraper, braquer en maintenant **Maj** ou un bouton d’épaule de manette ; frein + braquage permet aussi l’entrée en glisse. Il faut rouler à plus d’environ **44 km/h** sur la route. Un relâchement volontaire après une glisse suffisamment longue donne une courte poussée, avec deux niveaux de charge. Sortie de route, saut physique ou ralentissement annulent la charge ; le frein coupe la poussée. Ce dérapage avec bonus est une adaptation ludique, sans équivalence démontrée dans Vroom original.

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

Sur UE 5.8, **TAA correspond à `r.AntiAliasingMethod=2`** ; la valeur `3` correspond à MSAA. Les choix TAA et TSR du menu utilisent les constantes du moteur. Les préréglages Élevé et Ultra activent Lumen ; le coût dépend de la machine et de la définition. Ces plafonds restent des cibles. Les profils RTX 5090 ne disposent pas encore de mesures sur le PC pour cette version.

Le ciel fixe est capturé sans recapture permanente. L’exposition est fixe et le flou de mouvement est désactivé par défaut. En plein écran fenêtré, la définition de sortie suit celle du bureau ; une sortie 4K ne garantit pas une cadence donnée.

Le cockpit et les bras sont de vrais volumes 3D. Les gants utilisent le modèle de main gauche OpenXR de **Valve Corporation**, sous sa licence jointe : **3 028 sommets et 4 899 triangles par main**, la droite étant obtenue par symétrie. La prise des doigts est calculée une fois sur les articulations du modèle. Les deux gants sont ensuite des composants distincts, dont la prise est fixée dans le repère du volant : sa rotation déplace et oriente ensemble la jante et les mains, sans recalcul squelettique à chaque image. `M_RacingGlove` utilise deux textures de couleur et de normales ; leurs sources sont en **4K**, avec une taille maximale de **2 048 pixels dans le package Mac**. Elles apportent le grain du cuir, les renforts et les coutures. [docs/ASSETS.md](docs/ASSETS.md) précise la préparation et l'attribution.

Les mains tiennent la jante pendant le braquage, avec les dos des gants orientés vers le pilote et les pouces à l’intérieur. Le cadrage conserve le volant bas de la référence Amiga : **la main gauche monte dans un virage à droite, et la droite dans un virage à gauche**, tandis que l’autre peut passer sous le cadre. Les manches suivent les poignets. Les sept dessins Amiga guident la composition ; ils ne pilotent plus le placement des mains par interpolation de coordonnées d’écran. Cette prise 3D constitue une adaptation, sans correspondance pixel par pixel revendiquée. Les onze composants du cockpit et les rétroviseurs partagent le clignotement après remise en piste. [docs/COCKPIT_AMIGA.md](docs/COCKPIT_AMIGA.md) décrit ce placement et ses limites de validation ; [docs/AMIGA_ART.md](docs/AMIGA_ART.md) documente les dessins extraits.

Le bandeau de course est une interface **Canvas moderne, compacte et translucide**, avec contours arrondis, fond bleu nuit et accents cyan. Il conserve les informations inspirées de l’original : carte du circuit, trois voyants, Grand Prix, score, stage, tour, 27 repères de dépassement et jauge d’essence. Sa hauteur de référence est **112 pixels sur une fenêtre de 900 pixels de haut**, soit environ 12,4 %, avec une marge supérieure. Le grand boîtier 3D de 34 % n’est plus affiché. Les chronos et le retour de dérapage restent séparés ; les règles originales ne sont pas toutes reproduites.

Les abords comprennent quatre publicités rétro en 2048 × 512, des panneaux séparés sur poteaux et jambes de force, douze stands détaillés et trois portiques métalliques. Le portique de signalisation se trouve devant la grille ; ses cinq colonnes de feux s'allument progressivement pendant le décompte et s'éteignent au départ. La ligne chronométrée conserve son origine. Les publicités sont des créations typographiques du prototype. Les poteaux, troncs, murs et structures ont des volumes de collision ; l’herbe, les feuillages, les peintures au sol et les images publicitaires sont exclus. Une sortie de piste ralentit progressivement. Un choc contre le décor peut casser les pneus et impose un retour lent sur le bitume, commandes bloquées, puis une reprise à l’arrêt. Les règles et leurs adaptations sont décrites dans [docs/COLLISIONS.md](docs/COLLISIONS.md).

Les accotements portent des touffes d’herbe 3D à brins masqués, dont les pointes ondulent sous le vent. Le menu permet de couper l’herbe ou de choisir trois densités ; les distances de visibilité sont respectivement 30, 45 et 60 m. Les touffes sont instanciées et le vent est calculé par le matériau, sans déplacer les instances sur le CPU à chaque image. Les monoplaces utilisent désormais des meshes dédiés : nez effilé, pontons, cockpit ouvert, casque, suspensions, ailerons profilés et roues détaillées. `Tools/bootstrap_car_assets.py` génère **3 182 triangles par concurrent** ; les douze adversaires restent répartis dans quatre groupes instanciés, avec six livrées. Les arbres et bâtiments restent largement fondés sur des primitives, et la mini-carte reste en cache. Ce travail améliore les formes sans établir un rendu photoréaliste ni une copie exacte des voitures originales.

[docs/PERFORMANCE.md](docs/PERFORMANCE.md) et [docs/VALIDATION.md](docs/VALIDATION.md) consignent les mesures Mac datées, le build testé et leurs limites. Les anciennes mesures ne valident pas automatiquement un nouveau package ou un autre réglage graphique.

Le lancement peut demander une sortie 4K, sans garantie de débit d'images sur cette machine :

```sh
Tools/run_mac.command -ResX=3840 -ResY=2160
```

Le menu prend en charge **NVIDIA DLSS 4.5 optionnel sur Windows**, avec le plugin compatible UE 5.8 : il détecte sa disponibilité et n’active que les modes pris en charge (DLAA, Qualité, Équilibré, Performance). Sans plugin utilisable, le rendu natif reste disponible. **DLSS n’est pas disponible sur ce Mac**, et aucune génération d’images ni DLSS 5 n’est activée. Les profils NVIDIA et leur installation sont détaillés dans [docs/RTX5090.md](docs/RTX5090.md).

Le son utilise la banque `Content/Data/amiga_sound.bin` : moteur rééchantillonné selon le régime, crissement en glisse et impacts, mélangés en mono à 48 kHz. Une synthèse de secours prend le relais si la banque manque. Les moteurs adverses n’ont pas encore de son distinct.


## Reconstruire et vérifier

Les scripts proposent trois modes :

```sh
Tools/build_mac.command --editor
Tools/build_mac.command --assets
Tools/build_mac.command --package
```

`--editor` compile le module Editor et prépare les assets ; c'est le défaut. `--assets` ne recompile pas le C++, mais demande un module déjà utilisable. `--package` effectue ces deux étapes puis appelle `BuildCookRun` pour compiler, cuire, préparer et archiver une application **Mac Development** dans `Saved/Packages`. `--archive /chemin` change cette destination. Un package créé est un résultat local de développement ; ces commandes n'ajoutent pas de signature ou de notarisation de distribution. La carte, les matériaux, le mesh des touffes d’herbe et les données sous `Content/Data` sont inclus par la configuration de packaging.

Tests portables (six suites C++17 strictes, 17 tests Python et audit des monoplaces), puis vérification du package Mac :

```sh
python3 Tools/test_portable.py
python3 Tools/verify_mac.py --profile
```

La seconde commande contrôle les poses du cockpit, l’interface, les voitures et quatre scénarios de collision, puis mesure deux parcours affichés et leur cadence. Les sorties sont conservées dans `Saved/Validation`. `--editor` limite la vérification aux poses du cockpit dans le module Editor déjà compilé.

La suite de conduite contrôle accélération, freinage jusqu'à l'arrêt, rapports, symétrie du volant, sens d'un virage, hors-piste, borne de vitesse, comportement à 60/120 Hz et entrées non finies. La suite de contacts contrôle notamment les seuils de vitesse relative, la réponse unique, le balayage entre états, les petits chocs, la casse centrale, le saut, l'atterrissage, la remise en piste et la protection temporaire. Leur réussite vérifie la cohérence des modèles, sans constituer une comparaison avec Vroom exécuté sur ST ou Amiga.

Les scénarios Unreal reproductibles `-VroomCollisionTest=slow`, `crash` et `tyre`, leurs compteurs attendus et leurs commandes de capture sont décrits dans [docs/COLLISIONS.md](docs/COLLISIONS.md). Ils vérifient les événements de contact et de retour ; l'apparence et la fluidité du rendu demandent une observation séparée.

Les options de capture et de démonstration suivantes servent aux vérifications visuelles :

```sh
Tools/run_mac.command -VroomAutoDemo -VroomChaseCamera \
  -VroomCapture=/tmp/vroom-preview.png -VroomCaptureAfter=6 -VroomExitAfter=10
```

`-VroomAutoDemo` démarre automatiquement une conduite assistée de test ; elle n'est pas un mode démonstration original ni un pilote garanti sans sortie de route. `-VroomChaseCamera` choisit la caméra arrière. La capture est demandée après le nombre de secondes de temps d'application indiqué, HUD compris ; la sortie automatique est facultative. Les options n'apportent aucune validation par elles-mêmes : vérifier l'image et les journaux produits. Sur ce Mac, les journaux de jeu sont dans `~/Library/Logs/Vroom`. Le script de génération écrit `Saved/Logs/VroomBootstrap.log`.

## Suite du travail

L'édition de référence choisie est **Vroom Amiga**. Il reste à enregistrer des séquences de conduite reproductibles dans l'original, valider les courbes et le calibrage du relief en espace de conduite, puis comparer les commandes, temps, vitesses et trajectoires. La suite comprend l'affinement de la voiture et du cockpit, des décors crédibles, le son, les règles de course, pneus/carburant/moteur, les stands et la véritable intelligence artificielle. L'usage demandé est personnel ; aucune distribution ni vente n'est préparée.
