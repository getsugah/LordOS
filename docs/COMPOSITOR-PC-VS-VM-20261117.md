# Fluidité PC physique / VM — premier audit du 17 novembre 2026

L'utilisateur trouve plus fluide la VM de test lancée sur ce même PC que la
session physique. Cet audit identifie les différences et les pistes de mesure ;
il ne démontre pas une cause et ne modifie pas les réglages du système.

## Configurations retrouvées

- VM : `server/build/mouse-package-vm/installed-qemu-args.json`, QEMU,
  `q35,accel=kvm:tcg`, 2 vCPU, 4 Gio, virtio-vga, GTK `gl=off`.
  Résolution demandée 1024×768. Le journal `serial-transcript.log` contient
  également des sessions à 1143×1143 ; ne pas supposer que l'utilisateur a testé
  exactement la résolution initiale. Les traces confirment un écran, composition
  CPU et cadence logicielle 60 Hz après détection de flips immédiats.
  La configuration demande KVM avec repli TCG ; ces arguments seuls ne prouvent
  pas quel accélérateur a effectivement été utilisé.
- PC : paquet `0.8.8.9+localfix1.microanimations1`, thème default.
  `/var/log/lordos/users/getsugah/compositor.log` du démarrage à 06:55 confirme
  RX 7800 XT/radeonsi, composition GPU, curseur matériel, DP-2 2560×1440 et
  DP-1 1920×1080, tous deux annoncés à 60 Hz. Le fichier displays.json demande
  respectivement 59,951 et 59,940 Hz. Ces faits remplacent les observations à
  120 Hz d'anciennes sessions ; ce n'est pas une mesure de présentation réelle.
- Les sorties physiques totalisent 5 760 000 pixels : 7,32 fois un écran
  1024×768, ou 4,41 fois 1143×1143. Ces rapports ne sont ni des rapports de
  temps de calcul, ni une preuve que cette charge explique l'écart perçu.
- Le processus démarre avec `--headless 1280x800`, puis reçoit les écrans.
  Le journal confirme la transition vers 4480×1440 et le GPU. La ligne de
  commande conservée dans ps ne décrit pas son état graphique après attachement.

## Lecture du code correspondant au paquet

Base : `server/build/microanimations-0.8.8.9/`, et non seulement le dépôt racine,
dont main.c diffère. gpu.c est identique dans les deux arbres comparés.

1. `server/src/main.c:1880` : le pipeline de préparation anticipée exige une
   seule sortie. Il est aussi désactivé par configuration dans la session
   actuelle. Activer LORD_PIPELINE seul ne rendrait pas ce chemin disponible
   avec les deux écrans. Le rendre multi-sorties nécessite de traiter les états
   de peinture et de textures partagés ; retirer la condition serait insuffisant.
2. `server/src/main.c:1982` : le scanout direct Wayland exige une seule sortie.
   Sur deux écrans, ce chemin de plein écran reste composé. Cela concerne les
   applications compatibles, pas tous les gestes du bureau.
3. `server/src/main.c:2845` : le retour de présentation Wayland suit le principal.
   Les états de flip des sorties sont distincts, mais il faut examiner les
   timestamps de chacune pour juger leur régularité. Les FPS agrégés des lignes
   drag ne mesurent pas la cadence effectivement présentée par chaque écran.
4. Le chemin GPU conserve layout/peinture CPU pour l'interface, uploads puis
   composition GLES. Le rendu GPU actif ne garantit donc pas une faible latence
   du thread principal.
5. `server/src/wl/wl.c:653` : srf_damage ne conserve pas les rectangles clients.
   copy_buffer recopie toutes les lignes des buffers SHM. Une optimisation
   éventuelle doit gérer les changements de buffer, de dimensions et l'historique
   de validité ; elle ne concerne pas de la même façon les clients dmabuf.
6. Le chemin VM possède un rythme logiciel explicitement prévu pour les flips
   virtuels immédiats. Le chemin matériel utilise les flips KMS réels. Cela
   prouve une différence de fonctionnement, pas une optimisation globale pour VM.

## Première comparaison discriminante

Comparer sur le PC, avec une même fenêtre native puis une même fenêtre Wayland,
le déplacement, le redimensionnement et la réduction/restauration :

1. Deux écrans, fond statique, modes actuels.
2. Un seul écran, même résolution principale, même fond et mêmes gestes.
3. Un seul écran à une résolution comparable à la VM, si nécessaire.

Ces configurations séparent partiellement le coût du multi-écran et celui de la
surface rendue. Consigner l'application, l'heure et le ressenti pour chaque cas.
Ne pas désactiver le GPU en même temps : cela changerait un second facteur.
Ne pas assimiler une VM affichée dans une fenêtre à un test direct de KMS AMD.

Les captures CPU/GPU à 2 Hz déjà disponibles ne suffisent pas à attribuer une
saccade de quelques millisecondes. Pour choisir une correction, relever par
sortie entrée traitée, construction, peinture, upload, swap, soumission et flip.
L'ancien instrumenté 0.8.8.8 ne doit pas remplacer sans vérification le binaire
0.8.8.9 corrigé actuel. Réutiliser son mécanisme de mesure sur la bonne base si
une nouvelle session instrumentée est nécessaire.

## Résultat de cette étape

Pas de preuve d'une préférence générale du moteur pour les VM. Différences
importantes de résolution, chemin graphique et synchronisation confirmées.
Priorité d'investigation : régularité de présentation multi-sorties et coût CPU
de production d'une image, avant une modification globale des paramètres GPU.
Aucune optimisation ni amélioration physique n'est revendiquée par cet audit.
