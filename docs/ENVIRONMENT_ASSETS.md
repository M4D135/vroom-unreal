# Ressources du décor — Vroom 0.34

Le sol, l’asphalte, les rochers et deux silhouettes de feuillus utilisent des ressources existantes sous CC0. La 0.34 ajoute des conifères et parasols à branches détaillées, trois niveaux de détail par espèce, des variations de matière et des bosquets plus fournis. Le mobilier routier devient plus rythmé ; un nouveau profil graphique Photoréalisme active l’éclairage Lumen et des ombres d’arbres proches.

**État courant : les dernières corrections de matière et de fondu sont dans les sources ; leur package final reste à construire.** Le décor attend l’inspection des captures et les mesures de ce package. Le précédent R4 a échoué sur le tour USA à **157,821 ms maximum** ; son [rapport et ses traces](../Saved/Validation/v0.34.0/20260919-193242-package/results.json) sont conservés, sans valider les derniers changements. Cette révision finale n’est pas encore installée ni publiée. Les validations de la **0.33 du 19 septembre 2026** restent ci-dessous comme historique ; elles ne certifient pas le coût ou l’apparence des ajouts 0.34.

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
- Le rocher source fait 8000 triangles. Le bootstrap construit trois niveaux de détail et vérifie leurs budgets. L’import **0.33 sous Unreal 5.8 du 19 septembre 2026** a confirmé **2000/800/240 triangles**, sans erreur ni avertissement (`Saved/Validation/v033/pbr-bootstrap.log`). Les deux niveaux éloignés sont réduits à partir du LOD0 déjà simplifié ; leurs ratios tiennent compte de cette étape. Cette géométrie et ses ratios restent identiques en 0.34.
- Les UV photographiques sont conservés. La conversion glTF Y-up vers Unreal Z-up vérifie les faces contre les normales source. La normale OpenGL du rocher inverse son canal vert à l’import.
- Le rocher conserve l’emprise et le pivot du précédent modèle. Les placements, distances de visibilité, activation des ombres et logique de collision du décor sont gérés dans le code du circuit, indépendamment de l’import.
- Les neuf textures restent en 1K, avec compression couleur/normal/masks Unreal et mipmaps. Elles restent résidentes pour éviter un chargement en plein virage des surfaces générées à l’exécution.
- Le revêtement de route utilise une projection au sol à 4 m ; l’herbe à 1,4 m. La légère teinte régionale du terrain est conservée.
- L’import conserve une géométrie classique, sans displacement ni tessellation. L’éclairage global dépend du profil de rendu sélectionné, décrit plus bas.

## Feuillus photographiques

`Tools/bootstrap_tree_assets.py` remplace `SM_EnvironmentBroadleaf` et `SM_EnvironmentBirch` après la création du décor de base. Les deux feuillus photographiques de la 0.33 sont conservés en 0.34. Le nom technique `Birch` conserve un emplacement du jeu ; le modèle importé est un feuillu générique, sans prétention d’identification botanique.

| Emplacement | Triangles LOD0 / LOD1 / LOD2 | Hauteur / rayon maximal |
|---|---|---|
| Broadleaf | 880 / 525 / 321 | 9,3 m / 4 m |
| Birch | 726 / 476 / 278 | 10,5 m / 2,2 m |

La [provenance complète](../Content/ArtSource/TreesRealistic/SOURCE.md) conserve le fichier Blender original et les trois textures photographiques de l’auteur. Le lecteur SDNA exporte positions, normales, UV et triangles en JSON sans lancer Blender ni exécuter de script embarqué. L’orientation des objets est appliquée ; les translations de présentation sont retirées. Les trois LOD partagent le même pied et la même transformation pour éviter les déplacements à distance. Unreal recalcule les normales et tangentes, certains LOD source ayant des normales mises en cache périmées.

```sh
python3 Tools/bootstrap_tree_assets.py --export --audit
```

**Historique 0.33, 19 septembre 2026 :** l’audit portable confirme les comptes ci-dessus, les enveloppes, les UV, le sens des faces et l’absence de triangles dégénérés. Un second export donne le même JSON. L’import Unreal 5.8 confirme les mêmes LODs d’auteur, sans erreur ni avertissement (`Saved/Validation/v033/tree-bootstrap.log`) ; la réduction automatique supplémentaire est désactivée. Les six captures de terrain du package 0.33 ont été inspectées. Ses huit profils et ses six tours complets passent en réglage Performance ; le [rapport 0.33](FINALISATION_033.md) détaille les conditions et les résultats, séparément de l’import.

