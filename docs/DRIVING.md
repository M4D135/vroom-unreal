# Modèle de conduite du prototype

Ce modèle portable C++17 porte la conduite des six circuits dans Unreal. Il reprend des paramètres mesurables du code Atari ST de Vroom de 1991, mais **ce n'est pas un port intégral du moteur original**. Les distances sont en mètres, les vitesses en m/s et le temps en secondes. `Steer > 0`, `HeadingRad > 0` et `LateralM > 0` vont vers la droite, soit le côté +Y du repère local Unreal. La courbure est positive dans un virage à droite.

## Éléments vérifiés dans les sources

Fichiers consultés : `motor.asm` (titre « MOTOR TABLES 20 Cycles - V15 - 22 Jun 91 ») et `vr2.asm` (« Game Play - V15 - 26 Jul 91 »), archive téléchargée pour cette étude. La routine moteur est dans `vr2.asm`, malgré le nom voisin `vr1.asm`.

| Élément | Valeur ou formule observée | Localisation originale |
|---|---|---|
| Six rapports, coefficient vitesse → régime | 18944, 14336, 10803, 8601, 7424, 5939 | `motor.asm`, `GVNTBL`, lignes 12–18 |
| Coefficients d'inertie par rapport | 14280, 18870, 25041, 31451, 36440, 45550 | `motor.asm`, `GVVTBL`, lignes 5–11 |
| Passage automatique supérieur | 120, 195, 270, 345, 405, 500 unités internes | `motor.asm`, `GAUTBL`, lignes 33–45 ; `vr2.asm`, `AUTOG` |
| Rétrogradage automatique | 0, 105, 180, 255, 315, 360 unités internes | mêmes tables |
| Conversion de vitesse | km/h = vitesse interne × 2/3 | `vr2.asm`, ligne 2112 |
| Régime lié aux roues | rpm = vitesse interne × `GVNTBL[rapport]` / 256 | `vr2.asm`, `SPEDC2B`, lignes 954–958 |
| Table de couple | 833 valeurs espacées de 16 tr/min, de 0 à 13312 ; maximum 534 | `motor.asm`, `MOMTBL` |
| Freinage, pneus neufs | (94 × pédale)² / 16 | `vr2.asm`, `SPEDC0`, lignes 872–884 |
| Réduction originale du freinage avec l'usure | `(TIRUSE + 2048) / 4096`, soit 0,5…1 | `vr2.asm`, `SPEDC0`, lignes 877–881 |
| Résistance de l'air | vitesse interne² / 468 | `vr2.asm`, `SPEDC1`, lignes 886–889 ; le commentaire parle de 458 mais l'instruction emploie 468 |
| Roulement | 10 sur route ; +100 avec une roue sur bordure ; +198 hors route | `vr2.asm`, `SPEDC1A`, lignes 893–902 |
| Couple net engagé | couple moteur − frein − air − 10 − roulement | `vr2.asm`, `SPEDC2B`, lignes 949–952 |
| Variation de vitesse, branche non synchronisée | Δv interne = 256 × couple net / `GVVTBL[rapport]` | `vr2.asm`, `SPEDC4`, lignes 1002–1008 |

Les seuils supérieurs réellement utilisés par les rapports 1 à 5 correspondent à **80, 130, 180, 230 et 270 km/h**. Le sixième est le dernier rapport : le modèle ne passe pas de septième. Les seuils de rétrogradage des rapports 2 à 6 sont **70, 120, 170, 210 et 240 km/h**. La formule régime/vitesse à 13312 tr/min en sixième donnerait 382,54 km/h, mais ce nombre est une limite de correspondance théorique, **pas la vitesse de pointe mesurée du jeu**.

## Direction originale : souris et commandes numériques

Dans `vr2.asm`, lignes 416–448, la souris fournit `JOY+2` à `volant`, borné à **−271…+271**, sans filtre exponentiel dans cette branche. Son axe Y commande accélérateur/frein entre −94 et +94. Une réduction du lissage du prototype peut donc rapprocher la réponse d'une commande directe ; cela ne prouve pas un délai original de 45 ms.

