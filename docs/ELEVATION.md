# Relief des circuits : sources ST et adaptation en altitude

Le prototype dispose d'un profil de relief fondé sur les **47 événements du circuit d'Autriche**, dans `cr1.asm`. Les cinq autres fichiers de circuit de l'archive étudiée ne contiennent aucun événement de relief. Ils gardent un profil plat dans le prototype. Cela décrit cet état de développement, sans prouver le contenu de chaque édition commerciale ST ou Amiga.

Les événements ne sont pas une liste d'altitudes ni des angles en degrés. Le code ST en déduit une **inclinaison qui varie avec la distance**, utilisée pour la projection et l'effort moteur. Sa conversion en une route 3D fermée exige deux choix explicitement provisoires : une échelle physique et une correction de fermeture. Aucune sinusoïde de relief n'a été ajoutée aux circuits.

## Sources et unités retrouvées

L'archive est celle de développement de juillet 1991 documentée dans [REFERENCE.md](REFERENCE.md), SHA-256 `cf97c64b7950d7ffdb115b9dc49167f9a6cec1da51738f276e5a26d7db09efda`. Le fichier `cr1.asm` porte l'en-tête `Circuit 1 Austria - V15 - 13 Apr 91`. Les numéros de ligne ci-dessous désignent ces fichiers extraits, sans les exécuter.

| Preuve | Conséquence pour l'extraction |
|---|---|
| `cr1.asm`, macros `MTOP` et `TOP`, lignes 8–27 ; `CLAP`, ligne 138 | Les positions `Y` et la longueur du tour sont en **centimètres**. `MTOP` avance le cumul `DIS` ; `TOP` place son événement à `DIS + argument` sans avancer ce cumul. L'Autriche mesure 591 100 cm, soit 5 911 m. |
| `vr2.asm`, lignes 3449–3453 | Actions de relief : `1 = Flat`, `5 = Up`, `9 = Down`. L'extraction suit les octets, pas les commentaires de chaque événement, dont plusieurs sont contradictoires. |
| `vr2.asm`, `TSTCRHE` à `TSTCRHE4`, lignes 1828–1850 ; `sub6.asm`, `CMPROD`, lignes 223–230 | `F1DEP` est le déplacement longitudinal effectivement appliqué à `Y1`, chargé dans `d2`. Le relief dépend du déplacement parcouru ; sa « durée » est une longueur, pas un compteur de secondes. |
| `sub6.asm`, `VMSUP`, ligne 23 ; `flvco` à `sinhau`, lignes 571–582 | L'incrément d'inclinaison est le déplacement signé, décalé à gauche de `5 - vamo` bits. |
| `sub6.asm`, `inclco`, lignes 686–711 | L'indice du nouvel événement remplace `vamo` ; la table `corr = 0,1,-1,0` fournit le signe. Le dépassement du point de changement pendant un pas est corrigé. |
| `sub6.asm`, lignes 801–809 ; `vr2.asm`, `CINIT`, lignes 98–99 | L'incrément s'ajoute à `degre`, initialisé à zéro. `Flat` produit un incrément nul : **il conserve l'inclinaison courante**, il ne remet pas la pente à zéro. |
| `sub6.asm`, lignes 804–809 et 3207–3208 ; `vr2.asm`, lignes 905–907 puis 948–951 | Le moment de pente est `degre >> 7`, rangé après `volant`, ajouté à la résistance puis soustrait au couple. Le signe positif de `Up` correspond donc à une montée qui résiste au moteur. |
| `sub6.asm`, lignes 1284–1298 | `degre` intervient dans la projection avec la suspension, une borne `MINSUP = 21000` et `rt_pmd`. Son nom ne signifie pas des degrés physiques ; aucune conversion directe vers radians ou mètres d'altitude n'est établie ici. |

