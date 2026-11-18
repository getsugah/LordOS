# Version de diagnostic isolée

Instrumentation autorisée par l'utilisateur le 15 novembre 2026. Le compositeur
actuel et ses sources ne sont pas remplacés. Aucun changement de comportement
graphique, correction du curseur ou benchmark synthétique inclus.

Observations utilisateur affinées : pendant une période fluide, le survol d'un
bord ne produit pas de curseur fantôme, mais le clic maintenu pour déplacer une
fenêtre en produit encore un sur le secondaire. Pendant une période saccadée,
le fantôme est également visible au survol des bords, sans clic. L'affirmation
initiale d'une disparition totale du fantôme en période fluide est donc corrigée.
Le fantôme seul n'est pas un indicateur fiable de l'état saccadé. Il faut comparer
séparément survol et prise de fenêtre, dans chacun des deux états. Le changement
de forme reste une piste commune, sans preuve qu'il explique tout le phénomène
ou la baisse de fluidité. L'instrumentation distingue forme, état de grab et
nombre de boutons dans `pointer_dispatch`, ainsi que les mises à jour de curseur.

L'utilisateur rapporte aussi des saccades lors des animations de fermeture et
de réduction. Le symptôme dépasse donc le déplacement et les transitions de
curseur ; ne pas attribuer le manque de fluidité général au seul défaut de
curseur ou au chemin de drag GPU. La future capture doit inclure réduction et
restauration d'une même fenêtre, ainsi qu'un déplacement, dans les deux états.

## Artefact

Snapshot prêt :
`server/build/compositor-diag-20261115-201859/server/build/lord-server`.
Sources, Makefile et third_party copiés dans le même arbre isolé ; les modifications
locales existantes sont incluses dans la copie, sans être éditées. Le fichier
`instrumentation.patch` documente uniquement les ajouts d'instrumentation par
rapport aux sources locales ; `source-status.txt` conserve l'état Git au moment
de la préparation. Les objets et le binaire normaux ne sont pas reconstruits.

Recréation du snapshot, sans compilation ni lancement :
`python3 tools/diag/prepare-instrumented.py`.
La compilation GPU utilise le SDK déjà extrait dans
`server/build/mouse-package-sdk/root` ; aucun paquet installé ou téléchargé.

## Activation à préparer pour une session de test manuelle

La procédure d'activation manuelle est maintenant préparée dans
`docs/COMPOSITOR-TEST-MANUEL.md`, avec `session-test.py --start-test` et `--restore`.
Elle conserve le lancement géré par lord-sessiond plutôt qu'un lancement standalone
aux droits/priorités différents. Aucun de ces deux modes n'a été exécuté par l'agent.

Ce binaire ne peut pas instrumenter le processus déjà lancé. Il doit être choisi
pour une session de test dédiée, avec les arguments, droits DRM et environnement
du lancement normal de LordOS. Ne pas le lancer sur les écrans de la session
active. Aucune activation, remplacement, installation, bascule de session ou
relance n'a été effectuée automatiquement.

Au futur lancement manuel de cette version seulement, définir :

```sh
LORD_DIAG_FILE=/tmp/lordos-internal-UNIQUE.csv
LORD_DIAG_SECONDS=120
```

Ce sont des variables d'environnement à exporter pour le processus de test ;
ce bloc seul ne lance rien. Utiliser un chemin neuf, non un fichier existant.
Le mécanisme d'activation de session devra être vérifié avant de fournir une
commande qui prend effectivement le contrôle des écrans.

Sans LORD_DIAG_FILE : enregistreur désactivé. Avec : capture depuis l'entrée dans
la boucle principale, limitée à 120 s par défaut (10 à 600 s configurables) et
131072 événements. Limite mémoire d'environ 20 MiB, aucun accès disque par frame.
Un thread écrit le CSV une fois la capture arrêtée (durée, capacité ou sortie
normale du processus). Le fichier peut rester vide jusqu'à cette écriture ; un
crash/arrêt brutal avant vidage perd la capture. Permissions 0600, création exclusive.
Après la limite, seul l'enregistrement s'arrête ; le compositeur continue.

