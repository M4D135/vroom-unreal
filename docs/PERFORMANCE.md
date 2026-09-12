# Fluidité — Vroom 0.22

Le joueur, les adversaires et les contacts partagent une simulation à 120 Hz. Le rendu interpole les états précédents et courants, y compris le saut et les phases de contact. En cas de retard, le plafond de douze pas conserve la phase fractionnaire : il ne réaffiche pas la position précédente.

Le filtrage du braquage est exponentiel et calculé dans la simulation, indépendamment du nombre d'images affichées. La zone morte de la manette conserve une transition continue. La rotation de caméra suit un quaternion avec interpolation exponentielle. Un dernier lissage après le rééchantillonnage à 5 m réduit les changements de courbure parasites du tracé.

Les prises des mains restent fixes dans le repère du volant et tournent avec lui ; les manches suivent les poignets sans palier de braquage. La main gauche monte en virage à droite et la droite en virage à gauche, tandis que l’autre quitte le cadre. Cette description du mouvement ne constitue pas une mesure de fluidité ou une validation visuelle. La mini-carte conserve son tracé en cache ; les douze adversaires utilisent quatre groupes d'instances et fournissent leurs transformations précédentes au rendu temporel. Chaque concurrent représente 3 182 triangles. L'herbe est instanciée et animée dans le matériau, sans modifier ses instances à chaque image.

## Profil Mac

Le préréglage Performance utilise une résolution interne de 70 %, TAA (`r.AntiAliasingMethod=2` sous UE 5.8), une cascade d'ombre sur 80 m, l'éclairage ambiant, des reflets désactivés, l'herbe clairsemée animée, un plafond de 60 images/s et VSync. Le menu permet de modifier puis sauvegarder ces réglages. Le choix TAA correspond bien à la valeur 2 ; la valeur 3 citée dans d'anciennes notes correspond à MSAA, pas à TAA.

Les mesures de la version 0.22 et leur portée sont consignées dans [VALIDATION.md](VALIDATION.md). Les anciens chiffres ne constituent pas une mesure de ce nouveau package. Aucune mesure RTX 5090 n'est encore disponible.

## Reproduction

La commande complète capture les contrôles visuels hors écran, puis mesure séparément deux parcours affichés à l’écran et une trace de mouvement :

```sh
python3 Tools/verify_mac.py --profile
```

Elle conserve les images, journaux, CSV et résultats JSON dans `Saved/Validation`. Les métadonnées CSV sont exclues du comptage des images ; les pics réels restent comptabilisés.

Pour un profil isolé, utiliser le package autonome, sans compilation concurrente ni capture PNG :

```text
-windowed -ResX=1440 -ResY=900 -VroomVerify -VroomAutoDemo -VroomStartDistance=3295 -VroomStartSpeed=30 -csvCaptureFrames=1800 -csvGpuStats -ExitAfterCsvProfiling
```

Pour 2560 × 1664, changer les dimensions et ajouter `-ForceRes`. Les CSV Mac sont dans `~/Library/Containers/com.YourCompany.Vroom/Data/Library/Application Support/Epic/Vroom/Saved/Profiling/CSV/`.

```sh
python3 Tools/analyze_performance.py '/chemin/Profile.csv' --warmup 120 --output /tmp/performance.json
```

Un essai distinct avec `-VroomMotionTrace -VroomExitAfter=20` permet de vérifier le déplacement affiché ; passer son journal avec `--motion-log`. Le script exclut les 120 premières images séparément dans chaque source et calcule les percentiles, le nombre d'images dépassant 20 ms, les retours de position et les variations de rotation. Il ne synchronise pas arbitrairement deux captures différentes.

Une moyenne à 60 images/s ne suffit pas à exclure des saccades : consulter aussi P95, P99, maximum et les traces de mouvement. Un parcours court ne couvre pas tous les circuits ni les compilations de shaders au démarrage.
