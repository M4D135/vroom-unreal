# Assets du prototype

Les assets sont reconstruits par des générateurs déterministes :

| Générateur | Contenu préparé |
| --- | --- |
| `Tools/bootstrap_assets.py` | Matériaux de scène, publicités et carte `/Game/Maps/Circuit` ; appelle les générateurs associés. |
| `Tools/bootstrap_cockpit_assets.py` | Matériaux procéduraux du cockpit, sans remplacer la carte. |
| `Tools/bootstrap_hand_assets.py` | Deux textures Valve et matériau `M_RacingGlove`. |
| `Tools/bootstrap_grass_assets.py` | `SM_GrassTuft` et matériau masqué `M_VergeGrass`, avec vent GPU. |
| `Tools/bootstrap_car_assets.py` | Dix meshes de monoplaces et cinq matériaux de carrosserie, carbone, pneus et jantes. |

Le monde et les rubans de piste sont construits par les classes C++ au lancement.
Les monoplaces chargent désormais les meshes dédiés décrits ci-dessous ; les
arbres et une partie des bâtiments utilisent encore les formes standard Unreal.
Les PNG publicitaires vivent dans `Content/ArtSource`. Les sept poses Amiga
restent des références sous `docs/reference/amiga` ; les gants affichés sont
les volumes Valve, pas ces sprites. Voir [AMIGA_ART.md](AMIGA_ART.md).

Les matériaux de scène sont généralement opaques, éclairés et double face ; l’herbe utilise un masque et le saute-vent une transparence. Les teintes de piste,
gravier, gazon et feuillage varient avec un bruit en coordonnées monde ; le métal
a une réponse métallique, les pneus une forte rugosité. Les vibreurs alternent
rouge et blanc tous les trois mètres grâce à `floor(frac(UV.y) * 2)`, puisque le
ruban définit `UV.y = distance_en_mètres / 6`.

| Matériaux | Usage et réponse |
| --- | --- |
| `M_Asphalt`, `M_Gravel`, `M_Grass`, `M_Curb`, `M_Foliage` | Sol, végétation et vibreurs procéduraux. |
| `M_VergeGrass` | Touffes masquées, teintes variables, éclairage de feuillage double face et déplacement des pointes par le vent. |
| `M_F1Paint`, `M_F1Pearl`, `M_F1Carbon`, `M_F1Rubber`, `M_F1Alloy` | Livrée par instance, panneaux clairs, éléments de carbone, pneus mats et jantes métalliques. |
| `M_White`, `M_Red`, `M_Metal`, `M_Dark`, `M_Black`, `M_Wood` | Carrosserie, pneus, métal et accessoires de scène. |
| `M_RacingGlove` | Gants Valve : textures de couleur et de normales, cuir recoloré ivoire, renforts sombres et coutures conservés, rugosité 0,8 et métal nul. |
| `M_GloveLeather` | Cuir ivoire mat des manchettes et détails, grain UV et micro-normale procédurale discrète. |
| `M_SuitFabric` | Manches ivoire, trame fine, rugosité élevée et micro-normale plus faible que le cuir. |
| `M_GlovePatch`, `M_GloveBlue`, `M_CuffOchre` | Renforts anthracite, détails cyan et poignets ocre. |
| `M_Carbon`, `M_Dashboard`, `M_BrushedMetal` | Tressage UV noir discret, tableau de bord granuleux et métal satiné. |
| `M_WheelNavy`, `M_CockpitSlate` | Volant bleu marine et entourage bleu ardoise. |
| `M_GaugeFace`, `M_GaugeMark`, `M_GaugeNeedle`, `M_GaugeGreen`, `M_GaugeAmber` | Fond sombre, caractères ivoire, aiguilles et voyants légèrement émissifs. |
| `M_MirrorSurface` | Matériau non éclairé ; paramètre texture `MirrorImage` alimenté par la capture arrière HDR du C++. |
| `M_VisorGlass` | Saute-vent translucide légèrement cyan, sans distorsion de la route. Opacité de 0,055 de face à 0,17 aux angles rasants. |

Les motifs des gants, manches et éléments du cockpit suivent **UV0**, afin de
rester attachés aux pièces pendant le braquage. Les meshes concernés doivent
fournir des UV, des normales et des tangentes cohérentes. Les matériaux procéduraux `M_GloveLeather` et `M_SuitFabric`
utilisent trois évaluations de bruit décalées pour former une normale tangentielle
normalisée ; aucune normal map externe n'est nécessaire. Leur micro-relief, ainsi
que leur variation de couleur et de rugosité, s'atténue avec l'empreinte UV du
pixel : pleine intensité jusqu'à 0,25 cellule par pixel, puis disparition
progressive à 0,5. Ce filtrage spatial vise à limiter le scintillement sans
dépendre de l'anticrénelage temporel.

## Gants Valve