Le joystick suit un autre chemin : `RIGHT`/`LEFT`, lignes 595–622, ajoutent ou retirent **24 unités par cycle** jusqu'à ±270. `DUMMYB`, lignes 582–593, ramène le volant au centre de **12 unités par cycle**. En contre-braquage, la commande opposée réduit immédiatement l'ancien braquage à ±50, puis zéro, avant de repartir dans l'autre sens. L'inversion est donc traitée expressément ; elle ne consiste pas à traverser lentement toute la plage précédente. Ces valeurs sont par appel de routine, sans conversion certaine en millisecondes. À une cadence supposée de 20 Hz, une montée linéaire complète demanderait 12 appels et un retour 23 appels : le nouveau réglage arcade n'en est pas une copie temporelle.

La branche `FAST2` du joystick (`vr2.asm`, lignes 636–647) augmente la commande d'accélération de 8 unités par cycle, avec +6 sur certaines bordures et repli à 88 lorsque le résultat atteint 96 ; la souris peut fournir sa commande directement. Aucune de ces branches ne justifie de doubler les vitesses ou le temps du prototype. Son moteur atteint déjà environ 297 km/h en dix secondes sur le plat.

La routine de châssis `sub6.asm` multiplie le volant par le déplacement (`angul`, lignes 438–480 puis 513–516) et réduit son effet au-delà d'un seuil de sollicitation lié à vitesse², volant et état des pneus. Les coordonnées de projection et tables employées ne donnent pas directement un angle de roue SI ou une accélération latérale maximale.

Le clavier de conduite moderne est une adaptation. Les branches étudiées établissent précisément souris et joystick ; elles ne prouvent pas une commande WASD historique. Le fichier bas niveau `system.asm` de cette archive étiquetée ST contient aussi des registres Amiga (`JOY0DAT`, `$DFF016`) et une table `CONVJOY` annotée « COMPATIBILITE ST ». Il accumule les déplacements souris avec un décalage réglable de 0 à 3 bits, initialement 1 (`system.asm`, lignes 1484–1509, 1729–1757 et 1773). Cette révision mixte ne permet pas d'affirmer une sensibilité matérielle Atari ST précise ni de convertir ses unités en DPI modernes.

## Adaptations explicites

- **Moteur.** La branche `SPEDC4` est évaluée en flottants avec une conversion fondée sur 20 cycles historiques par seconde : `a = 256 × 20 × coupleNet / (GVVTBL × 5,4)`. Ce calibrage et les branches synchronisées restent à comparer à l’original exécuté. Les 27 échantillons de couple espacés de 512 tr/min proviennent de `MOMTBL` et sont interpolés ; l’écart maximal aux 833 entrées est de 2,375 unités.
- **Embrayage et rapports.** Le départ se fait en première avec une cible `max(régimeRoues, 1000 + 7500 × accélérateur)`, lissée sur 0,09 s. Un changement coupe le couple pendant 0,10 s. Le frein annule l’accélérateur ; sans accélérateur, un frein moteur simplifié ajoute 1,2 m/s² de décélération hors poussée bonus. Point mort initial, synchronisations et dommages de surrégime ne sont pas reproduits intégralement.
- **Pente.** L’accélération reçoit `−9,81 × g / sqrt(1 + g²)`, avec `g = dz/ds`. Cette adaptation SI disparaît pendant un saut. Le profil et son calibrage sont décrits dans [ELEVATION.md](ELEVATION.md).
- **Accotements.** Une empreinte de roues de 1,6 m fait varier progressivement le roulement de +0 à +198 au-delà de la demi-largeur. À haute vitesse, l’autorité latérale du volant diminue jusqu’à 50 % sur l’herbe. À faible vitesse, la sollicitation de virage et cette réduction deviennent progressives pour permettre de repartir en braquant vers la route, même en courbe. La résistance de l’herbe reste présente. Il n’y a plus de butée latérale invisible à proximité immédiate de la route.
- **Commande.** Le modèle utilise une réponse exponentielle `1 − exp(−dt/tau)`. La prise de braquage varie de **45 à 72 ms** de constante de temps selon la vitesse ; relâchement et contre-braquage emploient **25 ms**. Réduire une commande en conservant son signe utilise la réponse normale. L’entrée est façonnée par un exposant 0,92. Ce sont des réglages du prototype. Unreal applique en amont une zone morte continue et son filtre d’entrée au pas physique ; le filtre ne dépend plus du nombre d’images affichées.
- **Pneus.** Depuis la 0.30, `TyreGrip() = 0,65 + 0,35 × clamp(TyreHealth / 2048, 0, 1)`. Ce coefficient multiplie la commande latérale, le moment de freinage et les seuils de glisse. L'usure réduit donc l'autorité de direction et allonge le freinage. La borne **0,65 est un réglage de jouabilité**, différent de la borne originale **0,5 pour le freinage**. La source `sub6.asm`, routine `angul`, utilise par ailleurs `TIRUSE` pour choisir un seuil dans `LIMCRI` ; appliquer un même coefficient moderne aux trois effets n'en reproduit pas la table.

