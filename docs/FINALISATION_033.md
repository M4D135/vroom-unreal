# Finalisation 0.33 — conduite, course, terrain et installation

Bilan du **19 septembre 2026** : package 0.33 construit, 30 suites portables et 45 contrôles visibles réussis. Ce document décrit les corrections, leurs preuves et les limites conservées. Le rapport final identifie l’exécutable et toutes ses données par SHA-256 ; les résultats historiques de la 0.30 restent archivés séparément.

## Base consolidée et déplacement du projet

Le dépôt actif est désormais `/Users/david/Downloads/vroom-unreal`. L'ancien chemin `/Users/david/Documents/Codex/2026-09-05/su/work/vroom-unreal` n'est plus le répertoire de travail.

Le 19 septembre, les modifications locales du cockpit et du HUD ont été préservées dans `7dd9857` sur `backup/local-cockpit-20260919`, puis réunies avec `main` dans `84c3a30`. Cette base conserve le travail de gameplay 0.31/0.32 et la présentation locale du cockpit. Les corrections 0.33 s'appliquent à cette consolidation ; elles ne consistent pas à repartir de la 0.30 historique. Les sources, les données de génération et les nouveaux assets CC0 sont réunis dans le commit de finalisation 0.33 ; la publication suit la validation du package installé.

L'audit initial avait identifié deux circuits de lancement différents : `Desktop/Vroom.app` pointait directement vers `Binaries/Mac/Vroom.app`, tandis que `Applications/Vroom.app` suivait `active.json`. Le premier exécutait le build du 16 septembre ; le second sélectionnait la release `0.30.1-pneus`, dont le reçu était marqué `forced:true`. Cette ambiguïté empêchait d'associer sans réserve « le jeu lancé » aux tests cités.

## Corrections de conduite et de course

### Redémarrage après une sortie de piste

Le cas signalé montrait une voiture immobile dans l'herbe, avec 83 % de pneus et 98 % d'essence, en courbe. La dérive imposée par la courbure et la perte de direction sur l'herbe étaient trop fortes à très basse vitesse : leur rapport variait peu avec la vitesse, ce qui empêchait de regagner la route.

`DriveModel::CornerLoad()` fait désormais monter progressivement ces deux effets jusqu'à leur valeur précédente à **40 m/s, soit 144 km/h**. Le moteur, la friction de l'herbe et la pénalité de sortie de piste restent actifs. Le retour s'obtient en accélérant et en braquant ; il ne repose pas sur une téléportation ni sur un bonus de vitesse.

`offroad_restart_test.cpp` couvre 360 combinaisons : deux côtés, cinq courbures, quatre états de pneus, trois pentes et trois cadences. La trace `Saved/Validation/v033/offroad-restart-after.log` indique **360 retours réussis, maximum 2,93333 s** avec vitesse modérée pendant le retour. Le test reproduit également le cas à 83 % de pneus en maintenant l'accélérateur à fond. Les trajectoires et le lacet sont comparés à 30/60/120 Hz. Les cas visibles Unreal `offroad-left` et `offroad-right` passent dans la série `20260919-174842-package`, sans contact ni remise en piste forcée.

### Tours comptés, tours propres et records

`LapValidator` distingue deux notions : un petit débordement peut compter pour la distance de course tout en invalidant le chrono ; une coupe importante, un passage manqué ou un mouvement impossible ne doit pas compter comme tour de course. Le HUD utilise `ValidLaps` pour l'objectif des modes limités en tours.

Les observations non finies sont rejetées, une marche arrière progressive ne bénéficie plus d'une tolérance de 2 m à chaque appel, et un saut vers l'avant supérieur à un huitième du circuit invalide le passage. La finalisation du tour intervient après les contacts du même pas physique, pour qu'un choc sur la ligne ne soit pas oublié avant d'enregistrer un meilleur tour.

Le fantôme est amorcé à la ligne réellement franchie, par interpolation entre les deux poses observées, avec temps et distance nuls. Cela remplace la condition « premier échantillon à moins de 2 m », qui pouvait rejeter un tour propre à 30 FPS lorsque le premier échantillon se trouvait déjà plus loin. Le premier tour réellement raccourci par une grille placée après la ligne demeure exclu des records. La validité d'un passage légal aux stands provient du même validateur, sans second test contradictoire de largeur dans le tracker.

