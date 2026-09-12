# Réglages graphiques

Le menu conserve les modifications en attente : **Appliquer** modifie le rendu,
sauvegarde puis ferme ; **Annuler** ferme en restaurant les valeurs précédentes.
Les réglages se trouvent dans la section `Vroom.Graphics` de `GameUserSettings.ini`,
en plus des groupes de qualité gérés par `UGameUserSettings`.

Les préréglages Performance, Équilibré, Élevé et Ultra changent les détails,
la résolution interne et l’anticrénelage. Ils conservent le choix de synchronisation
verticale, de limite de fréquence et de DLSS. Les réglages de ray tracing matériel
et d’ombres virtuelles restent ceux de la plateforme au démarrage.

Sur Mac, le premier lancement utilise TAA, 70 % de résolution interne, 60 images/s
et la synchronisation verticale. TAA correspond à la valeur Unreal **2** ; la
valeur 3 désigne MSAA, utilisable avec le rendu forward. Le niveau GI Équilibré
garde l’occlusion ambiante mais désactive Lumen ; Élevé et Ultra l’activent.

## Intégration NVIDIA optionnelle

Le code relie **DLSS Super Resolution et DLAA** uniquement pour une compilation
Windows 64 bits avec le plugin officiel DLSS présent dans `Plugins/DLSS` et la
définition `VROOM_WITH_DLSS=1`. Sans cette dépendance, le même projet se compile
avec TAA/TSR et affiche la raison d’indisponibilité dans le menu.

L’intégration utilise les signatures du plugin NVIDIA officiel UE 5.8 v8.7.2 :
`IsDLSSSupported`, `IsDLSSModeSupported`, `GetDLSSModeInformation`, `EnableDLSS`
et `IsDLSSEnabled`. Le menu propose Désactivé, DLAA, Qualité, Équilibré et Performance,
en sautant les modes refusés par le runtime. La détection ne repose pas sur le nom
de la carte graphique : les modules doivent être chargés et le runtime doit
confirmer la prise en charge. Les erreurs de pilote, de plateforme, de GPU et
d’outil de capture sont distinguées.

Au premier lancement compatible sans choix DLSS enregistré, le mode Qualité
est sélectionné. Un choix explicite, y compris Désactivé, est conservé. Si une
configuration enregistrée n’est pas prise en charge, le jeu utilise ses réglages
natifs. Pendant DLSS, la résolution interne provient de l’API NVIDIA et les lignes
de résolution/anticrénelage sont verrouillées. Les préférences natives restent
conservées pour le retour à TAA, TSR ou FXAA.

**La génération d’images, Multi Frame Generation et Ray Reconstruction ne sont
pas branchées dans ce menu.** L’activation de DLSS SR/DLAA ne prétend pas activer
ces fonctionnalités. La compilation et l’exécution sur GPU NVIDIA doivent être
validées sur Windows ; une validation Mac ne constitue pas cette preuve.
