# Validation — Vroom 0.22, 12 septembre 2026

## Build et portée

Unreal Engine 5.8, Mac Apple M5 avec 24 Go, cible arm64 Development. Le package 0.22 a été compilé, cuit et archivé : `Saved/Logs/vroom-package-v022.log` contient `BUILD SUCCESSFUL`, sortie AutomationTool 0, BuildCookRun terminé en 72,04 secondes. Le raccourci du Bureau choisit ce dernier package.

Les essais du package démarrent depuis `/tmp`. Les journaux et PNG sont conservés dans `Saved/Validation/v0.22.0/` ; le récapitulatif structuré est [validation-results.json](validation-results.json). Les images de contrôle utilisent `-RenderOffScreen -MetalOffscreenOnly`, combinaison vérifiée sous Mac/Metal. Elles ne mesurent pas la présentation à l'écran.

## Démarrage Mac

Une première série a réussi onze scénarios, puis un lancement direct s’est arrêté avant le premier journal Unreal (`EXC_BAD_ACCESS`, 12 septembre à 19:15). Le rapport macOS pointe vers `FLLMTracker::PopTag`, via l’allocateur et LaunchServices. Le code UE 5.8 montre une désactivation du suivi mémoire à l’analyse de la ligne de commande, alors que des allocations Cocoa peuvent déjà avoir ouvert un scope. C’est l’explication retenue, sans modification du moteur.

Les lanceurs Mac et le vérificateur utilisent désormais `-LLM` pour conserver le suivi mémoire actif pendant le démarrage. Il s’agit du **Low Level Memory Tracker** d’Unreal, sans modèle de langage, collecte réseau, trace ou export CSV supplémentaire. Les mesures de cadence finales utilisent ce même réglage. Le lanceur livré a également réussi un démarrage natif suivi de l’arrêt automatique et de la vérification `PASS`. Un lancement direct de l’application sans le lanceur ne reçoit pas ce contournement.

## Cockpit et interface

La racine du cockpit reste à 62 cm, caméra inchangée. Les mains utilisent désormais une prise fixe dans le repère du volant : position et rotation suivent exactement son angle. Le gant ne glisse plus entre des coordonnées d’écran indépendantes de la jante. Les sept dessins Amiga restent des références de composition, sans pilotage direct du maillage. La main gauche monte en braquant à droite, et inversement ; la prise opposée peut sortir du cadre.

Les articulations proximales des quatre doigts sont adaptées à leurs longueurs (20°, 10°, 5°, 0° supplémentaires), puis leurs phalanges se referment autour de la prise. Le pouce est également fléchi. Les poignets sont inclinés vers le pilote autour de l’axe de prise, avec le dos des gants visible. Les manches suivent leurs transformations sans quantification du braquage.

Les poses neutre, petit braquage, braquage moyen, plein gauche/droit et la vue rapprochée de contrôle ont été inspectées dans l’éditeur. Les mêmes poses ont été capturées dans le package final, dont le neutre a été réinspecté. Le résultat ne prétend pas reconstruire exactement l’anatomie ni les sprites : il reste des recouvrements locaux de maillages dans l’épaisseur des doigts et à la base du pouce, masqués en grande partie par la jante. La silhouette et l’appui sont contrôlés visuellement ; aucune simulation de pression de la main n’est implémentée.

Le bandeau Canvas mesure 112 pixels dans l'espace 1440 × 900. Carte, voyants, circuit, score, stage, tour, 27 repères de dépassement et essence sont présents, dans un panneau translucide. Les monoplaces possèdent 10 meshes dédiés et 5 matériaux, six livrées pour les adversaires ; l'audit donne 3 182 triangles par concurrent, répartis en quatre groupes d'instances.

Le menu graphique a été contrôlé au clavier : navigation, changement d'option, annulation, application puis relancement. Le réglage d'ombres désactivé a été retrouvé après relancement ; le préréglage Performance a ensuite été rétabli et sauvegardé. L'automatisation des clics natifs a échoué côté outil : le parcours souris n'est pas validé de bout en bout.

## Route et accidents

