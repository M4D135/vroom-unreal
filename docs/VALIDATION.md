# Validation — Vroom 0.33

## Série du 19 septembre 2026

**PASS — 30/30 suites portables et 45/45 contrôles du package Mac.** Le [rapport 0.33](FINALISATION_033.md) détaille les corrections, les cas réellement exécutés, les résultats des huit profils et l’état de l’installation. La série `Saved/Validation/v0.33.0/20260919-174842-package/results.json` est complète, sans échec : 36 scénarios fonctionnels/visuels, huit profils et une trace courte. Les six parcours couvrent un tour entier. Toutes les fenêtres sont visibles et actives ; les commandes de course sont automatisées.

Le profil Performance mesure environ 60 FPS, un P99 maximal de **17,008 ms**, un maximum de **19,938 ms** et zéro image au-delà de 20 ms. Il utilise **70 % de résolution interne**, avec sorties 1440 × 900 et 2560 × 1664. Les traces ne contiennent ni donnée invalide ni recul de position. Les réglages Élevé/Ultra, Windows/DLSS et les périphériques physiques ne sont pas certifiés par cette série.

Le [rapport JSON courant](validation-results.json) correspond au SHA-256 exécutable `a3af96417a81859a643010c80aebcb52ebc4135a44b0d76680c0b69c9df9a35d`. Les mesures antérieures restent conservées dans [validation-results-030.json](validation-results-030.json) et ci-dessous.

## Archive — Vroom 0.30

## État du rapport final

**PASS — package Mac 0.30 installé et vérifié le 13 septembre 2026.** La série `Saved/Validation/v0.30.0/20260913-030921-package/results.json` est complète (`completed=true`), avec **31 cas réussis, aucun échec et toutes les sorties à zéro** : 22 scénarios fonctionnels/visuels, huit profils et une trace courte. Les six parcours longs confirment chacun un tour complet. Les mesures sont automatisées sous verrouillage (`onscreen_locked`, `interaction_verified=false`) ; elles ne constituent pas un essai interactif. L'installation a ensuite été contrôlée par deux lancements réels, depuis l'application native et le raccourci du Bureau.

La série finale couvre la correction des faces des roues, le précache PSO de la scène et du HUD Canvas, les lignes Canvas en quadrilatères à état statique, l'écran de préparation du circuit et l'inertie de rotation en dérapage limitée à 160°/s². Le dossier `Saved/Validation/v0.30.0/20260913-003243-package/` contient une série intermédiaire arrêtée sur un échec de cadence (`completed=false`) ; `Saved/Validation/v030/perf-diagnosis-repeat.json` décrit une répétition isolée également hors critères. Ils restent des preuves de diagnostic, sans certifier le package suivant. [PERFORMANCE.md](PERFORMANCE.md) conserve leurs mesures et la corrélation entre créations PSO lentes et pires images de la répétition.

