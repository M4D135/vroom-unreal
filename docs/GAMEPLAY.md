# Jeu, courses et stands — 0.34 en validation

Cette version ajoute des épreuves terminées, une progression, un trafic réactif et des arrêts au stand. Les règles ci-dessous décrivent le remake réellement codé. La référence choisie reste Vroom Amiga ; une ressemblance de fonction n'établit pas une reproduction exacte de son programme.

**État de la 0.34 :** les changements d'IA et de dérapage ci-dessous décrivent les sources de la nouvelle version. L'IA a passé ses tests portables ; ses sources sont gelées pour la compilation et la validation visible sous Unreal. Ces contrôles ne remplacent pas les preuves du [package 0.33](FINALISATION_033.md), déjà validé.

## Choisir une épreuve

Le menu propose **Arcade**, **Grand Prix** et **Chrono**, les six circuits et trois difficultés. Les boutons partagent leurs rectangles avec le traitement des clics. Les flèches ou la croix directionnelle déplacent la sélection, Entrée ou A la valident. Le changement de mode vers Arcade ou Grand Prix replace la sélection au Japon ; sélectionner ensuite un circuit permet de commencer à cette étape de la progression.

Chaque départ comporte un décompte de trois secondes, sonorisé : **rouge**, puis **orange**, puis **vert** exactement lorsque le chrono, le peloton et le joueur sont libérés. Chaque signal n'est émis qu'une fois, au volume des effets. Couper le son ou relancer pendant le décompte réinitialise la séquence. Pendant ce décompte, la voiture reste à l'arrêt. Maintenir l'accélérateur prépare le départ ; le remake ne sanctionne pas cette commande comme un faux départ.

Un léger contact avec le décor (60 km/h ou moins) rebondit sans recentrage forcé ; au-dessus, la casse et le retour sur la piste restent en vigueur.

Un tour n'est enregistré comme record, fantôme ou qualification que s'il est propre. Huit points de passage ordonnés interdisent de valider un tour en coupant, en reculant ou en franchissant la ligne sans avoir parcouru le circuit. Un léger débordement salit le chrono sans retirer le tour de course ; la voie des stands n'est pas une coupe. L'interface affiche la raison (`coupe de circuit`, `passage manqué`, `sens inverse`, `sortie de piste`, `contact`). Un tour déjà validé n'est pas retiré.

| Mode | Déroulement | Fin de l'épreuve |
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

## Adversaires et difficulté — IA 0.34

Le peloton compte douze voitures adverses. Le calcul de trafic est indépendant du rendu et utilise le pas physique de 120 Hz. Il tient compte des autres voitures et du joueur :

- anticipation du freinage avec neuf sondes entre la position actuelle et 250 m devant la voiture, en tenant compte de sa vitesse, de ses pneus et de la prudence du pilote ;
- trajectoire continue vers l'extérieur à l'approche d'un virage, vers la corde dans le virage, puis retour vers le centre à la sortie ;
- choix entre les voies latérales, le centre, la trajectoire de course et la destination actuelle, avec réservation de l'espace et prévision des autres mouvements ;
- dépassement mémorisé : le pilote garde le côté choisi jusqu'à avoir dégagé la voiture dépassée d'au moins 9 m, après un engagement minimal d'environ 2,3 s ;
- déplacement latéral limité à 1,8 m/s et accélération latérale limitée à 3,6 m/s² en piste ; une menace de contact peut interrompre immédiatement le déplacement ;
- freinage derrière une voiture lente tant que sa place n'est pas dégagée, surveillance d'une voiture rapide arrivant sur la voie visée et anticipation d'un joueur qui se rabat ;
- ralentissement du concurrent touché, puis récupération de son allure normale.

Le freinage vise la vitesse nécessaire à chaque courbure en fonction de la distance restante. Une épingle n'attend donc plus d'entrer dans une fenêtre fixe de 90 m pour être prise en compte. L'usure des pneus réduit aussi le freinage disponible. La réserve de calcul utilise une décélération inférieure à la limite réelle pour conserver une marge. Les trajectoires gauche/droite sont symétriques ; le choix d'une ligne reste soumis à la présence des autres voitures.