La route fait 9,5 m, contre 12 m précédemment. Cette restitution de proportions correspond à 3,96 nouvelles voitures de large, à rapprocher du rapport nominal 512/132 dans les sources originales. Ce n'est pas une mesure métrique retrouvée de l'Amiga. Le damier et les accotements suivent cette largeur.

Les scénarios petit contact, casse, saut sur pneu et choc contre un pilier réel vérifient chacun un seul contact. Petit choc : aucune récupération ; les trois autres : une récupération terminée, zéro erreur de visibilité. Dans le scénario décor, le choc observé arrive à 95,9 km/h, à 8,5 m du centre ; après immobilisation, le retour dure 3,37 s et la voiture reprend à 0 km/h. Les concurrents et le chronomètre continuent. Les portiques sont testés par volume 3D : les piliers sont solides, la traverse haute reste franchissable.

## Tests portables

Six suites C++17 compilées avec `-Wall -Wextra -Werror -pedantic -O2` : conduite, contacts, relief, tracé, cadence et dépassements. Toutes passent, ainsi que huit tests Python de relief, six de l’installateur NVIDIA et trois de l’analyseur de cadence (17 au total). L'audit géométrique des dix meshes passe.

La réponse de direction mesurée atteint 90 % en 108,33 ms, revient à 10 % en 58,33 ms et traverse le neutre au contrebraquage en 25 ms. Les tests couvrent la cadence 30/60/120 Hz, la convergence à 240 Hz, la conservation de phase après retard, les dépassements et la remise en piste progressive.

## Vérification reproductible du package

```sh
python3 Tools/test_portable.py --log Saved/Validation/v022/portable-tests.log
python3 Tools/verify_mac.py --profile
```

Le second script contrôle 13 captures et scénarios, puis lance deux mesures à l’écran de 1 800 images chacune et une trace de mouvement de 25 secondes. Il exige les résolutions attendues et les compteurs de scénario. Les 120 premières images des profils sont exclues ; les lignes de métadonnées CSV ne sont pas des images. Les pics réels restent comptabilisés. Les résultats chiffrés du paquet sont inscrits ci-dessous après exécution.

Les **16 essais de la série finale réussissent**, avec le suivi mémoire confirmé actif dans les journaux : 13 captures/scénarios, deux profils à l’écran et une trace de mouvement. Le préréglage Performance est chargé : TAA, résolution interne 70 %, 60 i/s, VSync. Les définitions effectives sont confirmées dans les métadonnées du moteur.

| Fenêtre | Débit moyen | P95 | P99 | Maximum | Images > 20 ms | GPU moyen |
|---|---:|---:|---:|---:|---:|---:|
| 1440 × 900 | 59.99 i/s | 16.800 ms | 16.862 ms | 18.385 ms | 0/1680 | 5.865 ms |
| 2560 × 1664 | 59.99 i/s | 16.857 ms | 16.943 ms | 20.800 ms | 1/1680 | 13.797 ms |

Sources : `Profile(20260912_192356).csv`, `Profile(20260912_192428).csv`.

La trace distincte de 25 secondes contient 1375 échantillons après préparation : aucun retour de distance, aucune valeur non finie ; variation de lacet maximale 0.571° par image. Le pic de 20,800 ms en grande définition reste comptabilisé ; il n’est pas effacé ni remplacé par une meilleure série.

Captures du package : [neutre](preview/cockpit-neutral.png), [braquage droit](preview/cockpit-right.png). Le champ de caméra rapproché utilisé pour inspecter les prises est uniquement une option de contrôle.

## Limites

Le support NVIDIA est conditionnel au plugin officiel DLSS 4.5 pour UE5.8 sous Windows RTX. Aucun test Windows/RTX ni activation DLSS n'a été effectué sur ce Mac ; DLSS n'est pas disponible sur Metal. Il n'y a pas de DLSS 5 fictif ni de génération d'images intégrée.

Les graphiques restent stylisés, l'IA suit des voies prédéfinies et les règles complètes du jeu original restent inachevées. Les observations concernent les poses, les formats et les parcours effectivement testés. Elles ne garantissent pas tous les circuits, les longues sessions, le démarrage à froid ni la 4K. Les anciens chiffres de cadence ne certifient pas ce package.
