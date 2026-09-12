# Collisions : références observées et limites

Recherche du 5 septembre 2026, pour retrouver les distinctions demandées par
David : petit choc qui ralentit, gros choc avec casse et reprise, contact sur les
roues qui fait décoller. Les règles numériques doivent être établies à partir du
code et comparées à une édition identifiée ; un souvenir ou une vidéo isolée ne
suffit pas à calibrer les seuils.

## Observation directe du jeu Amiga

[Vroom — Amiga Gameplay, RetrogamingHistory](https://www.youtube.com/watch?v=Bw5OfQjUxt4),
L'Archivio del Sig. Santilio, 3 avril 2009. Images réellement examinées dans
Chrome, avec pauses et recherche temporelle ; aucun téléchargement vidéo.
L'édition exacte et les réglages d'émulation ne sont pas identifiés. La séquence
montre Fuji Yama, 4,359 km, `Stage 1`, `Lap 0`. Temps arrondis du lecteur :

| Passage | Observation |
|---|---|
| [0:18](https://www.youtube.com/watch?v=Bw5OfQjUxt4&t=18s) | Compteur 189 avant l'incident ; vitesse exacte d'impact non mesurée. |
| [0:20–0:21](https://www.youtube.com/watch?v=Bw5OfQjUxt4&t=20s) | Contact avec un panneau à droite, éclatement, roue droite projetée, compteur 0. |
| [0:22–0:23](https://www.youtube.com/watch?v=Bw5OfQjUxt4&t=22s) | Roue rétablie, déplacement latéral vers la piste malgré le compteur toujours à 0. |
| [0:28](https://www.youtube.com/watch?v=Bw5OfQjUxt4&t=28s) | Course reprise, compteur 239. |

La casse, l'attente et le retour sur piste après ce choc contre un objet sont
observés. Ce passage montre une roue détachée ; il ne prouve ni saut intact
après contact roue contre roue, ni seuil de choc léger.

## Seconde vidéo, présentée comme Atari ST

[Vroom for Atari ST — ThisIsPete](https://www.youtube.com/watch?v=nwQOfkkL0CM),
9 septembre 2021, est reliée depuis le [site du vidéaste](https://atariatoz.wordpress.com/2021/09/12/vroom/).
Images examinées en pause dans Chrome :

- [10:27](https://www.youtube.com/watch?v=nwQOfkkL0CM&t=627s) : Autriche,
  Zeltweg, approche d'un arbre et d'un panneau sur la gauche.
- [10:30](https://www.youtube.com/watch?v=nwQOfkkL0CM&t=630s) : arbre au contact
  du cockpit, roue gauche absente, compteur 0.
- [10:32](https://www.youtube.com/watch?v=nwQOfkkL0CM&t=632s) : roue rétablie,
  voiture revenue sur le bitume et repartie.

Cela corrobore la reprise après une casse contre un objet. La plateforme est
celle annoncée par le vidéaste, sans vérification de son émulateur ou du binaire.
L'écran montre `Stage 2` en Autriche ; il faut donc identifier sa révision et
ses options avant d'utiliser cette vidéo pour fixer un ordre de championnat.
La transcription a servi uniquement à trouver ce passage, pas à déduire les
images ni une règle de collision.

## Manuel et témoignage du créateur

Le [manuel original retranscrit avec addendum Amiga](https://www.lankhor.net/jeux/manuel/manuel32-1.pdf)
présente l'usure des pneus et du moteur, leurs conséquences, les réparations aux
stands et les sons d'accident. Il ne donne pas de seuil de collision ni de règle
numérique de décollage. L'addendum réserve l'aide `HELP` d'alignement à
l'entraînement ; cette commande ne doit pas être confondue avec la remise en
piste automatique après un accident.

Dans son [entretien direct](https://www.oldies-but-goodies.fr/index.php?id=414&page=personnalite),
Daniel Macré décrit une simulation très simple fondée sur des tables, sans le
moteur physique complet des jeux Lankhor ultérieurs. Cela soutient une
reconstruction de règles et d'animations déterministes ; cela ne démontre pas
l'absence des effets de collision visibles à l'écran.

## Témoignages de joueurs et presse : corroboration, pas calibration

Le test personnel de **lifeschool**, [Lemon Amiga, Vroom](https://www.lemonamiga.com/review/vroom/406),
décrit les gros chocs comme une casse des pneus suivie d'un retour sur la piste,
et certains chocs moins rapides comme un saut avec un fort rebond à l'arrivée.
Il mentionne aussi les sons accompagnant le passage par-dessus une voiture lente.
Il précise ailleurs que le recentrage peut manquer et laisser le joueur revenir
lui-même. Ce sont des observations rapportées par un joueur, non un document de
conception ni une mesure des vitesses relatives et de la position d'impact.

La [transcription de la presse de 1992](https://amigareviews.leveluphost.com/vroom.htm)
contient deux indices supplémentaires : Carsten Borgmeier, *Amiga Joker*, mars
1992, p. 103, décrit une roue qui part en l'air pendant un crash et une perte de
temps ; Darren Evans, *Amiga Computing* 48, mai 1992, p. 75, relie les accidents
à la dégradation de l'adhérence des pneus. Ces témoignages contemporains
corroborent la casse et ses conséquences. Ils n'établissent pas le mécanisme
précis roue contre roue.

## Confrontation au code

L'analyse assembleur est menée séparément dans `docs/COLLISIONS.md` si ce fichier
est présent. Les termes généraux « choc rapide » et « choc lent » doivent être
affinés : le code retrouvé compare notamment une **différence de vitesse** et
la zone d'impact. Ne pas traduire directement le texte d'une critique en un
seuil sur la seule vitesse absolue du joueur.

L'archive de juillet 1991 reste un état de développement ST. Ses valeurs ne sont
pas encore prouvées identiques à toutes les éditions commercialisées ST/Amiga.