Les mains proviennent de `openxr_glove_left_model_slim.glb`, modèle OpenXR de **Valve Corporation**, sous la [licence fournie](../Content/ArtSource/Gloves/LICENSE). La [provenance des fichiers](../Content/ArtSource/Gloves/README.md) accompagne les textures. La main gauche possède **3 028 sommets et 4 899 triangles**. Le modèle source gauche est utilisé sans miroir du côté gauche (`Side = -1`) ; la droite est obtenue par symétrie du Z local (`-Side`), avec symétrie des normales et reconstruction des tangentes à partir des UV.

`Tools/bake_valve_gloves.py` prépare la prise du volant à partir des articulations du modèle et écrit `Content/Data/gloves.json`. Le calcul de pose est effectué une fois en préparation. Le jeu charge le maillage posé, ses UV et ses normales dans deux composants gauche/droite attachés à la racine du cockpit. Chaque main possède sa propre transformation : sa prise reste fixe dans le repère de la jante, puis la même rotation est appliquée au volant et à la main. Cette relation évite que deux animations indépendantes déplacent le gant et son point de prise différemment. Les doigts ne subissent pas de déformation squelettique à chaque image. Régénérer la pose demande le GLB source et NumPy.

Le cockpit compte **onze composants**, dont deux gants distincts. `DrivingHandPose`, dans `VroomCockpit.cpp`, calcule les poignets à partir de la prise et du rayon du volant. L’axe traversant les doigts s’aligne avec la tangente de la jante ; l’orientation vise une lecture du dos des gants et des pouces à l’intérieur. La pose préparée referme les quatre doigts et fléchit le bout du pouce. Les paramètres géométriques sont ceux du prototype, pas des mesures anatomiques déduites des sprites.

Volant et mains utilisent la même commande visuelle filtrée. Le mouvement reste continu au neutre et pendant le braquage. Les manches mettent à jour les sommets de leur section existante pour suivre les poignets. Une main passant sous le cadre conserve sa prise ; elle n’est pas replacée ailleurs pour rester visible. Les sept poses Amiga servent de référence de composition, sans interpolation active de leurs coordonnées d’écran. La taille des gants est définie en 3D, indépendamment du format de fenêtre. Voir [COCKPIT_AMIGA.md](COCKPIT_AMIGA.md) pour le cadrage et [VALIDATION.md](VALIDATION.md) pour les observations du build testé.

`M_RacingGlove` utilise `T_RacingGloveColor` et `T_RacingGloveNormal`. Les sources **4 096 × 4 096** sont conservées dans `Content/ArtSource/Gloves/ValveGloveColor.jpg` et `ValveGloveNormal.png`. Le package Mac limite ces textures à **2 048 pixels**. Le shader recolore le cuir rouge en ivoire tout en conservant les renforts sombres et les coutures ; la normale glTF est adaptée à Unreal par inversion de son canal vert. Cette matière emploie une véritable texture de normales, distincte du micro-relief procédural des manchettes et des manches.

## Monoplaces

`Tools/bootstrap_car_assets.py` construit les formes en centimètres : nez effilé,
pontons et entrées d’air, cockpit ouvert avec pilote casqué, biellettes de
suspension, fond plat, aileron arrière à deux plans et aileron avant séparé.
Les roues possèdent un profil de pneu arrondi, des jantes et cinq branches.
Ce sont des modèles procéduraux du prototype, sans revendication de géométrie
commerciale identique à Vroom.

| Meshes | Organisation |
| --- | --- |
| `SM_F1PlayerPaint`, `SM_F1PlayerTrim`, `SM_F1PlayerCarbon` | Carrosserie du joueur, séparée selon les finitions. |
| `SM_F1FrontWing`, `SM_F1WheelFront`, `SM_F1WheelRear` | Aileron avant et roues séparés pour conserver leurs transformations propres. |
| `SM_F1OpponentPaint`, `SM_F1OpponentTrim`, `SM_F1OpponentCarbon`, `SM_F1OpponentWheels` | Quatre parties réutilisées par tous les adversaires. |

L’audit géométrique donne **3 182 triangles par concurrent** : 772 pour la
peinture, 122 pour les panneaux clairs, 608 pour le carbone et 1 680 pour les
quatre roues. Les douze concurrents utilisent **quatre composants ISM**, avec
une instance par voiture dans chaque composant. Il ne faut pas assimiler ce
nombre de composants à un nombre garanti de draw calls : certaines parties
ont plusieurs sections de matériau.

Six livrées — bleu, rouge, jaune, vert, violet et orange — sont affectées par
paires sur la grille. Trois valeurs de couleur par instance alimentent
`M_F1Paint`, sans dupliquer le mesh pour chaque couleur. Le rendu continue de
transmettre les transformations affichées précédentes pour les vecteurs de
mouvement. Le budget géométrique ne constitue pas une mesure de performance.

L’audit peut être reproduit sans démarrer Unreal ni écrire d’assets :

```sh
python3 Tools/bootstrap_car_assets.py --audit
```

## Herbe des accotements

