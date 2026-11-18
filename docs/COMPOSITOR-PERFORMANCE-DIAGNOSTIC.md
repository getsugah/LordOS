# Diagnostic de fluidité — préparation, sans correction

## Capture réelle

Dans un terminal de la session LordOS, sans sudo :

```sh
python3 /home/getsugah/LordOS/tools/diag/compositor-capture.py
```

La capture dure au maximum 180 secondes (option `--seconds`, plafond 600).
Résultats dans `/tmp/lordos-perf-DATE-HEURE` ; conserver ce dossier avant un redémarrage.
Python 3 et sa bibliothèque standard suffisent. Aucun changement système, signal,
redémarrage, lancement de benchmark ou événement synthétique. Aucun contenu de fenêtre collecté.
Les noms de threads et journaux peuvent contenir des informations locales.

Pour chaque scénario, saisir `begin NOM`, faire les gestes pendant environ 15 secondes,
puis revenir saisir `end`. Ordre proposé : repos, déplacement, resize, firefox, vidéo.
Utiliser la même fenêtre, page, vidéo et le même écran ; noter l'application, la résolution
et cadence vidéo avec `note ...`, ainsi que toute saccade perçue.
Les transitions vers le terminal sont comprises dans les marqueurs : l'analyse devra
exclure les bords. `quit` ou Ctrl-C termine et sauvegarde.
Pour la comparaison mono-écran, faire une seconde capture et débrancher manuellement
le secondaire avant le lancement, si souhaité ; aucun changement automatique.

## Mesures et limites

- `/proc/stat` : occupation CPU globale et par cœur par différence des compteurs
  (éviter de compter deux fois guest ; idle et iowait présentés séparément).
- `task/*/stat` : CPU de chaque thread, user/system, par différence / CLK_TCK / durée.
  100 % représente un cœur. Vérifier le starttime pour les threads recréés.
- `schedstat` : temps exécuté, attente en file d'exécution et nombre de passages,
  si la comptabilité du noyau est active. Zéro n'établit pas l'absence d'attente.
  `wchan` et états sont des instantanés à 2 Hz, pas une durée de blocage.
- AMDGPU sysfs : busy GPU, busy contrôleur mémoire, VRAM/GTT utilisées, fréquences
  exposées. Busy mémoire n'est pas une bande passante ; mémoire allouée n'est pas
  une preuve de saturation. Ces mesures concernent toute la carte.
- DRM fdinfo : compteurs moteur et mémoire par client du compositeur, si le noyau
  les expose. Les fd d'un même client sont dédupliqués. Calculer les différences
  par client/moteur ; les changements de client et resets invalident l'intervalle.
- PSI : pression CPU/mémoire/I/O, pour distinguer contention système et charge locale.
- Journal du PID : au plus 1500 dernières lignes sur la période. Peut être vide,
  inaccessible ou tronqué. Aucun accès en lecture au pipe stderr du compositeur.
- Connecteurs : présence, activation et modes annoncés au démarrage de capture.
  Le fichier `modes` ne prouve pas le mode ni la fréquence actuellement utilisés.

La mesure à 2 Hz ne résout pas les frames de 8,33 ms à 120 Hz. Elle ne mesure
directement ni durée de dispatch d'événement, ni latence entrée-écran, ni durée GPU
d'une frame, ni flips/vblanks manqués. Une faible charge moyenne n'exclut pas de pics.
Un intervalle long au repos ou sur une vidéo à 24/30 FPS n'est pas une frame manquée.
L'absence de diagnostic de drag dans le journal n'est pas une absence de problème.

## Audit ciblé du code disponible

Le code local peut différer du binaire 0.8.8.8 installé ; le chemin du binaire est enregistré.

- `server/src/main.c:2635` et suite : boucle commune `poll`, dispatch entrées/Wayland,
  JS puis rendu ; un travail long sur le thread principal peut retarder les suivants.
  Le rendu tient compte des flips occupés et peut préparer une frame en avance
  (`fpipe.c`). Il n'y a pas ici de preuve d'un plafonnement général à 60 Hz en KMS réel :
  la cadence logicielle 60 Hz concerne notamment le mode sans écran.
