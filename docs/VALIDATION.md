# Validation du prototype — 5 septembre 2026

## Résultats vérifiés

- Compilation C++ des cibles VroomEditor et Vroom, Mac Apple Silicon Development, sous Unreal Engine 5.8.0 : réussie.
- Génération des onze matériaux et de la carte : commandlet Python terminé avec zéro erreur et zéro avertissement.
- Extraction des six circuits depuis les textes de juillet 1991 : JSON régénéré identique octet pour octet au fichier versionné. Les six cumuls de distances correspondent aux longueurs CLAP.
- Tests C++17 du modèle de conduite : tous réussis avec `-Wall -Wextra -Werror -pedantic`. Mesures du prototype : 297,215 km/h à dix secondes et 325,341 km/h à quatre-vingt-dix secondes, plein gaz en ligne droite.
- Application Mac construite, ressources préparées, intégrées et archivées avec BuildCookRun. L'étape `-package` est nécessaire sous UE 5.8 pour incorporer le contenu dans le bundle autonome. Le bundle contient les PAK/IoStore et le JSON des circuits.
- Lancement direct du binaire dans l'application livrée, depuis un dossier extérieur au projet : réussi. Circuit Autriche chargé à 5911,0 m ; police Roboto Bold embarquée chargée ; sortie automatique normale, code zéro.
- Capture réelle de l'application autonome : **3840 × 2160 pixels**, vue poursuite, route, vibreurs, mini-carte, chronomètre, vitesse et rapports visibles. À cette capture, le pilote de test roulait à 222,2 km/h, distance 258,1 m, écart latéral 0,06 m. Il s'agit d'une résolution de sortie vérifiée, sans certification du débit d'images ni du rendu natif interne.
- Essai automatique précédent dans le moteur : 1552,1 m parcourus, 187,0 km/h et écart latéral −1,03 m au moment de la capture. Il ne constitue pas un tour complet ni une comparaison avec le jeu ST.

La revue des captures a permis de corriger l'orientation des triangles de la chaussée et la police du HUD. Le menu a ensuite été contrôlé à 1440 × 900 et la course à 3840 × 2160.

## Limites du contrôle

Le Mac s'est verrouillé pendant le travail. Le contrôle manuel des touches et de la manette n'a pas pu être réalisé ; les essais de conduite ci-dessus utilisent le pilote automatique de test. Les six données de circuit sont chargées et proposées au menu, mais seul le circuit Autriche a fait l'objet de ces essais de roulage et captures. Aucun test de tour complet, de championnat ou de fidélité comparative n'a été effectué.

Le PC `david-pc` ne répondait pas en SSH sur son adresse configurée au début de la séance. Aucune compilation Windows, aucun test GPU NVIDIA et aucune intégration DLSS n'ont été effectués.

Les journaux comportent des avertissements du moteur, dont Enhanced Input non utilisé par le contrôleur actuel, un matériau auxiliaire de composant de ligne absent et une interrogation de fréquence audio. L'application a terminé normalement ; aucun test d'écoute comparative n'a été réalisé. La préparation initiale des pipelines graphiques provoque des pauses : aucune promesse de 60 images/s n'est faite.

## Captures dans l'application Mac

Le bundle généré utilise le sandbox macOS. Un chemin de capture arbitraire dans Documents peut être refusé. Pour la vérification, la capture a été écrite dans le dossier Documents de son conteneur, puis copiée dans les livrables :

```text
~/Library/Containers/com.YourCompany.Vroom/Data/Documents/
```

Les journaux de l'application autonome sont dans le sous-dossier `Data/Library/Logs/Vroom` du même conteneur. Les journaux de la version Editor sont dans `~/Library/Logs/Vroom`.
