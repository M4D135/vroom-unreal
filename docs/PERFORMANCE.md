# Performances — Vroom 0.33

## Mesures du 19 septembre 2026

Les huit profils du même package passent, sous fenêtres visibles et actives, sur Apple M5 / 24 Gio / macOS 26.4 / UE 5.8 Metal. Réglage : Performance, **70 % interne, TAA, 60 FPS avec VSync**. Les valeurs ci-dessous excluent 120 images de démarrage par profil. Le [rapport complet 0.33](FINALISATION_033.md) consigne aussi les contrôles de caméra et les empreintes du package.

| Profil | Images analysées | FPS moyen | P99 ms | Max ms | Images >20 ms | Verdict / CSV |
|---|---:|---:|---:|---:|---:|---|
| 1440 × 900 | 1680 | 59.99 | 16.926 | 17.031 | 0 | PASS, `Profile(20260919_175711).csv` |
| 2560 × 1664 | 1680 | 59.99 | 16.963 | 17.050 | 0 | PASS, `Profile(20260919_175747).csv` |
| Autriche | 7680 | 59.99 | 16.969 | 18.190 | 0 | PASS, `Profile(20260919_175823).csv` |
| France | 7680 | 59.99 | 16.977 | 18.433 | 0 | PASS, `Profile(20260919_180043).csv` |
| États-Unis | 7680 | 59.99 | 16.966 | 18.031 | 0 | PASS, `Profile(20260919_180301).csv` |
| Suède | 7680 | 59.99 | 17.008 | 19.938 | 0 | PASS, `Profile(20260919_180522).csv` |
| Japon | 7680 | 59.99 | 16.946 | 18.254 | 0 | PASS, `Profile(20260919_180740).csv` |
| Royaume-Uni | 7680 | 59.99 | 16.919 | 18.196 | 0 | PASS, `Profile(20260919_180959).csv` |

Commande : `python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak`. Le profil est appliqué temporairement ; la comparaison et toute reprise conservent les réglages explicitement mesurés. Les scénarios fonctionnels sont également visibles avec cette commande.


### Comparaison complémentaire du profil Élevé

Deux diagnostics visibles supplémentaires du même package mesurent le profil Élevé, à 100 % interne avec TSR et Lumen, sur 1 680 images analysées après 120 images exclues. Ils ne remplacent pas les six tours du profil Performance.

| Sortie | FPS moyen | P99 ms | Max ms | Images >20 ms | Résultat |
|---|---:|---:|---:|---:|---|
| 1440 × 900 | 60,00 | 16,930 | 17,155 | 0 | PASS |
| 2560 × 1664 | 25,90 | 47,591 | 56,100 | 1680 | ÉCHEC des seuils 60 FPS |

À haute définition, le temps GPU moyen de 38,49 ms est proche du temps image de 38,61 ms ; le thread de jeu reste à 1,71 ms. Ce diagnostic désigne le coût graphique comme la limite de cette mesure. Le profil Performance a été appliqué dans le panneau du jeu installé pour privilégier la fluidité ; l’ancien fichier de préférences est sauvegardé. Preuve complémentaire : `Saved/Validation/v033/high-profile/results.json`.

## Historique 0.30 — mesures conservées

Les sections suivantes concernent les versions antérieures ; elles ne sont pas les mesures de la 0.33.

# Fluidité — Vroom 0.30

**Le package Mac 0.30 passe la série automatisée du 13 septembre 2026** : huit profils de cadence, six tours complets et la trace courte de mouvement. Le rapport `Saved/Validation/v0.30.0/20260913-030921-package/results.json` est complet, sans échec. Les mesures ont été effectuées en fenêtre sous verrouillage (`onscreen_locked`), sans essai interactif. Les résultats détaillés figurent en fin de page ; [VALIDATION.md](VALIDATION.md) identifie le build et les empreintes.

## Ce que le moteur fait pour stabiliser le mouvement

Le joueur, les adversaires et les contacts partagent une simulation à 120 Hz. Le rendu interpole les états précédents et courants, y compris la hauteur des sauts et les phases de contact. En cas de retard, le plafond de douze pas conserve la fraction de temps restante, afin de ne pas réafficher l'état précédent. Ce plafond limite la quantité de rattrapage par image : une très longue pause du processus ne signifie pas que la simulation récupère tout le temps réel perdu.

