# Jouer à Vroom 0.35

Cette version propose trois formats de course, des commandes configurables et des records locaux. Les règles ajoutées sont des adaptations du prototype inspiré de Vroom ; elles ne constituent pas une restitution intégrale des règles Amiga.

## Lancer la version installée

Sur Mac, **`~/Applications/Vroom.app`**, **`~/Desktop/Vroom.app`** et **`~/Desktop/Vroom.command`** ouvrent tous la **0.35**. Le lanceur vérifie l’identité du binaire et son rapport. Compiler une nouvelle version ne remplace pas automatiquement celle à laquelle on joue.

L’ancienne 0.34 et les anciens lanceurs restent conservés pour retour arrière ; la procédure se trouve dans [le bilan de livraison](FINALISATION_035.md#installation-et-retour-arrière). Les essais de la 0.35 sont fonctionnels et ciblés, distincts des anciennes mesures de cadence.

Le menu se parcourt avec la souris, les flèches ou la croix directionnelle d'une manette. Entrée ou **A** valide l'élément souligné ; Échap ou **B** revient en arrière. Les noms A/B/X/Y employés ici suivent la disposition Xbox reconnue par Unreal. Les correspondances et la détection d'une manette physique restent à vérifier sur le matériel utilisé ; la présence de ces commandes dans le programme ne signifie pas qu'un essai complet avec chaque manette a été réalisé.

Choisir un mode, une difficulté et un circuit, puis **Rouler**. Les touches F1 à F6 sélectionnent directement les six circuits ; sur certains claviers Mac, maintenir `fn` pour accéder aux touches de fonction. Les pages **Graphismes** et **Commandes et son** sont accessibles depuis le menu. En course, les raccourcis correspondants sont O/F7 et F8.

L'écran **PRÉPARATION DU CIRCUIT** peut apparaître au chargement, au début d'une séance et après les graphismes. Le jeu prépare alors le rendu de la piste et de l'interface ; sa durée dépend des travaux à terminer. Le chrono reste arrêté et les commandes attendent la fin de cette préparation.

Sur le PC, le raccourci bureau **Vroom** lance maintenant la **0.35** via son exécutable natif. L’ancienne 0.34 et son raccourci sauvegardé restent disponibles pour retour arrière. Le dossier de jeu conserve ses fichiers ; les sauvegardes utilisateur restent séparées.

## Les trois modes

| Mode | Déroulement |
|---|---|
| Arcade | Terminer trois tours avant la limite de temps et atteindre l'objectif de dépassements. La difficulté demande 18, 27 ou 36 dépassements et change le temps disponible. Un succès ouvre l'étape suivante. |
| Grand Prix | Effectuer un tour de qualification en solo, puis disputer trois tours contre douze adversaires. Le résultat de qualification détermine la grille ; un tour invalidé place le joueur au fond. Les six premières places rapportent 10, 6, 4, 3, 2 et 1 point. |
| Chrono | Rouler seul, sans nombre de tours imposé. Les tours propres complets alimentent les records et le meilleur tour devient une référence rejouée par le fantôme. |

Les qualifications partent à l'arrêt. Le classement de la grille est calculé par rapport à une estimation du rythme des adversaires ; il ne résulte pas d'une séance où les douze autres pilotes roulent chacun leur tour chronométré.

L'enchaînement des étapes est **Japon → Autriche → France → Royaume-Uni → Suède → USA**. Pour parcourir les six dans cet ordre, choisir le Japon au départ. Choisir directement un autre circuit permet de commencer à cette étape. Les résultats proposent la course après les qualifications, l'étape suivante lorsqu'elle est disponible, un redémarrage ou le retour au menu.

Après chaque course Grand Prix, un tableau présente les treize pilotes : **VOUS** et les concurrents **F1-01 à F1-12**, leur place dans la course, leurs points et leurs victoires. Le classement général départage les égalités de points par le nombre de victoires, puis les places suivantes ; une égalité complète utilise l'identifiant du pilote. À la sixième étape, le vainqueur du championnat est indiqué. Recommencer une course retire exactement son attribution précédente de points et de places avant de la rejouer.

Les concurrents ayant déjà franchi l'arrivée sont classés par leur temps d'arrivée. **Ceux qui roulent encore au moment de l'arrivée du joueur sont classés selon leur avancement à cet instant.** C'est une approximation explicite : la course des adversaires n'est pas simulée jusqu'au dernier passage après l'arrivée du joueur.

Les points du championnat et son étape courante appartiennent à la séance en cours : ils ne sont pas une carrière sauvegardée. Les records de tours, eux, sont conservés sur disque.

## Conduire au clavier

Ces affectations sont les valeurs initiales. **Commandes et son** permet de les modifier.

| Touche | Action |
|---|---|
| Haut, Z ou W | Accélérer |
| Bas, S ou Espace | Freiner |
| Gauche/droite, Q/D ou A/D | Braquer |
| Maj gauche/droite | Entrer en glisse avec le braquage, maintenir en dosant le contre-braquage, relâcher pour le bonus gagné |
| E / X | Monter / descendre d'un rapport en boîte manuelle |
| G | Basculer boîte automatique/manuelle |
| C | Changer entre cockpit et poursuite |
| M | Activer ou désactiver la conduite à la souris |
| P | Pause / reprise |
| R | Recommencer la séance ; en Grand Prix après qualification, reprendre la course depuis sa grille |
| Échap | Menu |
| O ou F7 | Graphismes |
| F8 | Commandes et son |
| T / F | Demander les pneus / le plein, à l'arrêt dans l’allée des stands |
| Entrée | Valider au menu ; demander le service complet dans l’allée des stands |
| F1…F6 | Sélectionner Autriche, France, USA, Suède, Japon ou Royaume-Uni |
| Page suivante / précédente | Circuit suivant / précédent au menu |
| Tab / H | Mode / difficulté suivante au menu |
| F10 | Alterner le FOV préféré, sa variante −12° et sa variante +12° |

Les variantes F10 sont temporaires ; le FOV de base se règle et se sauvegarde entre **60° et 105°** dans **Commandes et son**. Les variantes de −12° et +12° sont bornées entre **55° et 110°**. Le cockpit 0.34 suit la photo F1 fournie comme nouvelle référence : position des yeux plus basse, carrosserie rouge et vrais pneus visibles, volant classique compact à trois branches, sans halo, et **gants blancs à neuf heures et trois heures**. Les rétroviseurs allongés reposent sur des bras rouges coudés, physiquement derrière les pneus. Le tableau de bord reste fixe derrière le volant pendant le braquage. Le cadrage de référence conserve un FOV de 68° et les yeux en position basse. La prise du pouce autour du cercle approuvée par David est celle du package validé et installé.

## Souris et manette

En conduite souris, le déplacement horizontal règle une position de volant conservée. Faire le mouvement inverse le recentre ; couper puis réactiver M remet la commande au neutre. **Bouton gauche : accélérateur ; bouton droit : frein.** La molette monte ou descend les rapports en boîte manuelle. Le bouton central met en pause ou reprend. Les boutons du menu restent cliquables.

| Manette — noms Xbox | Action |
|---|---|
| Stick gauche | Direction |
| Gâchette droite / gauche | Accélérateur / frein |
| LB ou RB | Entrer en dérapage avec le stick, maintenir en contre-braquant, relâcher pour le bonus gagné |
| Y / X | Rapport supérieur / inférieur |
| Start | Pause ; appliquer les préférences dans Commandes et son |
| Back / Select | Changer de vue |
| Croix directionnelle | Parcourir les menus ; changer une option avec gauche/droite |
| A / B | Valider / revenir ; A demande le service complet lorsque la voiture est arrêtée dans l’allée des stands |

Le remappage proposé concerne les touches du clavier. Les raccourcis manette restent fixes. Pour ouvrir les réglages sans clavier, mettre en pause puis choisir **Commandes et son** ; les graphismes sont accessibles depuis le menu principal.

## Réglages et attribution des touches

**Commandes et son** contient la sensibilité du stick, sa zone morte, la sensibilité souris, le champ de vision, la vue préférée, le type de boîte, les volumes général/moteur/effets et l'affichage du fantôme. Les valeurs sont modifiées dans une copie temporaire : **Appliquer** les enregistre, **Annuler** les abandonne. La séance retrouve son état de pause précédent.

La liste comporte plusieurs pages. Les flèches ou la croix directionnelle changent de ligne ; les boutons de page et Page précédente/suivante permettent de passer d'une page à l'autre. Pour attribuer une touche, sélectionner une commande, appuyer sur Entrée puis sur la nouvelle touche. Échap annule la saisie. Si la touche est déjà affectée, les deux commandes échangent leurs touches. Les alias ZQSD ou flèches ne restent pas actifs lorsqu'une de ces touches a été attribuée à une autre action.

Dans la page des commandes, Entrée lance la saisie d'une touche lorsque la sélection est une attribution ; le bouton **Appliquer** ou Start sur manette enregistre l'ensemble. Dans la page graphique, Entrée/A applique directement et Échap/B annule.

## Choisir la qualité graphique

**Graphismes** propose Performance, Équilibré, Élevé, Ultra et le nouveau **Photoréalisme**. Performance conserve l’objectif de **60 images/s**. Photoréalisme privilégie l’éclairage Lumen, les reflets et les ombres proches des arbres, avec TSR à 80 % et une cible de **30 images/s sur Mac**. Revenir à un autre profil rétablit le plafond normal de 60 images/s.

Le package **0.34 conservé** a réussi les huit mesures en Photoréalisme, couvrant les sorties 1440 × 900 et 2560 × 1664 et les six circuits complets : **29,996–30,000 images/s**, avec un maximum de **39,573 ms par image**, après préparation. Ces résultats à 30 images/s ne valident pas la cible de 60 images/s de Performance sur ce package. Le nom Photoréalisme ne garantit pas une scène entièrement photoréaliste. Choisir le profil selon la priorité souhaitée entre précision de l’éclairage et cadence de conduite ; le [bilan 0.34](FINALISATION_034.md) conserve les mesures et les échecs intermédiaires.

## Déraper, passer au stand et récupérer après un choc

À plus d’environ **44 km/h** sur la route, appuyer sur **Maj** ou **LB/RB** en braquant dans le sens du virage. Garder le bouton enfoncé, puis **contre-braquer modérément** pour tenir la glisse. Au clavier, la direction opposée peut maintenant être maintenue ; des corrections dosées chargent plus vite. À la manette, doser le stick. Le retour à l’écran indique la charge réellement gagnée par ce contrôle.

La glisse gagne maintenant **trois niveaux**, visibles sur la barre et dans les étincelles :

| Niveau | Couleur | Turbo normal | Avec lâcher parfait |
|---|---|---:|---:|
| I | Bleu | 0,55 s | 0,75 s |
| II | Ambre | 1,15 s | 1,35 s |
| III | Violet | 1,70 s | 1,90 s |

Relâcher **Maj ou LB/RB** libère le turbo gagné. À chaque nouveau niveau, l’indication **« RELÂCHE MAINTENANT : PARFAIT ! »** apparaît brièvement : relâcher pendant ce signal donne **0,20 s de poussée supplémentaire**. Continuer à tenir une glisse propre permet de viser le niveau suivant ; manquer le signal conserve le turbo normal. Les trois niveaux peuvent être obtenus au clavier en dosant les pressions, sans manette analogique.

Un bon contre-braquage resserre la trajectoire pour garder les longues glisses sur la piste. Les étincelles et les sons distinguent les niveaux ; la poussée arrive progressivement et s’efface en douceur. Le retour visuel n’ajoute aucune secousse de caméra ni variation brutale du champ de vision.

Relâcher trop tôt ne donne rien. Tenir toujours la direction d’entrée ou rester trop longtemps à fond dans la direction opposée fait perdre le contrôle et la charge. Le frein ne provoque pas automatiquement la glisse : il l’annule et coupe une poussée en cours. Un contact, l’herbe, un saut physique, une panne sèche ou une vitesse insuffisante annulent aussi la charge. Après une annulation, relâcher le bouton avant de tenter une nouvelle glisse ; un turbo actif doit se terminer avant d’en amorcer une autre. Ce mécanisme est une adaptation ludique du prototype.

Un petit frottement de roues avec un concurrent peut faire bondir brièvement la voiture. Corriger dans le sens opposé à la poussée reçue aide à la poser : si le choc vous pousse à gauche, corriger à droite, et inversement. Une réception bien corrigée conserve **100 %** de la vitesse d’arrivée, une commande neutre **97 %**, une mauvaise correction **94 %**. Ce rattrapage ne donne aucun boost et ne protège pas d’un second choc sévère.

Les pneus s'usent et l'essence baisse. Le bandeau avertit lorsque les réserves deviennent faibles. **Les stands se trouvent à droite après la ligne chronométrée.** Entrer dans l'allée, puis s’arrêter dans l’allée, près des garages ; la petite case n’est plus obligatoire. L'aide de service apparaît quand la voiture est correctement placée et presque immobile.

- Pneus : T, immobilisation de 4 secondes.
- Essence : F, immobilisation de 3 secondes.
- Les deux : Entrée/A, ou clic gauche lorsque la conduite souris est activée, immobilisation de 7 secondes.

Pendant le service, la voiture reste immobilisée et les adversaires ainsi que le chronomètre continuent. Ces services concernent l'Arcade et la course Grand Prix ; ils ne sont pas proposés pendant les qualifications ou le Chrono.

La piste peut être quittée. Le hors-piste ralentit la voiture ; les éléments solides du décor et les concurrents peuvent provoquer un choc. Après une casse, les commandes sont bloquées pendant l'immobilisation puis la remise en piste progressive. La voiture repart à l'arrêt. Une panne sèche et l'immobilisation provoquent une fin de partie en Arcade ou un abandon en Grand Prix.

## Records, delta et fantôme

Le menu affiche le record du circuit pour le mode sélectionné et les trois derniers temps disponibles. Dix tours propres sont conservés par circuit et par mode. Un tour avec sortie de piste ou contact ne remplace pas un record. Un premier tour raccourci parce que la grille se trouve après la ligne n'est pas accepté comme tour complet.

En Chrono, le meilleur tour propre est enregistré puis rejoué par une voiture transparente, sans collisions. Elle suit des positions et orientations enregistrées pendant le tour, interpolées entre les échantillons. Le fantôme est masqué lorsqu'il est trop proche du joueur pour ne pas recouvrir la vue cockpit. Il peut être désactivé dans les préférences ; cette option ne supprime pas le record. Aucun fantôme n'apparaît avant qu'un premier tour exploitable ait été enregistré.

Le delta compare le temps courant au temps de référence à la même distance sur le circuit : **négatif signifie de l'avance, positif du retard**. Un delta n'est affiché que lorsqu'une référence existe à cet endroit et que le tour courant reste propre.

Les fichiers sont locaux et ne sont pas transférés à GitHub :

| Donnée | Emplacement relatif au dossier `Saved` du jeu |
|---|---|
| Records et fantômes | `Player/records-v1.json` |
| Copie de sauvegarde précédente | `Player/records-v1.json.bak` |
| Préférences | `Config/Mac/GameUserSettings.ini`, sections `Vroom.Player` et `Vroom.Graphics` sur le package Mac |

Sur le package Mac actuel, le dossier utilisateur `Saved` se trouve sous `~/Library/Containers/com.YourCompany.Vroom/Data/Library/Application Support/Epic/Vroom/Saved/`. Il est distinct du dossier `Saved` du clone de développement. Les écritures de records sont effectuées en arrière-plan puis terminées à la fermeture ; les parcours automatisés ordinaires n'écrivent pas de records personnels.

## Installer une nouvelle version Mac après ses contrôles

Depuis la racine du dépôt :

```sh
Tools/build_mac.command --package
python3 Tools/test_portable.py
python3 Tools/verify_mac.py --visible --graphics-preset photorealism --profile --soak
python3 Tools/install_mac.py --validation "CHEMIN-ANNONCE-PAR-LE-VERIFICATEUR/results.json"
```

Remplacer le chemin de la dernière commande par celui annoncé par le vérificateur. Ce n'est pas un fichier fixe : chaque série crée son propre dossier daté. `--visible` affiche et active la fenêtre pour chaque scénario ; `--graphics-preset photorealism` applique temporairement le profil Photoréalisme à 30 images/s sur Mac, sans changer les préférences sauvegardées. Pour vérifier séparément la cible de 60 images/s, choisir `--graphics-preset performance`. `--profile` mesure les deux définitions et `--soak` parcourt les six circuits complets. L'installation exige le rapport réussi correspondant au binaire et aux données cuisinées ; elle refuse un package incomplet ou différent.

L'application de lancement stable est `~/Applications/Vroom.app`. Le manifeste actif est `~/Library/Application Support/Vroom/active.json` ; les versions installées et leurs rapports restent dans `~/Library/Application Support/Vroom/Releases/`. Les anciennes versions sont conservées pour un retour arrière. Une compilation seule laisse la version active inchangée.

## Périmètre de validation

Les tests portables ciblés de conduite et de contre-braquage **0.34 passent**, avec les trois niveaux, le lâcher parfait, les annulations et 126 combinaisons de glisse experte. Ils vérifient les règles du modèle, pas la fluidité affichée ni le placement visuel des mains.

La série **0.33 du 19 septembre 2026** a réussi ses 45 contrôles visibles, huit profils et six parcours complets en profil Performance à 70 % interne. Le [rapport 0.33](FINALISATION_033.md) conserve ces preuves ; elles portent sur le package précédent.

La **prise du pouce de la 0.34 est approuvée visuellement par David**, avec le volant circulaire, les gants blancs et le cadrage conservés. Les **31 suites portables passent**, dont les 10 contrôles des gants ; l’audit de séparation avec la jante passe également. La série visible `20260919-231125-package` termine avec **47/47 contrôles PASS**, dont les huit mesures et les six parcours complets. Les vues [neutre](images/cockpit-034-neutral.png), [gauche](images/cockpit-034-left.png) et [droite](images/cockpit-034-right.png) proviennent de cette série. La release **`0.34.0-c8a4441fbfd4` est installée** : les empreintes du binaire, des données et du rapport correspondent aux éléments validés, et la 0.33 reste disponible pour retour arrière. Le [rapport](validation-results.json) et le [bilan 0.34](FINALISATION_034.md) conservent ces preuves séparément des essais précédents. L’état Git est consultable dans le [dépôt](https://github.com/M4D135/vroom-unreal).

Les mesures historiques du package Mac 0.30, datées du **13 septembre 2026**, restent disponibles : 31 cas, huit profils, six circuits complets et 21 contrôles portables. Le profil Performance à 70 % interne atteignait environ 59,98–59,99 images/s sur le Mac Apple M5 24 Go, avec un maximum mesuré de 22,83 ms par image. Ces mesures sous verrouillage du Mac ne certifient ni la version 0.33 ni une session de jeu interactive.

Les essais et mesures réellement effectués sont consignés dans [VALIDATION.md](VALIDATION.md). Une vérification d'actions et de rectangles ne remplace pas un essai matériel de manette ou une session de jeu complète avec chaque périphérique. **La 0.35 Windows passe onze contrôles natifs visibles sur RTX 5060 Laptop ; l’activation DLSS reste non validée pour cette version.** Le Mac utilise Metal et ne propose pas le DLSS. Les circuits et leurs décors restent des interprétations ; le prototype ne revendique pas une copie intégrale de Vroom.

## Carrosserie et contacts — 0.35

Les chocs marquent progressivement l’avant, l’arrière et les côtés. L’aileron avant se déforme ; les adversaires encaissent aussi les coups et une poussée latérale. Un indicateur de carrosserie apparaît en bas à gauche après un dommage. La voiture peut tirer légèrement du côté touché et perdre de l’adhérence, tout en restant conduisible. Après un gros accident, la remise en piste apporte une réparation de secours ; terminer un service au stand remet la carrosserie à neuf.

Le drift de la 0.35 se déclenche uniquement dans une vraie courbe, avec un braquage dans son sens. Une ligne droite ne déclenche ni bond de drift ni charge de turbo. À la sortie du virage, une fenêtre de 0,18 s permet de relâcher et de conserver le bonus gagné ; maintenir le bouton au-delà interrompt la glisse sans turbo automatique.