## Déplacement latéral arcade

La conduite actuelle suit la branche arcade parallèle dérivée de `XBCMP3` et du déplacement `XB`. Le **cap de vitesse ne s’intègre plus pour piloter la voiture** : il reste nul en conduite normale. L’ancien modèle à accélération latérale et rayon `v²/grip` décrit dans les révisions précédentes a été retiré.

Pour une vitesse `v` en m/s, la commande latérale sur route vaut :

```text
vInterne = 5,4 × v
gripPneus = 0,65 + 0,35 × clamp(étatPneus / 2048, 0, 1)
volantLat = volantNormalisé × 271 × vInterne / 2048 × 50 / 256 × 1,12 × gripPneus
wash = v × courbure × 11,2 × transitionPetiteCourbure
latDot = volantLat − wash + impulsionDeContact + glisseAdditionnelle
sDot = v / clamp(1 − courbure × déport, 0,3, 3)
```

Les conversions `271`, `256` et le rythme latéral PAL de 50 Hz sont ceux retenus pour cette transcription. Le gain `1,12`, la longueur `11,2 m`, le lien entre courbure de spline et catégorie de virage originale sont des calibrages. La transition de petite courbure monte continûment de zéro à un entre `|K| = 0,0004` et `0,0008 /m` : franchir ce seuil ne provoque plus un saut de vitesse latérale. Une route droite ne recentre pas automatiquement la voiture.

Le rendu ajoute au tangent de route une dérive limitée à **±0,52 rad**. Sa cible dépend du déplacement latéral, du volant et de la glisse. `VisualHeading()` et `HeadingRad` utilisent cette dérive du châssis ; ils ne servent pas à faire tourner un vecteur de vitesse indépendant. Le châssis conserve une inertie de rotation, avec une accélération angulaire limitée à **160°/s²** ; entrée, contre-braquage et retour évitent ainsi les changements brusques de cap. La caméra suit `DrivingRotation`, qui moyenne les tangentes de route entre 0 et 12 m, puis cette dérive.

Le cockpit 0.34 suit maintenant la **photo F1 fournie par David**, qui remplace la contrainte précédente de cadrage Amiga. La caméra adopte une position plus basse ; le nez rouge, le pourtour du cockpit et les vraies roues avant participent à la vue. Un volant compact rectangulaire reçoit les gants blancs dans une prise à neuf heures et trois heures. Les rétroviseurs sont placés physiquement derrière les pneus sur des bras rouges coudés. Cette composition dépasse la première correction limitée au recul des mains : l’échelle, le placement et le cadrage sont réglés ensemble.

La référence photographique guide les proportions et les appuis visibles, sans fournir une reconstruction métrique. La compilation C++ du dernier ensemble passe ; l’import des assets est en correction et les captures finales doivent encore confirmer le contact apparent des pouces au neutre et en braquage. Les essais du modèle de conduite ci-dessous ne valident pas cette apparence.

## Dérapage et poussée de sortie

Appuyer sur **Maj** ou un bouton d’épaule de manette en braquant d’au moins **22 %** déclenche la glisse à partir de **12,2 m/s (43,92 km/h)** sur la route, avec du carburant et sans frein. Le sens de glisse est fixé à l’entrée. La glisse ajoute un déplacement latéral et du lacet visuel ; son entrée et sa sortie sont progressives, sur 0,10 et 0,16 s. Un petit saut visuel accompagne son déclenchement. **Le frein seul, même en virage, ne déclenche pas ce mode.**