Preuves portables : `lap-validity-after.log` et `ghost-lap-after.log` sous `Saved/Validation/v033`. Le scénario Unreal `progress-data` couvre en plus la persistance isolée, la lecture du fantôme, le delta et son nouvel ancrage. Une course complète jouée reste distincte de ces tests.

### Stands et consommables

La surface visible et le corridor autorisé partagent `VroomTrackLayout.h` : entrée **S=31–55 m**, voie de service **55–190 m**, sortie **190–214 m**, centre de voie **12,5 m**, demi-largeur **2,35 m**. Une interpolation continue relie ce centre au bord droit de la route. À la demande de David, le joueur peut demander le service dans une large partie de l'allée : **S=65–180 m, latéral=10,5–14,5 m**, avec `InLane` vrai et une vitesse absolue **inférieure à 0,8 m/s, environ 2,9 km/h**. Le service ne dépend donc plus de l'ancien petit emplacement S=96–108 m. Le HUD invite à s'arrêter dans l'allée des stands ; le marquage central sert de repère et ne représente pas une limite invisible d'activation.

Le service conserve ses durées : pneus 4 s, essence 3 s, les deux 7 s. La course et les autres pilotes continuent. L'autonomie des courses est calibrée selon la distance et la difficulté, à partir de 60 m/s et 78 % d'accélérateur. Les alertes de pneus et d'essence arrivent avant de manquer la seule entrée disponible pour le tour suivant. Les qualifications d'un tour et le Chrono n'utilisent pas ce budget de course nécessitant un arrêt.

Ce calibrage est une adaptation du remake. Les valeurs nominales calculées ne prouvent pas qu'un joueur ou chaque IA effectuera exactement le même nombre d'arrêts : les courses simulées et le package doivent fournir les observations correspondantes.

### Départ et son

Le signal utilise rouge, orange puis vert, lié à l'horloge de course. Mouvement du joueur, progression des adversaires et chrono commencent au même seuil. Le matériau des feux reçoit l'intensité et les trois composantes de couleur par instance ; un seul matériau est réutilisé.

Les bips sont mixés dans le flux stéréo existant à 48 kHz : rouge 880 Hz/0,16 s, orange 1175 Hz/0,20 s, vert 1760 Hz/0,38 s. Une voix préallouée avec fondus courts évite de créer un objet audio à chaque feu. Le tampon PCM réutilisé contient des blocs de 800 trames ; jusqu'à trois blocs sont servis par mise à jour pour limiter la latence et éviter un manque d'audio avec une limite à 30 FPS. Les volumes utilisateur continuent de s'appliquer.

Le harnais contrôle départ normal, redémarrage, pause et son coupé avec les traces `Start integration PASS`, `Start lights` et `Start beep`. Ces marqueurs et les tests du générateur ne remplacent pas une écoute du package, notamment pour vérifier la synchronisation perçue et l'absence de clics.

### Adversaires

Les six personnalités par livrée restent des adaptations explicites : rythme, précision, agressivité, prudence, régularité et gestion des ressources. L'économie de pneus et de carburant passe par l'effort de conduite ; elle ne fournit pas gratuitement des ressources supplémentaires.

L'IA anticipe une entrée aux stands à partir des ressources nécessaires pour le tour suivant, freine avant l'emplacement, attend un box occupé et effectue son service à l'arrêt. Elle conserve sa position latérale au début du service ; la remise à niveau des ressources suit les sept secondes d'immobilisation. La sortie tient compte d'une voiture proche ou approchante avant de rejoindre la piste. Une panne de carburant ne conserve plus une vitesse minimale artificielle.

`race_resources_test.py` simule 324 courses individuelles : six lignes centrales dérivées des vraies données, trois distances (2/3/6 tours), trois difficultés et six personnalités. La courbure y est une approximation portable de `BuildDriveablePreview`, pas le spline Unreal ni une course de treize voitures. `traffic_model_test.cpp` complète cette couverture par les interactions de trafic. Les nombres d'arrêts effectivement obtenus figurent dans le tableau ci-dessous.

## Dérapage maîtrisé et contacts

