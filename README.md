# Vroom — premiers tours sous Unreal Engine 5.8

Prototype personnel consacré au Vroom de Lankhor de 1991 : une monoplace, six silhouettes de circuits issues des petites cartes Atari ST, une conduite jouable et un habillage 3D Unreal. L'objectif est de disposer d'une base mesurable pour retrouver progressivement le jeu d'époque. **La fidélité intégrale et le photoréalisme ne sont pas encore réalisés.**

Le prototype contient une séance libre, un départ après trois secondes, douze voitures adverses, un chronomètre par tour, un meilleur tour propre, une mini-carte, deux caméras, six rapports automatiques ou manuels et un son de moteur synthétisé. Les adversaires suivent des voies prédéfinies et adaptent leur vitesse à la courbure ; ils ne reproduisent pas l'intelligence artificielle originale.

## Jouer sur le Mac

Le projet cible **Unreal Engine 5.8, Mac Apple Silicon**. L'installation utilisée se trouve dans `/Users/Shared/Epic Games/UE_5.8`. Une compilation du code nécessite Xcode avec les outils de développement et la configuration acceptée par cette installation Unreal. Les lanceurs n'installent aucun logiciel.

Si les binaires et assets ont déjà été préparés, double-cliquer **`Tools/run_mac.command`**. Il lance le projet en mode jeu, dans une fenêtre **1440 × 900**, sans reconstruire les binaires. Entrée lance la séance ; F1 à F6 choisissent le circuit. La première ouverture peut encore compiler des shaders.

Pour préparer un clone ne contenant que les sources, fermer l'éditeur et le jeu, puis lancer **`Tools/build_mac.command`**. Il compile `VroomEditor`, puis génère onze matériaux et la carte `Circuit` à l'aide de `Tools/bootstrap_assets.py`. Le générateur conserve la carte lorsqu'elle existe déjà ; il régénère uniquement ses matériaux identifiés par métadonnées et refuse d'écraser un matériau étranger. Les matériaux et la carte dans `Content` sont versionnés avec le projet. Les binaires, `Intermediate`, `Saved` et les caches Unreal restent des résultats locaux régénérables.

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
| G | Basculer entre boîte automatique et manuelle |
| E / X | Monter / descendre d'un rapport en boîte manuelle |
| C | Basculer entre cockpit et caméra de poursuite |
| M | Activer ou désactiver le braquage à la souris |
| F10 | Alterner les champs de vision 80°, 68° et 92° |
| P | Mettre la séance en pause ou reprendre |
| R | Remettre la voiture et les adversaires au départ ; la séance courante reste dans son état de marche/pause |
| Échap | Revenir au menu |

Une manette reconnue par Unreal peut commander le volant avec son stick gauche et les pédales avec ses gâchettes droite/gauche. La détection matérielle et les éventuelles correspondances propres à chaque manette restent à essayer. Le changement de rapport, le menu et les vues utilisent les touches ci-dessus. Sur certains claviers Mac, les touches F1…F6 et F10 demandent la touche `fn`.

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

Cet ordre suit les fichiers de sources, pas le championnat original. Le JSON conserve séparément les événements longitudinaux d'origine et les coordonnées des petites cartes. Le tracé 3D est une **prévisualisation plate dérivée de ces cartes**, lissée puis mise à l'échelle pour retrouver la longueur `CLAP`. Les rayons, le rapport d'aspect des pixels, les pentes et la correspondance exacte avec les événements originaux restent non validés. Le lissage ne reconstitue pas les tables de projection historiques. Voir [docs/REFERENCE.md](docs/REFERENCE.md) pour les sources, l'extraction et les preuves.

Le moteur portable reprend les six coefficients de rapport, les seuils automatiques, une courbe de couple échantillonnée, une résistance de l'air et un freinage quadratique relevés dans le code ST. Les passages supérieurs se font vers **80, 130, 180, 230 et 270 km/h**. Le régime maximal de table est **13 312 tr/min**. La simulation du joueur tourne par pas de **1/120 s** ; un temps de rendu excessif est borné pour éviter les grands sauts.

La direction, le patinage au départ, le frein moteur, les changements de rapport et l'adhérence sont des adaptations du prototype. Le modèle portable seul atteint environ **297 km/h après dix secondes** et **325 km/h après quatre-vingt-dix secondes**, plein gaz en ligne droite. Ces nombres sont des mesures de cette version, pas des caractéristiques établies du jeu original. [docs/DRIVING.md](docs/DRIVING.md) expose les constantes, les équations et toutes les approximations.

