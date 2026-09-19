# Vroom 0.34 — cockpit, adversaires et turbo maîtrisé

Travail du 19 septembre 2026, à partir de `98e3958` (0.33). Dépôt : `/Users/david/Downloads/vroom-unreal`. La release **`0.34.0-c8a4441fbfd4` est installée**, avec vérification des empreintes du binaire, des données et du rapport. La 0.33 est conservée pour retour arrière. L’état Git est consultable dans le [dépôt](https://git.estrasse.fr/david/vroom-unreal).

## Cockpit : nouvelle référence F1

La photo de cockpit F1 fournie par David remplace la contrainte précédente de composition strictement Amiga. La demande actuelle porte sur une position des yeux plus basse, une carrosserie rouge nettement visible, les vraies roues avant, un volant classique compact à trois branches, sans halo, et une prise à **neuf heures et trois heures**. Les gants blancs existants sont conservés.

Les rétroviseurs allongés sont placés derrière les pneus dans le repère physique de la voiture. Des bras rouges horizontaux et coudés les relient au pourtour du cockpit ; les anciennes diagonales blanches ont été retirées. La capture arrière partagée s'actualise à 30/60 Hz dans le cockpit et à 10 Hz en poursuite ; son cadrage est adapté aux vitres plus larges.

Le cadrage reste fixé à **(−65, 0, 82) cm**, pitch nul et **FOV 68°** pour les captures de référence. Le volant conserve l’échelle **0,85**, les gants blancs **0,92** et une inclinaison commune de **−25°**. Le tableau de bord est indépendant du braquage, fixé au châssis en **(47, 0, 55,5) cm**. La jante est un cercle de rayon constant **11,9 cm**, avec une section de rayon **0,765 cm**, sans creux destinés aux pouces. **David a approuvé visuellement la prise du pouce autour du cercle**, retenue sous le nom `pulp-candidate-1`. Elle conserve le placement des mains, les quatre autres doigts, les poignets, les bras et le cadrage. Les vues [neutre](images/cockpit-034-neutral.png), [gauche](images/cockpit-034-left.png) et [droite](images/cockpit-034-right.png) montrent cette pose et les raccords des manches dans le nouveau package `20260919-231125-package`.

Les **10 contrôles des gants passent** et l’[audit de jante](cockpit-grip-034.json) établit une séparation minimale de **0,667 mm** pour la pose courante. Le [contrôle ciblé pouce–index](thumb-grip-034.json) ne détecte aucune traversée stricte, sans certifier toute l’anatomie ou toutes les auto-intersections possibles. Les **31 suites portables passent**, ainsi que les **47 contrôles de la série visible finale**. L’accord visuel sur la prise et ces vérifications couvrent des aspects complémentaires ; leur périmètre est détaillé dans le [rapport](validation-results.json).

Le raccord des manches utilise la section du gant en **(−4,656 ; 28,5 ; −5,453) cm** dans son repère source, avec une tangente **(−0,0662 ; −0,9958 ; +0,0626)**. Son ellipse terminale est rentrée de 6 % sous le bracelet, soit environ **3,205 × 2,362 cm** de demi-axes à l’échelle actuelle. Le pivot du gant, sa prise et la jante restent identiques ; seuls le centre, l’axe et la section terminale de la manche sont ajustés.

## Finition des monoplaces

La passe extérieure affine les queues des pontons et le capot moteur pour supprimer les trois terminaisons rouges épaisses visibles depuis l’arrière. La transmission carbone, le diffuseur et ses séparations, deux petites sorties d’échappement et un feu arrière donnent une structure à cette partie de la voiture. Les entrées de radiateur et les coins arrondis des plaques latérales d’aileron complètent la coque. La [présentation officielle de la McLaren Honda MP4/5B de 1990](https://global.honda/en/F1/machine/1990_McLarenHondaMP45B/) a servi de référence pour la finition arrière ; le modèle du jeu reste une interprétation originale, sans marque ni copie exacte.

Le cockpit, ses vrais bras et gants blancs animés, le volant et les rétroviseurs sont désormais les mêmes composants 3D en vue intérieure et en poursuite. Le casque, le buste, le col et le harnais restent des composants supplémentaires visibles seulement en poursuite. Les anciens bras et gants incorporés au pilote extérieur ont été retirés pour éviter une seconde pose déconnectée du volant. Le cadrage, le nez visible, la prise à neuf heures et trois heures et les gants vintage restent inchangés.

Les géométries générées comptent **4 744 triangles pour la carrosserie du joueur**, **268 pour son aileron avant**, **1 144 pour son pilote extérieur** et **1 256 par roue**. Les pneus gardent leur rayon, leur largeur, leurs 40 segments et dix anneaux d’épaules. Les jantes à cinq branches reçoivent une épaisseur, une valve et deux discrètes marques de moulage sombres sur les flancs : ces détails suivent la rotation sans bandes blanches ni logos. Les groupes de normales séparent toujours les lèvres de jante de leurs parois internes. Les UV par projection dominante et leur pleine précision évitent les faces verticales aplaties ou quantifiées dans l’espace UV.

Les adversaires reprennent les mêmes profils de coque et un pilote assis aux bras modelisés, avec une tessellation réduite : **9 328 triangles par voiture complète**, dont **5 968 pour carrosserie, pilote, ailes et pièces fixes**, et **840 par roue**. Leurs roues utilisent 24 segments et les mêmes épaules arrondies. Les exports `SM_F1OpponentWheelFront` et `SM_F1OpponentWheelRear` ont leur pivot à l’origine et leur essieu suivant Y. Les centres restent **(128, ±91, 35) cm** à l’avant, rayon 35 et largeur 38 cm, et **(−115, ±96, 38) cm** à l’arrière, rayon 38 et largeur 46 cm. Les deux slots restent gomme puis alliage. Des porte-moyeux et axes noirs fixes rejoignent ces pivots ; les pneus, jantes et moyeux tournants sont exclusivement dans les roues séparées. L’assemblage historique `SM_F1OpponentWheels` reste exporté pour compatibilité et ne doit pas être affiché en même temps.

Les **22 contrôles ciblés de géométrie passent** : visibilité et orientation des surfaces, dégagement du cockpit, nez et ancrages de suspension, enveloppes et pivots des roues, raccords arrière et UV. Ces comptes et tests décrivent les modèles générés. L’import et la compilation du package réussissent ; les vues cockpit et extérieures ont été inspectées. La série finale avec la pose du pouce retenue réussit ses contrôles aux deux résolutions et sur les six circuits complets. Ces preuves ne constituent pas une validation audio à l’écoute.

## Animation des roues

Le roulement des quatre roues du joueur est maintenant animé à partir du déplacement interpolé. Les rayons avant/arrière de 35/38 cm donnent leurs phases propres. Le braquage tourne l’essieu, puis le pneu et la jante roulent autour de cet axe ; les bras de suspension suivent seulement le braquage. Une correction opposée au dérapage se voit immédiatement sur les roues. À 216 km/h pour une commande de 0,62, les roues avant prennent environ 9,34° et 9,88° au lieu d’environ 3,45°, avec un écart intérieur/extérieur modéré. L’enveloppe complète des pneus conserve au moins 8,02 cm de séparation longitudinale avec les coques des rétroviseurs.

Les 48 roues des concurrents sont réparties dans deux groupes d’instances avant/arrière, en complément des trois groupes de carrosserie. Leur orientation suit les virages et les changements de voie. Les buffers sont réemployés et les vecteurs de mouvement sont remis à zéro lors d’un repositionnement ; aucun composant n’est créé par image. Les tests couvrent 15/30/60/120/240 Hz, les appels de rendu répétés, la pause, les téléports et les remises à zéro. Ces animations ne changent pas les forces de conduite ni les collisions.

## Dérapage et retour visuel

Le drift garde son sens initial et demande un contre-braquage dosé. Un bon contrôle resserre progressivement la trajectoire grâce aux forces latérales ; il ne recentre pas artificiellement la voiture. Trois seuils de charge, **0,42 / 1,00 / 1,80**, donnent des turbos de **0,55 / 1,15 / 1,70 seconde**. Relâcher dans les **0,22 seconde** après un nouveau palier ajoute **0,20 seconde**. Franchir plusieurs fois le même palier ne renouvelle pas cette fenêtre.

La poussée monte en 0,10 s et retombe en 0,16 s, sans secousse de caméra ni saut de FOV. Le bouton doit être relâché et le turbo terminé avant une nouvelle amorce. Herbe, choc, freinage, perte de contrôle ou panne annulent la récompense. Les commandes analogiques et les taps clavier sont couverts par les tests.

Le HUD annonce le contre-braquage, le palier disponible et le moment du lâcher précis. Les sons montent avec le palier ; un pool borné à **48 étincelles**, actualisé à **30 Hz**, produit le bleu, l’orange et le violet aux roues et pendant le turbo. Un matériau additif est préparé avant la course. Aucun éclairage dynamique supplémentaire n’est créé. Recommencer une course vide les effets, y compris sur le même circuit.

## Adversaires

Chaque pilote examine neuf points jusqu’à 250 m devant lui et adapte sa distance de freinage aux pneus, à sa prudence et à sa réaction. Une trajectoire continue passe de l’extérieur vers la corde et la sortie. Les changements latéraux ont une accélération limitée.

Un dépassement réserve un côté et une cible ; le pilote attend que son arrière ait dégagé l’autre voiture de 9 m avant de se rabattre. Le calcul tient compte des véhicules qui approchent de l’arrière et de la vitesse latérale du joueur. Le recyclage du trafic Arcade efface cette intention. Aucun bonus de rattrapage ni ressource gratuite n’est ajouté.

Le test visible `traffic-overtake` place un rival à 50 m/s derrière le joueur arrêté : il doit anticiper, passer avec du dégagement latéral et terminer sa manœuvre sans collision. Les tests portables couvrent également joueur roulant, rabattements, freinages, symétrie des virages et peloton de douze voitures à 30/60/120 Hz.

## Décor

Les conifères passent à **1040/484/116 triangles**, les parasols à **498/288/210**, avec trois niveaux de détail, écorce photographique et rameaux ajourés. Les feuillus photographiques et le rocher scanné de la 0.33 restent présents. Les arbres forment des bosquets ; la densité théorique augmente d’environ 38 % avant exclusions de placement. Les troncs restent hors du corridor de sécurité de 24 m autour du tracé.

Le terrain combine une variation irrégulière et une seconde lecture de sa texture couleur existante, à plus grande échelle, entre 25 et 160 m pour atténuer les répétitions. Les pierres varient légèrement de teinte par instance. Les matériaux des arbres et rochers prennent en compte le fondu de distance et les transitions de LOD. Les feuilles et aiguilles conservent leur masque découpé avant ce fondu, pour ne pas rendre la végétation proche translucide. L’herbe est plus basse, de hauteur irrégulière, et son fondu s’étend sur les 24 derniers mètres de portée ; les distances maximales et les nombres d’instances restent inchangés. Aucun vent CPU n’est ajouté. Sources, licences et budgets : [ENVIRONMENT_ASSETS.md](ENVIRONMENT_ASSETS.md).

## Profil graphique Photoréalisme

Un cinquième préréglage, **Photoréalisme** (index 4), emploie TSR à **80 %**, Lumen pour l’éclairage global et les reflets au niveau **2**, ombres et végétation au niveau **3**, effets au niveau **2**, VSync et plafond de **30 images/s** sur Mac. Des groupes dédiés limitent les ombres des arbres à **160 m**, avec une transition à partir de 120 m ; les arbres éloignés restent visibles dans leurs groupes habituels.

Sur Mac, ce profil conserve les filtres TSR Epic avec un historique à la définition de sortie (100 %), afin d’éviter le coût de l’historique suréchantillonné à 200 %. Le rendu interne reste à 80 % ; les autres profils rétablissent leur réglage habituel.

Les quatre profils précédents restent disponibles. Performance conserve son objectif de **60 images/s** ; quitter Photoréalisme rétablit ce plafond normal. Les huit mesures du package final en Photoréalisme atteignent **29,996–30,000 images/s**, avec un maximum de **39,573 ms par image**, après préparation. Elles couvrent les deux définitions et les six circuits complets, sans certifier la cible de 60 images/s de Performance. Le nom du profil ne garantit pas une scène entièrement photoréaliste.

## Panneaux et portiques

Les quatre textures publicitaires existantes sont réemployées sur des panneaux 8 × 2 m, à intervalles nominaux de **180 m**, avec quelques paires décalées. Des portiques supplémentaires sont proposés tous les **750 m** ; trois panneaux de chevrons peuvent accompagner chacun des quatre virages repérés, en complément des balises trois/deux/une barres.

Les emprises sont vérifiées contre la spline la plus proche, y compris les autres branches du circuit. Une exclusion indépendante protège l’allée des stands. Les piliers des portiques restent à l’extérieur de la piste et leur traverse doit dégager de 4,5 m les portions de route voisines. Les pieds des panneaux sont ancrés au terrain. Les positions incompatibles sont écartées : les intervalles et nombres effectifs doivent être lus dans les journaux du package.

Le budget est borné à **64 panneaux**, **huit portiques supplémentaires** et **douze panneaux de chevrons** par circuit. Aucun nouveau matériau ni texture n’est nécessaire. Les six audits géométriques `track-audit-1` à `track-audit-6` passent dans la série finale, avec les contrôles de dégagement de la piste et des stands.

## Preuves et état

**PASS — 47/47 contrôles du package Mac, huit mesures de cadence et six tours complets.** Série du 2026-09-19, rapport `completed=true`, aucun échec et toutes les sorties à zéro. Fenêtres visibles et actives, commandes automatisées ; ces contrôles ne constituent pas une session jouée avec une manette physique.

Profil **Photoréalisme : TSR, 80 % interne, Lumen GI/reflets 2, ombres/végétation 3, effets 2, historique TSR à 100 % sur Mac, VSync et plafond de 30 images/s**. Les tableaux excluent les 120 premières images de chaque capture. Ces résultats à 30 Hz ne certifient pas 60 FPS ; les mesures 0.33 et 0.30 restent archivées ci-dessous.

- Suites portables : **31/31 PASS**, [portable-thumb-selected.log](../Saved/Validation/v034/portable-thumb-selected.log).
- Surfaces du cockpit et des roues : **22/22 PASS**, [car-surfaces-circle-final.log](../Saved/Validation/v034/car-surfaces-circle-final.log).
- Compilation et package : **PASS**, [vroom-034-thumb-selected-package.log](../Saved/Logs/vroom-034-thumb-selected-package.log).
- Cockpit : volant classique à trois branches, sans halo, tableau de bord fixe en **(47, 0, 55,5) cm**. Caméra **(−65, 0, 82) cm**, **FOV 68°**, volant **0,85**, gants blancs **0,92**, inclinaison commune **−25°**.
- Le [contrôle géométrique de la jante actuelle](cockpit-grip-034.json) vérifie les 4 899 faces de chaque gant : dégagement conservateur minimal **0.667 mm**. Son périmètre exclut les branches, la carrosserie et les manches ; il ne certifie pas une anatomie ni une copie millimétrique.
- La [preuve de contact du pouce](thumb-grip-034.json) porte sur la pose retenue : pulpe à **1,355 mm**, correctement orientée vers la jante ; les dix tests des gants rejettent aussi les anciennes poses ouverte et inversée. Aucun micro-ajustement postérieur à la validation visuelle de cette pose n’a été appliqué.

Échecs intermédiaires conservés : R4 USA (`20260919-193242-package`) a atteint **157,821 ms** dans une rafale CPU ; Vintage R1 (`20260919-201647-package`) est descendu à **27,37 FPS** en 2560. L’historique TSR Epic à 200 % coûtait trop cher sur Mac ; un essai A/B à 100 % a retrouvé **30,00 FPS**, maximum **33,62 ms**, sans changer le rendu interne à 80 %. Le réglage est intégré à la révision mesurée ici. Les seuils de validation restent inchangés.

| Mesure | Images analysées | FPS moyen | P99 ms | Max ms | Images >40 ms | CSV |
|---|---:|---:|---:|---:|---:|---|
| 1440 × 900 | 1680 | 30.00 | 33.638 | 33.899 | 0 | [Profile(20260919_232041).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_232041%29.csv) |
| 2560 × 1664 | 1680 | 30.00 | 33.591 | 35.382 | 0 | [Profile(20260919_232148).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_232148%29.csv) |
| Autriche | 7680 | 30.00 | 33.575 | 35.125 | 0 | [Profile(20260919_232254).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_232254%29.csv) |
| France | 7680 | 30.00 | 33.586 | 34.819 | 0 | [Profile(20260919_232726).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_232726%29.csv) |
| États-Unis | 7680 | 30.00 | 33.575 | 35.091 | 0 | [Profile(20260919_233154).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_233154%29.csv) |
| Suède | 7680 | 30.00 | 33.573 | 36.889 | 0 | [Profile(20260919_233625).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_233625%29.csv) |
| Japon | 7680 | 30.00 | 33.556 | 39.573 | 0 | [Profile(20260919_234054).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_234054%29.csv) |
| Royaume-Uni | 7680 | 30.00 | 33.518 | 34.856 | 0 | [Profile(20260919_234523).csv](../Saved/Validation/v0.34.0/20260919-231125-package/Profile%2820260919_234523%29.csv) |

| Preuve | SHA-256 |
|---|---|
| [Rapport source final](../Saved/Validation/v0.34.0/20260919-231125-package/results.json) | `69b2511dfbc3bcdb2ffb81e4f9d72a6700a79183b4e1e44eb295514b4176e1bb` |
| Exécutable mesuré | `b760aeaec8a9a6fc3370051dbf6a9fc13dbfe8a9a661b3905524d09b896bb3ea` |
| [vroom-034-thumb-selected-package.log](../Saved/Logs/vroom-034-thumb-selected-package.log) | `fab8dc9bea8d1289145390d4809032a728db64dd131a7d959a16a26337fafda6` |
| [portable-thumb-selected.log](../Saved/Validation/v034/portable-thumb-selected.log) | `4a33a6ca2bcd57bec0c68107f8027243d2d3058db4d7c35fa71e3ea3bf793279` |
| [car-surfaces-circle-final.log](../Saved/Validation/v034/car-surfaces-circle-final.log) | `93136111419012062ab00e00d3bc353513bd3a37011066b4161b311c86fb8bdc` |
| CSV perf-1440 | `4757e3d8366100cfeb0e92a2ad9bbe9413a44b7ff7f414e204b194027ce33d3d` |
| CSV perf-2560 | `d84961a5641ef4e8acf93fb8fcb96f1dd655a56f7435811a34fa10fdbc25be8d` |
| CSV soak-track-1 | `624fe490bb6028d1f634ce4ed7018c68af10c504f6d160b54ea83867ed962f98` |
| CSV soak-track-2 | `6b1c15f710d7c95fd34df9990968abf21eba469e4723297c4b2af973643cd739` |
| CSV soak-track-3 | `bb0ec9e740a935350e7b440067523780867a7affda4fe3597b4ba37b7e655f5a` |
| CSV soak-track-4 | `21f084162c7b5c791e8c6999549ce6602cac6189df4269cb30b8ace3cb057488` |
| CSV soak-track-5 | `17e1b6ca9ec112543bbb70847a4b42eb4435cf43e85683b1603117e8512f0e63` |
| CSV soak-track-6 | `bcdfbb9b9d0bda004aacda77971491ebfed8de2b0089bb7eab6ef314231e2ec4` |

Les empreintes de toutes les données embarquées, des captures et des journaux figurent dans le [rapport JSON courant](validation-results.json). Le vérificateur confirme leur stabilité pendant la série ; ces identités ne prouvent pas une installation. Les journaux et CSV sous `Saved/` restent des preuves locales non versionnées ; le bilan JSON et les captures de référence sous `docs/images/` sont conservés dans le dépôt.

Windows/NVIDIA, l’activation matérielle DLSS et les périphériques physiques ne sont pas certifiés par cette série Mac. **Installation vérifiée : `0.34.0-c8a4441fbfd4`.** Le lanceur officiel ouvre cette release ; le processus et le chemin du journal ont été contrôlés. Les empreintes de l’exécutable, des données et du rapport installé correspondent aux éléments validés. Le contrôle visible au clavier a appliqué et sauvegardé Photoréalisme (TSR à 80 %, historique à 100 %, plafond de 30 images/s), la vue cockpit et le FOV de 68° ; les touches W/S préexistantes sont conservées. La release 0.33 reste disponible pour retour arrière. Ce contrôle ne certifie pas toutes les interactions à la souris. L’état Git est consultable dans le [dépôt](https://git.estrasse.fr/david/vroom-unreal).