Une décision de trajectoire ne suffit pas à déclarer une voie libre. L'IA vérifie des positions futures pendant le déplacement et freine selon sa vitesse de rapprochement. Une seconde vérification à chaque pas empêche une manœuvre latérale de serrer un véhicule déjà à côté. Pour le joueur, elle utilise la position et la vitesse latérale observées, sans lire les commandes. Le modèle est déterministe ; les écarts de régularité sont de petites variations continues liées au parcours, et non des coups de frein d'une seule image.

Les multiplicateurs d'allure valent **0,83 / 0,96 / 1,07** en Facile / Normal / Expert. Un contact diminue la vitesse de l'adversaire et réduit temporairement sa vitesse cible ; la durée dépend de l'importance du choc, entre 0,5 et 3 secondes. Il ne s'agit pas d'une simulation de dégâts complète pour chaque rival.

Les six livrées conservent leur couleur d'origine. L'Amiga ne documentait pas un caractère par couleur : la table suivante est donc une règle explicite du remake. La couleur choisit le profil ; la difficulté générale multiplie encore l'allure.

| Livrée | Profil | Allure | Trajectoire | Dépassement | Prudence | Régularité | Gestion pneus | Gestion essence |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Bleu | Le Professeur | 0,98 | 1,20 | 0,88 | 1,18 | 1,22 | 0,86 | 0,92 |
| Rouge | L'Attaquant | 1,03 | 0,92 | 1,28 | 0,82 | 0,84 | 1,22 | 1,12 |
| Jaune | Le Sprinteur | 1,06 | 0,84 | 1,12 | 0,90 | 0,72 | 1,18 | 1,08 |
| Vert | Le Stratège | 1,00 | 1,08 | 1,00 | 1,12 | 1,10 | 0,80 | 0,84 |
| Violet | Le Technicien | 0,97 | 1,24 | 0,82 | 1,22 | 1,26 | 0,88 | 0,90 |
| Orange | Le Vétéran | 1,01 | 1,05 | 1,08 | 1,04 | 1,08 | 0,86 | 0,88 |

Les pilotes préparent leur arrêt selon les ressources nécessaires pour atteindre la prochaine entrée puis parcourir un tour supplémentaire. Le minimum reste 18 % d’essence ou 420 unités de pneus ; le seuil prédictif est généralement plus précoce. L'arrêt les immobilise 7 s, comme un service complet du joueur. Le joueur bénéficie d’une baisse de traînée dans le sillage d’une voiture (2 à 16 m, même file). Les IA ne reçoivent pas de bonus caché de vitesse dans cette situation, ni d'accélération destinée à rattraper artificiellement le joueur. Dans la voie des stands, un limiteur coupe l'allure vers 60 km/h. Pendant le décompte, l'accélérateur monte le régime sans avancer.

En Grand Prix, les adversaires gardent leur progression de course. En Arcade, une voiture située plus de 100 m derrière le joueur est replacée entre 230 et 494 m devant lui pour alimenter le trafic. Son historique de dépassement, ses positions précédentes, son mouvement latéral et sa manœuvre en cours sont réinitialisés. Ce renouvellement du trafic Arcade conserve le plein et les pneus neufs déjà prévus dans la 0.33 ; les voitures d'un Grand Prix ne bénéficient pas de cette remise à neuf. Le Chrono et la qualification masquent entièrement le peloton et ses contacts.

Le rendu interpole les positions longitudinales **et latérales**. L'angle visible associé au changement de voie suit un amortissement exponentiel à 12/s, sans modifier les positions testées par le modèle de trafic. Cet amortissement se fige pendant la pause. L'IA ne reproduit pas les routines historiques ; ses lignes de course, réservations de voie et règles de freinage sont une adaptation.

## Pneus et essence

L'état des pneus varie de 2 048 à zéro. Il agit désormais sur trois éléments : l'autorité latérale de la direction, la puissance du freinage et le seuil de perte d'adhérence. Le coefficient vaut `0,65 + 0,35 × état / 2048`, soit **1 avec des pneus neufs et 0,65 avec des pneus entièrement usés**. Le volant conserve sa plage d'animation ; c'est la réponse de la voiture qui change.