Le filtre du braquage est exponentiel et calculé au pas physique. La zone morte du stick a une transition continue. La caméra interpole son orientation par quaternion, avec un amortissement dépendant du temps écoulé. Le tracé bénéficie d'un lissage de la courbure après rééchantillonnage à 5 m.

L'orientation du châssis en glisse possède une vitesse angulaire continue et une accélération limitée à **160°/s²** au pas physique. Cette inertie supprime le changement immédiat d'angle à l'entrée et à la sortie du dérapage, tout en conservant la commande latérale. La limite concerne la contribution de glisse ; l'orientation affichée ajoute celle de la route et reste soumise au seuil de validation de 300°/s².

La 0.30 interpole également le déplacement latéral des concurrents. Leur angle de changement de voie est amorti ; leurs décisions sont espacées et leur vitesse latérale limitée. Les prises des mains tournent avec le volant et les manches suivent les poignets. Ces descriptions ne sont pas, en elles-mêmes, une preuve de fluidité visuelle.

La mini-carte conserve sa géométrie en cache. Les douze adversaires sont regroupés en instances et transmettent leurs transformations précédentes au rendu temporel. L'herbe est instanciée et son vent est calculé dans le matériau. Les traces et la fumée ont un budget limité et leur géométrie est actualisée à 30 Hz. Les captures des rétroviseurs et les autres ressources de rendu gardent leurs propres budgets ; leur coût réel doit apparaître dans les profils du package.

L'enregistrement du fantôme échantillonne les tours à 30 Hz. À la sauvegarde, le thread de jeu copie un instantané immuable ; la construction du JSON et l'écriture du fichier se font ensuite en arrière-plan, dans une chaîne ordonnée. La fermeture attend les écritures puis libère les ressources asynchrones avant l'arrêt du moteur.

## Diagnostic PSO et préparation du circuit

Deux mesures intermédiaires de la 0.30 ont échoué avant le correctif de précache. Elles sont conservées comme **diagnostic, sans valeur de validation du package final** :

| Source antérieure au correctif | P99 | Maximum | Images > 20 ms / analysées |
|---|---:|---:|---:|
| `Saved/Validation/v0.30.0/20260913-003243-package/Profile(20260913_003548).csv` | 26,076 ms | 70,004 ms | 23 / 1 680 |
| `Saved/Validation/v030/perf-diagnosis-repeat.json`, issu de `Profile(20260913_004358).csv` | 17,810 ms | 48,186 ms | 11 / 1 680 |

La deuxième mesure avait un maximum GPU de 13,795 ms et un temps moyen du thread de jeu de 1,019 ms. Ces chiffres seuls n'expliquent pas les pauses : le CSV contient aussi des attentes de threads et des créations de **PSO**, les configurations de pipeline graphique compilées par le pilote. Ses trois plus longues images après les 120 premières correspondent aux compteurs suivants :

| Index d'image du CSV, à partir de zéro | Durée d'image | `PSO/GraphicsPSOHitch` | `PSO/GraphicsPSOHitchTime` |
|---|---:|---:|---:|
| 780 | 48,186 ms | 1 | 35,653 ms |
| 787 | 38,577 ms | 2 | 46,181 ms |
| 801 | 35,710 ms | 2 | 48,497 ms |

Le compteur de temps additionne les créations lentes attribuées à cette image ; les travaux peuvent se chevaucher et cette somme n'est pas la durée d'image. Dans les sources UE 5.8, `Runtime/RHI/Private/PipelineStateCache.cpp`, `CheckAndUpdateHitchCountStat` produit ces compteurs pour des créations PSO à l'exécution dépassant 20 ms. Ils établissent la présence de compilations lentes dans les pires images de la répétition ; ils ne démontrent pas que chaque pic du premier profil a la même cause. Le nombre de shaders chargés ou créés peut rester constant pendant une création PSO Metal.

