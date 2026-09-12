# Collisions : règles retrouvées et intégration du prototype

Analyse originale du 5 septembre 2026, intégration mise à jour le 8 septembre. Les sources étudiées sont l'archive de développement Atari ST de juillet 1991 déjà décrite dans [REFERENCE.md](REFERENCE.md). La concordance de cet état avec une édition commerciale ST ou Amiga précise reste à démontrer. Les [observations vidéo et témoignages](COLLISION_REFERENCES.md) constituent une source distincte : les vidéos examinées montrent des accidents contre des objets, sans valider un seuil numérique ni un contact entre pneus de deux voitures.

L'implémentation est répartie entre `Source/Vroom/Public/VroomContactModel.h`, portable et indépendant d'Unreal, son intégration dans `VroomCar.cpp` et les volumes de décor de `VroomCircuit.cpp`. Les tests autonomes sont dans `Tests/contact_model_test.cpp`.

## Décision de collision avec une autre voiture dans le code ST

Le passage déterminant est `vr2.asm`, `TSCRHC6` à `TSCRHC9A`, lignes 1758–1787. Il compare la **différence de vitesse entre le joueur et la voiture devant**, puis la position du contact. La conversion de vitesse affichée se trouve ligne 2112 : `km/h = SP1D × 2/3`.

| Situation | Décision originale | Localisation |
|---|---|---|
| Écart inférieur à 105 unités internes, soit 70 km/h | Petit choc `CRASHCI`, indépendamment de la zone des pneus | `vr2.asm`, lignes 1758–1764 |
| Écart supérieur ou égal à 70 km/h et impact central | Gros choc `CRASHCJ` | `vr2.asm`, lignes 1767–1786 |
| Écart supérieur ou égal à 70 km/h et contact extérieur | Saut `JUMPJ` si point `D4 >= 40` à droite ou `D4 < -40` à gauche | `vr2.asm`, lignes 1780–1787 |

L'ordre est essentiel : un contact extérieur à faible vitesse relative reste un petit choc. Une voiture roulant à 216 km/h qui en touche une à 180 km/h n'a que 36 km/h d'écart ; ce cas n'est pas un gros choc selon cette branche. Inversement, 90 km/h contre une voiture arrêtée dépasse le seuil.

Le point `D4` est construit à partir du recouvrement horizontal des voitures projetées, dont la largeur de référence est 132 unités, avec des marges de trois unités (`TSCRHC3` à `TSCRHC5`, lignes 1740–1756). **Le seuil 40 n'est ni une distance en centimètres, ni une mesure physique directement réutilisable.** La détection longitudinale emploie la position avant `FRTCRH=64`, ligne 3259. Le prototype ne présente pas sa boîte de contact 3D comme la traduction exacte de ces coordonnées.

## Réponses originales

| Branche | Effet établi dans le code |
|---|---|
| Petit choc, `CRASHCI`, lignes 3065–3077 | Retire 75 unités à la vitesse du joueur, soit **50 km/h**, avec plancher à zéro ; retire 10 unités d'état des pneus ; prépare un bref affichage de choc. |
| Gros choc sur voiture, `CRASHCJ`, lignes 3099–3120 | Place la vitesse à **celle de la voiture devant moins 50 km/h**, avec plancher à zéro ; retire 100 unités d'état des pneus ; ramène le régime à 1000, la boîte au point mort et les commandes d'accélération/souris à zéro. |
| Animation du gros choc, `CRASHCA`, lignes 3122–3151 | Affiche la séquence pendant 16 cycles, puis attend la fin de la réponse de l'autre routine ; au besoin retire encore 30 unités de vitesse, soit 20 km/h, pendant cette attente. |
| Saut sur voiture, `JUMPJ`, lignes 3166–3182 | Double l'écart de vitesse pour obtenir l'impulsion du saut, retire 10 unités d'état des pneus et prépare les deux états d'animation. Cette branche n'applique pas la perte immédiate de 50 km/h du petit choc. |
| Début du saut, `sub6.asm`, lignes 1038–1055 | Borne l'impulsion à **120…400**. |
| Atterrissage, `sub6.asm`, lignes 1089–1101 | État 3 pour un atterrissage sans pneus cassés ; état 4 si l'impulsion initiale est **supérieure ou égale à 250**. |
| Pneus cassés après le saut, `JUMPA`, lignes 3185–3226 | Retire 90 unités supplémentaires, verrouille des commandes, attend l'arrêt par frottement, puis rétablit les pneus après un compteur comparé à **40 cycles**. Le commentaire « 50 Cycles » contredit l'instruction `CMPI #40` ; la valeur exécutée est 40. |

