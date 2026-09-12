# Cockpit original Amiga

Ces images servent uniquement de référence pour la modélisation 3D : aucun sprite original n’est affiché dans le jeu modernisé.

Les sept PNG dans `docs/reference/amiga/` proviennent des données originales de Vroom Amiga. Ils sont destinés au remake personnel demandé par David. Aucun programme du jeu n'a été exécuté pour les extraire. L'archive du jeu et les blocs décompactés ne sont pas inclus dans ce dépôt.

## Provenance

- [Lankhor.net, page de préservation](https://www.lankhor.net/jeux.php?jeu=32&menu=tele) : [jeu32-amiga.zip](https://www.lankhor.net/jeux/jeu/jeu32-amiga.zip), entrée `Version 2/Vroom/Disk.1`. Ce site de préservation n'est pas présenté comme un dépôt officiel de l'éditeur.
- [Sources du patch WHDLoad Vroom](https://whdload.de/games/Vroom.html) : disposition des pistes et points d'entrée du chargeur.
- [Archive assembleur juillet 1991](https://www.oldies-but-goodies.fr/games/vroom/download/Atari%20ST/Vroom-ST-sources-juillet-91.zip) : routines de pixels, palette, masque et placement. Elle contient aussi des fichiers Amiga; son identité complète avec le jeu commercial n'est pas affirmée.
- [Vidéo Amiga AL82](https://www.youtube.com/watch?v=U6bsfmMaFGU), observée à 2:18 et 2:33 : validation visuelle des formes et couleurs, sans téléchargement vidéo.

| Donnée | Octets | SHA-256 |
|---|---:|---|
| Archive ZIP | 2469463 | `d8155a4fac27a14d292682a318875bfa85a318292fed14ec116d09c246b5d6c5` |
| Disk.1, version 2 | 924396 | `d0636fc8446a8909f04ed5ec54190eb386954098617c604ce2732bdc0deb9048` |
| Disk.1, version 3 | 924396 | `953f0dc276242f9b8e2d1f363bb6e507602d8a680560dc3f516f0bbc703f884e` |
| ITCR.DAT décompacté, fichier 08 | 7024 | `a77a7d8875277b6efc9af470d54c307b8b1a6075c23d0920c92b5c9aa768b891` |

Les versions 2 et 3 produisent exactement les mêmes 7024 octets pour le cockpit. `docs/reference/amiga/cockpit.json` conserve les SHA des sept PNG, les mots de palette, les couleurs RGB et les coordonnées.

## Poses, masque et affichage

| Index texture/PNG | Direction | Élément dans ITCR.DAT |
|---:|---|---:|
| 00 | gauche extrême | 0 |
| 01 | gauche moyenne | 1 |
| 02 | petite gauche | 2 |
| 03 | neutre | 6 |
| 04 | petite droite | 5 |
| 05 | droite moyenne | 4 |
| 06 | droite extrême | 7 |

Chaque pose mesure **96×28 pixels** et se place à **(112,172)** dans une image de **320×200**. Cette position correspond à `sub5.asm:DSPCAR`, offset inférieur `32000-160+56` et 28 lignes dessinées vers le haut. L'ordre vient de `OFVOIT`, puis de `TABVOL` dans `vr2.asm`.

`TABVOL` utilise `(volant+271)>>3`. Les sept plages inclusives sont `[-271,-168]`, `[-167,-56]`, `[-55,-8]`, `[-7,8]`, `[9,56]`, `[57,168]`, `[169,271]`. La zone neutre est donc plus étroite que dans un découpage uniforme.

Le masque natif rend l'**indice 0 transparent** : `CHVOI` calcule `NOT(plane0 OR plane1 OR plane2 OR plane3)`. Les creux des compteurs sont donc transparents eux aussi. Dessiner leurs fonds et indications avant le sprite permet aux gants de les masquer naturellement. Rectangles dans les coordonnées locales de la texture : rapports `(32,16,16,6)`, vitesse `(52,16,12,5)`, RPM `(32,25,32,2)`. Le fond sombre d'indice 0 est RGB `(0,0,36)` dans la palette de prévisualisation.

Les canaux de palette 3 bits sont étendus sur 0…255 : `round(c*255/7)`. Les quatre bits de chaque pixel encodé sont inversés conformément aux écritures de plans de `vroom.s:tabins2`. Les gants sont ivoire/cyan, les poignets ocre et la coque rouge. La conversion DAC Amiga décrite par `system.asm:SETPALETTE` ferait `34*c`; ce dépôt conserve l'extension RGB de prévisualisation explicitement indiquée dans le JSON.

## Reproduction et import

L'extracteur nécessite Python et Pillow. Il valide les empreintes de l'entrée et du bloc décompacté; aucun téléchargement ni émulation n'est effectué. Il ne produit que les sept PNG et leur manifeste.

```sh
python3 Tools/amiga/extract_cockpit.py --input /chemin/jeu32-amiga.zip
```

`--input` accepte aussi un `Disk.1` vérifié de version 2 ou 3; `--output` permet un répertoire de comparaison. Le décodeur traduit le Huffman et les répétitions du chargeur disque aux offsets `0x194A…0x1CC6`, puis le parcours horizontal alterné des sept éléments du fichier 08. Il ne requiert pas de désassembleur lors de sa réexécution.

```sh
UnrealEditor-Cmd Vroom.uproject -run=pythonscript -script=/chemin/Tools/bootstrap_amiga_assets.py -unattended -nullrhi
```

L'import facultatif de référence crée `/Game/DeveloperReference/T_AmigaCockpit00` à`06`, en dehors des répertoires cuits pour le jeu : RGBA8 non compressé, groupe UI, nearest, sRGB, sans mipmaps ni streaming, adresse clamp et alpha conservé. Il contrôle les SHA et dimensions RGBA 96×28 avant import, les dimensions obtenues ensuite, et refuse d'écraser un asset sans sa métadonnée `Vroom.AssetGenerator=bootstrap_amiga_assets.py:1`.

Cette commande ne fait pas partie du bootstrap du jeu. Les assets de référence ne sont pas embarqués dans le package.

Validation : les sept exports reproductibles sont identiques pixel pour pixel aux sept poses ouvertes et inspectées dans le dossier de recherche. Les PNG source contiennent uniquement les niveaux alpha 0 et 255.

Import Unreal 5.8 exécuté le 6 septembre 2026 : sept assets sauvegardés, dimensions et réglages vérifiés, sortie du processus 0, aucune erreur ni avertissement dans le journal d'import.