L’entrée en drift exige le bouton dédié et un braquage à partir de 12,2 m/s. Le frein seul ne déclenche plus automatiquement de glisse. Le sens initial est conservé pendant que le joueur contre-braque : un contrôle dosé charge la poussée, avec une grâce de 0,38 s autour du neutre. Les taps de clavier sont possibles. Un maintien continu dans le même sens ne charge pas ; le plein contre-braquage doit aussi être modulé. Relâcher proprement après 0,42/1 unité donne un boost de 0,55/1,15 s. Contact, sortie de route, frein ou perte de contrôle annulent la récompense. Le HUD indique le sens du contre-braquage et le niveau prêt à être relâché.

Les contacts tiennent compte des voitures arrivant de l’arrière et des rapprochements latéraux, qui étaient auparavant ignorés. Un petit frottement roue contre roue peut provoquer un saut court sans casse ; corriger l’impulsion latérale pendant le vol conserve jusqu’à 100 % de la vitesse à la réception, contre 97 % au neutre et 94 % avec une mauvaise correction. Aucun boost n’est donné par le contact. Les paires de voitures ont un verrou de contact et un délai de réarmement ; les petits sauts ont également un délai global. Une seconde collision violente reste dangereuse pendant le saut.

Les gros chocs conservent leur temps d’immobilisation et leur retour progressif sur la route. Les frottements contre un obstacle repoussent désormais vers l’extérieur ; un contact initial dont la voiture s’éloigne ne peut plus la recoller au pilier. Ce filtre s’applique à chaque obstacle, laissant détecter un autre impact entrant. Les audits statiques conservent la détection de tout chevauchement.

`drift_skill_test.cpp` couvre commandes analogiques et clavier, 30/60/120 Hz, courbes, usure et échecs volontaires sur une piste large de 9,5 m. Les tests de contacts couvrent les deux côtés, l’arrière, le balayage, les réceptions et la prévention des répétitions. Les scénarios visibles `drift-feedback`, `drift-unskilled`, `collision-rub` et `collision-rear` vérifient leur intégration dans Unreal.

## Terrain, route et repères

L'ancien ruban d'herbe s'arrêtait à 24 m de chaque côté de l'axe, environ 2,5 m au-dessus du terrain grossier. Le nouveau dégagement suit le niveau de la route sur les 20 premiers mètres, puis rejoint les triangles du terrain sur 16 m supplémentaires. Les cellules du terrain situées sous la route et les stands sont abaissées pour ne pas traverser le bitume dans le relief. Arbres et tribunes utilisent ce terrain plutôt qu'une formule de hauteur indépendante.

Le bitume reste large de 9,5 m. La route est échantillonnée tous les 2 m au plus ; la voie des stands emploie un maillage continu échantillonné tous les mètres, au lieu de petits cubes horizontaux. Bordure et ligne blanche droites sont interrompues aux accès. Les reliefs et silhouettes régionales sont conservés ; cette intervention corrige les raccords, elle ne prétend pas reconstruire les sites réels.

Quatre repères sont choisis en amont de maxima de courbure, avec espacement minimal de 260 m. Des balises à trois, deux puis une barre annoncent le virage. Un portique marque l'arrivée et un autre la voie des stands, en complément du portique de départ. Les structures emploient les groupes d'instances et matériaux existants, sans police ni texture nouvelle par panneau. Les positions définitives sont imprimées par `VROOM: Landmark track=… distance=… bend=… curvature=…` ; ce placement procédural n'est pas présenté comme un décodage des événements de l'Amiga.

`VerifyTrackGeometry()` analyse les triangles effectivement construits : présence de bitume, écart vertical avec la trajectoire, terrain masquant la route, bordures fermant les accès et balayage du volume de voiture contre les obstacles. `-VroomTrackAudit` et les six cas `track-audit-*` exigent `Track geometry PASS`. Le test portable des raccords et les six audits visibles du package passent : aucune surface manquante, masquée ou bloquée, aucune bordure fermant les stands, écart route/trajectoire maximal de 0,341 cm. La correction suédoise coupe les triangles qui croisent les ouvertures, avec une marge visuelle de 1 m ; le corridor des stands reste inchangé.

### Modèles et matières du décor

