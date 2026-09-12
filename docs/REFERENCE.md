# Références Vroom

## État des preuves

Des sources assembleur ST sont accessibles dans une [archive de juillet 1991](https://www.oldies-but-goodies.fr/games/vroom/download/Atari%20ST/Vroom-ST-sources-juillet-91.zip), proposée par [Oldies but Goodies](https://www.oldies-but-goodies.fr/test/314). Téléchargement inspecté sans exécution : 36 fichiers `.asm`/`.s` et un `.rtf`. Les fichiers présentent des en-têtes V15 et des révisions allant jusqu'en juillet 1991. Le fichier `motor.asm` annonce une version du 22 juin; `vr1.asm` porte des annotations du 23 et 25 juillet. Cela constitue un état de développement exploitable. Sa complétude, son origine directe chez les auteurs et son identité avec la version commerciale ne sont pas établies.

Empreinte SHA-256 de l'archive, 251901 octets :

```text
cf97c64b7950d7ffdb115b9dc49167f9a6cec1da51738f276e5a26d7db09efda
```

L'archive originale reste dans le dossier de travail `work/reference`, hors dépôt du prototype. Le JSON ne contient que l'extraction des paramètres et des coordonnées utiles. Aucune licence de logiciel libre n'a été trouvée dans l'archive; disponibilité publique et logiciel libre sont deux états distincts. Le cadre demandé par David est un remake personnel.

## Circuits extraits

Les numéros ci-dessous correspondent aux fichiers de sources. Ils ne définissent pas l'ordre d'un championnat.

| ID | Fichier | Pays | Longueur CLAP | Événements | Points de prévisualisation |
|---|---|---|---:|---:|---:|
| 1 | cr1.asm | Autriche | 5911 m | 66 | 285 |
| 2 | cr2.asm | France | 3813 m | 19 | 249 |
| 3 | cr3.asm | USA | 6405 m | 23 | 299 |
| 4 | cr4.asm | Suède | 4018 m | 21 | 300 |
| 5 | cr5.asm | Japon | 4359 m | 15 | 232 |
| 6 | cr6.asm | Royaume-Uni | 4778 m | 21 | 225 |

`Content/Data/tracks.json` sépare les événements du circuit, la petite carte du tableau de bord et une prévisualisation 3D dérivée de cette carte. `Tools/extract_tracks.py` peut régénérer le fichier avec la seule bibliothèque standard Python :

```sh
python3 Tools/extract_tracks.py /chemin/vers/sources-extraites Content/Data/tracks.json
```

Le script ne télécharge rien et n'exécute aucun programme original. Il accepte les fichiers texte UTF-8 ou Latin-1. Les empreintes individuelles dans le JSON désignent les octets des fichiers réellement fournis à l'extracteur; leur normalisation peut les faire différer des entrées de l'archive.

## Décodage démontré

Dans `cr1.asm:8`, `MTOP` émet un indice, un octet combinant action et indicateurs, une position longitudinale en centimètres et un mot réservé. Il ajoute son troisième argument à l'accumulateur `DIS`. `TOP`, défini à la ligne 19, exprime une position relative au `DIS` courant sans modifier cet accumulateur. Le quatrième argument n'est pas utilisé dans ces définitions. Les appels et leurs numéros de ligne sont conservés dans le JSON.

Les deux bits bas de l'action distinguent courbes, relief, objets verticaux et voies. Les routines `sub1.asm:390` et suivantes ainsi que `vr2.asm:3434` documentent ces traitements. Pour une courbe, l'action 4 indique la droite et 8 la gauche. La routine `sub1.asm:795–800` ajoute un à l'indice brut et applique un signe négatif à gauche. Le commentaire décrit les types 1 à 8 comme allant du serré au large. **Ces indices ne sont pas des rayons ni des courbures en mètres inverses.** Les nombres entre parenthèses des commentaires des fichiers de circuit ne sont pas convertis en paramètres physiques, leur signification n'étant pas démontrée.

Pour le relief, les actions 1, 5 et 9 signifient plat, montée et descente. L'export conserve les événements mais laisse `slope` et `altitude_m` à `null`. Le rendu original comporte des transitions et des tables de projection dont la conversion complète n'est pas effectuée. Seul le fichier Autriche de cet état de sources possède des événements `TOP`; cela ne prouve pas l'absence de relief dans les autres circuits commercialisés.

Les paires `YX` de `CDRW` sont des coordonnées de la carte du tableau de bord. `drwcrc.asm:64–87` ajoute Y à une adresse d'écran puis utilise X comme position de pixel. Le pas de ligne est 160 octets. Le JSON exporte donc `y_px = Y / 160`, conserve X et garde les doublons servant à la progression sur la carte. `CSCA` est une échelle de progression entière, pas une échelle géométrique en mètres par pixel.

## Géométrie provisoire du prototype

Le bitume mesure désormais **9,5 m de large** (`HalfWidthM = 4.75`), contre 12 m auparavant. Dans `vr2.asm:3264–3265`, les demi-largeurs de route valent 256 « pixels at line 0 » ; les collisions entre voitures emploient une largeur de 132 dans cette même base (`vr2.asm:1739–1751`). Le rapport nominal original est donc **512/132 ≈ 3,88 largeurs de voiture**. Les nouveaux meshes de concurrents mesurent 2,3984 m hors tout : le prototype passe d’environ 5,00 à 3,96 voitures de large. **9,5 m est un choix de restitution de ces proportions, pas une largeur métrique retrouvée ni une mesure validée de l’édition Amiga.**

Les vibreurs, lignes et herbes suivent cette demi-largeur. Le gravier garde 1,5 m de chaque côté du bitume, dont 0,7 m couverts par le vibreur ; les 16 cases du damier couvrent exactement la largeur de piste. Les voies de grille restent à ±2,2 m. Ce rétrécissement conserve la caméra et les dimensions des voitures.

`preview_points_m` reprend les pixels consécutifs distincts de la petite carte. Une mise à l'échelle uniforme fait correspondre le périmètre fermé à `CLAP`. L'origine est le premier point, le premier point n'est pas répété à la fin et la dernière arête rejoint implicitement le premier. L'altitude est fixée à zéro uniquement pour cette représentation provisoire.

Au chargement, `AVroomCircuit::SelectTrack` reconstruit une copie conduisible via `Vroom::BuildDriveablePreview` : quatre passes d’un noyau triangulaire à sept points, une passe de Chaikin, rééchantillonnage à 5 m, puis deux passes du même noyau sur ces points rapprochés et mise à l’échelle à `CLAP`. Les deux passes d'origine laissaient des sauts de cap de 10 à 15° tous les ~19 m (pixels de la mini-carte) : le virage se lisait comme une suite de lignes droites. Les points forment une spline Unreal fermée `Curve`. Une seconde mise à l'échelle uniforme ajuste la longueur mesurée de la spline à `CLAP`. La courbure physique est une table filtrée (±12 m), pas un écart de lacet à ±4 m. Le JSON original reste inchangé. La position de départ du rendu est le début de cette spline. La caméra suit `DrivingRotation` (moyenne de regard 0–12 m) plus `VisualHeading`, pas le cap de vitesse entier.

Le filtre après rééchantillonnage réduit une ondulation introduite par l’interpolation linéaire de cette étape. Le banc analytique du 8 septembre 2026 reproduisant les tangentes cubiques automatiques Unreal et le regard moyen mesure en Autriche une variation maximale de lacet de **0,1625 à 0,0173 °/m²** avant/après ce filtre ; les six tracés présentent une réduction. Les points correspondants se déplacent au maximum de **1,23 à 3,41 m** selon le circuit, et la longueur finale reste celle demandée. Il s’agit d’une comparaison géométrique en double précision, sans exécution du rendu Unreal : ces résultats ne mesurent pas les images/s ni la latence caméra. Le test portable `Tests/track_preview_test.cpp` contrôle les sauts de courbure des raccords cubiques et la conservation de virages distincts.

Cette opération restitue une silhouette reconnaissable, mais ne démontre ni la géométrie originale de conduite, ni le rapport d'aspect de ses pixels, ni les rayons, ni les pentes. Le lissage et la spline Unreal sont des interprétations du prototype. Les événements longitudinaux originaux et cette géométrie ne doivent pas être présentés comme une reconstruction physique validée.

Vérifications effectuées : six fichiers présents; tous les cumuls `MTOP` égaux à `CLAP`; positions d'événements dans les limites du tour; Y de carte multiple de 160; aucune duplication terminale du premier point; erreur de périmètre après arrondi inférieure à 0,001 m pour chaque prévisualisation. Aucun test comparatif avec le jeu original exécuté n'a encore été fait.

## Manuel et témoignages

Le [manuel original retranscrit, avec addendum Amiga](https://www.lankhor.net/jeux/manuel/manuel32-1.pdf) décrit entraînement, arcade, compétition et démonstration. La souris commande la conduite; ses boutons changent les six rapports manuels. Le joystick de compétition avec assistance est un ajout Amiga. F10 change la focale. Essence, pneus et moteur s'usent; T et G servent aux stands. Le moteur est annoncé à 535 N·m à 11000 tr/min, maximum 13000. L'arcade exige des dépassements, la compétition commence par un tour qualificatif. L'ordre ST est Suède, France, Autriche, Royaume-Uni, USA, Japon; l'addendum Amiga indique Japon, Autriche, France, Royaume-Uni, Suède, USA. Les reliefs et décors des circuits étaient imaginés.

Dans son [entretien Atari Legend de 2002](https://www.atarilegend.com/interviews/3), Daniel Macré attribue une grande part du ressenti aux contrôles de Jean-Luc Langlois et à l'optimisation de l'affichage. Il indique une sortie ST en octobre 1991 suivie d'environ six mois de conversion Amiga.

Dans un [autre entretien direct](https://www.oldies-but-goodies.fr/index.php?id=414&page=personnalite), Macré décrit un modèle de voiture largement guidé par des tables et une gestion simplifiée des pneus. Les adversaires d'arcade sont alimentés au fil du jeu; ceux de compétition disputent leur course. Cela oriente notre travail vers la reproduction du comportement observé et du code retrouvé, puis vers une validation sur une édition précise.

La [fiche de préservation Lankhor.net](https://www.lankhor.net/jeux.php?jeu=32&menu=presentation) est utile pour les documents et les crédits, mais le site est une initiative de Frédéric Letellier. Ses commentaires de visiteurs ne sont pas traités comme une documentation technique d'auteur.

## Référence visuelle des mains

La prise a aussi été comparée au [cockpit de F1 25](https://cdn-1.motorsport.com/static/img/news/f125-steeringwheel-1-2-16x9.jpg), illustré dans la presse, et aux vues de la [présentation PC officielle EA](https://www.ea.com/games/f1/f1-25/news/f1-25-pc-features). Cela sert de repère d'orientation des poignets et d'enroulement des doigts ; aucun asset de F1 25 n'est intégré au projet.

La prise sur volant circulaire a également été observée dans les captures officielles de [Automobilista 2 — Formula HiTech](https://store.steampowered.com/app/2697780/Automobilista_2__Formula_HiTech/) : dos des gants vers le pilote et pouces à l’intérieur. Cette référence concerne l’orientation anatomique, sans remplacer le cadrage et les gestes Amiga.

## Travail restant pour annoncer la fidélité

L’édition choisie par David est **Vroom Amiga**. Il reste à identifier et vérifier l’exemplaire exact de référence; retrouver les différences entre juillet 1991 et l'édition retenue; décoder la projection et les transitions de courbe; mesurer focale, échelle latérale et rythmes de simulation; comparer des commandes enregistrées avec le comportement original. Le bandeau Canvas compact conserve les informations de course inspirées de l’Amiga, dans une présentation translucide moderne ; il remplace le grand boîtier 3D essayé auparavant. Les sons extraits et les meshes dédiés des monoplaces servent la restitution visuelle et sonore, avec des adaptations graphiques explicites. Les sept poses du cockpit Amiga guident le cadrage bas et le sens des gestes. Les mains du prototype ont désormais une prise fixe dans le repère du volant : la même rotation entraîne les gants et la jante, la main gauche montant en virage à droite et la droite en virage à gauche. L’autre main peut passer sous le cadre tout en conservant sa prise. La géométrie des doigts et l’orientation des poignets restent des adaptations 3D ; aucune correspondance pixel par pixel n’est revendiquée. Les dérapages avec poussée de sortie et l’herbe animée restent également des adaptations du prototype ; ils ne constituent pas des preuves d’une copie intégrale du jeu original.