Avec le doublement d'écart de `JUMPJ`, l'impulsion 250 correspond à un écart de **83⅓ km/h**. Entre 70 et 83⅓ km/h, un contact extérieur peut donc donner un saut sans casse des pneus selon ces routines. Cela reste une déduction de cet état de sources, à vérifier sur le jeu exécuté.

Deux décalages de l'écart dans `CRASHCJ` sont annotés `*** BUG ***` par les auteurs ; le prototype ne reproduit pas cette multiplication par quatre pour sa réponse visuelle. Il conserve en revanche le doublement employé dans le calcul d'impulsion de saut, lui aussi annoté, afin de garder la relation observable entre les seuils des deux routines.

La source peut ignorer un nouvel impact lorsque la voiture est déjà suffisamment haut (`htprl < -188`). Elle peut aussi transformer un second contact sur un saut encore bas en accident. Le prototype ignore les nouveaux contacts entre voitures pendant toute sa phase aérienne ; les objets solides du décor restent détectés à la hauteur du véhicule.

## Les chocs contre des objets sont une autre branche

`TSCRHV15` choisit un gros accident contre un objet au-dessus de 90 unités, soit **60 km/h de vitesse absolue** ; en dessous ou à ce seuil, c'est le petit accident sur objet. Pour un objet marqué comme saut, `TSCRHV17` demande plus de 180 unités, soit **120 km/h**, sinon il repart vers la branche d'accident. Ces seuils ne doivent pas être mélangés au seuil relatif de 70 km/h entre voitures.

`CRASHI`/`CRASHJ` mettent la vitesse à zéro et verrouillent la réponse. `CRASHA` peut afficher une roue détachée, puis `CRASHF`/`SHIFTA` ramènent la voiture sur la route avant de rendre les commandes (`vr2.asm`, lignes 2962–3058). `SHIFTA` attend que `topvib` ne signale plus l’herbe ou le vibreur ; il ne demande pas nécessairement le centre exact. Ce retour latéral est cohérent avec les incidents contre panneaux et arbres vus dans les vidéos. **Il ne prouve pas que la branche de choc contre une voiture déclenche exactement le même recentrage.** Le prototype applique désormais sa récupération progressive aux gros chocs entre voitures, aux accidents contre le décor et aux atterrissages avec casse : c’est une adaptation de gameplay explicite.

Les objets du prototype suivent la branche d’accident avec arrêt et récupération. Les objets historiques spécialement marqués pour déclencher un saut à plus de 120 km/h ne sont pas encore reproduits comme tels.

## Règles explicites du prototype

Les paramètres source conservés sont le seuil relatif de 70 km/h, la petite perte de 50 km/h, la priorité petit choc → contact central ou pneu, l'impulsion de saut bornée et son seuil de casse à l'atterrissage. Contre le décor, les pneus cassent strictement au-dessus de 60 km/h ; à 60 km/h exactement, le petit accident reste applicable.

Les éléments suivants sont des adaptations :

- **Contacts entre voitures.** Un balayage du déplacement relatif entre les deux derniers états teste simultanément la bande longitudinale `±4,1 m` et latérale `±1,95 m`. La zone extérieure commence à `|déport relatif| >= 1,15 m`. Ces dimensions sont des réglages du modèle ; elles remplacent la projection des sprites et ne tiennent pas compte de la rotation exacte des carrosseries ou des roues. Le gros choc conserve une vitesse égale à celle du concurrent moins **32,5 km/h**, avec plancher à zéro, au lieu des 50 km/h de la source. Les contacts ajoutent aussi une impulsion latérale.
- **Une réponse par rencontre.** Jusqu'à 32 paires sont mémorisées. Une paire se réarme après une séparation supérieure à la boîte de contact plus 0,25 m. Cela empêche une perte de 50 km/h à chaque image. Le segment balayé détecte aussi une voiture entièrement traversée entre deux états.
- **Périmètre entre voitures.** Le joueur doit rattraper un concurrent ; les contacts initiés par une voiture arrivant derrière et les contacts latéraux à vitesse strictement égale ne sont pas une simulation complète. Un très petit écart inférieur ou égal à 0,05 m/s est ignoré.
- **Décor solide.** Des boîtes orientées sont dérivées des instances de poteaux, troncs, murs de stands et structures. Une grille de cellules de 40 m sélectionne les volumes proches. Le véhicule, approximé par une boîte de **4,4 × 2,4 × 1,2 m**, est balayé en trois dimensions dans leur repère, en tenant compte de son orientation et de sa hauteur. La détection porte sur des volumes englobants, pas sur chaque triangle. Les feuilles, herbes, surfaces au sol, peintures et images des publicités sont exclues ; les supports des panneaux restent solides. La dimension verticale permet de passer sous une traverse haute et de heurter un objet pendant un saut. L’impact place la voiture au point de contact et arrête sa vitesse. Le seuil de casse utilise la vitesse du déplacement 3D pendant le pas, qui peut différer du compteur pendant une dérive ou un saut.
- **Sortie de piste.** Le bitume mesure 9,5 m ; l’herbe freine progressivement et réduit la direction. Sortir seul de la piste ne déclenche pas une casse invisible. L’ancienne limite latérale et l’accident forcé à `HalfWidthM + 14` ont été supprimés ; le décor réellement rencontré déclenche l’accident.
- **Saut 3D.** Le contact extérieur entre voitures produit un arc parabolique allant jusqu’à 2,8 m. Ses paramètres de durée vont de 0,75 à 1,30 s selon l’impulsion. Tangage et roulis sont calculés analytiquement. Les commandes restent disponibles en l’air, avec une autorité latérale réduite ; le modèle routier utilise un frottement aérien faible. Les petits bonds sur vibreurs et les sauts sur crêtes sont d’autres adaptations. Il ne s’agit pas d’un port complet de `SPEDC10`/`SPEDC30`.