Les deux matériaux de feuilles sont `Masked`, `Two Sided Foliage`, avec couverture alpha conservée dans les mipmaps au seuil source **0,45**. Le masque source est remis en couverture opaque autour de ce seuil avant multiplication par le fondu de distance ; le seuil final du matériau est **0,5** avec tramage temporel. La couleur RGB des photos reste intacte, sans peinture uniforme par la teinte régionale. Les trois textures sRGB restent résidentes et limitées à 1K. Le tronc utilise un masque de fondu, entièrement opaque avant la transition de distance. Les chemins des meshes et leurs métadonnées de propriétaire sont conservés pour rejouer le bootstrap complet ; les matériaux et textures ont leurs propres noms. L’import conserve les collisions des troncs et les distances de visibilité. Aucun vent supplémentaire n’est activé par cet import.

## Conifères et parasols 0.34

Le même bootstrap remplace désormais `SM_EnvironmentConifer` et `SM_EnvironmentUmbrella`. Les anciennes masses de feuillage opaques sont remplacées par des troncs et branches effilés, puis des cartes courbées portant un masque de rameaux et d’aiguilles. Le parasol possède une couronne horizontale irrégulière laissant apparaître ses branches. Les sommets contigus sont partagés pour permettre le recalcul de normales lisses sur l’écorce ; les coutures UV restent indépendantes.

| Emplacement | Triangles LOD0 / LOD1 / LOD2 | Limites de hauteur / rayon | Sections de matériaux |
|---|---|---|---|
| Conifer | 1040 / 484 / 116 | 11,5 m / 3 m | 2 |
| Umbrella | 498 / 288 / 210 | 9,8 m / 4,8 m | 2 |

Le conifère précédent comptait 920 triangles et le parasol 572, chacun sans LOD supplémentaire. Le nouveau conifère augmente légèrement le détail proche et réduit fortement le nombre de triangles éloignés. Les trois niveaux sont générés de façon déterministe ; ils partagent le pied et la même transformation d’emprise. Les seuils de taille écran sont **1 / 0,08 / 0,028**, sans réduction automatique supplémentaire par Unreal. Les noms des meshes, leurs propriétaires et les troncs physiques sont conservés.

Les deux matériaux sont l’écorce photographique existante `M_RealisticTreeBark` et `M_EnvironmentEvergreenNeedles`. Le second utilise `Masked` et `Two Sided Foliage`. Son masque d’aiguilles est découpé autour du seuil source **0,35**, sur une bande alpha étroite de 0,025, avant d’être multiplié par le fondu de distance ; le seuil final du matériau est **0,5**. Cela conserve les aiguilles proches opaques au lieu de traiter leur alpha filtré comme une transparence permanente. Il produit les aiguilles par calcul à partir des UV ; le masque tient compte de la taille du pixel pour adoucir les détails fins à distance. Une variation fixe par instance nuance le vert. Il ne comporte **ni WPO, ni dépendance au temps, ni animation CPU, ni texture supplémentaire**. Son coût de masquage et la stabilité des détails restent à contrôler dans le rendu final.

Les silhouettes et le masque d’aiguilles viennent du générateur du projet. L’écorce conserve la provenance **rubberduck / CC0** déjà documentée ; aucune photo de feuille caduque n’est utilisée comme aiguille. Aucun nouveau téléchargement, fichier source externe ou changement de licence des ressources importées n’est introduit.

Le contrôle portable du 19 septembre 2026 passe **6 tests** dans `Tests/environment_sources_test.py` : sources épinglées, géométrie/UV du rocher, budgets des textures, feuillus d’auteur, déterminisme, pied, emprise et budgets des nouveaux LOD, ainsi que câblage du masque et couverture des aiguilles avant le fondu. `python3 Tools/bootstrap_tree_assets.py --audit` expose les quatre espèces. Ces contrôles de données ne remplacent pas l’inspection visuelle et les mesures du package.

## Variations et bosquets 0.34

Le matériau de l’herbe ajoute des plages de teinte fraîche ou sèche à une échelle supérieure à sa texture de 1,4 m. Elles sont calculées en coordonnées du monde, sans animation. Entre 25 et 160 m, la couleur passe progressivement vers une seconde lecture de la même photographie, décalée et à une échelle 5,7 fois plus grande : un échantillonnage couleur supplémentaire, sans nouvelle texture. La photographie PBR et sa teinte régionale restent présentes. Le rocher conserve ses textures et sa géométrie ; sa couleur reçoit une légère variation fixe par instance afin de limiter les répétitions visibles.