Asphalte, pelouse et rocher emploient des ressources PBR existantes CC0. Deux silhouettes de feuillus remplacent leurs anciennes formes, avec des feuilles photographiques détourées et trois niveaux de détail : 880/525/321 et 726/476/278 triangles. Le rocher photogrammétrique utilise 2000/800/240 triangles. Les sources et empreintes sont versionnées ; l’import Unreal a réussi sans erreur ni avertissement. Les conifères et arbres parasols restent les modèles précédents. Ces apports ne rendent pas toute la scène photoréaliste. Voir [ENVIRONMENT_ASSETS.md](ENVIRONMENT_ASSETS.md).

## Cockpit, HUD et éléments existants

À la demande de David le 19 septembre, le cockpit et le générateur de matériau des gants repartent des sources 0.30 (`bce35ea`). Les gants restent blancs/ivoire. Le générateur doré était resté dans le dépôt malgré les précédentes restaurations des assets : cette cause est corrigée, donc une nouvelle génération conserve la version blanche. Aucun changement de caméra ou de position des mains n’est apporté par les nouveaux décors.

Le resserrement demandé ensuite conserve les rotations, l’échelle, la caméra, les 3 028 sommets, les 4 899 faces et les UV du gant. `GripDepth=-2.8` rapproche chaque main de 3,10 mm du centre du volant. Le fit local ajoute une correction maximale de 4,30 mm sur 173 sommets, avec normales recalculées ; le déplacement ponctuel cumulé reste inférieur à 7,1 mm. Les trois tests vérifient les sommets et la conservation de la pose, sans prétendre éliminer toute intersection entre faces et tube. La validation visuelle emploie les poses jouables neutre/gauche/droite ; le cadrage technique `inspect-neutral` est retiré de la série.

Le HUD conserve le bandeau compact avec pneus et essence. Il affiche la progression en tours valides et les alertes fondées sur les ressources nécessaires au prochain passage aux stands. La préparation des états graphiques Canvas est exécutée également au menu, sans y imposer l'écran de préparation de course ; les captures attendent que cette préparation soit terminée.

Les triangles de suspension ne sont pas une nouveauté annoncée pour la 0.33 : le modèle possède déjà des liaisons doubles entre châssis et moyeux. `CarWishbones` fournit huit liaisons ; les tests contrôlent leur nombre, leur direction vers les moyeux et la distinction avant/arrière. Les meshes générés comportent également les bras et biellettes. Cela vérifie des données et leur construction, pas une simulation indépendante de suspension physique ni toute leur apparence en mouvement.

Les références disponibles restent [REFERENCE.md](REFERENCE.md), [DRIVING.md](DRIVING.md), [COCKPIT_AMIGA.md](COCKPIT_AMIGA.md), les sprites extraits dans `reference/amiga/` et `reference-comparison.json`. Les sources ST de juillet 1991 documentent des paramètres ; la vidéo et les sprites Amiga servent au visuel. Aucun nouvel essai en émulateur de l'original n'est revendiqué ici.

## Installation unique et retour arrière

Le package validé a été installé le 19 septembre 2026. Les points d’entrée suivent désormais ce chemin :

```text
~/Desktop/Vroom.app -> ~/Applications/Vroom.app
~/Desktop/Vroom.command -> exécute le même VroomLauncher
VroomLauncher -> ~/Library/Application Support/Vroom/active.json
active.json -> release explicite dans Releases/<version>-<empreinte>/Vroom.app
```

L'option `--force` a été retirée. L'installation refuse un rapport incomplet, forcé, en échec, un cas sans `PASS`, des profils insuffisants ou un package différent de ses empreintes. Dès 0.33.0, les cas hors piste gauche/droite et départ deviennent obligatoires. Les critères des anciennes versions validées restent acceptés pour permettre un retour arrière.

La release active est `0.33.0-e360bd1e33de`, avec le même exécutable SHA-256 que la série finale. Le rapport installé a pour empreinte `1d71b30aaca19f82d17b34f8c4e873d133459efa88b0e7fb68271d98c3c61785`. Les reçus locaux sont `installed-manifest.json`, `installation.log` et `installed-launch-checks.json` sous `Saved/Validation/v033`.

Le manifeste contient aussi le SHA-256 du rapport. Le lanceur refuse un rapport modifié/incomplet et un exécutable différent. Les précédents lanceurs, raccourcis et manifestes sont sauvegardés ; un échec de publication du nouveau manifeste restaure les points d'entrée précédents. Les anciennes releases restent en place. Une application étrangère nommée Vroom.app n'est pas remplacée.

