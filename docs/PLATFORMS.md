# Mac, Windows et Linux

Les améliorations du jeu doivent être reprises sur **Mac et Windows**, puis sur **Linux** lorsque sa cible native sera disponible. Le gameplay, le démarrage, le HUD, le cockpit et les assets approuvés appartiennent au même projet : une correction commune doit être livrée dans **le même commit**, sans entretenir de copies divergentes par plateforme.

Les profils graphiques restent distincts lorsque le matériel ou le moteur le justifient : Metal sur Mac, DirectX 12 sur Windows, Vulkan sur Linux. Résolution, antialiasing, ombres et budget GPU peuvent différer ; chaque différence doit être expliquée et mesurée sur la plateforme concernée. Un réglage ou un résultat Windows ne constitue pas une validation Mac ou Linux.

## État au 4 octobre 2026 — 0.35

- **Mac** : compilé sous UE 5.8.3, onze scénarios ciblés visibles réussis. Raccourcis habituels `Vroom.app` et `Vroom.command` activés sur la 0.35 avec son icône ; la 0.34 reste sauvegardée. Le lancement normal passe bien `-LLM`, requis pendant l’initialisation Cocoa. Aucun nouvel essai FPS prolongé.
- **Windows** : même code de jeu, onze scénarios visibles réussis sur RTX 5060 Laptop / DirectX 12, session console 1. Le raccourci habituel est remplacé par celui de la 0.35, ancienne version conservée. Il vise l’exécutable natif testé ; Windows refuse le petit bootstrap auxiliaire avec l’erreur 4551. Aucune stratégie de sécurité n’a été modifiée.
- **Linux** : code commun prêt, aucun build ni essai natif de la 0.35. L’accès SSH configuré `david-pc` (192.168.1.136) ne répond pas ; le PC Windows accessible ne possède ni les binaires de plateforme Linux Unreal ni le compilateur croisé. [État vérifié](linux-readiness-035.json).

Les 198 fichiers du manifeste de sources, de configuration et d’assets Windows correspondent aux fichiers Mac. Les binaires restent propres à leur plateforme. Voir [le bilan](FINALISATION_035.md), [les preuves Mac](validation-035-mac.json) et [Windows](validation-035-windows.json). Ces contrôles de gameplay ne réattribuent pas les anciennes mesures de performance aux nouveaux binaires.

## Historique — état de reprise au 23 septembre 2026

| Plateforme | Sources communes | Compilation | Tests natifs | Installation |
|---|---|---|---|---|
| Windows | Correctifs partagés présents | Builds 010 et 011 compilés | 010 : 39 contrôles complets ; 011 : 5 contrôles ciblés du démarrage et revue des images, [détails](WINDOWS.md) | Raccourci bureau Vroom vers 011, jeu lancé en session interactive ; versions antérieures conservées |
| Mac | Correctif de démarrage prêt dans les sources communes | Recompilation du correctif bloquée par les permissions locales | Le nouveau correctif n’est pas encore validé dans un binaire Mac | Installation existante conservée |
| Linux | Sources communes et scripts natifs existants | Cible future, compilation non certifiée | Aucun résultat natif certifié | Aucune installation certifiée |

Sur Mac, l’essai de compilation s’est arrêté avec le code **134** : UnrealBuildTool a levé `UnauthorizedAccessException` en tentant de supprimer un fichier `Trace-backup-….uba` dans `~/Library/Application Support/Epic/UnrealBuildTool`. Le journal de compilation avait été redirigé, mais ce cache restait hors des chemins autorisés. Aucun contournement du sandbox ni changement de l’installation du moteur n’a été effectué. La présence du correctif dans les sources ne signifie donc pas qu’il est déjà actif dans le jeu installé.

## Conditions de livraison

- Construire chaque cible depuis le même commit et identifier ses binaires et données. Conserver les assets approuvés ; leur régénération est une opération distincte d’une compilation.
- Tester le comportement commun sur chaque plateforme disponible, avec des essais visibles du cockpit, des commandes et du démarrage, puis les contrôles de gameplay, de rendu et de cadence adaptés au matériel.
- Publier séparément les états **sources mises à jour**, **compilation réussie**, **tests réussis** et **version installée**. Préserver la version précédente et son moyen de retour arrière avant toute activation.
- Garder les scripts et paramètres Linux existants comme préparation ; ne pas transformer leur présence en affirmation de compatibilité ou de performance vérifiée.

Un système commun de construction et de validation pourra être conçu ensuite. Cette étape n’ajoute aucune infrastructure CI ni service permanent.
