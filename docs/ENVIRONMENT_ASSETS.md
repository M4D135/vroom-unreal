# Ressources réalistes du décor — Vroom 0.33

Le sol, l’asphalte, les rochers et deux silhouettes de feuillus utilisent des ressources existantes sous CC0. La voiture, le cockpit, les gants, la caméra et l’éclairage ne sont pas modifiés par cet import.

## Sources et licence

| Source | Auteur / bibliothèque | Ressource importée | Licence |
|---|---|---|---|
| [Asphalt010](https://ambientcg.com/view?id=Asphalt010) | ambientCG | Couleur, normale DirectX, rugosité 1K | [CC0](https://docs.ambientcg.com/license/) |
| [Grass004](https://ambientcg.com/view?id=Grass004) | ambientCG | Couleur, normale DirectX, rugosité 1K, pelouse de 1,4m | [CC0](https://docs.ambientcg.com/license/) |
| [Rock Moss Set 02](https://polyhaven.com/a/rock_moss_set_02) | Kless Gyzen / Poly Haven | Rocher08 et son atlas couleur/normale/rugosité 1K | [CC0](https://polyhaven.com/license) |
| [Free vegetation asset pack](https://opengameart.org/content/free-vegetation-asset-pack) | rubberduck | Deux feuillus avec trois LOD auteur, écorce et deux photos de branches détourées | [CC0, copie locale](../Content/ArtSource/TreesRealistic/LICENSE-CC0.txt) |

CC0 autorise la modification et la redistribution, y compris dans le dépôt du jeu. Les modèles importés restent identifiés par leur auteur d’origine. Les sources, les URL exactes et leurs empreintes sont conservées dans `Content/ArtSource/realistic/manifest.json` et `Content/ArtSource/TreesRealistic/manifest.json`.

## Import reproductible et limites

- `python3 Tools/fetch_environment_assets.py --check` vérifie toutes les sources locales sans réseau.
- `python3 Tools/fetch_environment_assets.py` les récupère depuis les bibliothèques d’origine. Une empreinte déjà connue qui change provoque une erreur.
- `Tools/bootstrap_environment_assets.py`, appelé par le bootstrap Unreal habituel, importe ces sources locales. Aucun téléchargement n’a lieu en jeu ou pendant le bootstrap.
- Le rocher source fait 8000 triangles. Le bootstrap construit trois niveaux de détail et vérifie leurs budgets. L’import Unreal 5.8 du 19 septembre a confirmé **2000/800/240 triangles**, sans erreur ni avertissement (`Saved/Validation/v033/pbr-bootstrap.log`). Les deux niveaux éloignés sont réduits à partir du LOD0 déjà simplifié ; leurs ratios tiennent compte de cette étape.
- Les UV photographiques sont conservés. La conversion glTF Y-up vers Unreal Z-up vérifie les faces contre les normales source. La normale OpenGL du rocher inverse son canal vert à l’import.
- Le rocher conserve l’emprise et le pivot du précédent modèle. Les placements, culling, activation des ombres et logique de collision du décor restent dans le code existant.
- Les neuf textures restent en 1K, avec compression couleur/normal/masks Unreal et mipmaps. Elles restent résidentes pour éviter un chargement en plein virage des surfaces générées à l’exécution.
- Le revêtement de route utilise une projection au sol à 4 m ; l’herbe à 1,4 m. La légère teinte régionale du terrain est conservée.
- Pas de displacement, tessellation, GI supplémentaire ou nouvel effet de post-traitement.

## Feuillus photographiques

`Tools/bootstrap_tree_assets.py` remplace `SM_EnvironmentBroadleaf` et `SM_EnvironmentBirch` après la création du décor de base. Les conifères et arbres parasols restent les silhouettes précédentes. Le nom technique `Birch` conserve un emplacement du jeu ; le modèle importé est un feuillu générique, sans prétention d’identification botanique.

| Emplacement | Triangles LOD0 / LOD1 / LOD2 | Hauteur / rayon maximal |
|---|---|---|
| Broadleaf | 880 / 525 / 321 | 9,3 m / 4 m |
| Birch | 726 / 476 / 278 | 10,5 m / 2,2 m |

La [provenance complète](../Content/ArtSource/TreesRealistic/SOURCE.md) conserve le fichier Blender original et les trois textures photographiques de l’auteur. Le lecteur SDNA exporte positions, normales, UV et triangles en JSON sans lancer Blender ni exécuter de script embarqué. L’orientation des objets est appliquée ; les translations de présentation sont retirées. Les trois LOD partagent le même pied et la même transformation pour éviter les déplacements à distance. Unreal recalcule les normales et tangentes, certains LOD source ayant des normales mises en cache périmées.

```sh
python3 Tools/bootstrap_tree_assets.py --export --audit
```

L’audit portable du 19 septembre confirme les comptes ci-dessus, les enveloppes, les UV, le sens des faces et l’absence de triangles dégénérés. Un second export donne le même JSON. L’import Unreal 5.8 confirme les mêmes LODs d’auteur, sans erreur ni avertissement (`Saved/Validation/v033/tree-bootstrap.log`) ; la réduction automatique supplémentaire est désactivée. Les six captures de terrain du package final ont été inspectées. Les huit profils et les six tours complets passent en réglage Performance ; le [rapport 0.33](FINALISATION_033.md) détaille les conditions et les résultats, séparément de l’import.

Les deux matériaux de feuilles sont `Masked`, seuil alpha **0,45**, `Two Sided Foliage`, avec couverture alpha conservée dans les mipmaps. La couleur RGB des photos reste intacte, sans peinture uniforme par la teinte régionale. Les trois textures sRGB restent résidentes et limitées à 1K. Le tronc est opaque. Les chemins des meshes et leurs métadonnées de propriétaire sont conservés pour rejouer le bootstrap complet ; les matériaux et textures ont leurs propres noms. Les placements HISM, collisions des troncs et distances de visibilité restent inchangés. Aucun vent supplémentaire n’est activé par cet import.

Ces apports améliorent la matière du décor ; ils ne constituent pas une scène entièrement photoréaliste. Les performances et l’intégration visuelle sont vérifiées sur le package produit, distinctement des contrôles portables des sources.

## Empreintes des fichiers livrés

| Fichier | Octets | SHA-256 |
|---|---:|---|
| `Asphalt010/Asphalt010_1K-JPG_Color.jpg` | 1093417 | `412598f9cfa046ff222a76ecd66fde576e42f5141938ffe3be2e2c0e9082e5c8` |
| `Asphalt010/Asphalt010_1K-JPG_NormalDX.jpg` | 2105871 | `f14feaa81ac68b62601e8fd63f3b71772ada78947aba87d57874d8f4ebd61e72` |
| `Asphalt010/Asphalt010_1K-JPG_Roughness.jpg` | 746915 | `5f0f9ee453565de9fdf838140a425f59b215ca3c87a3c269e39247d6f688c13f` |
| `Grass004/Grass004_1K-JPG_Color.jpg` | 1985561 | `9e1c60da44b34a9738b1256ba827541f097dc583521b1d281e61b5f4b4217bc5` |
| `Grass004/Grass004_1K-JPG_NormalDX.jpg` | 2339065 | `c375ab178faa4df0b0e0b13dabc5594ab8e8e8220a9d40e8815ff8e602ec058c` |
| `Grass004/Grass004_1K-JPG_Roughness.jpg` | 851664 | `706580983860f942e141acdd68c4128dd683be50fcc5d85b7c67a9a3a8c7bac0` |
| `rock_moss_set_02/rock_moss_set_02_1k.gltf` | 11608 | `c3b992a7622e94f7d8da97a47277a2a5177a513af0312a127ec908d4056c97db` |
| `rock_moss_set_02/textures/rock_moss_set_02_nor_gl_1k.jpg` | 273527 | `dd2503789089fb72ef469efd14364d393e769c0de8781a251475cac7e0b6f51d` |
| `rock_moss_set_02/textures/rock_moss_set_02_diff_1k.jpg` | 194307 | `91dd2cc77d5198b067d77e5bdb3fa8ee7b8950aeff5aec2c1ccaddb76dcca481` |
| `rock_moss_set_02/textures/rock_moss_set_02_rough_1k.jpg` | 146226 | `12252219a98a560940359973e83af831abdcfab0ead3f0a2a7b135a0143105f1` |
| `rock_moss_set_02/rock_moss_set_02.bin` | 1316572 | `2df9d8eaaca4207529b0f613d4886d3962d0b9ffc0cdfd2bc4bf35dbcd391cb5` |
