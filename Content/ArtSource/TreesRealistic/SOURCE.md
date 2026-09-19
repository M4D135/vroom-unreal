# Arbres photographiques CC0 — provenance et reproduction

Source primaire : [free vegetation asset pack](https://opengameart.org/content/free-vegetation-asset-pack), publié par **rubberduck** le 23 janvier 2015, consulté le 19 septembre 2026. La page attribue l’ensemble à CC0 et l’auteur précise : « all textures are self made, using my own resources ». Licence complète : `LICENSE-CC0.txt` ; URL canonique : https://creativecommons.org/publicdomain/zero/1.0/.

Archive d’origine : https://opengameart.org/sites/default/files/free_vegetation_pack.zip

SHA-256 de l’archive : `eda8b21ea4723589ece0667be9a1a72fe7d7a0d22c7cd9ac33748c7346e8d29c`.

Le sous-ensemble conservé comprend le fichier Blender original et les trois textures réellement référencées par les deux arbres retenus. `manifest.json` fixe leurs SHA-256, ainsi que celui de l’export JSON. Aucune image n’est générée ou repeinte : les photographies et leur alpha restent ceux de l’auteur.

| Emplacement Vroom | Maillages source Blender | Triangles LOD0 / LOD1 / LOD2 | Enveloppe maximale |
|---|---|---|---|
| `SM_EnvironmentBroadleaf` | `Mesh.109`, `Mesh.017`, `Mesh.020` | 880 / 525 / 321 | hauteur 9,3 m, rayon 4 m |
| `SM_EnvironmentBirch` | `Mesh.012`, `Mesh.021`, `Mesh.023` | 726 / 476 / 278 | hauteur 10,5 m, rayon 2,2 m |

Ce sont deux silhouettes de feuillus ; le nom technique `Birch` conserve un emplacement existant et ne constitue pas une identification botanique. Les conifères ne sont pas remplacés par ce pack.

## Reproduire

Depuis la racine du dépôt :

```sh
python3 Tools/bootstrap_tree_assets.py --export --audit
```

Le lecteur SDNA charge exclusivement les structures statiques du fichier Blender 2.73 ; il n’exécute ni Blender, ni script embarqué. Il applique la rotation et l’échelle des objets, retire la translation de présentation, normalise le pied et l’enveloppe aux dimensions du décor existant, puis exporte positions, normales, UV par coin et triangles classés par texture. La transformation de référence LOD0 est partagée par les trois niveaux. Le changement de repère et l’orientation des faces sont vérifiés par l’audit.

Le bootstrap Unreal appelle `build_tree_assets()` après la création du décor de base. Les trois LOD sont ceux de l’auteur ; Unreal recalcule normales et tangentes à partir de la géométrie, car les normales mises en cache dans certains LOD source sont périmées. Les UV photographiques sont conservés. Les deux matériaux de feuilles sont **Masked**, seuil alpha **0,45**, **Two Sided Foliage** ; le tronc est opaque. Les trois textures sRGB, limitées à 1K et résidentes, évitent les chargements de texture en virage. Les mipmaps préservent la couverture alpha. Aucun traitement uniforme des couleurs régionales ne remplace les photos.

Les maillages gardent leur chemin et leur propriétaire `bootstrap_environment_assets.py:1`, permettant de rejouer le bootstrap complet. Les matériaux/textures dédiés portent `bootstrap_tree_assets.py:1`. Les placements HISM, distances de visibilité et troncs physiques du jeu restent inchangés. Aucune animation CPU ni nouveau vent n’est ajouté ; le rendu et son coût sont à vérifier dans le package visible avant validation finale.
