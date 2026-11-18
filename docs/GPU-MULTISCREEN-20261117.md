# Premier correctif GPU multi-écran — 17 novembre 2026

## Problème traité

Le chemin de déplacement GPU gardait une seule paire de textures pour toutes les
sorties : copie du contenu sous la fenêtre et couche de la fenêtre. Envoyer des
pixels pour une sortie évincait ceux de l'autre, même si celle-ci devait encore
réafficher son dernier rectangle GPU. Une alternance pouvait donc provoquer une
copie complète ou le refus du quad, suivi d'une réparation CPU.

Chaque GOut possède maintenant sa paire DragTextures. Elle est allouée au premier
usage, conservée indépendamment des autres et libérée avec la sortie. Le test de
précision du shader utilise une paire temporaire distincte. Une taille modifiée
continue à exiger le remplissage complet ; les listes de zones sales et le budget
des envois différés restent gérés par paint.c.

Ce changement cible les évictions et la réutilisation des rectangles lors du
passage entre sorties. Il ne transforme pas toute l'interface en rendu GPU,
ne sépare pas encore le cache CPU de peinture par sortie et n'active pas le
pipeline anticipé multi-écran. Aucun gain global de FPS ou de latence physique
n'est établi à ce stade.

## Validation

- Test réel EGL/GLES hors écran, sur RX 7800 XT/radeonsi : 202 vérifications,
  aucune erreur. Aucun démarrage de compositeur ni changement d'écran.
- Même test sur llvmpipe : succès ; ASan/UBSan : succès, détection des fuites
  désactivée pour ce passage avec Mesa.
- Le test inclut le test de production du shader : 11 373 321 pixels pour
  l'arithmétique prémultipliée et le placement 4096×2048.
- Deux tailles de sorties, contenus distincts, alternance avec listes de zones
  sales vides : aucun pixel retransmis après le remplissage initial. Réaffichage
  sans source CPU, mise à jour d'un texel, redimensionnement d'une seule texture,
  libération et réallocation : pixels attendus et isolation vérifiés.
- Le code antérieur, exécuté avec le même scénario de départ, échoue après le
  remplissage des deux sorties : il ne conserve pas les deux paires à la fois.
- Compilation GPU=1 de la base corrigée 0.8.8.9, build 20261117.multigpu1,
  sans avertissement ; GBM/EGL/GLES liés, sans runtime de sanitizer ni bibliothèque
  manquante. La variante CPU de gpu.c compile également.
- Moteur de rendu recompilé puis exécuté depuis server : 8 915 vérifications,
  aucun échec (dont damage, fuzz, scissor et pixels de déplacement).
- Archive .deb extraite : 2 898 entrées fichiers/liens identiques au staging,
  y compris les modes. Version et SHA256 enregistrés dans la livraison.

Le test hors écran vérifie les pixels et les transferts, pas le scanout KMS ni
les intervalles de présentation des deux moniteurs. La conservation des paires
augmente la mémoire GPU après utilisation de plusieurs sorties : environ
4 × (pixels de la copie du dessous + pixels de la couche) octets par sortie.
Le coût dépend de la taille de fenêtre et du nombre de sorties effectivement
utilisées ; aucun stockage de drag n'est alloué pour une sortie jamais utilisée.

## Sources et livraison

Sources suivies : server/src/gpu/gpu.c, gpu.h, server/Makefile,
server/tests/gpu_multioutput_test.c et documentation GPU.
Copie de compilation : server/build/multigpu-0.8.8.9, dérivée de la base du
paquet microanimations1. Le dépôt racine contient d'autres modifications locales
qui n'ont pas été remplacées ni incorporées arbitrairement dans ce paquet.

Livraison : server/build/DEB-multigpu-0.8.8.9.
Version : 0.8.8.9+localfix1.microanimations1.multigpu1.
Seul le binaire lord-server et les métadonnées de paquet changent par rapport
à microanimations1. Les thèmes, applications et scripts de maintenance sont
conservés. Le manifeste de code et les MD5 du staging sont vérifiés.

Le paquet n'est pas installé et la session active n'est pas redémarrée.
La notice INSTALLATION.md de la livraison décrit installation et retour arrière.