`SM_GrassTuft` comporte deux cartes croisées subdivisées verticalement, soit
**16 triangles**. Les UV vont de zéro à la racine à un à la pointe. Le matériau
`M_VergeGrass` découpe sept brins effilés par carte et anime leurs pointes par
`WorldPositionOffset` ; le paramètre `WindStrength` laisse les racines fixes.
Aucune texture externe n’est nécessaire pour ces touffes.

Trois populations HISM permettent de choisir faible, moyenne ou forte densité,
ou de couper la végétation. Leur visibilité s’arrête à 30, 45 ou 60 m selon le
niveau. Les touffes évitent la route et les stands, ne projettent pas d’ombre,
et ne changent pas de transformation sur le CPU à chaque image. Le vent peut
être désactivé indépendamment dans les options graphiques.

## Bandeau de course

Le bandeau actif est dessiné avec `Canvas` et la fonte Roboto du moteur,
avec fond bleu nuit translucide, angles arrondis et accents cyan. Sa hauteur
de référence est 112 pixels sur 900. Carte, voyants, score, stage, tour,
dépassements et essence restent présents dans cette présentation moderne.
Le grand boîtier 3D essayé précédemment n’est plus appelé par le rendu actif ;
ses fonctions et assets peuvent encore exister dans le projet. Les chronos,
le retour de dérapage et les instruments de la vue de poursuite sont également
rendus par Canvas. Les instruments du cockpit restent de la géométrie 3D.

## Saute-vent et rétroviseurs

Le saute-vent utilise `BLEND_Translucent` et le mode économique `TLM_Surface`,
avec un terme de Fresnel dans l'opacité. Il conserve le test de profondeur et
n'ajoute ni réfraction ni capture supplémentaire. Son bord bleu est une pièce
géométrique distincte. Les rétroviseurs partagent une seule capture arrière
512 × 128 à 15 Hz ; le matériau utilise une texture blanche standard du moteur
comme valeur de paramètre par défaut, remplacée au lancement par le render target.

Exécution après compilation du module Editor :

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  /chemin/absolu/Vroom.uproject -run=pythonscript \
  -script=/chemin/absolu/Tools/bootstrap_assets.py -unattended -NullRHI -nosplash
```

Pour une itération limitée au cockpit, remplacer le chemin de script par
`/chemin/absolu/Tools/bootstrap_cockpit_assets.py`.

Les générateurs vérifient la métadonnée `Vroom.AssetGenerator` avant de
remplacer leurs assets : `bootstrap_assets.py:1` pour la scène et les matériaux
procéduraux, `bootstrap_hand_assets.py:1` pour les textures et le matériau Valve,
`bootstrap_grass_assets.py:1` pour l’herbe et `bootstrap_car_assets.py:1` pour
les meshes et matériaux des monoplaces.
Un asset homonyme attribué à un autre générateur provoque un arrêt. La carte
existante est conservée. Les vérifications explicites de connexion de graphe et d’enregistrement
interrompent la génération en cas d’échec. Les journaux de shaders et de cook
restent nécessaires pour constater la compilation effective des matériaux.

Les signatures utilisées ont été vérifiées dans les headers locaux UE 5.8 :
`MaterialEditingLibrary.h`, `EditorAssetLibrary.h`, `LevelEditorSubsystem.h` et
les `MaterialExpression*.h` sous `Runtime/Engine/Public/Materials`.
Les nouveaux graphes utilisent notamment `DDX`, `DDY`, `Length`, `Max`,
`Subtract`, `Saturate`, `AppendVector`, `Normalize` et `Fresnel`. Leurs entrées
et propriétés ont été vérifiées dans les headers UE 5.8 ; le pin de position
du bruit est explicitement `World Position` même lorsqu'on lui fournit des UV.
L'exécution dans Unreal reste la validation des noms Python et des shaders.

Ces matériaux et les volumes simples servent à valider la conduite et la
lisibilité. Ils ne constituent pas la direction artistique finale ni un rendu
photoréaliste.

## Publicités et feux de départ

Quatre textures 2048 × 512, au rapport 4:1, sont conservées dans `Content/ArtSource`. `Tools/generate_billboards.py` les reconstruit avec Pillow et les fontes Roboto fournies par le moteur. Ce sont des habillages typographiques créés pour le prototype, pas des scans revendiqués comme identiques aux publicités du jeu commercial. Leur patine est déterministe et leur texte reste lisible.

`Tools/bootstrap_scenery_assets.py` importe `T_Ad01` à `T_Ad04` avec mipmaps et groupe World, puis crée les quatre matériaux éclairés correspondants. Le script principal fait aussi cette étape pour reconstruire un clone neuf. Les matériaux et textures existants doivent porter la métadonnée du générateur avant remplacement.

`M_StartLight` utilise la valeur de donnée personnalisée numéro 0 de chaque instance pour allumer ses lentilles rouges. Les cinq colonnes du portique peuvent ainsi changer d'état ensemble sans créer d'acteurs supplémentaires.