Metal 5.8 prend en charge le précache PSO natif. Le composant procédural standard ne collecte toutefois pas les données nécessaires : [VroomProceduralMeshComponent.cpp](../Source/Vroom/Private/VroomProceduralMeshComponent.cpp) ajoute cette collecte pour le cockpit, les rétros, la piste, le terrain, la fumée et les traces. Il utilise la déclaration adaptée à `FLocalVertexFactory` : accès manuel aux sommets lorsque la plateforme le prend en charge, déclaration explicite avec quatre canaux UV sinon. Il collecte aussi les matériaux d'effets avant leurs premiers sommets et diffère la création du proxy de rendu jusqu'au callback natif de fin de compilation. La géométrie reste celle du composant procédural existant.

Le HUD Canvas suit un chemin séparé de ces composants 3D. [VroomHUD.cpp](../Source/Vroom/Private/VroomHUD.cpp) dessine les lignes épaisses avec des quadrilatères en triangles et un état de rasterisation statique. Il prépare ses rectangles opaques et translucides, contours arrondis, lignes, polygones opaques `NGon` et polices sur la vraie cible de sortie, puis les recouvre par l'écran de préparation. Une synchronisation jusqu'au thread RHI attend la soumission de ces dessins et leur compilation PSO. Cette préparation est refaite lors d'une nouvelle préparation de circuit ou après les graphismes ; elle ne se limite pas aux captures de validation.

Le profil Mac active `r.PSOPrecaching=1`, `r.PSOPrecache.Components=1`, `r.PSOPrecache.Resources=1` et `r.PSOPrecache.ProxyCreationStrategy=1`. Les composants sont à nouveau collectés après application des réglages graphiques définitifs. La VSync et la limite de 60 images/s restent inchangées ; une modification de ces paramètres ou une désactivation d'App Nap ne constitue pas ce correctif.

Le jeu affiche **« PRÉPARATION DU CIRCUIT »** au chargement, à la préparation d'une course et après fermeture des graphismes. Les entrées des menus et la simulation attendent les travaux natifs de précache, les travaux des composants et la préparation Canvas, puis trois images consécutives sans travail restant pour laisser recréer les proxies. Le chrono reste arrêté, la présentation conserve son interpolation et l'état de pause précédent est rétabli. Le temps d'attente dépend des travaux réels : ce n'est pas un délai fixe ajouté seulement aux essais.

Les profils passent `-LogCmds="LogPSOHitching Verbose"`. Conserver et rapprocher les informations suivantes en cas de nouveau pic :

- `FrameTime`, `GPUTime`, `GameThreadTime`, les percentiles et les maxima ;
- `Exclusive/GameThread/EventWait`, `Exclusive/RenderThread/EventWait` et les attentes nommées ;
- `PSO/GraphicsPSOHitch`, `PSO/GraphicsPSOHitchTime`, les équivalents Compute lorsqu'ils existent et `PSO/PSOMisses` ;
- les messages `LogPSOHitching` donnant ressource, passe et statut du précache ;
- `VROOM: Native PSO preparation pending=…`, `Circuit preparation started` et `Circuit preparation ready`, avec durée et nombre restant ;
- `VROOM: Canvas PSO preparation submitted output=…` et `Canvas PSO preparation ready`, pour la préparation du HUD sur la définition réellement utilisée.

Ces compteurs aident au diagnostic. Aucun critère « zéro PSO » n'a été ajouté : les critères de cadence ci-dessous restent ceux de publication. La série finale les satisfait après les correctifs de précache et de lignes Canvas ; les essais intermédiaires suivants restent conservés avec leurs échecs.

## Diagnostic intermédiaire : entrée en dérapage en France

La série `Saved/Validation/v0.30.0/20260913-015803-package/` passe les quatre profils de cadence exécutés après le précache scène et Canvas, puis s'arrête sur la continuité du mouvement en France. Son `results.json` conserve `completed=false` et l'échec `soak-track-2-camera`. **Cette série ne valide donc pas le package final.**

| Profil exécuté | Images analysées | Cadence moyenne | P99 | Maximum | Images > 20 ms |
|---|---:|---:|---:|---:|---:|
| `perf-1440`, sortie 1440 × 900 | 1 680 | 59,99 images/s | 16,936 ms | 17,033 ms | 0 |
| `perf-2560`, sortie 2560 × 1664 | 1 680 | 59,97 images/s | 17,071 ms | 23,080 ms | 1 |
| `soak-track-1`, tour complet Autriche | 7 680 | 59,99 images/s | 17,018 ms | 18,234 ms | 0 |
| `soak-track-2`, tour complet France | 7 680 | 59,99 images/s | 17,028 ms | 18,159 ms | 0 |