Le tableau répétitif `esevt1` (`sub6.asm`, lignes 3179–3186) ne fournit pas un relief caché pour les autres circuits : ses codes `7` et `3` ont les deux bits bas à `3` et passent par `objdiv`, selon la table `natur`, lignes 2773–2791. Ils concernent les bandes visuelles de la route.

## Représentation de l'inclinaison source

Dans la version continue de ce calcul, pour une abscisse `s` en mètres :

```text
k = signe × 100 × 2^(5 - indice)
I(s + d) = I(s) + k × d
A(s + d) = A(s) + I(s) × d + k × d² / 2
```

`I` représente l'accumulateur original `degre`. `A` est son intégrale auxiliaire, introduite par l'extracteur ; son unité est « unité d'inclinaison ST × mètre ». **A n'est pas une altitude originale retrouvée.** Le signe vaut `+1` pour `Up`, `-1` pour `Down`, `0` pour `Flat`. Tous les événements de relief de cette Autriche utilisent l'indice `4`, donc `k = +200`, `-200` ou `0` unités par mètre.

Les événements sont triés par abscisse pour l'intégration. Leur ordre de déclaration et leurs cinq arguments restent conservés dans `events`. Chaque segment de `source_elevation.segments` fournit son début, sa fin, sa longueur `duration_m`, la ligne source, le signe, l'indice, les inclinaisons de début/fin et les intégrales de début/fin. À vitesse constante `v > 0`, le temps d'un segment serait `duration_m / v` ; aucune durée fixe n'est imposée.

Quelques repères vérifiables :

| Distance source | Séquence et résultat |
|---|---|
| 450–490 m | `Down` 10 m, `Up` 20 m, `Down` 10 m, puis `Flat` : inclinaison `0 → -2000 → +2000 → 0`. Petit creux qui retrouve son niveau avant correction de fermeture. |
| 1 293,42–1 373,42 m | `Up` 20 m, `Down` 40 m, `Up` 20 m, puis `Flat` : petite bosse. |
| 2 640,36–2 670,36 m | `Flat` conserve l'inclinaison déjà atteinte de **+14 000** ; le segment continue donc à monter dans l'altitude adaptée. |
| 3 006,12–3 206,12 m | `Up` 100 m puis `Down` 100 m : la pente monte jusqu'à `+20 000`, puis revient à zéro. La route gagne un niveau au lieu de former une bosse symétrique. |
| Tour complet | Inclinaison initiale et finale : `0`. Intégrale auxiliaire : **+6 835 000 unités·m**. |

Le calcul conserve la logique en distance et les frontières d'événements. Il ne reproduit pas les débordements des registres 16 bits, toute la projection ST, les mouvements de suspension ni les branches de saut originales.

## Deux adaptations nécessaires pour une route 3D fermée

L'échelle de visualisation choisie est `c = 1 / 150000` mètre de montée par mètre parcouru et par unité d'inclinaison. Ce coefficient est un **calibrage du prototype**, pas une valeur trouvée dans le jeu. Il donne une pente brute maximale de 13,33 % avant correction.

L'intégrale source aurait alors gagné **45,5667 m à chaque tour**. Pour fermer la route sans ajouter de nouvelles variations d'inclinaison, l'extracteur retire une pente moyenne constante :

```text
L = 5911 m
I_moyen = A(L) / L = 1156,318727795635 unités
z(s) = c × (A(s) - I_moyen × s)
dz/ds = c × (I(s) - I_moyen)
```

La correction est **−0,007708791519 m/m**, soit environ −0,771 %. Elle préserve la position et l'intensité relative des changements de pente mais modifie la pente absolue partout. En particulier, le départ du prototype est légèrement descendant alors que l'inclinaison source commence à zéro. Il s'agit d'un compromis de fermeture documenté.

