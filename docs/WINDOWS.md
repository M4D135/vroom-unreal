# Construire Vroom 0.34 sur Windows

## Livraison 0.35 — 4 octobre 2026

La 0.35 est compilée avec UE 5.8.3 et passe onze scénarios ciblés visibles sur le PC RTX 5060 Laptop : [rapport](validation-035-windows.json), [bilan commun](FINALISATION_035.md). Le raccourci bureau Vroom ouvre cette version. Le package actif se trouve dans `C:\Vroom-Build-20260923\Packages\Win64-035-final-20261004`.

Utiliser le raccourci ou `Windows\Jouer.cmd` : ils appellent l’exécutable natif `Windows\Vroom\Binaries\Win64\Vroom.exe`, comme le vérificateur. Le petit bootstrap `Windows\Vroom.exe` a été refusé sur ce PC par le contrôle d’application Windows (4551). Les paramètres de sécurité n’ont pas été modifiés. Les anciens dossiers sont conservés ; les mesures 0.34 ci-dessous restent historiques.


**État au 23 septembre 2026 : le build Windows `010` a réussi les 39 contrôles de la suite complète.** Unreal Engine 5.8.3 a compilé et empaqueté le jeu ; les essais visibles en DirectX 12 sur RTX 5060 Laptop GPU comprennent 32 scénarios fonctionnels, un profilage et six circuits en endurance. Le build `011` ajoute le masquage commun du démarrage et `-NoSplash` aux lanceurs : ses cinq contrôles ciblés et la revue des premières images passent également. Il est lancé en session interactive avec le raccourci bureau `Vroom`. Les six essais longs restent des mesures du binaire `010`, distinctes des tests ciblés de `011`.

Une archive nommée **`Vroom-0.34.0-Windows-Sources-<commit>.zip` est un kit de compilation**, contenant les sources et assets du dépôt au commit indiqué. Elle doit être extraite puis compilée selon ce guide ; ce n’est pas le ZIP du jeu prêt à lancer.

## Préparer le PC

- Installer **Unreal Engine 5.8** pour Windows avec les composants de compilation Win64.
- Installer **Visual Studio 2022 17.14 ou ultérieur**, ou **Visual Studio 2026 18.0 ou ultérieur**, avec les charges de travail C++ bureau et développement de jeux C++.
- Installer un **Windows SDK 10.0.22621.0 ou ultérieur**. Epic recommande 10.0.26100 ou plus récent ; suivre sa [configuration officielle de Visual Studio pour UE 5.8](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine) pour les composants complémentaires.
- Installer Git pour récupérer le dépôt. **Python 3.10+** sert aux contrôles automatisés ; il n’est pas nécessaire au simple lancement du jeu empaqueté.

