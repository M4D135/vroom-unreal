# Jouer à Vroom 0.30

Cette version propose trois formats de course, des commandes configurables et des records locaux. Les règles ajoutées sont des adaptations du prototype inspiré de Vroom ; elles ne constituent pas une restitution intégrale des règles Amiga.

## Lancer la version installée

Sur le Mac, ouvrir **`~/Applications/Vroom.app`** ou double-cliquer **`~/Desktop/Vroom.command`**. Ces deux entrées utilisent la version explicitement installée après validation. Compiler une nouvelle version ne remplace plus automatiquement celle à laquelle on joue.

Le menu se parcourt avec la souris, les flèches ou la croix directionnelle d'une manette. Entrée ou **A** valide l'élément souligné ; Échap ou **B** revient en arrière. Les noms A/B/X/Y employés ici suivent la disposition Xbox reconnue par Unreal. Les correspondances et la détection d'une manette physique restent à vérifier sur le matériel utilisé ; la présence de ces commandes dans le programme ne signifie pas qu'un essai complet avec chaque manette a été réalisé.

Choisir un mode, une difficulté et un circuit, puis **Rouler**. Les touches F1 à F6 sélectionnent directement les six circuits ; sur certains claviers Mac, maintenir `fn` pour accéder aux touches de fonction. Les pages **Graphismes** et **Commandes et son** sont accessibles depuis le menu. En course, les raccourcis correspondants sont O/F7 et F8.

L'écran **PRÉPARATION DU CIRCUIT** peut apparaître au chargement, au début d'une séance et après les graphismes. Le jeu prépare alors le rendu de la piste et de l'interface ; sa durée dépend des travaux à terminer. Le chrono reste arrêté et les commandes attendent la fin de cette préparation.

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
| Maj gauche/droite | Déraper en maintenant le braquage à vitesse suffisante |
| E / X | Monter / descendre d'un rapport en boîte manuelle |
| G | Basculer boîte automatique/manuelle |
| C | Changer entre cockpit et poursuite |
| M | Activer ou désactiver la conduite à la souris |
| P | Pause / reprise |
| R | Recommencer la séance ; en Grand Prix après qualification, reprendre la course depuis sa grille |
| Échap | Menu |
| O ou F7 | Graphismes |
| F8 | Commandes et son |
| T / F | Demander les pneus / le plein, à l'arrêt dans l'emplacement des stands |
| Entrée | Valider au menu ; demander le service complet dans l'emplacement des stands |
| F1…F6 | Sélectionner Autriche, France, USA, Suède, Japon ou Royaume-Uni |
| Page suivante / précédente | Circuit suivant / précédent au menu |
| Tab / H | Mode / difficulté suivante au menu |
| F10 | Alterner le FOV préféré, sa variante −12° et sa variante +12° |

Les variantes F10 sont temporaires ; le FOV de base se règle et se sauvegarde dans **Commandes et son**. Les ajustements des mains, des roues et des supports de rétroviseurs conservent la position de caméra et la plage de braquage ; les captures normales du package final montrent les pneus fermés et les mains sur la jante au neutre et en braquage. Les bornes finales de caméra sont 55° à 110°.

## Souris et manette

En conduite souris, le déplacement horizontal règle une position de volant conservée. Faire le mouvement inverse le recentre ; couper puis réactiver M remet la commande au neutre. **Bouton gauche : accélérateur ; bouton droit : frein.** La molette monte ou descend les rapports en boîte manuelle. Le bouton central met en pause ou reprend. Les boutons du menu restent cliquables.

| Manette — noms Xbox | Action |
|---|---|
| Stick gauche | Direction |
| Gâchette droite / gauche | Accélérateur / frein |
| LB ou RB | Maintenir le dérapage |
| Y / X | Rapport supérieur / inférieur |
| Start | Pause ; appliquer les préférences dans Commandes et son |
| Back / Select | Changer de vue |
| Croix directionnelle | Parcourir les menus ; changer une option avec gauche/droite |
| A / B | Valider / revenir ; A demande le service complet lorsque la voiture est arrêtée dans son emplacement |

Le remappage proposé concerne les touches du clavier. Les raccourcis manette restent fixes. Pour ouvrir les réglages sans clavier, mettre en pause puis choisir **Commandes et son** ; les graphismes sont accessibles depuis le menu principal.

## Réglages et attribution des touches

**Commandes et son** contient la sensibilité du stick, sa zone morte, la sensibilité souris, le champ de vision, la vue préférée, le type de boîte, les volumes général/moteur/effets et l'affichage du fantôme. Les valeurs sont modifiées dans une copie temporaire : **Appliquer** les enregistre, **Annuler** les abandonne. La séance retrouve son état de pause précédent.

La liste comporte plusieurs pages. Les flèches ou la croix directionnelle changent de ligne ; les boutons de page et Page précédente/suivante permettent de passer d'une page à l'autre. Pour attribuer une touche, sélectionner une commande, appuyer sur Entrée puis sur la nouvelle touche. Échap annule la saisie. Si la touche est déjà affectée, les deux commandes échangent leurs touches. Les alias ZQSD ou flèches ne restent pas actifs lorsqu'une de ces touches a été attribuée à une autre action.

