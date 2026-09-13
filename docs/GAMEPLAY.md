# Jeu, courses et stands — 0.30

Cette version ajoute des épreuves terminées, une progression, un trafic réactif et des arrêts au stand. Les règles ci-dessous décrivent le remake réellement codé. La référence choisie reste Vroom Amiga ; une ressemblance de fonction n'établit pas une reproduction exacte de son programme.

## Choisir une épreuve

Le menu propose **Arcade**, **Grand Prix** et **Chrono**, les six circuits et trois difficultés. Les boutons partagent leurs rectangles avec le traitement des clics. Les flèches ou la croix directionnelle déplacent la sélection, Entrée ou A la valident. Le changement de mode vers Arcade ou Grand Prix replace la sélection au Japon ; sélectionner ensuite un circuit permet de commencer à cette étape de la progression.

Chaque départ comporte un décompte de trois secondes. Pendant ce décompte, la voiture reste à l'arrêt et le chronomètre de course ne compte pas ces secondes. Maintenir l'accélérateur prépare le départ ; le remake ne sanctionne pas cette commande comme un faux départ.

| Mode | Déroulement de la 0.30 | Fin de l'épreuve |
|---|---|---|
| Arcade | Trois tours, du trafic renouvelé et un quota net de dépassements | Réussite au passage de la ligne du troisième tour si le quota est atteint ; échec si le quota manque, si le temps expire avant l'arrivée, ou après immobilisation sans essence |
| Grand Prix | Un tour qualificatif en solitaire, une grille de treize voitures puis trois tours de course | Classement du joueur à l'arrivée et points de championnat ; panne sèche à l'arrêt traitée comme un abandon sans point |
| Chrono | Tours libres en solitaire, sans voitures adverses | Les tours propres alimentent les records et le fantôme ; le joueur termine ou recommence depuis le menu de pause |

En Arcade, les quotas valent **18 / 27 / 36** en Facile / Normal / Expert. Le temps accordé est `3 × longueur / vitesseDeRéférence + 25 secondes`, avec une vitesse de référence de **33 / 39 / 45 m/s** selon la difficulté. Atteindre le quota avant la ligne n'arrête pas immédiatement la partie : il faut terminer les trois tours. Les quotas, ce temps accordé et le format de trois tours sont des choix du remake.

Un dépassement confirmé ajoute 500 points ; être dépassé en retire 500. Le compteur représente donc des dépassements **nets**, pas le nombre de voitures différentes battues. La confirmation emploie une zone de recouvrement de 3 m et conserve le côté précédent de chaque adversaire. La réapparition du trafic n'accorde ni ne retire de points. Le score lié à la vitesse conserve les cinq seuils issus de `SCORV`, et le score affiché est borné à 99 999.

## Qualification, grille et progression

Le tour qualificatif part à l'arrêt sur la ligne chronométrée, à `S=0`. Sortir de la route ou subir un contact invalide le tour. Un tour invalidé place le joueur treizième mais permet de prendre le départ. Le chrono est comparé à un **temps de référence calculé** sur le circuit : intégration d'une vitesse adaptée à la courbure par segments de 25 m, puis cinq secondes supplémentaires. Ce n'est pas une séance de qualification effectivement disputée par les douze adversaires.

La position obtenue remplit un vrai emplacement de grille. Les treize emplacements occupent les deux files, avec 16 m entre rangées ; la grille s'étend de `S=24` à `S=120`. La ligne d'arrivée reste `S=0` modulo la longueur du circuit. Le premier tour de course est donc légèrement plus court qu'un tour complet depuis cette ligne ; il n'est pas utilisé comme un record de tour complet. Le mode Chrono et la qualification démarrent directement à la ligne.

Les points reprennent le barème du manuel : **10, 6, 4, 3, 2, 1** pour les six premiers, puis zéro. Le résultat attribue les points aux treize pilotes une seule fois. Les concurrents déjà arrivés sont classés par leur temps de franchissement ; les autres sont classés par leur progression longitudinale à l’arrivée du joueur. Cette attribution provisoire ne simule pas leur course jusqu’au dernier passage. Recommencer une course terminée retire exactement les points, les victoires et les décomptes de places de cette tentative pour les treize pilotes avant de la rejouer.

La progression suit l'ordre de l'addendum Amiga :

1. Japon
2. Autriche
3. France
4. Royaume-Uni
5. Suède
6. USA