Garder le bouton appuyé et **contre-braquer modérément** permet de charger la poussée. Le contrôle est évalué après une mise en place de **0,24 s** ; la charge progresse selon sa qualité, pas simplement selon la durée du bouton maintenu. La cible de contre-braquage varie entre 28 % et 48 % selon la courbure. De courts passages au neutre sont tolérés pour permettre les corrections. Au clavier, des pressions brèves sur la direction opposée alternées avec des relâchements permettent d’atteindre les **trois niveaux** ; maintenir le braquage d’entrée ne charge pas. Depuis la 0.35, le contre-braquage plein au clavier peut être maintenu : il charge à 68 % de la qualité optimale. Le dosage précis charge plus vite ; maintenir le braquage d’entrée ne récompense toujours pas le joueur.

La **0.34** récompense aussi les glisses longues et le bon moment pour relâcher. Un relâchement volontaire sur la route, avec un contrôle récent, libère le niveau atteint :

| Niveau | Charge nécessaire | Durée normale | Durée avec lâcher parfait | Poussée maximale ajoutée |
|---|---:|---:|---:|---:|
| I — bleu | 0,42 | 0,55 s | 0,75 s | 14 m/s² |
| II — ambre | 1,00 | 1,15 s | 1,35 s | 22 m/s² |
| III — violet | 1,80 | 1,70 s | 1,90 s | 26 m/s² |

Chaque nouveau niveau ouvre **une fenêtre de 0,22 s** : relâcher pendant cette fenêtre ajoute **0,20 s** au turbo gagné. Attendre plus longtemps conserve le niveau normal. Repasser sous puis au-dessus du même seuil ne renouvelle pas la fenêtre ; il faut gagner un niveau supérieur ou une nouvelle glisse. Les seuils sont une intégrale de qualité de contrôle et ne correspondent donc pas à des durées fixes depuis l’entrée. La charge plafonne à 2,10. La poussée monte progressivement pendant **0,10 s** et s’efface sur ses **0,16 s** finales.

Le contre-braquage maîtrisé rend la trajectoire plus serrée pour tenir ces longues glisses sur la route de **9,5 m**. La qualité filtrée sur **0,09 s** augmente la réduction de dérive due à la courbe de 58 % jusqu’à 78 % et réduit jusqu’à 25 % le déplacement latéral ajouté par la glisse. Le modèle modifie les forces latérales ; il ne recentre pas la voiture et ne déplace pas artificiellement la caméra. Sans contrôle, la trajectoire reste plus large.

Un relâchement trop tôt ne donne rien. Le frein, l’herbe, un saut physique, un contact, une panne sèche ou une vitesse inférieure au seuil annulent la charge. Frein, hors-piste, contact, saut physique et panne sèche coupent aussi une poussée en cours. Après annulation, il faut relâcher puis réarmer le bouton ; le délai de **0,24 s** et l’absence de boost actif empêchent l’enchaînement gratuit de bonds. Le lâcher parfait ne contourne aucun de ces garde-fous.

Le HUD indique la direction de contre-braquage, les trois seuils, **« RELÂCHE MAINTENANT : PARFAIT ! »** pendant la fenêtre puis le niveau et la durée restante du turbo. Les étincelles reprennent le bleu, l’ambre et le violet ; leur réserve est bornée à **48 particules**, actualisées à 30 Hz. Les trois niveaux ont des signaux sonores distincts. Un léger éclat apparaît au bord de l’écran pendant la poussée, sans secousse de caméra ni saut de champ de vision. Les effets sont remis à zéro au redémarrage d’une course.

Ce saut, les trois niveaux de poussée et leur chronométrage sont des **ajouts ludiques du prototype**, pas des comportements originaux démontrés. L'usure des pneus retire 12 unités par seconde en sollicitation excessive de virage, plus les pertes dues à certains chocs. Le freinage seul ne possède pas encore son propre terme d'usure ; un état de zéro reste conduisible avec le coefficient 0,65. Le `TIRUS` original peut au contraire terminer l'épreuve sur éclatement lorsque l'état passe sous zéro (`vr2.asm`, lignes 2122–2136). Le remake ne reproduit pas encore cette panne ni l'usure du moteur.

