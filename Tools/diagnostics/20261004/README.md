# Vérification ciblée 0.35

`verify_gameplay_windows.py PROJET ARCHIVE SORTIE` exécute onze scénarios visibles du package Windows 0.35, en important les vérificateurs de manifeste et de journaux du projet. Le dossier de sortie doit être nouveau. Python doit tourner dans la session console interactive de l’utilisateur ; une session SSH seule n’affiche pas le jeu sur son bureau. Aucun profil FPS ni parcours prolongé n’est exécuté.

Les contrôles couvrent ligne droite sans drift, contre-braquage clavier, drift expert, réparation aux stands, crash, frottement de roues, dépassement IA, départ normal, pause, silence et cockpit neutre. Le script vérifie les empreintes du package avant et après, la visibilité de la fenêtre, le GPU sélectionné et DirectX 12. Les images sont examinées séparément. Il ne remplace pas la campagne complète et ne teste pas une manette physique.

Commande utilisée sur le PC RTX 5060 Laptop :

```powershell
C:\Vroom-Build-20260923\Tools\Python313\python.exe C:\Vroom-Build-20260923\Tools\verify-gameplay-035.py C:\Vroom-Build-20260923\Project\vroom-035-20261004 C:\Vroom-Build-20260923\Packages\Win64-035-final-20261004 C:\Vroom-Build-20260923\Logs\gameplay-035-20261004
```

La tâche ponctuelle de validation n’a aucun déclencheur récurrent. Ne pas arrêter un jeu déjà ouvert par l’utilisateur pour lancer ce diagnostic. Aucun service de synchronisation ou de modèle n’est installé.

Sur Mac, les mêmes scénarios sont disponibles dans `Tools/verify_mac.py`. Tout lanceur Cocoa doit conserver l’argument `-LLM` d’Unreal déjà utilisé par le lanceur normal : son omission dans le premier petit harnais de cette session a provoqué un crash `FLLMTracker::PopTag` avant l’ouverture du jeu. Le harnais a été corrigé et le cas a ensuite passé. Il s’agit du suivi mémoire bas niveau du moteur, pas d’un modèle de langage.