Cloner le [dépôt](https://git.estrasse.fr/david/vroom-unreal), avec ses fichiers `Content` versionnés, dans un chemin simple tel que `C:\Dev\vroom-unreal`. Ouvrir PowerShell dans ce dossier. Les scripts n’installent pas ces outils et ne configurent aucun service distant.

## Compiler et créer le ZIP

```powershell
.\Tools\build_windows.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.8' -Configuration Development -CreateZip
```

Adapter `-EngineRoot` si Unreal est installé ailleurs ; la variable `VROOM_UE_ROOT` permet aussi de fournir ce chemin. Le mode par défaut compile l’éditeur puis utilise AutomationTool pour construire, cuire et empaqueter le jeu Windows. Il conserve les assets versionnés. Fermer le jeu et l’éditeur avant de construire dans le même dossier.

Après réussite effective, les sorties attendues sont :

| Sortie | Contenu |
|---|---|
| `Saved/Packages/Win64/` | Application Windows et fichiers nécessaires à son lancement |
| `Saved/Packages/Win64/vroom-build.json` | Manifeste versionné par schéma, identité du binaire, des sources et du contenu empaqueté |
| `dist/Vroom-0.34.0-Win64-Development.zip` | Archive transportable du package Development |
| `dist/Vroom-0.34.0-Win64-Development.zip.sha256` | Empreinte SHA-256 du ZIP pour contrôler son transfert |

Le manifeste de schéma 2 porte les empreintes de tous les fichiers du package, en plus du binaire et des sources. Le ZIP contient `vroom-build.json` à sa racine et le dossier `Windows/`, avec `Jouer.cmd`, `LISEZ-MOI.txt`, le jeu, ses données, les prérequis Visual C++ et `Licences/`. Le script fournit explicitement ce sous-dossier à AutomationTool : UE 5.8 n’ajoute pas automatiquement `Windows/` lorsqu’un autre composant du chemin commence déjà par `Win64` ou `Windows`. Les chemins du tableau sont les sorties par défaut ; une compilation avec des chemins explicites, comme celle du 23 septembre, produit les mêmes éléments aux emplacements demandés.

Le dossier d’archivage doit être vide et un ZIP existant n’est jamais écrasé. Pour reconstruire en conservant un package précédent, choisir deux nouveaux chemins :

```powershell
.\Tools\build_windows.ps1 -Configuration Development -ArchiveDirectory 'Saved\Packages\Win64-r2' -CreateZip -ZipPath 'dist\Vroom-0.34.0-Win64-Development-r2.zip'
```

Ajouter le même `-ArchiveDirectory` aux commandes de lancement et de vérification pour cibler cette nouvelle construction.

`-Mode Editor` construit seulement le module éditeur. `-Mode Assets` régénère explicitement les assets. `-Configuration Shipping` demande une autre construction ; elle n’hérite d’aucune validation Development et n’est pas utilisée par la suite de mesure.

## Lancer et contrôler

Depuis le clone ayant construit le package :

```powershell
.\Tools\run_windows.ps1 -ExpectedVersion 0.34.0 -Windowed -Width 1920 -Height 1080
```

Le lanceur recherche le binaire natif, vérifie sa version et l’intégrité de tout le package, puis démarre le jeu en DirectX 12 sans reconstruire. Pour utiliser le ZIP sur un autre PC, extraire **toute l’archive**, puis ouvrir **`Windows\Jouer.cmd`** (ou `WindowsNoEditor\Jouer.cmd`). Ce lanceur ouvre une fenêtre 1920 × 1080 en DirectX 12 et utilise le dossier utilisateur pour les sauvegardes. Unreal Engine, Visual Studio et Python ne sont pas nécessaires pour jouer au package.

Sans préférences déjà enregistrées, le profil Windows démarre désormais en **fenêtre 1920 × 1080, Ultra, plafond 60 FPS**. Les choix du joueur chargés depuis sa sauvegarde restent prioritaires sur ce profil initial. Le réglage natif `r.Shadow.Virtual.NonNanite.IncludeInCoarsePages=0` évite l’inclusion des maillages non Nanite dans les pages grossières des ombres virtuelles ; `r.TSR.History.ScreenPercentage=100` réduit le coût de l’historique TSR. Ces valeurs ont été essayées dans les sondes décrites ci-dessous ; elles ne garantissent pas 60 FPS sur tout matériel ou tout circuit.

Si le runtime Visual C++ manque, lancer `Engine\Extras\Redist\en-us\vc_redist.x64.exe` depuis le dossier de jeu extrait, puis réessayer. UE 5.8 fournit aussi le redistribuable ARM64 ; la cible de ce jeu est x64. Le seul `.exe`, séparé de ses données, ne suffit pas. L’ouverture de `Jouer.cmd` ne remplace pas les contrôles d’intégrité et de comportement du vérificateur.

Sur le PC avec le clone et un bureau Windows interactif, régler d’abord les graphismes et le plafond dans le jeu, puis lancer :

```powershell
.\Tools\verify_windows.ps1 -ExpectedVersion 0.34.0 -Profile -Soak -TargetFPS 60
```

Le défaut attend une **RTX 5090** ; pour une autre carte, fournir son nom attendu avec `-ExpectedGPU`. `-TargetFPS` choisit les seuils d’acceptation, sans modifier les options graphiques. Les captures sont visibles ; les rapports, journaux et CSV sont enregistrés dans `Saved/Validation/v0.34.0/<date>-windows/`. Les critères 60/120 Hz et le contrôle DLSS sont décrits dans [RTX5090.md](RTX5090.md).

Pour un essai reproductible sur le PC RTX 5060, `-GraphicsPreset 3` applique temporairement le profil **Ultra**, avec antialiasing natif et plafond de **60 FPS**, sans sauvegarder les préférences graphiques du joueur :

```powershell
.\Tools\verify_windows.ps1 -ExpectedVersion 0.34.0 -ExpectedGPU 'NVIDIA GeForce RTX 5060 Laptop GPU' -GraphicsPreset 3 -Width 1920 -Height 1080 -Profile -Soak -TargetFPS 60
```

Ajouter `-ArchiveDirectory` pour désigner le package effectivement construit. L’option est transmise au jeu sous la forme `-VroomGraphicsPreset=3`. Sans `-GraphicsPreset`, les choix graphiques sauvegardés restent utilisés. Le plafond demandé et `-TargetFPS 60` ne constituent pas une mesure de cadence obtenue.

Les captures de performance et d’endurance Windows utilisent `-csvNoProcessingThread` avec `-ExitAfterCsvProfiling`. Deux captures natives de 1 800 images reproduisaient un crash de fermeture du thread de traitement CSV d’UE 5.8.3, code `777003` ; le même essai avec traitement CSV synchrone a produit son fichier et quitté avec le code `0`. Ce drapeau est absent des scénarios fonctionnels. Les seuils de validation et les 120 images de chauffe écartées de l’analyse restent inchangés. Le vérificateur rejette également les avertissements natifs de débordement de la file VSM et du pool de pages, même lorsqu’un scénario annonce `Verification PASS`.

Un ZIP construit n’est pas une validation de jeu. Le package `010` a réussi la suite complète décrite ci-dessous, sur le matériel et le profil indiqués. Le build suivant `011` a passé les cinq contrôles ciblés décrits plus bas ; les prérequis sur une autre machine vierge, les contrôleurs physiques et DLSS restent à vérifier. Aucun service de compilation cloud n’a été installé.

L’outillage a été contrôlé sur Mac : **cinq fichiers PowerShell analysés sans erreur** et **28 assertions sur des packages factices et leur ZIP**. Les **27 tests Python actuels du vérificateur passent sur Mac et Windows**, y compris le rejet des avertissements VSM et la présence du drapeau CSV uniquement pendant les captures de performance/endurance. Les contrôles Mac utilisent PowerShell 7.6.6 et des fixtures, sans exécuter Unreal ni un jeu Windows. Ces [preuves et limites](RTX5090.md#contrôles-de-loutillage-sur-mac) restent distinctes des essais natifs décrits ci-dessous.

### Préparation native du 23 septembre 2026

Les sources du commit `6191b5a751c8907c28542e1f5c6b79a5e260d3c6` ont été extraites dans `C:\Vroom-Build-20260923\Project\vroom-unreal`. L’archive transférée a été vérifiée sur les deux machines : SHA-256 `22c56d0ddea02fc95c032c1b004a84fd7c334c7cba79698723a05defd4c0d664`.

Visual Studio 2022 17.14.41 et les SDK Windows sont disponibles. La compilation native a effectivement utilisé le **compilateur MSVC 14.44.35229**, installé dans le dossier de toolchain **14.44.35207**, et le **SDK Windows 10.0.22621.0**. La présence du SDK 10.0.26100.0 ne signifie donc pas qu’il a été sélectionné pour ce build. Python 3.13.15 est installé dans le dossier `Tools\Python313` de cette préparation, sans modification du PATH global.

Sous Windows PowerShell 5.1, les cinq scripts se parsèment sans erreur et les **28 assertions de packages factices passent**. Le premier passage des tests Python a révélé quatre échecs causés par le parseur POSIX `shlex` dans le faux processus de test. La fixture conserve désormais les arguments structurés tout en appelant la véritable fonction de génération de commande Windows. Avec ce correctif et un cas couvrant les chemins Windows/POSIX, **20 tests Python passent sous Windows et sur Mac**. Ce correctif de fixture ne modifiait ni les scripts de production ni les assets du jeu ; les corrections natives suivantes sont décrites séparément.

Les journaux locaux se trouvent sous `C:\Vroom-Build-20260923\Logs` : `powershell-syntax-001.json`, `windows-package-fixtures-001.log`, `windows-verification-fixtures-002.log` et `preflight-receipt-20260923.json`. Le fichier de test initial est conservé dans ce même dossier. Ces essais ne lancent aucun jeu.

Le premier lancement natif a également révélé que Windows PowerShell 5.1 évaluait le défaut de `ProjectDir` avant de renseigner `$PSScriptRoot`. Les trois points d’entrée `build_windows.ps1`, `run_windows.ps1` et `verify_windows.ps1` résolvent maintenant ce défaut dans leur corps, en conservant la priorité des chemins explicitement fournis. Les sondes natives sans moteur/package disponible échouent désormais sur les préconditions attendues, sans erreur de liaison du chemin de projet (`entrypoint-*-probe.log`). La compilation a pu commencer avec un chemin de projet explicite.

Le catalogue Windows obtenu via Legendary 0.21.1 retournait zéro asset, y compris avec `--include-ue`. L’utilisateur a ensuite lancé l’installation depuis Epic dans `C:\Program Files\Epic Games\UE_5.8`. **L’installation d’Unreal Engine 5.8.3, changelist 58210709, est maintenant terminée.** Un raccourci `Projet Vroom` sur le bureau pointe vers le `.uproject`.

Les cibles **Editor et Game Development Win64 ont compilé**, puis la cuisson des assets, l’empaquetage et la création du ZIP ont réussi. Le premier archivage UAT plaçait les fichiers directement dans le dossier `Win64-<date>` ; le script fournit désormais un sous-dossier `Windows` explicite, conforme au manifeste et au lanceur de distribution. Le runtime Visual C++ **14.50.35719** a été installé avec le code retour **3010** (réussite avec redémarrage demandé). Aucun redémarrage n’a été effectué pendant cette passe.

### Build 010 : suite complète réussie

Le package **`Win64-20260923-164051`**, binaire SHA-256 `5523f2fc3d1ceeef687a8f67b2b6b74a817f422157110584dbb5b1a5d7affbbc`, a terminé la série **`20260923-164326-windows`** avec `completed=true` : **39/39 PASS, tous les codes de sortie à 0**. Les 32 scénarios fonctionnels, le profilage et les six circuits en endurance passent les contrôles d’exécution, de rendu journalisé, de cadence et de mouvement applicables. Chaque endurance confirme un tour complet. Les avertissements VSM précédemment observés sont désormais bloquants dans cette suite.

Les essais ont lieu en **DirectX 12**, sur **NVIDIA GeForce RTX 5060 Laptop GPU**, dans la **session Windows interactive 2**, à **1920 × 1080, Ultra, plafond 60 FPS**, sans DLSS. Le rapport est conservé sous `/Users/david/Documents/Codex/2026-09-07/re/windows-native-validation-20260923/diagnostics/20260923-164326-windows/results.json` ; les chemins des journaux et CSV natifs figurent dans ce rapport.

Les 120 premières images de chaque capture sont écartées. Le profilage analyse **1 680 images / 28 secondes** ; chaque endurance analyse **7 680 images / environ 128 secondes**.

| Capture | FPS moyens | p99 image (ms) | Maximum image (ms) | Images > 20 ms | GPU moyen (ms) |
|---|---:|---:|---:|---:|---:|
| `performance` | 59,999 | 17,250 | 24,390 | 3 / 1 680 | 12,816 |
| `soak-track-1` | 59,999 | 17,431 | 26,568 | 21 / 7 680 | 14,050 |
| `soak-track-2` | 59,998 | 17,661 | 27,089 | 32 / 7 680 | 13,962 |
| `soak-track-3` | 59,998 | 17,438 | 25,848 | 24 / 7 680 | 14,017 |
| `soak-track-4` | 59,999 | 17,671 | 27,160 | 28 / 7 680 | 13,999 |
| `soak-track-5` | 59,997 | 17,583 | 26,280 | 23 / 7 680 | 13,971 |
| `soak-track-6` | 59,999 | 17,298 | 26,393 | 19 / 7 680 | 13,994 |

Les sept captures satisfont les seuils enregistrés : moyenne ≥ 59 FPS, p99 ≤ 18 ms, maximum ≤ 33,503 ms et au plus 0,5 % d’images au-delà de 20 ms. Des pointes jusqu’à **27,160 ms** sont mesurées ; ce résultat ne garantit pas une durée uniforme de 16,67 ms par image. Les traces de mouvement ne présentent aucun recul de distance ; leurs maxima sont **0,747°** entre échantillons et **121,942°/s²** d’accélération angulaire, sous les limites de 3° et 300°/s². Les traces et les CSV ont une chauffe indépendante, sans alignement temporel entre échantillons.

Cette validation concerne **ce binaire et ce profil sur cette machine**. Les captures visibles nécessitent une revue visuelle pour les défauts que les marqueurs automatiques ne détectent pas. Elle ne certifie pas le 4K/120 Hz, un autre GPU, DLSS, les périphériques physiques, un PC vierge ni les versions Mac/Linux.

### Diagnostics conservés : builds 006 et 009

Le premier scénario `neutral` avait échoué avec `Screenshot missing`, sa limite de six secondes expirant avant une préparation PSO d’environ huit secondes. La correction de `PreviewClock` a permis de produire les captures. La première suite du build `006`, `20260923-155713-windows`, passait 32 scénarios fonctionnels mais présentait des avertissements `[VSM] Non-Nanite Marking Job Queue overflow` et un crash de profilage `777003` ; son CSV à 59,46 FPS / p99 19,38 ms ne validait donc pas cette construction.

Les diagnostics sont conservés dans le même dossier local `diagnostics`, avec la comparaison `windows-native-validation-20260923/diagnostic-comparison.json`. Les reçus `probe-20260923-161519-performance-repeat` et `probe-20260923-161639-performance-no-thread` isolent le crash CSV : seconde reproduction `777003`, puis sortie `0` en ajoutant seulement `-csvNoProcessingThread`.

La comparaison graphique suivante utilise le traitement CSV synchrone dans **les deux** sondes ; elle distingue son effet sur la fermeture du gain GPU apporté par les réglages combinés de pages grossières et d’historique TSR.

| Sonde de 1 800 images, 1 680 analysées | FPS moyens | p99 (ms) | Maximum (ms) | GPU moyen (ms) | Seuils |
|---|---:|---:|---:|---:|---|
| `161639-performance-no-thread` | 59,770 | 18,792 | 22,272 | 15,267 | Échec p99 |
| `161813-performance-tsr100` | 59,997 | 17,465 | 18,706 | 13,985 | PASS |

Ces sondes courtes ne suffisaient pas : le build `009` a encore échoué au départ sur le débordement VSM. Les sondes du départ et du stand ont isolé le suréchantillonnage des ombres directionnelles autour de la voiture. Le build `010` ramène les deux biais de résolution VSM directionnelle de −1,5 à 0 sur Windows, en conservant les ombres virtuelles et le contrôle bloquant de leurs avertissements. C’est cette construction qui a ensuite passé les six circuits ci-dessus.

### Build 011 : démarrage corrigé, cinq contrôles ciblés PASS

Le package `C:\Vroom-Build-20260923\Packages\Win64-20260923-171119` intègre le correctif partagé **`e02ac13`**. Compilation Editor/Game, cook et empaquetage réussis. ZIP `Vroom-0.34.0-Win64-20260923-171119.zip`, SHA-256 `93e1a268ded402813a668f192a001536a340977b796759ab3ae71d91c9593fb1`.

La série visible `startup-validation-20260923-151521-006-b7131450` passe **5/5** : lancement normal, quatre premières images, navigation des menus, départ, puis profilage séparé. Chaque processus sort à zéro ; les empreintes du package et de ses sources restent conformes. Rapport SHA-256 `db5bcf67e57c94f47bb501e6f8777bd53eb08a7bce72197342dee2c845f7bf6a`, conservé localement dans le dossier `diagnostics` cité ci-dessus.

Les deux scripts exacts de cette campagne sont archivés dans `Tools/diagnostics/20260923/`, avec leurs contraintes de machine et de session ; ils ne remplacent pas la suite complète.

La revue visuelle des quatre premières images confirme deux aplats sombres, puis deux images du menu complet. Le titre après préparation et le cockpit au départ ont aussi été inspectés. Aucune scène seule avant le titre dans ces captures. Les lectures GPU de ces images ne servent pas de mesure de durée de démarrage.

Le profil séparé compte **1 800 images, 1 680 analysées** : **59,997 FPS**, p99 **17,390 ms**, maximum **18,171 ms**, **0 image >20 ms**, GPU moyen **12,844 ms**. La trace ne présente aucun recul de distance. Les seuils natifs restent inchangés. Ces tests ciblés valident le changement de démarrage ; ils ne réattribuent pas les six soaks de `010` au nouveau binaire.

Le raccourci `C:\Users\degun\Desktop\Vroom.lnk` lance ce package avec `-NoSplash -SaveToUserDir -d3d12 -windowed -ResX=1920 -ResY=1080`. Le jeu libre a été lancé en session Windows 2 sans sortie automatique ni limite de durée ; la version précédente et ses preuves sont conservées.

## Démarrage et cohérence des plateformes

Le correctif partagé `e02ac13` ajoute un masque opaque au viewport avant le premier dessin complet du HUD. La scène continue à se préparer derrière ; le masque ne dépend ni d’un délai fixe ni de la seule existence de la voiture. `PreparingCircuit` est actif dès la construction de la voiture. Les lanceurs habituels Mac et Windows passent aussi `-NoSplash`, comme le lanceur Linux existant.

Pour le contrôle visuel du démarrage, `-VroomVerify -VroomStartupCaptureDir=CHEMIN` demande les quatre premières images du viewport. Le dossier de sortie doit exister. Ces lectures GPU servent à vérifier le contenu des images, **pas à mesurer la cadence ou la durée de chargement**. La mesure de performance s’exécute séparément, sans ce paramètre.

La [règle multiplateforme](PLATFORMS.md) distingue le code partagé, les binaires construits, les tests et les versions installées. La recompilation Mac du correctif est actuellement bloquée par les droits d’accès au cache UnrealBuildTool. Linux reste à compiler et à tester nativement.