Les CSV correspondants sont `Profile(20260913_020223).csv`, `Profile(20260913_020256).csv`, `Profile(20260913_020328).csv` et `Profile(20260913_020542).csv`, dans ce même dossier. Les réglages restent ceux du profil Mac à 70 % interne décrit ci-dessous. Les quatre autres circuits et la trace courte finale n'ont pas été exécutés dans cette série interrompue.

La trace France contient 7 675 échantillons analysés, sans donnée invalide ni recul de distance. Sa variation maximale de lacet est de 1,233° par image, mais son accélération maximale atteint **855,99°/s²**, au-delà du seuil inchangé de **300°/s²**. Le pic se situe à la ligne 4 318 de `soak-track-2.log`, à 2 934,54 m. La cadence et la continuité sont deux critères distincts : des images régulières peuvent encore montrer un mouvement brusque.

Le diagnostic rattache ce pic au passage du frein de la conduite automatique de 0 à 0,6 alors que le braquage dépasse le seuil d'entrée en glisse de 0,22. Cela déclenche l'entrée en dérapage et son petit saut programmé de 0,42 m sur 0,26 s. La décomposition du lacet affiché en angle de route et angle de glisse donne, au pic, **852,26°/s² dus à la glisse contre 3,73°/s² dus à la route**. L'accélération maximale absolue de l'angle de route sur toute la trace analysée est de 112,58°/s². La montée observée à cet instant vient du saut de dérapage ; elle ne démontre pas une bosse du terrain français.

Ce diagnostic a motivé une correction de la rotation dans le modèle de conduite, suivie du profil ciblé ci-dessous puis de la série finale. Les seuils sont restés inchangés et ces quatre profils de l'ancien binaire n'ont pas été réutilisés pour certifier son remplaçant.

## Diagnostic ciblé : rotation corrigée, lignes Canvas encore coûteuses

Le profil France du nouveau package après correction de rotation, conservé sous `Saved/Validation/v030/perf-france-yaw-before-lines.json`, provient de `Profile(20260913_021937).csv`. La continuité passe sur les 1 675 échantillons analysés : accélération maximale de lacet **189,02°/s²**, variation maximale **1,258° par image**, aucune donnée invalide ni recul de distance. Cet essai ciblé ne remplace pas les six parcours complets.

La cadence échoue sur les 1 680 images analysées : **59,68 images/s** en moyenne, P99 **19,272 ms**, maximum **53,847 ms**, **17 images au-delà de 20 ms**. Le CSV compte **3 361 `PSO/PSOMisses` après les 120 premières images**, avec au moins un manque de cache sur chacune des 1 680 images restantes. La lecture des sources UE 5.8 confirme un mécanisme de recréation : les lignes Canvas épaisses appellent `RHICreateRasterizerState` dans `BatchedElements.cpp:1014`, Metal alloue un nouvel état dans `MetalState.cpp:540`, et `PipelineStateCache.cpp` inclut le pointeur de cet état dans sa clé de cache. Un état équivalent créé à nouveau peut donc manquer le cache malgré la préparation initiale.

Le correctif remplace le helper de lignes épaisses par des quadrilatères formés de triangles, avec un état de rasterisation statique, en conservant leur apparence. Il prépare aussi explicitement les polygones opaques `NGon` sous l'écran de chargement. La répétition après ce changement est `Saved/Validation/v030/perf-france-yaw.json`, issue de `Profile(20260913_022855).csv` : les manques de cache passent de 3 361 à 1 sur les 1 680 images analysées, sans compilation PSO lente signalée. Elle reste en échec sur le maximum de durée d'image. Un pic d'environ 24 ms apparaît dans `TickActors`, de cause encore indéterminée ; les compteurs ne permettent pas de l'attribuer aux PSO. **Ce diagnostic ciblé ne constitue toujours pas un succès de la série finale.**

## Mesure Suède perturbée par un autre projet Unreal