Les placements du circuit forment davantage de bosquets cohérents : la distribution passe de **32 % de groupes de trois** à **42 % de groupes de quatre**, les autres placements restant isolés. Cela représente environ **38 % d’instances supplémentaires en moyenne théorique**, avant exclusions de placement ; les nombres réels dépendent du circuit. La distance minimale de sécurité de **24 m** par rapport à la route reste appliquée. Les performances de cette densité accrue seront mesurées avec les nouveaux modèles, et ne se déduisent pas des résultats 0.33.

Ces apports améliorent les silhouettes, la matière et la variété du décor ; ils ne constituent pas une scène entièrement photoréaliste. La validation visible et la cadence de la 0.34 restent à consigner après les tests du package produit.

## Transitions de végétation et de distance

Tous les matériaux d’un arbre, écorce comprise, activent la transition tramée entre LOD : un seul matériau opaque exclu de cette transition suffisait à conserver des changements secs de silhouette. Les matériaux des arbres et rochers lisent aussi `PerInstanceFadeAmount` pour respecter les débuts et fins de portée définis par les groupes d’instances. Le masque des feuilles est appliqué avant le fondu : les zones pleines restent opaques à proximité, les zones vides restent transparentes.

Les touffes d’herbe sont plus basses et leur hauteur varie en plages irrégulières. Leur disparition commence **24 m avant la portée maximale**, contre 12 m auparavant ; les limites restent **30/45/60 m** selon la qualité. Le vent existant reste calculé par le matériau. Aucun nouvel arbre, rocher ou brin n’est ajouté par cette correction de transition.

Ces changements n’augmentent ni la résolution des textures ni les distances maximales. Le masquage et les transitions peuvent néanmoins modifier le coût GPU ; les mesures du package final et la vérification du feuillage en mouvement restent nécessaires.

## Panneaux, balises et portiques 0.34

Les quatre publicités rétro déjà présentes sont réutilisées sur des panneaux de **8 × 2 m**, répartis tous les **180 m** environ, avec quelques paires décalées. Des portiques supplémentaires sont proposés tous les **750 m** ; le placement peut être décalé ou écarté lorsque la géométrie l’exige. Les balises trois/deux/une barres et les chevrons rouges et blancs rendent les approches des quatre virages retenus plus lisibles.

Les structures emploient les groupes d’instances et matériaux existants, sans nouvelle texture. Le nombre est borné à **64 panneaux**, **huit portiques supplémentaires** et **douze panneaux de chevrons** par circuit. Le placement vérifie l’emprise entière contre la spline la plus proche, protège séparément l’allée des stands et ancre les pieds des panneaux au terrain. Les portiques gardent leurs piliers hors piste et une garde verticale minimale prévue de 4,5 m au-dessus d’une autre portion de route. Les collisions restent déduites des structures opaques réellement créées.

Le journal `VROOM: Road furniture` expose le nombre de panneaux, portiques, balises et chevrons retenus pour chaque circuit ; `VROOM: Extra gantry` donne les distances. L’audit géométrique Unreal des six circuits reste nécessaire pour valider les placements construits.

## Profil Photoréalisme et ombres proches

Le cinquième profil (index **4**) emploie **TSR à 80 %**, Lumen GI et reflets au niveau **2**, ombres et végétation au niveau **3**, effets au niveau **2**, VSync et plafond à **30 images/s sur Mac**. Performance et les trois autres profils précédents restent disponibles ; leur comportement antérieur est conservé et quitter Photoréalisme rétablit le plafond normal de 60 images/s.

Sur Mac, ce profil conserve les filtres TSR Epic avec un historique à la définition de sortie (100 %), afin d’éviter le coût de l’historique suréchantillonné à 200 %. Le rendu interne reste à 80 % ; les autres profils rétablissent leur réglage habituel.

Les arbres visibles conservent leur distance de visibilité de 650 m. Des groupes dédiés participent uniquement aux ombres, activés avec Photoréalisme et limités à **160 m**, avec transition à partir de **120 m**. Cette séparation borne la portée des ombres supplémentaires. Elle ne constitue pas encore une mesure de leur coût : le rendu final à 30 images/s doit être testé séparément du profil Performance à 60 images/s.

Le mot Photoréalisme nomme ici le profil demandé ; il ne certifie pas qu’une capture du dernier package ressemble déjà à une photographie. La qualité des aiguilles, les transitions de LOD, les ombres et la cadence restent à examiner dans les essais visibles.

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
