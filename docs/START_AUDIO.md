# Signal de départ — 0.35

Deux appels de préparation brefs précèdent un appel plus long une quinte au-dessus. Le timbre est une synthèse originale : ce n’est pas un enregistrement officiel de F1.

| Feu | Fondamentale | Durée | Amplitude maximale de la voix |
|---|---:|---:|---:|
| Rouge | 880 Hz | 0,18 s | 0,85 |
| Orange | 880 Hz | 0,18 s | 0,85 |
| Vert | 1 320 Hz | 0,42 s | 1,00 |

`StartToneVoice`, dans `Source/Vroom/Public/VroomRaceRules.h`, est le générateur utilisé par le mélangeur. La fondamentale et les trois harmoniques ont des poids de 0,54, 0,28, 0,12 et 0,06. La mise en présence reste bornée mathématiquement. L’attaque de 2,5 ms et l’extinction de 40 ms évitent les discontinuités. Le gain du signal dans le mélangeur passe de 0,45 à 0,58 ; le volume du composant audio reste à 0,55, avant préférences utilisateur. Aucun fichier commercial ni nouvel objet audio par feu n’est ajouté.

Les états des feux, leurs instants de déclenchement, la pause, le redémarrage et le volume nul restent inchangés. Le vert retentit lorsque le chronomètre atteint zéro.

## Contrôles

`Tests/race_rules_test.cpp` vérifie le niveau, l’énergie, les extrémités silencieuses, les variations entre échantillons, la présence d’une seconde harmonique et la reproductibilité. Les essais natifs de départ vérifient la synchronisation avec le jeu et le mélangeur.

L’aperçu local `Saved/Builds/20261004-kart-derby/start-grid-035.wav` est produit par un programme qui inclut le générateur C++ courant et applique les gains du jeu, volumes utilisateur à un. Il n’est pas normalisé artificiellement et ne contient pas le moteur. L’ancien rendu 0.34 est conservé dans `Saved/Validation/v034/start-audio/`.