La route fait douze mètres de large ; bordures, dégagements et barrières sont provisoires. Le hors-piste augmente le roulement et réduit l'adhérence. Une limite latérale simplifiée maintient la voiture à proximité du circuit. Les contacts avec les adversaires réduisent la vitesse au lieu de déclencher une physique de choc. Les contacts sont vérifiés à chaque image ; des cas de traversée sur une très longue image restent possibles. Il n'y a pas de collisions physiques avec tous les objets du décor.

## Image et son

Le projet demande Lumen, les ombres virtuelles et l'anticrénelage temporel TSR dans sa configuration. Le résultat réel dépend du support et du réglage de la machine. L'exposition est fixe et le flou de mouvement est désactivé. Les voitures, arbres, stands et barrières sont assemblés avec des primitives Unreal ; les matériaux sont procéduraux. Il n'y a ni voiture photogrammétrique, ni personnage fini, ni textures 4K artistiques dans cette première version.

Le lancement peut demander une sortie 4K, sans garantie de débit d'images sur cette machine :

```sh
Tools/run_mac.command -ResX=3840 -ResY=2160
```

**Aucun plugin DLSS n'est intégré et cette version Mac ne fournit pas DLSS.** Une résolution de sortie 4K ne transforme pas les primitives provisoires en assets photoréalistes. Le son est une synthèse mono à 48 kHz dont la fréquence suit le régime ; aucun échantillon sonore du jeu original n'est embarqué. Les pneus, collisions et adversaires n'ont pas encore leur propre son.

## Reconstruire et vérifier

Les scripts proposent trois modes :

```sh
Tools/build_mac.command --editor
Tools/build_mac.command --assets
Tools/build_mac.command --package
```

`--editor` compile le module Editor et prépare les assets ; c'est le défaut. `--assets` ne recompile pas le C++, mais demande un module déjà utilisable. `--package` effectue ces deux étapes puis appelle `BuildCookRun` pour compiler, cuire, préparer et archiver une application **Mac Development** dans `Saved/Packages`. `--archive /chemin` change cette destination. Un package créé est un résultat local de développement ; ces commandes n'ajoutent pas de signature ou de notarisation de distribution. La carte, les matériaux et `Content/Data/tracks.json` sont inclus par la configuration de packaging.

Tests du modèle de conduite, sans démarrer Unreal :

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -ISource/Vroom/Public Tests/drive_model_test.cpp -o /tmp/vroom-drive-tests
/tmp/vroom-drive-tests
```

Ils contrôlent accélération, freinage jusqu'à l'arrêt, rapports, symétrie du volant, sens d'un virage, hors-piste, borne de vitesse, comportement à 60/120 Hz et entrées non finies. Leur réussite vérifie la cohérence du modèle, sans constituer une comparaison avec Vroom exécuté sur ST ou Amiga.

Les options de capture et de démonstration suivantes servent aux vérifications visuelles :

```sh
Tools/run_mac.command -VroomAutoDemo -VroomChaseCamera \
  -VroomCapture=/tmp/vroom-preview.png -VroomCaptureAfter=6 -VroomExitAfter=10
```

`-VroomAutoDemo` démarre automatiquement une conduite assistée de test ; elle n'est pas un mode démonstration original ni un pilote garanti sans sortie de route. `-VroomChaseCamera` choisit la caméra arrière. La capture est demandée après le nombre de secondes de temps d'application indiqué, HUD compris ; la sortie automatique est facultative. Les options n'apportent aucune validation par elles-mêmes : vérifier l'image et les journaux produits. Sur ce Mac, les journaux de jeu sont dans `~/Library/Logs/Vroom`. Le script de génération écrit `Saved/Logs/VroomBootstrap.log`.

## Suite du travail

Il reste à fixer l'édition ST ou Amiga de référence, enregistrer des séquences de conduite reproductibles dans l'original, décoder les courbes et reliefs en espace de conduite, puis comparer les commandes, temps, vitesses et trajectoires. Viendront ensuite une voiture et un cockpit détaillés, des décors crédibles, le son, les règles de course, pneus/carburant/moteur, les stands et la véritable intelligence artificielle. L'usage demandé est personnel ; aucune distribution ni vente n'est préparée.