La série `20260913-024921-package` passe les deux définitions et les parcours Autriche, France et États-Unis, puis échoue en Suède avec un pic d'environ 145 ms vers 00:58:54 UTC. `Saved/Validation/v030/sweden-concurrent-unreal.log` conserve la preuve d'une charge concurrente : à 02:58:52 locales, `powerd` signale `UnrealEditor` PID 77008 avec l'assertion « Running Saigneurs » ; une compilation `clang` importante est encore active à 03:02. La mémoire libre du système descend d'environ 8,5 à 4 Gio, tandis que celle de Vroom reste proche de 2,34 Gio. **Cette mesure est contaminée par le lancement d'un autre projet Unreal** ; elle n'a entraîné ni modification du code de cadence ni élargissement des seuils. La Suède a été remesurée dans la reprise finale `030921`, qui passe. `--resume` a conservé les cinq profils réussis après vérification des empreintes identiques du package et des mêmes critères ; le rapport final conserve l'historique de cette reprise.

## Profil Mac visé

Le préréglage Performance est prévu pour une cible de **60 images/s** : résolution interne de 70 %, TAA, une cascade d'ombre sur 80 m, éclairage ambiant, reflets désactivés, herbe clairsemée animée et VSync. Sous Unreal 5.8, TAA correspond à `r.AntiAliasingMethod=2` ; la valeur 3 désigne MSAA.

Ces valeurs sont les défauts sans préférences sauvegardées. Le menu peut les modifier : le rapport final doit identifier les réglages effectivement chargés, la définition de sortie et la machine. Le nom « Performance » ne garantit aucun débit d'images.

Les journaux des profils avant et après précache confirment les mêmes réglages effectivement chargés :

```text
Graphics applied scale=70.0 AA=2 shadows=1 GI=0 reflections=0 foliage=1 wind=1 effects=1 blur=0 fps=60 vsync=1 DLSS=0
```

Les mesures portent donc sur le **profil fluide avec une résolution interne de 70 %**, TAA, ombres faibles, éclairage ambiant, reflets coupés, végétation clairsemée avec vent, effets moyens, flou coupé, limite de 60 images/s et VSync active. Les fenêtres de sortie 1440 × 900 et 2560 × 1664 ne correspondent pas à un rendu interne natif à ces définitions. Les six parcours complets utilisent une fenêtre de 1440 × 900 avec ce même réglage interne. Les profils RTX/NVIDIA ne disposent pas encore d'une mesure validée du package Windows.

## Mesurer le package

Depuis la racine du dépôt, après compilation et préparation des ressources :

```sh
python3 Tools/verify_mac.py --profile --soak
```

Le script exécute d'abord les scénarios et les captures visuelles hors écran. Il ouvre ensuite des fenêtres réelles pour les profils :

- deux CSV de 1 800 images, un par définition courte ;
- six CSV de 7 800 images, un par circuit, chacun avec télémétrie et confirmation d'un tour complet ;
- une trace de mouvement distincte de 25 secondes.

Les parcours longs emploient une conduite assistée de mesure qui ignore les contacts. Ils évaluent le rendu et la continuité sur toute la longueur des circuits ; ils ne remplacent pas les scénarios de collision ni les tests du comportement des adversaires.

Aucune capture PNG n'est demandée pendant les profils. Éviter les builds, générations de ressources et autres charges de test simultanées. Le script passe `-LLM`, `-windowed`, `-ForceRes` et les dimensions demandées ; les définitions réellement utilisées sont aussi à contrôler dans les métadonnées moteur.

Le vérificateur utilise aussi `Tools/activate_mac.m` pour contrôler et demander l'activation de l'application par son PID. Le statut est conservé dans le rapport sous `foreground`. Un premier essai de diagnostic a donné `foreground_before=1 foreground_after=1` : le jeu était déjà actif. Ce contrôle ne démontre donc pas que le lancement en arrière-plan ou App Nap causait les pics résiduels.

La série `20260913-023133-package` a ensuite passé ses 22 scénarios avant de s'arrêter sur le contrôle d'activation de `perf-1440`. La session Mac était verrouillée : `CGSessionCopyCurrentDictionary` signalait `locked=1`, `onConsole=1`, et `loginwindow` occupait le premier plan. Le rapport de cette série reste incomplet. Le vérificateur distingue désormais `onscreen_active`, `onscreen_locked` et `onscreen_unconfirmed`. Le verrouillage explicitement constaté permet de poursuivre le rendu en fenêtre et la boucle de mouvement automatisée avec les mêmes seuils de cadence et de continuité. Le rapport conserve `interaction_verified=false` : **ces mesures sous verrouillage ne sont pas un essai interactif ni une observation de la présentation à l'écran par le joueur**. Un échec d'activation sans preuve de verrouillage demeure bloquant.