La série suivante, `Saved/Validation/v0.30.0/20260913-015803-package/`, passe ses quatre profils de cadence exécutés, avec un maximum global de 23,080 ms. Elle reste **incomplète et en échec** : le tour France atteint 855,99°/s² d'accélération de lacet pour une limite de 300°/s² (`soak-track-2-camera`). Au pic, la glisse contribue 852,26°/s² et l'angle de route 3,73°/s². Le diagnostic identifie l'entrée en dérapage déclenchée par le frein automatique de 0 à 0,6 sous braquage, avec son saut programmé ; ce résultat n'établit pas une bosse de terrain. [PERFORMANCE.md](PERFORMANCE.md#diagnostic-intermédiaire--entrée-en-dérapage-en-france) détaille les mesures. La correction du modèle de conduite a été suivie d'un nouveau package et de la série finale complète. Aucun seuil n'a été assoupli et aucun profil de cet ancien binaire n'a été réutilisé pour certifier son remplaçant.

La série `20260913-024921-package` passe cinq profils (deux définitions, Autriche, France et États-Unis) avant une mesure Suède contaminée par le lancement et la compilation simultanés d'un autre projet Unreal, documentés dans [PERFORMANCE.md](PERFORMANCE.md#mesure-suède-perturbée-par-un-autre-projet-unreal). La reprise finale `030921` a réexécuté la Suède, puis le Japon, le Royaume-Uni et la trace courte. Les cinq profils réussis et les 22 cas précédents ont été conservés après vérification des empreintes identiques du package, des preuves et des mêmes critères. L'historique des rapports interrompus demeure dans le rapport final ; la mesure Suède contaminée n'a pas été transformée en réussite.

| Preuve finale | Résultat |
|---|---|
| Build | `Saved/Logs/vroom-stable-lines-package.log` : `BuildCookRun` 61,62 s, `ExitCode=0` |
| Rapport / machine | Série `20260913-030921-package`, 13 septembre 2026, Mac Apple M5, 24 Go, UE 5.8 / Metal / Development |
| Binaire et données | SHA-256 binaire ci-dessous, 28 fichiers de données identifiés dans le rapport |
| Tests portables | **21/21 PASS**, `Saved/Validation/v030/release-portable-final.log` |
| Scénarios et captures | **22/22 PASS**, pneus fermés et panneau graphique vérifiés dans les images normales |
| Cadence | **8/8 PASS**, P99 maximal 17,4814 ms, pic maximal 22,8332 ms |
| Continuité et fermeture | Six tours complets + trace courte PASS ; 31 processus sortis à zéro, dont la persistance |
| Préparation | Canvas prête et `pending=0` dans les huit profils ; étape mesurée de 0,056 à 0,102 s ; aucun message PSO lent |
| Installation et lancement de la version active | **PASS**, `installation-final.log` et `installed-launch-checks.json` sous `Saved/Validation/v030` ; lanceur natif et Bureau vérifiés |

SHA-256 du binaire mesuré : `3094777016a987055bf8f3a733db00faeaed47c71f96e372effb28108ec1bab0`. Le récapitulatif [validation-results-030.json](validation-results-030.json) et les images publiées doivent correspondre à cette série et à cette empreinte ; les captures anciennes ne certifient pas automatiquement ce package. Les huit profils détaillés et les sept traces de mouvement sont dans [PERFORMANCE.md](PERFORMANCE.md#résultats-du-package-final).

## Procédure reproductible

La cible Mac préparée est Unreal Engine 5.8, Apple Silicon, configuration Development. La machine de travail est un Mac Apple M5 avec 24 Go ; la machine et les réglages effectivement mesurés doivent être rappelés dans le résultat final.

Les journaux des profils avant et après précache confirment le profil fluide suivant :

```text
Graphics applied scale=70.0 AA=2 shadows=1 GI=0 reflections=0 foliage=1 wind=1 effects=1 blur=0 fps=60 vsync=1 DLSS=0
```

Il utilise une **résolution interne de 70 %**, TAA, ombres faibles, éclairage ambiant, reflets désactivés, végétation clairsemée et vent actif, effets moyens, flou désactivé, limite de 60 images/s et VSync active. **1440 × 900 et 2560 × 1664 désignent les fenêtres de sortie, pas un rendu interne natif à ces définitions.** Ce sont les réglages réellement chargés dans les huit profils réussis.

```sh
Tools/build_mac.command --package
python3 Tools/test_portable.py --log Saved/Validation/v030/portable-tests.log
python3 Tools/verify_mac.py --profile --soak
python3 Tools/install_mac.py --validation "Saved/Validation/v0.30.0/DOSSIER-DE-LA-SERIE/results.json"
```

Le chemin de la dernière commande est celui annoncé par le vérificateur. Un dossier daté est créé pour chaque série sous `Saved/Validation/v0.30.0/`. Les processus de jeu démarrent depuis `/tmp`, indépendamment du répertoire courant du développeur. Les journaux d'origine sont sous le conteneur utilisateur Mac, dans `Documents/VroomVerification`, puis copiés dans la série du clone avec les PNG, CSV et le JSON récapitulatif.

Ne pas lancer de compilation, de génération de ressources ou de capture supplémentaire pendant les profils de cadence. Un build seul ne remplace pas le package actif du lanceur.

Le vérificateur contrôle l'activation des fenêtres par PID avec `Tools/activate_mac.m` et conserve le résultat `foreground` dans le rapport. Un premier essai de diagnostic a indiqué `foreground_before=1 foreground_after=1`, donc une application déjà active. Ce contrôle supplémentaire n'établit pas que l'arrière-plan ou App Nap était la cause des pics résiduels.

La série intermédiaire `20260913-023133-package` a passé ses 22 scénarios, puis s'est arrêtée sur l'activation de `perf-1440` alors que macOS était verrouillé (`locked=1`, `onConsole=1`, premier plan occupé par `loginwindow`). Elle reste incomplète. Le vérificateur enregistre maintenant `display_mode=onscreen_locked` lorsque ce verrouillage est constaté, et peut poursuivre les mesures automatiques en fenêtre, avec les seuils FPS et mouvement inchangés. Les autres états sont `onscreen_active` et `onscreen_unconfirmed` ; une activation non confirmée sans verrouillage prouvé reste en échec. **La série finale utilise `onscreen_locked` et conserve `interaction_verified=false`** : les profils automatisés ne constituent pas un essai interactif, et un profil sous verrouillage ne prouve pas la présentation visible au joueur.

## Scénarios du vérificateur

`Tools/verify_mac.py` applique les cas suivants au package, avec `-VroomVerify` :

| Cas | Ce que le programme contrôle |
|---|---|
| `progress-data` | Sauvegarde et relecture isolées des records, historique, échantillons du fantôme, interpolation du delta, rejet d'un format invalide et copie précédente ; le processus doit aussi se fermer normalement |
| `menu-flow` | Rectangles des menus aux trois formats et dans les trois modes, actions réelles démarrer/pause/reprise/menu et ouvrir/fermer les graphismes, échange et rejet d'attributions de touches, bornes des préférences et annulation |
| `pit-service` | Service complet avec accélérateur maintenu, immobilisation pendant le service, restauration des consommables, reprise et poursuite du chrono/adversaires |
| `drift-feedback` | Scénario de glisse et présence du résultat `Drift feedback PASS`, avec capture rapprochée dans le temps du retour visuel |
| `collision-slow`, `collision-crash`, `collision-tyre`, `collision-scenery` | Compteurs de contact, casse/saut et remise en piste, dont un choc contre un élément solide du décor |
| Six poses cockpit | Neutre, droite, gauche, petit braquage, braquage moyen et vue rapprochée de contrôle |
| `neutral-16x9`, `cars`, `options` | Neutre en 1600 × 900, monoplaces et panneau graphique |
| `session-qualifying`, `session-results`, `session-arcade`, `session-pit`, `session-controls` | États préparés pour vérifier la présentation des qualifications, résultats, objectif Arcade, stands et commandes |

Les états `session-*` sont synthétiques : leur présence ne prouve pas qu'une course complète a été jouée. Les auto-tests de menus passent par le code des actions et des zones, sans constituer une manipulation physique de la souris ou d'une manette.

Chaque processus doit annoncer la bonne version, sortir avec le code zéro et fournir ses compteurs attendus. Un message `PASS` suivi d'un crash à la fermeture échoue donc à la vérification. Le script rejette les erreurs de compilation de matériaux qu'il reconnaît. Les captures doivent avoir les dimensions demandées.

Le cas `options` ouvre le panneau par la même action que le menu, une seule fois après création effective du circuit. Le script exige `Graphics preview PASS`, émis uniquement si l'état graphique est ouvert. La revue de `options.png` doit en plus confirmer qu'il montre les lignes de qualité et les boutons Appliquer/Annuler, plutôt que le menu de départ. Les captures de jeu ne sont demandées qu'après la fin de la préparation du circuit ; ce report ne change pas le début des enregistrements CSV.

Les images emploient `-RenderOffScreen -MetalOffscreenOnly`. Elles permettent d'inspecter le rendu, **sans mesurer sa présentation à l'écran**. Les profils de cadence utilisent le rendu en fenêtre, avec l'état d'activation ou de verrouillage consigné ci-dessus, et ne produisent pas de PNG pendant la mesure.

## Cadence et continuité du mouvement

La commande `--profile --soak` ajoute huit profils CSV : deux de 1 800 images en **1440 × 900** et **2560 × 1664**, puis six de 7 800 images en **1440 × 900**, un par circuit. Une trace distincte de 25 secondes contrôle également le mouvement. Chaque CSV commence à l'image zéro et exclut exactement ses 120 premières images ; les lignes de métadonnées ne sont pas comptées comme des images. Même si la préparation du circuit dure davantage, les images au-delà de cette exclusion restent analysées. Aucun seuil, nombre d'images ou délai d'exclusion n'a été élargi pour le correctif PSO.

Le précache natif collecte les matériaux des composants procéduraux et attend la fin de compilation avant de créer leurs proxies. Le HUD prépare aussi ses primitives Canvas et ses polices sur la vraie cible de sortie, avec une synchronisation du thread RHI. L'écran « PRÉPARATION DU CIRCUIT » bloque la simulation et les entrées jusqu'à disparition des travaux natifs, des travaux des composants et de cette préparation Canvas, suivie de trois images de disponibilité. Le chrono ne progresse pas pendant cette attente et l'état de pause précédent est restauré. Vérifier ce comportement au chargement, au début/changement de course et après fermeture des graphismes, avec les messages `Canvas PSO preparation submitted/ready` dans les journaux.

Chaque profil active `LogPSOHitching Verbose` afin d'identifier les ressources, passes et statuts de précache concernés par une création lente. Les messages `Circuit preparation started/ready` et leur durée accompagnent les compteurs CSV `PSO/GraphicsPSOHitch`, `PSO/GraphicsPSOHitchTime` et les attentes des threads lorsqu'ils sont présents. Ils servent au diagnostic ; aucun seuil supplémentaire de zéro création PSO n'est imposé. La réussite reste conditionnée par tous les critères de cadence et de continuité ci-dessous.

Les parcours longs utilisent `-VroomTourDemo -VroomMotionTrace`. Ils exigent le marqueur d'un tour complet de chaque circuit. Cette conduite assistée ignore les contacts : elle couvre le tracé, le rendu et la continuité de caméra ; les collisions sont vérifiées séparément.

Les seuils de publication proviennent de [Tools/regression_gates.py](../Tools/regression_gates.py) :

| Mesure | Condition exigée |
|---|---:|
| Nombre de lignes d'images du CSV | Exactement 1 800 ou 7 800 selon le cas |
| Images invalides après préparation | Aucune |
| Cadence moyenne | Au moins 59,0 images/s |
| 99e percentile du temps d'image | Au plus 18,0 ms |
| Plus grande durée d'image | Au plus 33,5 ms |
| Part d'images dépassant 20 ms | Au plus 0,5 % |
| Trace de mouvement courte | Au moins 1 000 échantillons analysés et valides |
| Trace de chaque parcours long | Au moins 7 000 échantillons analysés et valides |
| Retour de distance affichée sur les parcours assistés | Aucun |
| Plus grande variation de lacet entre deux images | Au plus 3,0° |
| Plus grande accélération de lacet absolue | Au plus 300°/s² |

Ce sont des **critères à satisfaire**, pas des mesures obtenues. Ils sont destinés au profil Mac visant 60 Hz. Une moyenne seule ne suffit pas : percentiles, maxima, excursions au-delà de 20 ms et continuité sont évalués séparément. La procédure ne relaxe pas silencieusement les seuils après un échec.

La série finale analyse **49 440 images**, dont trois au-delà de 20 ms, sans donnée d'image invalide. Les moyennes vont de **59,9831 à 59,9870 images/s**. La plus grande accélération de lacet des six tours est **219,37°/s²**, sous la limite de 300°/s² ; aucune trace ne présente de donnée analysée invalide ni de recul de distance. [PERFORMANCE.md](PERFORMANCE.md#résultats-du-package-final) donne les huit lignes de résultats, les noms de CSV, les sept traces et leurs limites.

## Points de contrôle visuels de la 0.30

Le cockpit de référence et les ajustements de prise sont décrits dans [COCKPIT_AMIGA.md](COCKPIT_AMIGA.md). Il faut inspecter le contact des doigts avec la jante, le sens des mains et leurs transitions ; les tests numériques ne certifient pas seuls leur apparence. Les vues cockpit normales restent distinctes de `inspect-neutral`, vue technique rapprochée qui ne représente pas le cadrage de jeu. Le bandeau est haut de 84 pixels dans l'espace de référence 1440 × 900. Ses informations changent selon le mode : dépassements Arcade, positions Grand Prix, objectif de qualification et référence Chrono.

Les roues montrent une enveloppe de pneu fermée, sans jante ni moyeu visibles à travers la bande de roulement dans les vues normales contrôlées. Le correctif du sens des faces passe les quatre tests de [car_wheel_surfaces_test.py](../Tests/car_wheel_surfaces_test.py) ; les captures normales neutre, droite et gauche du package final conservent les petits rétroviseurs bas devant les pneus. Le panneau `options.png` présente bien la page Graphismes avec ses réglages et Appliquer/Annuler. Les mains restent placées sur la jante et suivent son sens de rotation dans les poses contrôlées ; des images fixes ne certifient pas tous les instants d'une animation.

Le résultat Grand Prix comporte un classement de treize pilotes dans un panneau de 1 060 × 570 pixels. Vérifier sa lisibilité, les points et les boutons sur les formats testés. Les aides de service doivent être réservées aux modes qui autorisent les stands. Le fantôme, les traces de pneus, la fumée, le son stéréo et les décors demandent eux aussi une observation adaptée : leur existence dans le code ne garantit pas à elle seule leur qualité perceptive.

## Fermeture et lancement Mac

Les lanceurs et le vérificateur passent `-LLM` afin de conserver le **Low Level Memory Tracker d'Unreal** actif pendant l'initialisation Cocoa. Ce réglage n'active aucun modèle de langage ni service réseau. Un lancement direct du binaire avec d'autres arguments n'est pas automatiquement couvert par les essais du lanceur.

Une série de développement de la 0.30 a révélé une fermeture anormale après le `PASS` du test de progression : le rapport macOS identifiait la destruction tardive d'une `TSharedFuture` renvoyant un événement dans un pool déjà détruit. Le code libère maintenant la future après son attente dans `OnPreExit`. **Le scénario `progress-data` du package final réussit et sort avec le code zéro**, comme les 30 autres cas ; la validation inclut donc la fermeture du processus, pas seulement le succès du test interne.

## Installation liée au rapport

Le vérificateur enregistre les SHA-256 du binaire et des fichiers cuisinés, puis vérifie qu'ils n'ont pas changé pendant la série. `completed` ne devient vrai qu'après la fin de tous les contrôles demandés.

`Tools/install_mac.py` refuse un rapport d'éditeur, incomplet ou en échec, ainsi qu'un package différent du rapport. Il réévalue les seuils, exige au moins huit profils, les six circuits complets, la trace finale de mouvement, les scénarios requis et leurs sorties réussies. La commande complète ci-dessus fournit les deux profils de définition et les six profils de circuit. Les empreintes des données doivent aussi correspondre.

Les versions installées restent dans `~/Library/Application Support/Vroom/Releases/`. Le manifeste `active.json` désigne celle à lancer ; `~/Applications/Vroom.app` et `~/Desktop/Vroom.command` constituent les entrées stables. Les anciennes versions sont conservées. Cette installation locale n'est pas une notarisation de distribution.

La version finale est installée dans **`~/Library/Application Support/Vroom/Releases/0.30.0-9dc508f3fb51/Vroom.app`**. `Saved/Validation/v030/installation-final.log` confirme la mise à jour du lanceur et du manifeste. `Saved/Validation/v030/installed-launch-checks.json` confirme les deux démarrages, natif et Bureau : lanceur sorti à zéro, `Version 0.30`, `Verification PASS` et fermeture normale `LogExit: Exiting.` dans les deux cas. Ces lancements vérifient l'installation et l'arrêt ; ils ne constituent pas une session interactive de course.

## Limites

Le build Windows, le rendu RTX et l'activation du plugin DLSS restent non validés sur le matériel cible. Aucun essai Mac ne les remplace. Le DLSS n'est pas disponible sur Metal.

Les contrôles de menus ne prouvent pas la compatibilité matérielle d'une manette. Les parcours de deux minutes et plus ne garantissent pas des sessions de plusieurs heures, le démarrage avec tous les caches froids, les autres réglages graphiques ou la 4K. Une comparaison exécutée de bout en bout dans Vroom Amiga reste distincte de cette validation technique du remake.