Après chaque Grand Prix, le joueur peut continuer vers la qualification suivante, recommencer ou retourner au menu. En Arcade, continuer demande d'avoir réussi l'étape. Sélectionner manuellement un circuit réinitialise les points et situe la partie à cette étape ; les circuits précédents ne sont pas joués automatiquement. Le résultat affiche le classement général des treize pilotes, leurs points, leurs victoires et leur place dans la dernière course. Les égalités sont départagées par les victoires, puis par le nombre de deuxièmes places, de troisièmes places et ainsi de suite ; une égalité complète utilise l’identifiant du pilote. La dernière étape désigne le champion. Les cumuls restent en mémoire pendant la partie ; reprendre un championnat après fermeture n’est pas implémenté.

## Adversaires et difficulté

Le peloton compte douze voitures adverses. Le calcul de trafic est indépendant du rendu et utilise le pas physique de 120 Hz. Il tient compte des autres voitures et du joueur :

- vitesse adaptée à la courbure observée 25 et 90 m plus loin ;
- choix d'une trajectoire parmi trois voies, avec vérification de l'espace disponible et réservation des destinations proches ;
- déplacement latéral progressif, limité à 1,8 m/s, et délai entre les décisions pour éviter les changements permanents ;
- freinage derrière une voiture lente tant que la voie de dépassement n'est pas réellement atteinte ;
- ralentissement du concurrent touché, puis récupération de son allure normale.

Les multiplicateurs d'allure valent **0,83 / 0,96 / 1,07** en Facile / Normal / Expert. Un contact diminue la vitesse de l'adversaire et réduit temporairement sa vitesse cible ; la durée dépend de l'importance du choc, entre 0,5 et 3 secondes. Il ne s'agit pas d'une simulation de dégâts complète pour chaque rival.

En Grand Prix, les adversaires gardent leur progression de course. En Arcade, une voiture située plus de 100 m derrière le joueur est replacée entre 230 et 494 m devant lui pour alimenter le trafic. Son historique de dépassement et ses positions précédentes sont réinitialisés. Le Chrono et la qualification masquent entièrement le peloton et ses contacts.

Le rendu interpole les positions longitudinales **et latérales**. L'angle visible associé au changement de voie suit un amortissement exponentiel à 12/s, sans modifier les positions testées par le modèle de trafic. Cet amortissement se fige pendant la pause. L'IA ne reproduit pas les routines historiques ; ses lignes de course, réservations de voie et règles de freinage sont une adaptation.

## Pneus et essence

L'état des pneus varie de 2 048 à zéro. Il agit désormais sur trois éléments : l'autorité latérale de la direction, la puissance du freinage et le seuil de perte d'adhérence. Le coefficient vaut `0,65 + 0,35 × état / 2048`, soit **1 avec des pneus neufs et 0,65 avec des pneus entièrement usés**. Le volant conserve sa plage d'animation ; c'est la réponse de la voiture qui change.

Une sollicitation excessive en virage retire 12 unités de pneu par seconde. Certains chocs en retirent aussi. La 0.30 conserve une usure simplifiée : le freinage seul ne possède pas encore sa propre usure, et arriver à zéro ne provoque pas automatiquement l'éclatement historique. Le remplacement aux stands restaure le coefficient à 1.

L'essence diminue avec l'accélérateur, à raison de `0,00055 × accélérateur × secondes` de réservoir. Le plein représente donc environ trente minutes à pleine commande théorique ; une course courte n'impose pas systématiquement un ravitaillement. À zéro, l'accélérateur ne fournit plus de couple. En Grand Prix, l'immobilisation qui suit termine la course comme un abandon ; en Arcade, elle termine l'étape comme un échec. Le Chrono permet toujours de recommencer depuis le menu.

Les calculs précis et les différences avec les sources de 1991 figurent dans [DRIVING.md](DRIVING.md).

## Arrêts au stand

La voie des stands est distincte de la piste, côté droit positif. Elle longe les garages de `S=55` à `S=190`, entre 10,5 et 14,5 m du centre de la route. L'emplacement de service se trouve autour de **S=102 m, déport +12,5 m**. La zone admise pour le service couvre `S=96…108`, déport `+11…+14`, à moins de **0,5 m/s**, soit 1,8 km/h.

Il faut rejoindre cette zone et s'arrêter. Ralentir sur la ligne de départ au milieu de la piste ne permet plus de refaire instantanément le plein.