## Mesures

Chaque ligne commence par CLOCK_MONOTONIC en nanosecondes et un type d'événement.
Les horodatages ne sont pas ceux de l'arrivée physique des événements d'entrée.

- `output_state` : index, CRTC, largeur/hauteur, Hz du mode, curseur matériel,
  capacité du plan curseur, échelle, index de sortie GPU.
- `pointer_dispatch`, `wayland_dispatch` : début/fin du traitement applicatif.
  Pointer ajoute mouvement, nombre de boutons, roue, grab actif, forme.
- `loop_work`, `poll_wait` : début/fin de travail avant poll et attente dans poll,
  timeout demandé. Poll peut attendre normalement au repos ; son attente n'est
  pas une preuve de blocage. Les handlers après poll ne sont pas tous minutés
  séparément ; le prochain timestamp permet de borner le temps non attribué.
- `render_total` : début/fin rendu, layout, peinture, soumis/préparé.
- `phase`, `phase_extra` : mesures LbPhase existantes pour le principal, dont
  réveil, rendu, peinture, upload, swap, submit, pipeline et refus drag GPU.
  Voir `server/src/bench/lbframes.h` pour leur sémantique précise. `phase` contient
  wake/render0/built/submit/prev_flip/period puis paint/up/gdrag/swap/submit_call/
  feed/piped/gdrag/gdrag_why/unshown. `phase_extra` contient up_px/gdrag_px/feed_px/
  par/swake/sub0/sub1/from_flip/sfrom_flip/gdrag_sub.
- `output_gpu` : CRTC, upload/drag upload/swap/submit en ms, pixels uploadés,
  scanout direct, code retour. `output_present_call` : CRTC, début/fin, submit,
  flip pending. Mesures par sortie, pas seulement le principal.
- `output_flip` : CRTC, timestamp DRM, séquence, Hz et dernier submit. Les grands
  intervalles comprennent les périodes sans rendu ; ne pas les compter d'emblée
  comme frames manquées. `main_shown` inclut la fence GPU si disponible (0 = inconnue).
- `cursor_update` : début/fin, mode, génération, dimensions, curseur matériel actif,
  index de l'écran sous le pointeur.
- `cursor_image` : début/fin ioctl, CRTC, succès et état visible précédent,
  dimensions. `cursor_show` : début/fin ioctl, CRTC, visibilité demandée, retour.
  Cela permet de chercher les réactivations d'un secondaire précédemment masqué
  et le délai avant son masquage. Les timestamps des appels ne mesurent pas
  directement l'affichage optique du curseur.

Analyse après capture réelle :

```sh
python3 tools/diag/analyze-live.py /tmp/lordos-internal-UNIQUE.csv
```

L'instrumentation ajoute horodatages, copies mémoire, mutex et un thread de vidage.
Les fences GPU existantes sont activées durant l'enregistrement ; cela ajoute
une interrogation ciblée. L'effet d'observation n'est pas nul et doit rester dans
les limites du diagnostic. Aucun réglage CPU/GPU, rythme de présentation, chemin
de curseur, méthode de damage ou règle de frame callback n'est modifié.

## Validation sans lancer de compositeur

Compilation GPU avec symboles et frame pointers. Vérification unitaire de
l'enregistreur : désactivation par défaut, producteurs concurrents, arrêt à 131072
événements, timestamps ordonnés, sortie exclusive et absence d'écriture après arrêt.
ASan/UBSan utilisés ; LeakSanitizer désactivé car incompatible avec l'environnement
de traçage. Aucun test de cadence fictif ni capture d'une session de remplacement.

Compilation finale réussie sans avertissement dans
`/tmp/lordos-instrumented-build-ready.log` ; symboles `diag_init`, `diag_event`,
`gpu_frame_ex` et liaison `eglSwapBuffers` vérifiés sans exécuter le binaire.
`manifest.json` contient le SHA-256 du binaire et du patch ainsi que le HEAD source ;
`build-command.txt` conserve la commande exacte (à exécuter depuis le snapshot).