Les résultats sont copiés sous `Saved/Validation/v0.30.0/<série-datée>/` avec un `results.json`. Le script vérifie les empreintes du binaire et des données avant et après la série. Les CSV originaux du package Mac se trouvent sous :

```text
~/Library/Containers/com.YourCompany.Vroom/Data/Library/Application Support/Epic/Vroom/Saved/Profiling/CSV/
```

## Calculs et critères d'acceptation

`Tools/analyze_performance.py` distingue les images numériques des en-têtes répétés, événements et métadonnées du CSV Unreal. Il exclut les 120 premières images de chaque source, puis calcule les durées moyennes, percentiles, maxima et dépassements de seuil. La cadence moyenne correspond à `1000 / durée moyenne en ms`.

Le CSV commence toujours à l'image zéro. Seules les 120 premières images sont exclues : les pics suivants restent comptabilisés, même si l'écran de préparation est encore affiché. L'attente du précache n'allonge pas cette exclusion. Un en-tête tardif ou une valeur de métadonnée ne doit ni créer une fausse image de 600 ms, ni masquer une vraie image lente. Les données invalides et manquantes sont signalées.

Les critères Mac 60 Hz sont définis dans [regression_gates.py](../Tools/regression_gates.py) :

| Critère | Seuil |
|---|---:|
| Cadence moyenne | ≥ 59,0 images/s |
| 99e percentile | ≤ 18,0 ms |
| Pic maximal | ≤ 33,5 ms |
| Images de plus de 20 ms | ≤ 0,5 % des images analysées |
| Données d'images invalides | 0 |
| Nombre brut d'images | Exactement celui demandé : 1 800 ou 7 800 |

Pour la télémétrie de mouvement : aucune marche arrière de la distance affichée sur les parcours assistés, aucune donnée analysée invalide, variation maximale de lacet de 3° par image et accélération maximale absolue de 300°/s². La trace courte exige au moins 1 000 échantillons analysés ; chaque parcours long en exige au moins 7 000 ainsi que le marqueur du tour complet.

Ces seuils sont les limites de publication explicites ; les résultats mesurés sont présentés ci-dessous. Leur réussite ne prouve pas qu'aucune saccade n'est perceptible dans toutes les conditions.

## Analyse d'un fichier isolé

```sh
python3 Tools/analyze_performance.py '/chemin/Profile.csv' \
  --warmup 120 --output /tmp/performance.json
```

Pour analyser aussi une télémétrie de mouvement, ajouter `--motion-log /chemin/motion.log`. Les 120 premières lignes d'images sont exclues séparément dans chaque source. Deux enregistrements distincts ne sont pas présentés artificiellement comme synchronisés.

Une exécution de l'analyseur seule calcule les statistiques ; c'est le vérificateur complet qui applique les seuils de régression. Une capture isolée ou le mode `--editor` ne fournit donc pas un rapport installable.

## Publication locale d'une mesure validée

Une fois la série complète terminée avec succès :

```sh
python3 Tools/install_mac.py --validation \
  "Saved/Validation/v0.30.0/DOSSIER-DE-LA-SERIE/results.json"
```

L'installateur vérifie la complétude, réapplique les critères et compare les empreintes aux fichiers à installer. Il met à jour le manifeste de la version active et conserve les anciennes versions. Le lancement stable se fait par `~/Applications/Vroom.app` ou le raccourci `~/Desktop/Vroom.command`. Construire un nouveau package ne modifie pas cette sélection.

## Résultats du package final

**PASS — 13 septembre 2026, Mac Apple M5, 24 Go, Unreal Engine 5.8, Metal, Development.** Rapport : `Saved/Validation/v0.30.0/20260913-030921-package/results.json`, `completed=true`, 31 cas terminés avec le code zéro, sans échec. Il comprend les 22 cas fonctionnels/visuels, les huit profils ci-dessous et la trace courte. Le SHA-256 du binaire est `3094777016a987055bf8f3a733db00faeaed47c71f96e372effb28108ec1bab0` ; les 28 fichiers de données, les CSV et les journaux sont également identifiés par empreinte dans le rapport.