Les tests d'installation utilisent un répertoire utilisateur temporaire et un lanceur factice ; ils ne lancent pas Unreal et ne constituent pas une installation réelle. Le lanceur conserve `-LLM` pour le Low Level Memory Tracker d'Unreal, nécessaire au démarrage Cocoa observé sur ce Mac ; il ne lance aucun modèle de langage.

## Validation et procédure reproductible

Depuis le dépôt actif :

```sh
cd /Users/david/Downloads/vroom-unreal
python3 Tools/test_portable.py --log Saved/Validation/v033/portable-final.log
Tools/build_mac.command --package
python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak
```

Le profil explicite est temporaire : il ne remplace pas les préférences graphiques du joueur. Le rapport et chaque journal enregistrent les réglages réellement appliqués ; une reprise entre profils différents est rejetée.

Le dernier outil annonce un dossier sous `Saved/Validation/v0.33.0/`. Une reprise n'est autorisée que si exécutable, données, paramètres et preuves restent identiques ; conserver les séries échouées et leurs causes.

```sh
python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak --resume Saved/Validation/v0.33.0/SERIE/results.json
python3 Tools/install_mac.py --validation Saved/Validation/v0.33.0/SERIE_VALIDEE/results.json
```

La seconde commande ne doit être exécutée qu'avec la série complète effectivement réussie. Après installation, contrôler que Bureau et Applications résolvent le même lanceur, relever le manifeste et l'empreinte du bundle installé, puis tester son lancement et sa fermeture normale. La compilation seule ne modifie pas la release active.

Les seuils de cadence restent : moyenne ≥59 FPS, P99 ≤18 ms, maximum ≤33,5 ms et ≤0,5 % d'images au-delà de 20 ms, après 120 images exclues. Les limites de mouvement restent 3° de variation et 300°/s² d'accélération de lacet, sans recul de distance dans les parcours assistés. La définition de sortie, l'échelle interne, l'état de verrouillage et l'activation réelle sont à reporter ; une mesure sous verrouillage n'est pas un essai interactif.

### État des preuves

| Contrôle | Preuve et résultat |
|---|---|
| Redémarrage hors piste, modèle | PASS, 360 cas, `Saved/Validation/v033/offroad-restart-after.log` |
| Tours et ancrage du fantôme | PASS, `lap-validity-after.log`, `ghost-lap-after.log` |
| Raccords et sélection des repères, modèle | PASS, `track_layout_test.cpp` ; quatre repères trouvés sur les six données de tracé avec courbure portable approximative |
| Installation et reprise du harnais | 20 tests d’installation et 55 tests de reprise PASS dans la suite finale |
| Première suite portable 0.33 | **24/27**, trois échecs dans trafic, ressources et fixture de reprise ; `Saved/Validation/v033/portable-tests.log` est une preuve intermédiaire, pas une certification |
| Suite portable intermédiaire après restauration du cockpit | **28/28 PASS**, `Saved/Validation/v033/portable-after-visible-and-cockpit-restore.log` |
| Suite portable finale, modèles et nouveau gameplay | **30/30 PASS**, exit 0, `Saved/Validation/v033/portable-final-r2.log` ; dont 55 contrôles de reprise, 20 d’installation, 4 sur les sources du décor, 3 sur les gants |
| Package final | **Build réussi**, exit 0, `Saved/Logs/vroom-033-skill-package-r2.log`. SHA exécutable `a3af96417a81859a643010c80aebcb52ebc4135a44b0d76680c0b69c9df9a35d` ; manifeste complet des données dans le rapport de validation |
| Cas du moteur, cockpit et terrains | **45/45 PASS**, série `20260919-174842-package` ; vues normales neutre/gauche/droite et six captures de terrain inspectées |
| Écoute du départ et conduite manuelle | Les sons sont contrôlés par le modèle et leurs déclenchements synchronisés dans Unreal ; aucune écoute humaine ni séance complète à la manette n’est revendiquée |
| Installation locale | **PASS** : release `0.33.0-e360bd1e33de`, empreintes identiques au package validé, Bureau et Applications résolvent le même lanceur. Panneau graphique, menu, course et fermeture normale contrôlés à l’écran depuis Applications. Le lancement Bureau atteint la préparation complète ; son contrôle visuel final a été interrompu par le verrouillage du Mac. Voir `Saved/Validation/v033/installed-launch-checks.json`. |

