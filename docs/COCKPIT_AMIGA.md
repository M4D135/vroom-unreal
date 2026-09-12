# Référence du cockpit : Vroom Amiga

David a choisi explicitement l'édition **Amiga** le 6 septembre 2026. Les sources ST de juillet 1991 restent une référence technique pour les modèles et l'extraction ; elles ne prouvent pas à elles seules la parité avec l'édition Amiga commercialisée.

## Observation

Référence visuelle : [Vroom Longplay (Amiga), AL82](https://www.youtube.com/watch?v=U6bsfmMaFGU&t=138s), observée directement dans le lecteur. À 2:18, le cadre complet au rapport 1,60 montre un bandeau supérieur occupant environ 34 % de la hauteur : carte à gauche, colonne de trois feux, titre du Grand Prix et nom du circuit en jaune/orange, puis score, stage, tour, petites voitures et jauge verticale. Les proportions sont des mesures approximatives de la vidéo, pas des coordonnées extraites des sprites.

Le groupe roues/rétroviseurs occupe environ 54 % de la largeur. Le petit volant bleu marine, large d'environ 24 %, reste presque entièrement sous le cadre : son arc supérieur commence vers 86–88 % de la hauteur totale. Dans le sprite neutre extrait (`cockpit_pose_03.png`), les premiers pixels des gants se trouvent vers 91–91,5 % : le sprite est placé à y=172 sur l’écran de 200 pixels et le haut des gants vers y=10–11 dans ce sprite. Les rétroviseurs rouges sont devant les pneus sombres ; de petites liaisons claires les rejoignent. Les instruments comportent une grille de rapports à gauche, une vitesse numérique à droite et une barre de régime verte/ambre/rouge dessous. Deux petits voyants T/E encadrent le bloc.

À [2:02](https://www.youtube.com/watch?v=U6bsfmMaFGU&t=122s), un gant clair est haut sur la jante ; le poignet a un liseré ocre et les ombres/renforts sont bleutés. À 2:33 et 2:38, le gant gauche atteint 10–11 heures, avec l'avant-bras oblique venant du bas gauche ; l'autre main sort presque du cadre. Le repère rouge tourne avec la jante. La route et les concurrents sont visibles dans les rétroviseurs.

## Application dans ce prototype

La référence a été précisée pendant la séance : **reprendre les formes en vraie 3D et moderniser l'interface**, sans afficher les sprites originaux dans la version jouable. Les sept images Amiga de 96 × 28 pixels, leur masque et leur palette restent des documents de travail dans `docs/reference/amiga` ; l'extraction est décrite dans [AMIGA_ART.md](AMIGA_ART.md).

Le volant, les mains et les manches sont des maillages avec volume, normales et matériaux éclairés. Les gants utilisent le modèle OpenXR de Valve : la prise des doigts est calculée une fois à partir des articulations, avec cuir ivoire, renforts sombres et coutures texturées. Le modèle source de main gauche reste à gauche sans miroir ; sa symétrie produit la main droite. La provenance et la licence sont dans [ASSETS.md](ASSETS.md).

Les deux gants sont des composants séparés, attachés à la racine du cockpit. Leur transformation est calculée dans le repère du volant : une prise locale fixe définit le contact, puis la même rotation entraîne la jante et les mains. L’orientation du gant place le dos vers le pilote et le pouce à l’intérieur ; la pose préparée referme les doigts autour de la prise. Les manches rejoignent les poignets au fil du braquage.

Le cadrage conserve le volant bas au neutre. **La main gauche monte dans un virage à droite ; la main droite monte dans un virage à gauche.** La main opposée descend et peut quitter le cadre tout en restant attachée à sa prise. La caméra n’est pas remontée pour garder les deux mains visibles.

Les sept dessins originaux servent de référence pour cette composition et le sens des gestes. Le rendu actif n’interpole plus des couples poignet/dos de main mesurés dans les sprites : ces positions d’écran ne suffisaient pas à assurer une prise cohérente sur une jante en volume. Le placement actuel est une adaptation 3D continue, sans correspondance pixel par pixel ni reconstruction exacte de l’anatomie revendiquée.

Le filtre visuel utilise la même commande pour la jante et les mains. Les avant-bras mettent à jour leurs sommets pour suivre les poignets ; les coudes restent sous le cadre. Le cockpit comprend onze composants et conserve son clignotement commun après récupération. L’appui visible, le cadrage au neutre et les mouvements à gauche et à droite doivent être évalués dans le rendu du build concerné ; une relation géométrique correcte ne suffit pas à valider l’apparence des doigts.

La grille des rapports, les trois chiffres de vitesse et la barre de régime suivent la simulation. Les rétroviseurs partagent une capture arrière 3D. Cockpit et miroirs se masquent en poursuite et partagent le clignotement de protection des roues.

Le HUD affiché est désormais dessiné avec **Canvas** : panneau bleu nuit translucide, angles arrondis, caractères modernes et accents cyan. Il mesure 112 pixels de haut dans l’espace de référence 1440 × 900, environ 12,4 % de la hauteur, avec une marge supérieure. Le grand bandeau 3D de 34 % n’est plus utilisé dans le parcours de rendu actif. La présentation conserve la carte, les trois voyants, Grand Prix et circuit, score, stage, tour, 27 repères de dépassement et essence. Le score, les dépassements et le carburant suivent leurs états simplifiés ; les vies et les règles complètes de l’arcade ne sont pas reproduites. Le chrono reste en bas à gauche ; vitesse, rapport et régime figurent sur le cockpit, ou en complément Canvas dans la vue de poursuite. Le dérapage et la poussée ont leur propre indicateur.

Cette interface est une modernisation explicite des informations originales. Les références et les réglages décrits ne garantissent ni une fidélité intégrale du gameplay ni la fluidité d’un nouveau build ; les observations datées sont consignées dans [VALIDATION.md](VALIDATION.md).
