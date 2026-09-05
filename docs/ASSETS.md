# Assets du prototype

`Tools/bootstrap_assets.py` crée onze matériaux Unreal et `/Game/Maps/Circuit`.
Le monde, les rubans de piste et les voitures sont construits par les classes C++
au lancement. Les formes de base viennent du contenu standard d'Unreal Engine.
Aucune texture ni aucun modèle externe n'est nécessaire.

Les matériaux sont opaques, éclairés et double face. Les teintes de piste,
gravier, gazon et feuillage varient avec un bruit en coordonnées monde ; le métal
a une réponse métallique, les pneus une forte rugosité. Les vibreurs alternent
rouge et blanc tous les trois mètres grâce à `floor(frac(UV.y) * 2)`, puisque le
ruban définit `UV.y = distance_en_mètres / 6`.

Exécution après compilation du module Editor :

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  /chemin/absolu/Vroom.uproject -run=pythonscript \
  -script=/chemin/absolu/Tools/bootstrap_assets.py -unattended -NullRHI -nosplash
```

Une nouvelle exécution reconstruit les matériaux portant la métadonnée
`Vroom.AssetGenerator=bootstrap_assets.py:1` ; un matériau homonyme sans cette
métadonnée provoque un arrêt pour préserver un asset édité ailleurs. La carte
existante est conservée. Les échecs de connexion du graphe, de compilation de
matériau ou d'enregistrement provoquent une exception explicite.

Les signatures utilisées ont été vérifiées dans les headers locaux UE 5.8 :
`MaterialEditingLibrary.h`, `EditorAssetLibrary.h`, `LevelEditorSubsystem.h` et
les `MaterialExpression*.h` sous `Runtime/Engine/Public/Materials`.
L'exécution dans Unreal reste la validation des noms Python et des shaders.

Ces matériaux et les volumes simples servent à valider la conduite et la
lisibilité. Ils ne constituent pas la direction artistique finale ni un rendu
photoréaliste.