Le profil adapté retrouve exactement `z(0) = z(L) = 0` et la même tangente aux deux extrémités. Sa pente varie de **−6,7709 % à +12,5625 %**. Les altitudes échantillonnées vont de **−17,5363 m** vers 2 285 m à **+8,3620 m** vers 4 825 m, soit environ **25,90 m** entre le point bas et le point haut. Ces valeurs sont celles de cette adaptation, pas un relevé topographique du vrai Zeltweg.

L'extracteur refuse de générer ce profil si une future source ne referme pas son inclinaison : une simple correction moyenne d'altitude ne suffirait alors pas à garantir la continuité de la tangente.

## Contrat JSON et intégration Unreal

Le schéma de `Content/Data/tracks.json` passe à la version `2`. Les points `preview_points_m` gardent leur `z = 0` et leurs valeurs XY précédentes. Un profil indépendant évite de modifier la référence longitudinale `S`/`CLAP` lorsque la route monte.

```json
{
  "preview_elevation": {
    "samples_m": [[0.0, 0.0], [5.0, -0.03854396]],
    "grades_m_per_m": [-0.007708791519, -0.007708791519]
  }
}
```

Cet extrait montre les deux premiers points, pas le profil complet. L'Autriche contient **1 227 échantillons** : grille de 5 m, toutes les frontières de relief, puis `[5911, 0]`. Les pentes analytiques `dz/ds` correspondent aux échantillons de même indice ; la pente du dernier point est celle du premier. Le client peut employer une interpolation cubique de Hermite entre points avec ces tangentes, puis répéter périodiquement le profil. Comme aucun changement de courbure verticale ne reste à l'intérieur d'un intervalle, cette interpolation reproduit le polynôme quadratique intégré, aux arrondis JSON près, tout en gardant une tangente continue.

Les cinq autres circuits exposent `samples_m = []`, `grades_m_per_m = []`, le statut `flat_fallback_no_relief_events` et un nombre d'événements source nul. Le client utilise alors hauteur et pente nulles. La source `cr1.asm` et l'identifiant JSON `1` désignent l'Autriche ; ce numéro n'est ni une abscisse, ni un indice de pixel de mini-carte, ni une preuve d'ordre de championnat. L'alignement des événements sur la géométrie XY reste tributaire de la reconstruction provisoire des mini-cartes, décrite dans [REFERENCE.md](REFERENCE.md).

Pour le rendu, la hauteur se convertit en centimètres Unreal et le tangage suit `atan(dz/ds)`. La prise en compte éventuelle de la gravité, `−9,81 × sin(atan(dz/ds))` m/s² le long de la route, est une adaptation physique du prototype ; elle ne remplace pas une validation du couple de pente ST et ne doit pas être présentée comme son port exact. Le relief, les véhicules, les bordures et le terrain doivent partager la même abscisse et le même profil afin d'éviter des objets flottants ou enfoncés. Une balistique de décollage sur crête n'est pas fournie par ces données.

## Reproduction et validations

Depuis la racine du projet, avec l'archive déjà extraite dans le répertoire voisin :

```sh
python3 Tools/extract_tracks.py ../reference/vroom-source-1991 Content/Data/tracks.json
python3 Tests/track_elevation_test.py
```

Seule la bibliothèque standard de Python est requise. Les huit tests vérifient le maintien d'inclinaison sur `Flat`, les signes et le décalage d'indice, le tri des événements sans mutation, les unités en centimètres, la priorité des octets sur des commentaires contradictoires, le maintien des petites formes avec Hermite, la fermeture en hauteur et en tangente, le refus d'une source non périodique, l'absence de relief inventé et la cohérence du JSON complet.

Ces tests ont réussi sur le Mac le 5 septembre 2026. Une comparaison avec le JSON précédent confirme que les six identités de circuit, longueurs, empreintes source, données brutes d'événements, mini-cartes et coordonnées XY restent identiques. Cette validation concerne l'extraction et son contrat numérique ; elle ne constitue pas un test de rendu Unreal ni une comparaison du relief final avec un jeu ST/Amiga exécuté.