Le profil réellement chargé est celui décrit plus haut : **70 % interne, TAA, VSync, plafond 60 images/s**. Tous les profils signalent **`onscreen_locked`, `interaction_verified=false`**. Il s'agit de mesures automatisées du rendu en fenêtre et du mouvement sous verrouillage, sans validation de la présentation interactive au joueur.

| Profil | CSV dans la série finale | Images analysées | Moyenne images/s | P95 ms | P99 ms | Maximum ms | Images > 20 ms |
|---|---|---:|---:|---:|---:|---:|---:|
| 1440 × 900 | `Profile(20260913_024923).csv` | 1 680 | 59,985 | 16,861 | 16,926 | 17,060 | 0 |
| 2560 × 1664 | `Profile(20260913_024956).csv` | 1 680 | 59,987 | 16,865 | 16,941 | 17,035 | 0 |
| Autriche | `Profile(20260913_025029).csv` | 7 680 | 59,986 | 16,883 | 17,019 | 18,005 | 0 |
| France | `Profile(20260913_025242).csv` | 7 680 | 59,986 | 16,916 | 17,040 | 20,040 | 1 |
| États-Unis | `Profile(20260913_025456).csv` | 7 680 | 59,987 | 16,893 | 17,030 | 20,294 | 1 |
| Suède | `Profile(20260913_030929).csv` | 7 680 | 59,983 | 17,233 | 17,481 | 22,833 | 1 |
| Japon | `Profile(20260913_031142).csv` | 7 680 | 59,986 | 16,891 | 17,101 | 18,493 | 0 |
| Royaume-Uni | `Profile(20260913_031355).csv` | 7 680 | 59,986 | 16,874 | 17,003 | 18,149 | 0 |

Les six parcours complets utilisent 1440 × 900. Sur les **49 440 images analysées**, trois dépassent 20 ms ; aucune donnée d'image n'est invalide. Le maximum global est **22,8332 ms** et le P99 le plus élevé **17,4814 ms**, sous les limites respectives de 33,5 et 18 ms. Les 120 premières images de chaque source sont les seules exclues.

| Mouvement | Échantillons analysés | Variation de lacet maximale | Accélération de lacet maximale | Tour complet |
|---|---:|---:|---:|---|
| Autriche | 7 675 | 1,665° | 54,51°/s² | Oui |
| France | 7 675 | 0,750° | 219,37°/s² | Oui |
| États-Unis | 7 675 | 0,699° | 185,14°/s² | Oui |
| Suède | 7 675 | 1,310° | 213,76°/s² | Oui |
| Japon | 7 675 | 0,756° | 213,26°/s² | Oui |
| Royaume-Uni | 7 675 | 0,561° | 212,60°/s² | Oui |
| Trace courte de 25 secondes | 1 380 | 0,568° | 51,10°/s² | Sans objet |

Toutes les traces passent les critères de continuité, sans donnée analysée invalide ni recul de distance. Les journaux des huit profils confirment la fin de préparation Canvas et `pending=0` ; les durées `Circuit preparation ready` vont de **0,056 à 0,102 s**. Ce compteur mesure cette étape de préparation, pas le démarrage complet de l'application. Aucun message de compilation PSO lente n'apparaît dans ces huit journaux.

Le build final est documenté par `Saved/Logs/vroom-stable-lines-package.log` : `BuildCookRun` en **61,62 s**, `ExitCode=0`. Les **21/21 contrôles portables** passent dans `Saved/Validation/v030/release-portable-final.log`. Les diagnostics antérieurs restent des résultats distincts, conservés pour expliquer les corrections et la reprise. La mesure interactive, les autres qualités graphiques et Windows/NVIDIA restent hors de cette validation.

Le même package est installé dans `~/Library/Application Support/Vroom/Releases/0.30.0-9dc508f3fb51/Vroom.app`. Les deux entrées stables, application native et raccourci du Bureau, ont démarré la bonne version et fermé normalement ; `Saved/Validation/v030/installed-launch-checks.json` conserve leurs reçus.
