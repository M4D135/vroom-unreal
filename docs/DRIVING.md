# Modèle de conduite du prototype

Ce modèle portable C++17 sert à rendre le premier circuit jouable dans Unreal. Il reprend des paramètres mesurables du code Atari ST de Vroom de 1991, mais **ce n'est pas un port intégral du moteur original**. Les distances sont en mètres, les vitesses en m/s et le temps en secondes. `Steer > 0`, `HeadingRad > 0` et `LateralM > 0` vont vers la droite, soit le côté +Y du repère local Unreal. La courbure est positive dans un virage à droite.

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
| Résistance de l'air | vitesse interne² / 468 | `vr2.asm`, `SPEDC1`, lignes 886–889 ; le commentaire parle de 458 mais l'instruction emploie 468 |
| Roulement | 10 sur route ; +100 avec une roue sur bordure ; +198 hors route | `vr2.asm`, `SPEDC1A`, lignes 893–902 |
| Couple net engagé | couple moteur − frein − air − 10 − roulement | `vr2.asm`, `SPEDC2B`, lignes 949–952 |
| Variation de vitesse, branche non synchronisée | Δv interne = 256 × couple net / `GVVTBL[rapport]` | `vr2.asm`, `SPEDC4`, lignes 1002–1008 |

Les seuils supérieurs réellement utilisés par les rapports 1 à 5 correspondent à **80, 130, 180, 230 et 270 km/h**. Le sixième est le dernier rapport : le modèle ne passe pas de septième. Les seuils de rétrogradage des rapports 2 à 6 sont **70, 120, 170, 210 et 240 km/h**. La formule régime/vitesse à 13312 tr/min en sixième donnerait 382,54 km/h, mais ce nombre est une limite de correspondance théorique, **pas la vitesse de pointe mesurée du jeu**.

## Adaptations explicites

- **Temps continu.** Le prototype emploie la branche `SPEDC4` en flottants et adopte 20 évaluations historiques par seconde pour convertir l'accélération en SI : `a = 256 × 20 × coupleNet / (GVVTBL × 5,4)`. L'en-tête « 20 Cycles » motive ce choix ; une comparaison sur émulateur reste nécessaire pour vérifier la cadence réelle et les branches synchronisées.
- **Couple interpolé.** Les 27 échantillons à 512 tr/min sont extraits exactement de `MOMTBL`, puis interpolés linéairement. L'écart maximal aux 833 entrées originales est de 2,375 unités, soit moins de 0,45 % de la valeur maximale 534. Cela n'établit pas la même précision pour l'accélération finale.
- **Démarrage.** Le modèle démarre en première et utilise un embrayage simplifié, avec cible de régime `max(régimeRoues, 1000 + 7500 × accélérateur)` lissée sur 0,09 s. L'original commence au point mort et engage automatiquement à plus de 8500 tr/min. Le patinage, le point mort, les synchronisations, les à-coups et les dommages de surrégime ne sont pas reproduits.
- **Passage de rapport.** Les seuils sont ceux de la table. Chaque changement coupe le couple pendant 0,10 s, y compris en montée ; l'original réserve explicitement deux cycles de point mort au rétrogradage. Les boutons manuels changent d'un rapport sur leur front montant.
- **Frein moteur.** Une décélération supplémentaire fixe de 1,2 m/s² remplace la logique `GDNTBL` et `Nmax` du code original. Le frein de service conserve sa réponse quadratique, avec des pneus toujours neufs. Appuyer sur le frein annule l'accélérateur dans ce prototype.
- **Bordures.** Une empreinte de roues de 1,6 m rend l'augmentation de roulement progressive de +0 à +198 en franchissant la limite. Le milieu donne +99, voisin du palier +100 d'origine. L'adhérence diminue jusqu'à 55 %. Pas de rebond de suspension, d'usure, de crevaison ou de collision avec les décors dans cette classe.
- **Direction entièrement nouvelle.** Modèle bicyclette avec empattement 2,75 m, angle maximal `0,38 / (1 + vitesse/22)` rad, lissage de volant 0,12 s, accélération latérale limitée à `20 + 0,0015 × vitesse²` m/s² sur route. Ces valeurs sont des réglages du prototype, sans revendication de provenance Vroom.
- **Repère de circuit.** `sDot = v × cos(heading) / (1 − courbure × déport)`, `headingDot = yawRate − courbure × sDot`, `lateralDot = v × sin(heading)`. Ce repère permet de perdre la trajectoire à l'extérieur d'un virage puis de revenir en braquant. La progression est métrique le long du centre du circuit, pas l'odomètre réel de la voiture.
- **Limites de la première version.** Pas de marche arrière, tête-à-queue complet, transfert de charge, pneus, carburant, aspiration, pente ou collision avec une autre voiture. L'angle relatif est limité à ±1,35 rad ; le facteur métrique à [0,3 ; 3] évite la singularité mathématique loin du circuit. Le rendu doit fournir courbure et demi-largeur à la position courante.

## Intégration et validation

API publique : `Vroom::DriveModel`, `DriveInput`, `DriveState`. L'état est volontairement éditable pour réinitialisation et tests. `Reset()` remet la voiture au départ, sans modifier la préférence `Automatic`. Le propriétaire Unreal doit appeler `Step` à **1/120 s**, en accumulant son temps de rendu. `Step` sous-divise aussi les pas plus grands à cette cadence ; un appel ne simule jamais plus de 0,25 s. Un `dt` invalide ou négatif est ignoré. Les entrées et états non finis sont ramenés à des valeurs sûres.

Depuis la racine du projet :

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -ISource/Vroom/Public Tests/drive_model_test.cpp -o /tmp/vroom-drive-tests
/tmp/vroom-drive-tests
```

Les tests vérifient une accélération effective, les changements automatiques, un freinage jusqu'à l'arrêt sans marche arrière, la symétrie gauche/droite, le signe d'un virage non suivi, la résistance hors piste, la vitesse bornée, l'équivalence des propriétaires 60 et 120 Hz, les fronts des boutons de vitesse et la résistance aux valeurs non finies. Ce sont des contrôles de comportement du **prototype**, pas une validation de fidélité par rapport au jeu émulé.

Pour valider ensuite le ressenti historique : enregistrer sur Atari ST émulé le régime, la vitesse affichée, le rapport et le temps pendant un départ, un freinage, chaque passage de rapport et une sortie de route. Comparer les mêmes séquences d'entrées dans le prototype avant de modifier les paramètres de direction ou la conversion de temps.