Un petit bond provoqué par un frottement de roues utilise un contrôle distinct : corriger à l’opposé de la poussée reçue pendant le bond améliore la réception. Le coefficient appliqué à la vitesse d’arrivée est **0,97 au neutre**, jusqu’à **1,00 avec une correction propre**, et descend à **0,94 avec une mauvaise correction**. L’impulsion latérale résiduelle est amortie. Cette réception n’accorde ni vitesse supplémentaire ni charge de dérapage ; les gros chocs gardent leur récupération pénalisante. [COLLISIONS.md](COLLISIONS.md) décrit les deux familles de bonds.

La consommation d'essence est `accélérateur × dt × 0,00055` de réservoir. Une panne sèche coupe le couple ; une immobilisation qui suit termine une course Grand Prix ou une étape Arcade. Les remplacements de pneus et ravitaillements exigent désormais un arrêt dans la zone de service, pendant 4 et 3 secondes respectivement ; [GAMEPLAY.md](GAMEPLAY.md) décrit ces règles et les modes de course. Pas de marche arrière, de tête-à-queue complet ni de transfert de charge. L'aspiration réduit la traînée d'air jusqu'à 30 % dans le sillage d'un concurrent, sans bonus de couple.

Les contacts sont traités séparément. Après une casse, le propriétaire Unreal immobilise la voiture puis la ramène progressivement à l'intérieur du bitume en 2,8 à 7 secondes selon son éloignement. La reprise se fait **à l'arrêt**, sans vitesse offerte ; chrono et adversaires continuent pendant la pénalité. [COLLISIONS.md](COLLISIONS.md) décrit les règles de contact et les références historiques, à distinguer de cette politique de reprise.

## Intégration et validation

API publique : `Vroom::DriveModel`, `DriveInput`, `DriveState`. `Reset()` remet l’état à zéro et conserve la préférence `Automatic`. `DriftChargeTier()`, `DriftPerfectWindowRemaining`, `BoostPerfect` et `BoostDurationSeconds()` fournissent au HUD le niveau gagné, la fenêtre restante et la durée réellement attribuée. Le joueur, les contacts, les adversaires et le chronomètre avancent à **120 Hz**. Le propriétaire Unreal conserve la phase fractionnaire entre images et interpole les deux derniers états, y compris la hauteur du saut visuel. Une image trop longue simule au plus douze pas (0,10 s) ; conserver cette phase évite de repartir sur une ancienne pose après un retard. Le temps simulé peut alors avancer moins vite que le temps réel.

Appelé seul, `Step` sous-divise aussi les grands pas à 120 Hz et borne un appel à 0,25 s. Un `dt` invalide ou négatif est ignoré ; entrées et états non finis sont ramenés à des valeurs sûres. La borne numérique de vitesse est 120 m/s. Ces garde-fous ne sont pas des limites physiques revendiquées pour Vroom original.

Depuis la racine du projet :

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -ISource/Vroom/Public Tests/drive_model_test.cpp -o /tmp/vroom-drive-tests
/tmp/vroom-drive-tests
/tmp/vroom-drive-tests --handling-benchmark

clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -ISource/Vroom/Public Tests/frame_timing_test.cpp -o /tmp/vroom-frame-tests
/tmp/vroom-frame-tests
```

Les contrôles portent sur l’accélération, les rapports, le freinage, la symétrie gauche/droite, la dérive due à la courbe, le hors-piste, les valeurs non finies, la glisse et ses sorties invalides. `Tests/drift_skill_test.cpp` ajoute le contre-braquage filtré, la modulation clavier, les trois niveaux de boost, le lâcher parfait, la progression de la poussée et l’absence de bonus sans contrôle ; `Tests/offroad_restart_test.cpp` vérifie la reprise depuis l’arrêt dans l’herbe, en courbe et en pente.

Les tests ciblés de la **0.34 passent** (`Saved/Validation/v034/drift-model-targeted.log`). Les **126 combinaisons** de vitesse initiale 30/40/50 m/s, de courbure ±0,018/±0,010/±0,003/0, de filtres 20/45 et de cadence 30/60/120 Hz atteignent le niveau III puis son lâcher parfait sur la largeur réelle : déport maximal **3,902 m**, pour une demi-largeur de 4,75 m. La rotation du châssis reste limitée à 160°/s². Huit alternances clavier de 0,20 s de contre-braquage et 0,10 s au neutre atteignent également le niveau III aux trois cadences. Ces essais vérifient les règles et la cohérence temporelle, pas la cadence d’affichage Unreal.

La série historique **0.33** `20260919-174842-package` a validé le package précédent et ses huit profils, en Performance à 70 % interne. Elle ne certifie pas le nouveau rendu de la 0.34. Pour valider un nouveau package après compilation :

```sh
python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak
```

Le préréglage Performance de cette commande est temporaire et conserve les préférences du joueur. Les captures et leurs journaux doivent être examinés ; les seuls tests portables ne valident ni le rendu ni la fidélité historique.

La suite `Tests/tyre_model_test.cpp` compare un freinage avec pneus neufs et entièrement usés, vérifie une autorité latérale réduite et le retour à l'adhérence neuve après remplacement. Elle s'exécute avec les autres suites via `python3 Tools/test_portable.py`. Ses seuils comparent deux états du remake ; ce ne sont pas des distances de freinage mesurées dans le jeu Amiga.

## Mesures portables du 8 septembre 2026

Banc `--handling-benchmark` exécuté à 120 Hz, sans Unreal. La réponse de commande seule, depuis le centre à l’arrêt, atteint 90 % en **108,3 ms**, revient à 10 % après relâchement en **58,3 ms**, et traverse zéro en contre-braquage en **25,0 ms**. Ces délais sont quantifiés au pas de simulation ; ils n’incluent ni le filtre d’entrée Unreal ni la latence du clavier ou de l’écran.

| Départ, volant déjà au maximum sur route droite | Vitesse latérale au premier pas | Déplacement latéral supplémentaire dans les 300 ms suivant le relâchement |
|---|---:|---:|
| 144 km/h | 6,267 m/s | 0,139 m |
| 288 km/h | 11,963 m/s | 0,258 m |

Ces résultats décrivent un déplacement latéral arcade. Ils ne doivent pas être présentés comme des rayons de virage : les anciennes mesures de rayon reposaient sur le modèle de cap désormais supprimé. Une comparaison de commandes enregistrées avec **Vroom Amiga**, édition choisie comme référence, reste nécessaire pour mesurer la fidélité de la conduite.

## Conduite kart et dégâts — 0.35

La vitesse latérale des pneus possède une courte inertie : réponse de 25 ms en adhérence, jusqu’à 90 ms en glisse, et 25 ms pour une correction opposée. Le volant garde sa réponse existante. Le relâchement reprend rapidement de l’adhérence ; aucun gain moteur supplémentaire n’est ajouté au départ. Les impulsions de collision restent séparées.

Les dégâts avant/arrière/gauche/droite viennent des contacts acceptés par le modèle de collision. Ils réduisent l’adhérence au maximum de 18 %, augmentent la traînée au maximum de 24 % et peuvent ajouter un léger tirage de direction (3,5 % maximum). Ces bornes concernent la carrosserie ; l’usure des pneus conserve son effet distinct. À l’arrêt, le tirage est nul. La récupération limite chaque zone à 45 % de dégâts, sans remise à neuf ; un service stand terminé répare entièrement la carrosserie.

Le drift de la 0.35 se déclenche uniquement dans une vraie courbe, avec un braquage dans son sens. Une ligne droite ne déclenche ni bond de drift ni charge de turbo. À la sortie du virage, une fenêtre de 0,18 s permet de relâcher et de conserver le bonus gagné ; maintenir le bouton au-delà interrompt la glisse sans turbo automatique.

### Rythme des adversaires (0.35)

Le moteur des rivaux accélère fortement au départ puis réduit progressivement son effort à l’approche de la pointe. Les personnalités gardent leurs différences de prudence, trajectoire et dépassement ; leur multiplicateur de vitesse est resserré entre 0,97 et 1,03. Une assistance de peloton bornée à −2 % / +4 % selon l’écart intervient seulement quand le joueur roule sur la piste avec du carburant, hors stands. Elle ne copie ni sa vitesse instantanée ni sa position. Les règles d’évitement et de freinage restent actives.

`rival_pace_test.cpp` compare douze départs individuels au joueur à 1, 2, 6 et 20 secondes ; `traffic_model_test.cpp` vérifie aussi freinage anticipé, dépassement, séparation des voitures et trois courses complètes à douze concurrents avec arrêts aux stands.