| Service | Commande par défaut | Durée |
|---|---|---:|
| Pneus et remise en état des effets de casse | T | 4 s |
| Essence | F | 3 s |
| Les deux | A / validation, ou clic gauche en conduite souris | 7 s |

Pendant le service, la voiture est immobilisée même si l'accélérateur reste maintenu. Le chrono et les adversaires continuent. Les pneus et le carburant ne sont restaurés qu'à la fin du temps nécessaire. Le service terminé, l'accélérateur permet de repartir normalement. Les arrêts sont disponibles en Arcade et en course Grand Prix ; les séances solo n'en ont pas besoin pour réaliser un tour chronométré.

Les dimensions de la voie, son emplacement, les durées et la commande F sont des choix de cette version. Le manuel original mentionne un arrêt immobile et T/G ; G reste ici le changement de mode de boîte pour préserver les commandes déjà établies.

## Commandes, records et reprise

Le clavier, la souris et une manette reconnue par Unreal peuvent commander le jeu. En conduite souris, le mouvement horizontal positionne le volant, le clic gauche accélère et le droit freine ; la molette monte ou descend les rapports. Le bouton du milieu ouvre ou ferme la pause. La souris originale utilisait aussi son axe vertical pour les pédales : ce mode moderne ne reproduit pas ce dispositif à l'identique.

La manette utilise le stick gauche, les deux gâchettes, les boutons d'épaule pour déraper, les boutons de façade pour les rapports et la validation, et les boutons dédiés pour la pause et la vue. Les menus et les résultats se parcourent avec la croix et A/B. Le panneau Commandes et son permet de régler sensibilité, zone morte, champ de vision, volumes, préférences de vue/boîte et affectations clavier. Les correspondances matérielles d'une manette donnée restent à valider sur cette manette.

Les records sont conservés par circuit et par mode, avec les derniers tours et un fantôme du meilleur tour propre en Chrono. Le fantôme n'a pas de collision. Les préférences et les records sont enregistrés ; la course ou le championnat en cours ne sont pas repris après fermeture. [VALIDATION.md](VALIDATION.md) précise les vérifications réellement exécutées sur le package.

## Ce qui vient de la référence, ce qui reste une adaptation

Le [manuel retranscrit avec addendum Amiga](https://www.lankhor.net/jeux/manuel/manuel32-1.pdf) établit les familles de modes, le tour qualificatif avant la compétition, le quota de dépassements en Arcade, les services à l'arrêt, le barème 10/6/4/3/2/1 et l'ordre de championnat retenu. Les fichiers historiques établissent aussi les tables de rapports et les effets de l'usure sur le freinage et le seuil de glisse. Ces éléments sont comparés séparément dans [REFERENCE.md](REFERENCE.md).

Le format moderne de trois tours, les quotas selon difficulté, le délai Arcade, la qualification contre un temps calculé, le renouvellement spatial du trafic, les stands de 3/4/7 secondes, la plage d'adhérence 0,65…1 et le bonus après dérapage appartiennent au remake. L'usure du moteur, la sanction du faux départ et les pannes exactes de l'original restent absentes. Aucun parcours exécuté dans un émulateur de l'édition Amiga n'est présenté comme une validation de cette version.

## Vérifications reproductibles

`python3 Tools/test_portable.py` découvre les suites C++ et Python. Les nouveaux tests gameplay contrôlent les transitions de modes, la qualification, les points accordés une seule fois, les six étapes, la panne sèche, le quota à l'arrivée, la durée de service, la réduction d'adhérence/freinage, le dépassement d'une voiture arrêtée et un peloton simulé pendant soixante secondes.

`-VroomPitTest -VroomVerify -VroomExitAfter=9` active un essai d'intégration : service complet réel avec accélérateur maintenu, contrôle de l'immobilisation pendant sept secondes, restauration du plein/pneus, puis vérification du redémarrage et de l'avancement du chrono et des concurrents. Il doit produire `VROOM-PIT` puis `Verification PASS` ; le drapeau seul ne constitue pas une validation.

`-VroomSessionPreview=qualifying`, `results`, `arcade`, `pit` ou `controls` prépare des états synthétiques pour des captures reproductibles. Ces captures vérifient la présentation, **pas** l'exécution d'une course complète. `-VroomTourDemo -VroomTrack=0…5` pilote un parcours de mesure ; il ignore les contacts et journalise sa couverture avec `VROOM-TOUR`. Ce dernier parcours sert aux mesures de rendu et de mouvement, pas à certifier le comportement des collisions en jeu.