### Cadence du package final

Série `Saved/Validation/v0.33.0/20260919-174842-package/results.json` : **completed=true, 45/45 cas PASS, aucun échec, toutes les sorties à zéro**. Elle comprend 36 scénarios fonctionnels/visuels, huit profils et une trace de conduite courte. Les 45 fenêtres sont `onscreen_active` ; les tests sont automatisés et visibles, pas une séance jouée à la main.

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

Machine : **Apple M5, 24 Gio, macOS 26.4**, Unreal Engine 5.8 / Metal / Development. Profil Performance temporaire : échelle interne **70 %**, TAA, ombres faibles, GI/reflets coupés, herbe animée clairsemée, 60 FPS avec VSync. Les sorties 1440 × 900 et 2560 × 1664 ne sont donc pas des rendus internes natifs à ces définitions. Les préférences sauvegardées restent inchangées pendant la mesure.

Les six tours sont complets. Accélération maximale de lacet sur ces parcours : **123,479°/s²**, sous la limite de 300 ; aucune donnée invalide ni recul de distance. La trace courte finale relève **0,568°** de variation maximale par image et **51,094°/s²**, sans recul. Chaque profil exclut les 120 premières images ; aucun seuil n’a été assoupli. Toutes les preuves portent sur le même binaire et les mêmes données.


### Comparaison complémentaire du profil Élevé

Deux diagnostics visibles supplémentaires du même package mesurent le profil Élevé, à 100 % interne avec TSR et Lumen, sur 1 680 images analysées après 120 images exclues. Ils ne remplacent pas les six tours du profil Performance.

| Sortie | FPS moyen | P99 ms | Max ms | Images >20 ms | Résultat |
|---|---:|---:|---:|---:|---|
| 1440 × 900 | 60,00 | 16,930 | 17,155 | 0 | PASS |
| 2560 × 1664 | 25,90 | 47,591 | 56,100 | 1680 | ÉCHEC des seuils 60 FPS |

À haute définition, le temps GPU moyen de 38,49 ms est proche du temps image de 38,61 ms ; le thread de jeu reste à 1,71 ms. Ce diagnostic désigne le coût graphique comme la limite de cette mesure. Le profil Performance a été appliqué dans le panneau du jeu installé pour privilégier la fluidité ; l’ancien fichier de préférences est sauvegardé. Preuve complémentaire : `Saved/Validation/v033/high-profile/results.json`.

### Arrêts observés — simulations portables sur les six circuits

| Distance | Facile | Normal | Expert | Réserve d’essence minimale observée |
|---|---:|---:|---:|---:|
| 2 tours | 1 arrêt | 1 arrêt | 1 arrêt | 19,23 % |
| 3 tours | 2 arrêts | 2 arrêts | 2 arrêts | 29,27 % |
| 6 tours | 1 arrêt | 1 arrêt | 2 arrêts | 17,45 % |

Ces nombres sont observés sur chaque circuit et chacun des six profils, soit 324 courses. La réserve est le minimum des trois difficultés pour cette distance (rapport `Saved/Validation/v033/race-resource-summary.json`). Trois autres courses complètes de douze voitures valident le peloton, les files aux stands et la sortie. Elles emploient le modèle partagé et une approximation portable du tracé ; les audits et parcours Unreal sont rapportés séparément.


## Limites conservées

Le résultat reste une adaptation 3D personnelle inspirée de Vroom Amiga. Les tracés proviennent de données historiques transformées ; les décors, personnalités et règles de ressources sont en partie réinterprétés. Les contrôles Mac/Metal ne valident ni Windows/RTX, ni DLSS, ni toutes les qualités graphiques. Le nom proposé pour sortir du titre historique est **Traject 91** ; son adoption n’a pas encore été choisie par David, et la version installée garde donc son nom actuel. Une moyenne proche de 60 FPS ne suffit pas à déclarer le jeu fluide : les pics, la continuité, l'audio et la conduite effective demandent chacun leur preuve.