Dans la page des commandes, Entrée lance la saisie d'une touche lorsque la sélection est une attribution ; le bouton **Appliquer** ou Start sur manette enregistre l'ensemble. Dans la page graphique, Entrée/A applique directement et Échap/B annule.

## Déraper, passer au stand et récupérer après un choc

Maintenir Maj ou LB/RB en braquant à plus d'environ 44 km/h sur la route. Frein et braquage permettent aussi d'entrer en glisse. Le retour à l'écran indique la charge ; relâcher volontairement le dérapage après le premier ou le second seuil déclenche une poussée courte. Une sortie de route, un saut ou un ralentissement annule la charge, et le frein coupe la poussée. Ce mécanisme est une adaptation ludique du prototype.

Les pneus s'usent et l'essence baisse. Le bandeau avertit lorsque les réserves deviennent faibles. **Les stands se trouvent à droite après la ligne chronométrée.** Entrer dans l'allée, puis s'arrêter dans l'emplacement marqué. L'aide de service apparaît quand la voiture est correctement placée et presque immobile.

- Pneus : T, immobilisation de 4 secondes.
- Essence : F, immobilisation de 3 secondes.
- Les deux : Entrée/A, ou clic gauche lorsque la conduite souris est activée, immobilisation de 7 secondes.

Pendant le service, la voiture reste immobilisée et les adversaires ainsi que le chronomètre continuent. Ces services concernent l'Arcade et la course Grand Prix ; ils ne sont pas proposés pendant les qualifications ou le Chrono.

La piste peut être quittée. Le hors-piste ralentit la voiture ; les éléments solides du décor et les concurrents peuvent provoquer un choc. Après une casse, les commandes sont bloquées pendant l'immobilisation puis la remise en piste progressive. La voiture repart à l'arrêt. Une panne sèche et l'immobilisation provoquent une fin de partie en Arcade ou un abandon en Grand Prix.

## Records, delta et fantôme

Le menu affiche le record du circuit pour le mode sélectionné et les trois derniers temps disponibles. Dix tours propres sont conservés par circuit et par mode. Un tour avec sortie de piste ou contact ne remplace pas un record. Un premier tour raccourci parce que la grille se trouve après la ligne n'est pas accepté comme tour complet.

En Chrono, le meilleur tour propre est enregistré puis rejoué par une voiture transparente, sans collisions. Elle suit des positions et orientations enregistrées pendant le tour, interpolées entre les échantillons. Le fantôme est masqué lorsqu'il est trop proche du joueur pour ne pas recouvrir la vue cockpit. Il peut être désactivé dans les préférences ; cette option ne supprime pas le record. Aucun fantôme n'apparaît avant qu'un premier tour exploitable ait été enregistré.

Le delta compare le temps courant au temps de référence à la même distance sur le circuit : **négatif signifie de l'avance, positif du retard**. Un delta n'est affiché que lorsqu'une référence existe à cet endroit et que le tour courant reste propre.

Les fichiers sont locaux et ne sont pas transférés à Forgejo :

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
python3 Tools/verify_mac.py --profile --soak
python3 Tools/install_mac.py --validation "Saved/Validation/v0.30.0/DOSSIER-DE-LA-SERIE/results.json"
```

Remplacer le chemin de la dernière commande par celui annoncé par le vérificateur. Ce n'est pas un fichier fixe : chaque série crée son propre dossier daté. Le vérificateur capture les écrans, contrôle les scénarios et mesure les deux définitions ainsi que les six circuits complets. L'installation exige le rapport réussi correspondant au binaire et aux données cuisinées ; elle refuse un package incomplet ou différent.

L'application de lancement stable est `~/Applications/Vroom.app`. Le manifeste actif est `~/Library/Application Support/Vroom/active.json` ; les versions installées et leurs rapports restent dans `~/Library/Application Support/Vroom/Releases/`. Les anciennes versions sont conservées pour un retour arrière. Une compilation seule laisse la version active inchangée.

## Périmètre de validation

Le package Mac 0.30 passe la série automatisée du **13 septembre 2026** : 31 cas réussis, huit profils de cadence, les six circuits parcourus entièrement et 21 contrôles portables. Le profil Performance à 70 % interne atteint environ 59,98–59,99 images/s en moyenne sur le Mac Apple M5 24 Go, avec un maximum mesuré de 22,83 ms par image. Ces mesures ont été effectuées sous verrouillage du Mac ; elles ne remplacent pas une session de jeu interactive. **Cette version est installée** ; le lanceur natif et le raccourci du Bureau ont chacun démarré la 0.30, passé le contrôle interne et fermé normalement.

Les essais et mesures réellement effectués sont consignés dans [VALIDATION.md](VALIDATION.md). Une vérification d'actions et de rectangles ne remplace pas un essai matériel de manette ou une session de jeu complète avec chaque périphérique. **Le build Windows et l'activation DLSS sur une carte NVIDIA restent non validés.** Le Mac utilise Metal et ne propose pas le DLSS. Les circuits et leurs décors restent des interprétations ; le prototype ne revendique pas une copie intégrale de Vroom.
