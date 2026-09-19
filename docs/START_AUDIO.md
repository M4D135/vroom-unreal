# Signal de départ — 0.34

Le départ utilise deux appels de préparation identiques, puis un appel plus long une quinte au-dessus. Le timbre réunit une fondamentale et quatre harmoniques, avec une attaque douce et une extinction progressive. Il remplace les sinusoïdes aiguës précédentes ; il ne prétend reproduire ni un signal réglementaire de F1 ni exactement le son commercial de Vroom.

| Feu | Fondamentale | Durée | Amplitude maximale de la voix |
|---|---:|---:|---:|
| Rouge | 392 Hz | 0,24 s | 0,76 |
| Orange | 392 Hz | 0,24 s | 0,76 |
| Vert | 588 Hz | 0,48 s | 1,00 |

`StartToneVoice`, dans `Source/Vroom/Public/VroomRaceRules.h`, est le générateur effectivement utilisé par le mélangeur. Les poids harmoniques totalisent un : le signal reste borné sans écrêtage. Les harmoniques deux à cinq prennent davantage de place pour un timbre plus clair, et une légère mise en présence conserve mathématiquement la plage −1…1. L’attaque dure **5,5 ms** et l’extinction 70 ms ; le corps reste soutenu après le début du son. Les gains existants du mélangeur et du composant audio restent inchangés, pour laisser entendre le moteur. La voix reste unique, sans nouvel objet sonore ni allocation PCM au changement de feu. Elle ne calcule plus d’oscillateur après sa fin.

Les états des feux, leurs instants de déclenchement et les règles de pause, de redémarrage et de volume nul restent inchangés. Le vert retentit lorsque le chronomètre atteint zéro ; sa fin accompagne les premiers instants d’accélération.

## Référence et limites

La banque locale `Content/Data/amiga_sound.bin` exporte moteur, crissements, accident et bruit blanc. Les sources de référence `sub0.asm`, section `inison`, et `system.asm`, section de gestion du son, décrivent aussi un échantillon de bond. Aucun de ces éléments n’a été identifié comme un son de départ autonome pendant cette recherche. Aucun départ original n’a été écouté ou authentifié ici : ce nouveau timbre est une adaptation du prototype, sans emprunt audio supplémentaire.

## Contrôles et écoute

Le test portable `Tests/race_rules_test.cpp` contrôle le niveau, l’énergie, les extrémités silencieuses, les sauts entre échantillons, la présence d’une seconde harmonique et la reproductibilité après redémarrage. Il passe après cette modification. Les **11 contrôles** de `Tests/start_verification_test.py` passent également ; ils vérifient les preuves de séquence, pause, redémarrage et silence, sans constituer une écoute réelle du package.

Le rendu de comparaison est disponible localement dans `Saved/Validation/v034/start-audio/` : `after-energetic.wav` est le son courant et `comparison-soft-energetic.wav` compare la première proposition à la version plus claire. Les premières comparaisons avec le bip aigu sont conservées séparément. Le son courant atteint un pic PCM de **4 515/32 767**, contre 3 474 pour la première proposition, soit **+2,28 dB en crête** et **+1,67 dB RMS** sur la même séquence. Le petit programme `render-energetic.cpp` inclut le vrai générateur C++ courant et applique les gains du jeu avec les volumes utilisateur à un. Les fichiers ne sont pas normalisés artificiellement et ne comprennent pas le moteur. L’écoute du mélange final dans le prochain package reste à effectuer.
