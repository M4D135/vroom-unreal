# Construire Vroom 0.34 sur Windows

**État au 23 septembre 2026 : environnement Windows préparé, aucun exécutable Windows 0.34 produit ou testé.** Les sources 0.34 et les outils de compilation sont présents sur un PC Windows 11 avec une RTX 5060 Laptop GPU. Les fixtures de l’outillage passent nativement et l’installation d’Unreal Engine 5.8 a été lancée depuis Epic. La version Mac et les tests de l’outillage ne valident pas le jeu Windows.

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

Le manifeste de schéma 2 porte les empreintes de tous les fichiers du package, en plus du binaire et des sources. Le ZIP contient `vroom-build.json` à sa racine et le dossier `Windows/` (ou `WindowsNoEditor/` selon la sortie Unreal), avec `Jouer.cmd`, `LISEZ-MOI.txt`, le jeu, ses données, les prérequis Visual C++ et `Licences/`. **Ces chemins décrivent ce que la commande doit produire ; aucun ZIP ou `.exe` Windows n’est disponible à l’issue de la préparation sur Mac.**

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

Si le runtime Visual C++ manque, lancer `Engine\Extras\Redist\en-us\vc_redist.x64.exe` depuis le dossier de jeu extrait, puis réessayer. UE 5.8 fournit aussi le redistribuable ARM64 ; la cible de ce jeu est x64. Le seul `.exe`, séparé de ses données, ne suffit pas. L’ouverture de `Jouer.cmd` ne remplace pas les contrôles d’intégrité et de comportement du vérificateur.

Sur le PC avec le clone et un bureau Windows interactif, régler d’abord les graphismes et le plafond dans le jeu, puis lancer :

```powershell
.\Tools\verify_windows.ps1 -ExpectedVersion 0.34.0 -Profile -Soak -TargetFPS 60
```

Le défaut attend une **RTX 5090** ; pour une autre carte, fournir son nom attendu avec `-ExpectedGPU`. `-TargetFPS` choisit les seuils d’acceptation, sans modifier les options graphiques. Les captures sont visibles ; les rapports, journaux et CSV sont enregistrés dans `Saved/Validation/v0.34.0/<date>-windows/`. Les critères 60/120 Hz et le contrôle DLSS sont décrits dans [RTX5090.md](RTX5090.md).

Un ZIP construit n’est pas une validation de jeu. La compilation Windows, les prérequis sur une machine vierge, le lancement, le GPU sélectionné, la cadence, les contrôleurs physiques et DLSS restent à vérifier nativement. Aucun résultat Mac ne les remplace ; aucun service de compilation cloud n’a été installé.

L’outillage a été contrôlé sur Mac : **cinq fichiers PowerShell analysés sans erreur**, **28 assertions sur des packages factices et leur ZIP**, et **19 tests Python du vérificateur réussis**. Ces contrôles utilisent PowerShell 7.6.6 et des fixtures, sans exécuter Unreal ni un jeu Windows. Les [preuves et limites](RTX5090.md#contrôles-de-loutillage-sur-mac) restent séparées des futurs essais natifs.

### Préparation native du 23 septembre 2026

Les sources du commit `6191b5a751c8907c28542e1f5c6b79a5e260d3c6` ont été extraites dans `C:\Vroom-Build-20260923\Project\vroom-unreal`. L’archive transférée a été vérifiée sur les deux machines : SHA-256 `22c56d0ddea02fc95c032c1b004a84fd7c334c7cba79698723a05defd4c0d664`.

Visual Studio 2022 17.14.41, MSVC 14.44.35207 et le SDK Windows 10.0.26100.0 sont disponibles. Python 3.13.15 est installé dans le dossier `Tools\Python313` de cette préparation, sans modification du PATH global.

Sous Windows PowerShell 5.1, les cinq scripts se parsèment sans erreur et les **28 assertions de packages factices passent**. Le premier passage des tests Python a révélé quatre échecs causés par le parseur POSIX `shlex` dans le faux processus de test. La fixture conserve désormais les arguments structurés tout en appelant la véritable fonction de génération de commande Windows. Avec ce correctif et un cas couvrant les chemins Windows/POSIX, **20 tests Python passent sous Windows et sur Mac**. Les scripts de production et les assets du jeu sont inchangés.

Les journaux locaux se trouvent sous `C:\Vroom-Build-20260923\Logs` : `powershell-syntax-001.json`, `windows-package-fixtures-001.log`, `windows-verification-fixtures-002.log` et `preflight-receipt-20260923.json`. Le fichier de test initial est conservé dans ce même dossier. Ces essais ne lancent aucun jeu.

Le catalogue Windows obtenu via Legendary 0.21.1 retournait zéro asset, y compris avec `--include-ue`. L’utilisateur a ensuite lancé l’installation depuis Epic dans `C:\Program Files\Epic Games\UE_5.8` ; son dossier de téléchargement existe, mais les exécutables du moteur ne sont pas encore présents au moment de ce relevé. Un raccourci `Projet Vroom` sur le bureau pointe vers le `.uproject`. Après installation complète, il reste à compiler et à exécuter la validation dans une session Windows interactive avec `-ExpectedGPU 'NVIDIA GeForce RTX 5060 Laptop GPU'`.