Une sollicitation excessive en virage retire 12 unités de pneu par seconde. Certains chocs en retirent aussi. L'usure reste simplifiée : le freinage seul ne possède pas encore sa propre usure, et arriver à zéro ne provoque pas automatiquement l'éclatement historique. Le remplacement aux stands restaure le coefficient à 1.

L'essence diminue avec l'accélérateur. En **course** (Arcade et Grand Prix hors qualification), la consommation et l'usure sont dimensionnées sur la longueur du circuit et la difficulté pour qu'un départ plein/pneus neufs n'aille pas au bout de trois tours sans un arrêt. Le Chrono et la qualification conservent l'ancienne consommation douce (`0,00055` par seconde à pleine commande).

Autonomie nominale sur une course de trois tours, à 60 m/s et 78 % d’accélérateur :

| Difficulté | Essence (tours) | Pneus (tours en ligne droite) | Arrêts observés des IA sur 3 tours |
|---|---:|---:|---:|
| Facile | 1,95 | 2,07 | 2 |
| Normal | 1,74 | 1,86 | 2 |
| Expert | 1,56 | 1,68 | 2 |

Les **324 simulations ont été rejouées avec l'IA 0.34**, sur les approximations portables des six tracés : 2/3/6 tours, trois difficultés et six profils. Toutes terminent leur course. Les nombres d'arrêts restent ceux de la 0.33 : un arrêt pour deux tours ; deux arrêts pour trois tours ; un arrêt en Facile/Normal et deux en Expert pour six tours. Le minimum d'essence observé est désormais **15,93 %**, sur une course de six tours en Suède en difficulté Normal. Les services durent toujours sept secondes et restaurent les ressources à leur fin. Le journal local est `Saved/Validation/v034/ai-resource-races.log` ; il s'agit de mesures du modèle portable, pas de parties jouées manuellement ni d'une validation du package Unreal 0.34. Les preuves de la version précédente restent dans le [rapport 0.33](FINALISATION_033.md).

Les paramètres partagés sont dans `VroomRaceRules.h` et ne dépendent pas du nombre d’images par seconde. Les alertes utilisent l’autonomie nécessaire jusqu’aux prochains stands. Les profils d’IA changent leur effort de conduite, sans facteur donnant des pneus ou du carburant gratuits. À zéro, l’accélérateur ne fournit plus de couple ; une IA ne conserve plus une vitesse minimale gratuite. L’immobilisation sans essence termine la course en abandon ou échec selon le mode.

Les calculs précis et les différences avec les sources de 1991 figurent dans [DRIVING.md](DRIVING.md).

## Dérapage contrôlé et petits contacts — 0.34

Le dérapage commence avec **Maj** ou **LB/RB** et un braquage dans le sens du virage, sur la route à plus d’environ **44 km/h**. Maintenir ensuite le bouton tout en dosant le **contre-braquage** construit la charge. Au clavier, de courtes pressions sur la direction opposée, séparées de relâchements, permettent ce dosage. La charge récompense la maîtrise de la glisse ; conserver la direction initiale ou bloquer durablement le volant à fond dans l’autre sens finit par l’annuler.

Relâcher volontairement le bouton avec le contrôle et la charge nécessaires libère un turbo. La 0.34 ajoute un troisième palier et une récompense de précision au lâcher :

| Palier | Charge nécessaire, en secondes équivalentes de contrôle parfait | Durée du turbo | Avec lâcher parfait |
|---|---:|---:|---:|
| 1 | 0,42 | 0,55 s | 0,75 s |
| 2 | 1,00 | 1,15 s | 1,35 s |
| 3 | 1,80 | 1,70 s | 1,90 s |

La charge dépend de la qualité du contre-braquage ; ces seuils ne sont donc pas des durées fixes de maintien du bouton. Atteindre un nouveau palier ouvre une fenêtre de **0,22 s** pour relâcher et gagner **0,20 s** de turbo supplémentaire. La fenêtre n'est ouverte qu'une fois par palier pendant cette glisse : perdre puis regagner le même seuil ne la renouvelle pas. Un lâcher plus tardif conserve le turbo normal si la glisse reste maîtrisée. La charge est plafonnée à 2,10 ; les boosts ne se cumulent pas et aucune nouvelle glisse ne démarre pendant un turbo actif. La poussée monte et redescend progressivement.

