# NVIDIA DLSS — installation facultative Windows

**État du lot 0.30 préparé le 12 septembre 2026 : intégration facultative présente dans le code, validation Windows/NVIDIA encore absente.** david-pc est inaccessible par son accès SSH configuré pendant cette passe. Ni la carte actuellement active, ni le chargement du plugin, ni un gain de performance DLSS ne sont confirmés sur cette machine.

Référence consultée le 8 septembre 2026 : le paquet retenu était **DLSS 4.5 pour Unreal Engine 5.8**, paquet **8.7.2**, daté du 21 juillet 2026. Le descripteur `DLSS.uplugin` indique `EngineVersion: 5.8.0` et `VersionName: 8.7.2-NGX310.6.0`. La page officielle décrit DLSS 4.5 ; cette intégration ne doit pas être appelée « DLSS 5 ». [NVIDIA DLSS](https://developer.nvidia.com/rtx/dlss)

Le code du jeu peut être compilé et lancé sans les composants NVIDIA. L'intégration facultative vise Windows x64 ; le support effectif dépend du GPU, du pilote et du résultat renvoyé par l'API NVIDIA. Le Mac conserve les choix TAA et TSR et affiche l'indisponibilité de DLSS. Installer les fichiers sur un Mac ne permet pas d'y activer DLSS.

## Installer depuis l'archive officielle

Télécharger [le paquet NVIDIA DLSS 4.5 pour UE 5.8, version 8.7.2](https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/dlss/UE-DLSS-5.8/8.7.2/2026.07.21_UE5.8_DLSS4.5Plugin_v8.7.2.zip) et conserver l'archive **hors du dépôt Git**. Le téléchargement complet est d'environ 721 Mo. L'installateur ne télécharge rien et ne lance aucun binaire.

Depuis la racine du projet :

```powershell
py -3 Tools/install_nvidia_dlss.py --archive "C:\Users\David\Downloads\2026.07.21_UE5.8_DLSS4.5Plugin_v8.7.2.zip"
```

Sous macOS/Linux, pour préparer les fichiers d'un futur build Windows :

```sh
python3 Tools/install_nvidia_dlss.py --archive /chemin/hors-du-depot/paquet-officiel.zip
```

L'option `--project-root /chemin/du/projet` permet de désigner un autre clone contenant `Vroom.uproject`. L'installation place uniquement :

- `Plugins/DLSS` : super résolution, DLAA et modules associés du paquet.
- `Plugins/StreamlineNGXCommon` : dépendance déclarée par le plugin DLSS.
- `Plugins/.vroom-nvidia.json` : versions et empreintes SHA-256 de l'archive locale et des fichiers installés.

Ces trois chemins sont exclus de Git. Aucun paquet propriétaire, DLL, bibliothèque ou archive n'est ajouté au code versionné par cet outil. L'empreinte du reçu identifie l'archive utilisée ; ce n'est pas une signature d'authenticité NVIDIA. L'installateur n'édite ni `Vroom.uproject`, ni les règles de compilation du projet.

## Compilation et contrôles

L'installateur conserve les sources, ressources, DLL et bibliothèques NVIDIA nécessaires à l'exécution. Il omet les fichiers de débogage `.pdb` et les répertoires `Intermediate`. Il passe explicitement `Installed` à `false` dans les deux descripteurs installés : il s'agit de **plugins de projet reconstruits depuis leurs sources**, dont UnrealBuildTool doit régénérer les modules et les manifestes pour l'UE 5.8 utilisé. Les descripteurs dans l'archive d'origine restent intacts.

Sur Windows x64, fermer le jeu et l'éditeur, puis utiliser le build Windows du projet avec les outils C++ et UE 5.8. Vérifier ensuite le menu graphique et les journaux du jeu sur la machine RTX réelle. La présence d'un `.uplugin` ne démontre ni le chargement du runtime ni un gain de fluidité. Aucun résultat de performance NVIDIA n'est revendiqué par les tests de l'installateur.

La génération d'images, Multi Frame Generation et Dynamic Multi Frame Generation ne sont **pas intégrées dans cette passe**. L'installateur ne place pas les plugins correspondants. La présence de ces fonctions dans le paquet NVIDIA complet n'implique pas leur activation dans Vroom.

## Réexécution et validation

L'outil vérifie la version UE des deux descripteurs et la présence des sources avant l'installation. Il refuse les chemins sortant de l'archive, les liens symboliques, les fichiers spéciaux, les archives chiffrées et les doublons de chemins, y compris les conflits de casse Windows.

Un plugin déjà présent avec le même contenu est accepté. Un contenu différent est refusé avant de placer l'un ou l'autre plugin ; les fichiers existants sont préservés. Une compilation peut avoir changé les binaires : pour changer de version, conserver d'abord les dossiers existants dans une sauvegarde distincte, puis relancer l'installation dans des chemins libres. Le script ne fournit pas d'option d'écrasement forcé.

Tests locaux, utilisant uniquement de petites archives synthétiques :

```sh
python3 Tests/nvidia_installer_test.py
```

Ils couvrent l'installation et sa réexécution, la conservation des runtimes et des sources, le refus d'une version UE différente, les traversées de chemin, les liens, les collisions de casse et la préservation d'un plugin existant modifié. Ils ne remplacent pas un build ou un lancement Windows avec le paquet NVIDIA réel.

## Preuve d'activation à produire sur Windows

Après installation facultative et compilation native Development, choisir le mode graphique dans le jeu puis lancer :

```powershell
.\Tools\verify_windows.ps1 -ExpectedVersion 0.30.0 -Profile -ExpectedDLSS quality -TargetFPS 60
```

Cette commande est destinée au **futur package 0.30.0 effectivement construit**. Le manifeste du build, l'empreinte de l'exécutable et la version du journal doivent correspondre. Le contrôle conserve l'adaptateur DX12 choisi, les messages NVIDIA, les préférences appliquées et le mode effectif que le jeu rapporte après `IsDLSSEnabled()`. Un repli vers le rendu natif échoue si `quality` est exigé. `-ExpectedDLSS native` permet une mesure séparée sans DLSS ; `any` enregistre seulement le mode observé.

Les valeurs `DLSS=0/1/2/3/4` du journal correspondent à natif/DLAA/Quality/Balanced/Performance. Une ligne de montage du plugin ou un fichier `.uplugin` ne satisfait pas à elle seule cette vérification. Aucune génération d'images n'est intégrée : le débit mesuré concerne les images rendues par le jeu. Les critères, limites et fichiers de preuve sont décrits dans [RTX5090.md](RTX5090.md).