- `server/src/main.c:467`, `server/src/ui/grab.c:111` : déplacement en C, position
  invalidée ; redimensionnement invalide aussi le contenu et son layout.
  `dispatch_pointer` imprime des statistiques `drag:` à la fin d'un geste.
  Ce sont des temps de travail/intervalle de construction, pas des timestamps
  de scanout ; le champ « present » est un reste de temps CPU.
- `server/src/main.c:1250`, `:1857`, `server/src/gpu/gpu.c:1268` : layout/peinture
  de l'UI sur CPU, uploads de textures, composition GLES puis eglSwapBuffers et KMS.
  Une carte puissante n'élimine donc pas un coût CPU de peinture ou d'upload.
  Chemins de drag GPU et damage partiel présents ; leurs refus sont à vérifier.
  Échec GPU : fallback CPU journalisé. Partage distant : readback possible.
- `server/src/main.c:2749`, `server/src/wl/wl.c:4743` : retours de flips et feedback
  de présentation fondés sur l'écran principal. Les sorties ont des états distincts,
  mais le feedback principal ne permet pas de mesurer indépendamment le secondaire.
- `server/src/kms.c:168` : mode choisi par le picker, sinon mode préféré ; vérifier
  les lignes `kms: ... @...` et les réglages existants pour confirmer 120 Hz réels.
  `wl_output` publie la fréquence ; ce n'est pas une mesure de cadence livrée.
- Scanout direct limité à un seul écran dans `out_present` : une comparaison vidéo
  mono/bi-écran change potentiellement le chemin de rendu, pas seulement la charge.
- `server/src/bench/lbframes.h` : mesures internes déjà prévues (wake, avant rendu,
  peinture, upload, swap, submit, fence GPU, flip), principalement écran principal.
  Activation liée aux scénarios LordBench et au drag scripté : ne pas les utiliser
  comme remplacement d'une capture interactive. Pas d'interface passive activée ici.

Ce sont des points de vérification, aucune cause n'est établie sans capture.

## Approfondissement seulement après la première capture

Prérequis observés lors de la préparation : python3 et journalctl disponibles,
compteurs AMDGPU présents, aucun lord-server actif, perf et modetest absents,
perf_event_paranoid=3. Aucun paquet installé, aucun paramètre modifié.

Si les compteurs ne suffisent pas, installation Debian 13 à autoriser séparément :
`sudo apt install linux-perf` ; `libdrm-tests` seulement si une interrogation KMS
supplémentaire est nécessaire. Ne pas utiliser les options de modeset de modetest.
Un profil perf facultatif, après vérification des permissions, serait borné ainsi :

```sh
perf record -e cpu-clock -F 49 -g -p PID -o /tmp/lordos-perf.data -- sleep 20
perf report --stdio -i /tmp/lordos-perf.data
```

Ce profil attribue le temps CPU échantillonné aux fonctions ; il ne mesure pas les
attentes hors CPU. Les piles peuvent être incomplètes sans symboles/frame pointers.
Avec paranoid=3 il peut être refusé : ne pas changer sysctl automatiquement et
discuter l'accès privilégié avant d'approfondir. Aucun profil lancé par l'outil.
Une exposition passive des mesures `LbPhase` existantes nécessiterait une modification
interne et éventuellement compilation/session de test : demander l'accord préalable.

## Rapport après capture

Comparer les scénarios marqués : CPU principal et workers, attente ordonnanceur,
busy global GPU vs compteurs moteur du compositeur, mémoire, journal et ressenti.
Séparer observations, hypothèses et données absentes. Attribuer les fonctions
seulement avec un profil ou une mesure interne ; ne pas déduire une cause de busy seul.
Classer ensuite les pistes selon les preuves : peinture/layout, upload/drag fallback,
GPU ou synchronisation/pacing, mode écran/multi-sorties, client vidéo/Firefox.
Les optimisations restent des propositions ; aucune correction n'est autorisée ici.