Le frein n’est pas une entrée automatique en dérapage : il annule la glisse et le boost. Contact, herbe, saut physique, panne sèche et vitesse insuffisante effacent la charge. Il faut relâcher le bouton avant de réarmer une glisse annulée ; répéter rapidement des pressions ne procure pas de bonus gratuit.

Un frottement léger de roues peut produire un petit bond rattrapable. Pendant le bond, contre-braquer **contre la poussée latérale reçue** réduit le mouvement résiduel à la réception. La voiture conserve alors entre **94 % et 100 %** de sa vitesse d’arrivée : **97 % au neutre**, **100 % pour une correction propre**, **94 % pour une mauvaise correction**. Ce sont des coefficients de conservation, jamais une accélération offerte. Le contact invalide le chrono et efface la charge de dérapage ; un gros impact conserve la casse et sa récupération lente. Les détails et les seuils sont dans [COLLISIONS.md](COLLISIONS.md).

## Arrêts au stand

La voie des stands est distincte de la piste, côté droit positif. Elle longe les garages de `S=55` à `S=190`, entre 10,5 et 14,5 m du centre de la route. Le marquage central reste un repère autour de **S=102 m, déport +12,5 m**, mais le service est accepté dans toute la zone **S=65…180**, déport **+10,5…+14,5**, à moins de **0,8 m/s**, soit environ 2,9 km/h. Il n’est donc plus nécessaire de viser la petite case.

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

`python3 Tools/test_portable.py` découvre les suites C++ et Python. Les tests gameplay contrôlent les transitions de modes, la qualification, les points accordés une seule fois, les six étapes, la panne sèche, le quota à l'arrivée, la durée de service, la réduction d'adhérence/freinage, le dépassement d'une voiture arrêtée et un peloton simulé pendant soixante secondes. `Tests/race_rules_test.cpp` ajoute les feux rouge/orange/vert, l'invalidation des tours, l'obligation d'un arrêt sur les six circuits et les trois difficultés, et les profils d'IA par couleur.

Pour l'IA 0.34, `Tests/traffic_model_test.cpp`, `Tests/race_rules_test.cpp` et `Tests/race_resources_test.py` ont été exécutés avec succès avant le gel des sources. Les nouveaux scénarios contrôlent, à **30/60/120 Hz**, le freinage avant une épingle avec pneus neufs puis usés, les trajectoires symétriques, le dépassement engagé et son rabattement, la priorité d'une voiture qui arrive derrière, le déplacement latéral du joueur et l'espace entre deux voitures côte à côte. Un peloton de douze pilotes traverse aussi des virages serrés et la ligne de départ, avec un joueur plus lent, sans recouvrement des voitures dans ces simulations. Les trois courses complètes de douze voitures, avec file d'attente et sortie des stands, restent réussies. Le journal ciblé est `Saved/Validation/v034/ai-tests.log`.

Le package 0.33 a déjà passé ses contrôles visibles ; le [rapport 0.33](FINALISATION_033.md) en conserve les résultats. **Le package 0.34 est encore en attente de validation visible** à ce stade documentaire. Les tests portables ci-dessus ne prouvent ni sa cadence de rendu ni l'intégration de ses nouvelles manœuvres. Après compilation, la série visible doit être exécutée et examinée sur ce nouveau package :

```sh
python3 Tools/verify_mac.py --visible --graphics-preset performance --profile --soak
```

`-VroomPitTest -VroomVerify -VroomExitAfter=9` active un essai d'intégration : service complet réel avec accélérateur maintenu, contrôle de l'immobilisation pendant sept secondes, restauration du plein/pneus, puis vérification du redémarrage et de l'avancement du chrono et des concurrents. Il doit produire `VROOM-PIT` puis `Verification PASS` ; le drapeau seul ne constitue pas une validation.

`-VroomSessionPreview=qualifying`, `results`, `arcade`, `pit` ou `controls` prépare des états synthétiques pour des captures reproductibles. Ces captures vérifient la présentation, **pas** l'exécution d'une course complète. `-VroomTourDemo -VroomTrack=0…5` pilote un parcours de mesure ; il ignore les contacts et journalise sa couverture avec `VROOM-TOUR`. Ce dernier parcours sert aux mesures de rendu et de mouvement, pas à certifier le comportement des collisions en jeu.