Les phases de casse bloquent les commandes avant le retour :

| Déclencheur | Durée de `Crashed` |
|---|---:|
| Gros choc central entre voitures | 0,80 s |
| Objet heurté au-dessus de 60 km/h | 1,05 s |
| Objet heurté à 60 km/h ou moins | 0,18 s |
| Atterrissage avec pneus cassés | 1,20 s |

La glissade après un choc entre voitures ralentit à 45 m/s² pendant cette phase. Au début de `Recovering`, la vitesse devient nulle et la position latérale atteinte sert de départ au retour. L’animation de casse peut projeter une roue ; une casse en l’air conserve d’abord sa hauteur puis redescend. Ces effets ne constituent pas une reproduction physique des débris originaux.

**Le retour est progressif et pénalise le temps de course.** La cible est `clamp(déport, -0.45 × demi-largeur, +0.45 × demi-largeur)`, soit au maximum ±2,1375 m du centre pour la piste actuelle. Un véhicule déjà dans cette bande garde son déport. La durée vaut `2,8 + 0,09 × distance latérale à parcourir` secondes, bornée à 7 s. Une interpolation quintique donne une vitesse et une accélération nulles au départ et à l’arrivée ; l’orientation revient progressivement dans l’axe. Les commandes restent bloquées et le déplacement longitudinal est arrêté, tandis que le chronomètre et les concurrents continuent.

La fin de récupération rend les commandes une seule fois, **à 0 km/h**, sans téléportation latérale ni accélération offerte. Elle conserve la distance de course, le carburant et l’usure des pneus ; le tour reste invalidé pour le meilleur temps. La protection avec clignotement dure ensuite **1,6 s**. Les durées, la cible intérieure et l’application de cette pénalité aux gros contacts entre voitures sont des choix du prototype, sans revendication de reproduction complète de la remise en piste historique.

## Intégration et cohérence temporelle

Le joueur, les adversaires, les collisions et le chronomètre avancent dans le même pas fixe de **1/120 s**. Les positions précédentes et courantes servent à la fois au balayage des contacts et à l'interpolation du rendu. Les écarts longitudinaux sont remis dans le voisinage du joueur sur le circuit fermé ; leur valeur précédente est reconstruite à partir des déplacements respectifs pendant le pas.

Les animations du modèle dépendent du temps écoulé et exposent leur durée et progression. Une longue image ne doit pas provoquer une répétition de contacts ni une accélération des animations. L'intégration borne toutefois le temps accumulé par image : un test de six secondes simulées peut durer davantage en temps réel si la machine produit de longues images. La pause suspend les pas de jeu.

La validation des compteurs de contacts ne suffit pas à mesurer la fluidité du rendu, l'apparence de la casse ou la fidélité aux trajectoires originales. Ces points demandent des observations et mesures séparées.

## Tests portables

Depuis la racine du projet, sans ouvrir Unreal :

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -ISource/Vroom/Public Tests/contact_model_test.cpp -o /tmp/vroom-contact-tests
/tmp/vroom-contact-tests

clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -ISource/Vroom/Public Tests/drive_model_test.cpp -o /tmp/vroom-drive-tests
/tmp/vroom-drive-tests
```

Les tests de contacts vérifient notamment : seuil exact, vitesse relative plutôt qu'absolue, perte limitée à une fois, réarmement après séparation, priorité du petit choc même sur un pneu, gros choc central, saut propre, atterrissage avec casse et fin de récupération unique. Ils couvrent aussi le seuil strict de 60 km/h contre un objet, la conservation de hauteur lors d’un choc aérien, le balayage d’un obstacle étroit, le passage sous une traverse et l’absence de faux contact diagonal. Le retour est contrôlé pour sa monotonie, l’absence de dépassement de cible et ses départ/arrivée doux ; sa position et sa protection concordent à 30, 60 et 120 Hz. La suite de contacts a été compilée avec les options strictes ci-dessus et exécutée avec succès lors de la livraison du modèle du 8 septembre. Cela valide le modèle portable, pas les volumes générés ou leur rendu dans Unreal.

## Scénarios reproductibles dans Unreal

Avec le module courant déjà compilé et les assets présents, lancer séparément les quatre scénarios. Les scripts de lancement ne reconstruisent pas les binaires : un ancien module ne valide pas le nouveau code.

```sh
Tools/run_mac.command -VroomCollisionTest=slow -VroomChaseCamera -VroomExitAfter=8
Tools/run_mac.command -VroomCollisionTest=crash -VroomChaseCamera -VroomExitAfter=8
Tools/run_mac.command -VroomCollisionTest=tyre -VroomChaseCamera -VroomExitAfter=8
Tools/run_mac.command -VroomCollisionTest=scenery -VroomChaseCamera -VroomExitAfter=8
```

Les trois premiers préparent automatiquement une rencontre avec le concurrent 0 à proximité de la position 100 m. Le quatrième vise un pilier réel du portique de départ. Ils n'exigent aucune commande de conduite ; les autres voitures sont éloignées du point d'essai. Pour ces scénarios, **`VroomExitAfter=8` utilise huit secondes de simulation**, et non huit secondes de temps mural.

| Scénario | Conditions initiales joueur / concurrent | Attendu dans les journaux |
|---|---|---|
| `slow` | 20 / 15 m/s, soit 72 / 54 km/h ; contact central | Un contact `kind=1`, perte de vitesse, aucune remise en piste ; `contacts=1 respawns=0`. |
| `crash` | 80 / 40 m/s, soit 288 / 144 km/h ; contact central | Un contact `kind=2`, vitesse restante proche de 111,5 km/h, glissade puis récupération à l’arrêt ; `contacts=1 respawns=1`. |
| `tyre` | 55 / 10 m/s, soit 198 / 36 km/h ; concurrent décalé de 1,55 m à droite | Un contact `kind=3`, arc de saut puis atterrissage avec casse et récupération ; `contacts=1 respawns=1`. |
| `scenery` | Joueur à 30 m/s, soit 108 km/h ; départ à s=150 m, déport +8,5 m, près du pilier à s=160 m / +9 m | Un accident contre le décor, puis retour à l’intérieur de la piste à vitesse nulle ; `contacts=1 respawns=1`. |

Pour les scénarios entre voitures, le concurrent commence 4,4 m devant le joueur ; les vitesses évoluent légèrement avant le contact. Les 111,5 km/h du cas `crash` sont donc une valeur attendue approximative, pas une assertion d'égalité. Observer les positions et les lignes `Recovery started s=... lateral=... duration=...` puis `Control restored s=... lateral=...` pour vérifier la glissade et le trajet de retour. Le scénario `scenery` vérifie aussi que les commandes sont rendues, que la voiture se trouve sur le bitume et que sa vitesse reste inférieure à 0,1 m/s ; les compteurs ne suffisent pas à contrôler toute la trajectoire.

La vérification intégrée termine le processus avec un code zéro en cas de succès et un code un en cas d'échec. La ligne à rechercher dans `Saved/Logs/Vroom.log` est `VROOM: Verification PASS scenario=...`, ou `FAIL` si les conditions ne sont pas satisfaites. Ne pas se contenter du simple démarrage de l'application.

Pour demander une image pendant le saut :

```sh
Tools/run_mac.command -VroomCollisionTest=tyre -VroomChaseCamera \
  -VroomCapture=/tmp/vroom-tyre-jump.png -VroomCaptureAfter=0.55 -VroomExitAfter=8
```

Dans les scénarios de collision, `VroomCaptureAfter` emploie lui aussi le temps de simulation. Examiner la capture pour confirmer que la voiture est visible au-dessus du concurrent, puis une autre capture plus tard pour l'atterrissage ou la remise en piste. Les assertions ne vérifient pas à elles seules les pixels, la hauteur visible, la fluidité ou la conformité au jeu original. Aucun nouveau résultat Unreal n’est revendiqué par cette mise à jour documentaire ; les mesures et captures datées sont consignées séparément dans [VALIDATION.md](VALIDATION.md).
