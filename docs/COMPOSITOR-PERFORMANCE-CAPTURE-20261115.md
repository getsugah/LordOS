# Analyse de la capture du 15 novembre 2026

Source : `/tmp/lordos-perf-20261115-194740`, PID 9552.
195 échantillons sur 97,60 s ; pas moyen 503 ms, maximum 505 ms.
Le début enregistré correspond à 19:47:40,51 heure de Paris.
Aucune capture supplémentaire ni modification du compositeur effectuée.

## Complément utilisateur : effet possible de la collecte

L'utilisateur indique avoir lancé uniquement la commande, sans saisir de marqueurs,
et avoir ressenti une amélioration générale pendant la collecte, disparue à son arrêt.
L'activité exacte durant les 98 secondes reste inconnue. Cette observation rend
possible un effet de l'observateur : les chiffres ci-dessous décrivent le système
pendant la collecte, potentiellement dans un état plus fluide, et ne caractérisent
donc pas nécessairement son comportement habituel. Aucun état avant/après n'a été mesuré.

Le collecteur lit notamment des compteurs et fréquences AMDGPU deux fois par seconde,
par les interfaces du pilote, et produit des réveils CPU et écritures périodiques.
Une lecture seule ne garantit pas l'absence d'effet transitoire sur le timing ou
l'activité matérielle. Une influence énergétique/driver est une hypothèse, pas une
cause établie ; ne pas forcer les fréquences sur cette base.

Un mode `--compare` est préparé sans lancer de capture : 8 phases automatiques de
15 s, pause/complet/pause/proc/pause/gpu/pause/complet. Répéter le même geste, garder
le même terminal visible et noter les numéros des phases fluides. La pause conserve
le timer Python à 2 Hz et la console ouverte, mais aucune lecture périodique proc/GPU.
Elle ne reproduit donc pas exactement l'arrêt du processus ; si toutes les phases
restent fluides jusqu'à sa sortie, le résultat restera compatible avec un effet
commun (timer, terminal, contexte), sans isoler les lectures GPU. Les transitions
peuvent aussi avoir des effets persistants : vérifier la répétabilité avant conclusion.

Commande : `python3 tools/diag/compositor-capture.py --compare`.
Les marqueurs sont automatiques. Le mode GPU seul n'enregistre pas les champs CPU :
l'analyse doit en tenir compte. Les compteurs ne sont pas lus pendant les pauses.

## Conclusion étayée

Les mesures ne montrent pas de saturation globale durable CPU, GPU ou mémoire.
Elles montrent des pointes de travail du thread principal. Elles ne suffisent pas
à déterminer la cause des saccades : aucun timestamp de présentation n'est disponible.
La faible charge globale ne garantit pas le respect du budget de 8,33 ms à 120 Hz.

| Mesure | Moyenne | P95 | Maximum |
|---|---:|---:|---:|
| CPU machine, sur 16 CPU logiques | 2,59 % | 5,08 % | 12,69 % |
| CPU processus compositeur, 100 % = un cœur | 18,26 % | 53,70 % | 75,54 % |
| CPU thread principal, même convention | 12,99 % | 33,80 % | 75,54 % |
| GPU busy, carte entière | 8,41 % | 13 % | 22 % |
| Contrôleur mémoire busy | 1,59 % | 3 % | 4 % |

CPU : différences de ticks à 100 Hz divisées par la durée monotone de chaque intervalle.
Moyennes des intervalles, pratiquement de même durée. P95 empirique sans interpolation.
Charge globale : user/nice/system/irq/softirq/steal, hors idle et iowait, sans
double comptage guest. GPU : valeurs sysfs échantillonnées, avec leur propre fenêtre
d'intégration ; les maxima ne représentent pas des maxima par frame.

VRAM observée : 668 à 777 MiB, sur environ 16 GiB exposés (moins de 5 %).
GTT maximale : environ 151 MiB. Aucune augmentation de PSI mémoire pendant la capture.
PSI CPU « some » : +0,738 s cumulée à l'échelle machine ; PSI I/O « some » :
+0,206 s. Ces chiffres ne sont pas des durées d'attente du compositeur.

Le processus consomme 17,82 secondes CPU, dont 12,68 pour le thread principal
(10,42 user et 2,26 system). Les sept threads 9556–9562 consomment chacun environ
0,56–0,58 s ; leur fonction ne peut pas être identifiée par leur nom identique.
Le thread `lord-serve:cs0` consomme 0,54 s. Pas de saturation soutenue d'un cœur
visible à cette résolution, mais des pointes plus courtes restent possibles.

## Répartition temporelle, sans attribution aux gestes

Toutes les secondes sont relatives au premier échantillon.

| Période | CPU processus | CPU principal | GPU busy |
|---|---:|---:|---:|
| 0–10 s | 4,81 % | 3,98 % | 8,30 % |
| 10–20 s | 13,51 % | 11,33 % | 8,50 % |
| 20–30 s | 22,85 % | 22,35 % | 2,80 % |
| 30–40 s | 4,08 % | 3,58 % | 9,30 % |
| 40–50 s | 11,43 % | 10,24 % | 7,70 % |
| 50–60 s | 11,63 % | 7,85 % | 8,00 % |
| 60–70 s | 43,14 % | 22,66 % | 10,50 % |
| 70–80 s | 44,52 % | 26,83 % | 10,45 % |
| 80–90 s | 13,29 % | 10,46 % | 9,53 % |
| 90–fin | 10,93 % | 9,32 % | 9,19 % |

Vers 20–26 s, le principal atteint environ 70–76 % d'un cœur sur certains
intervalles de 0,5 s, alors que le busy GPU est faible sur le bloc 20–30 s.
Cela justifie de rechercher du travail CPU dans cette période ; cela ne prouve
ni peinture coûteuse, ni fallback CPU, ni attente de présentation.
Vers 60–80 s, les autres threads contribuent davantage tandis que le GPU reste
modérément occupé. Il manque l'identité des actions pour interpréter ces différences.

Trois observations instantanées du principal en état D : +17,61, +28,18 et +31,20 s.
Elles établissent des attentes non interruptibles à ces instants, sans en mesurer
la durée ou l'origine (pas nécessairement du disque). `wchan` vaut 0 partout :
aucune fonction d'attente identifiable. Les autres observations sont 165 états S
et 27 états R ; ces proportions ne mesurent pas le temps passé dans ces états.
Le compteur schedstat du principal augmente de seulement 0,709 ms en file
d'exécution ; sans validation de cette comptabilité noyau, ne pas en tirer une
exclusion des problèmes d'ordonnancement.

## Limites concrètes

- Aucun marqueur begin/end/note : impossible de comparer repos, drag, resize,
  Firefox et vidéo. Ne pas inventer leur ordre à partir des variations de charge.
- Journal vide, avec avertissement d'accès restreint : aucune statistique drag,
  aucun mode KMS, aucune preuve de fallback GPU exploitable.
- DRM fdinfo vide sur tous les échantillons : activité GPU propre au compositeur
  inconnue ; la cause (permissions, exposition noyau, autre) n'est pas établie.
- Les liens `/proc/PID/exe` et stderr n'ont pas été résolus : chemin/version exacte
  du binaire non confirmé. Les fichiers stat/status ont cependant été collectés.
- Deux connecteurs DP actifs au début ; fréquence active non mesurée. Les modes
  annoncés ne prouvent pas 120 Hz.
- La sélection sysfs de l'outil inclut aussi des connecteurs : leurs erreurs ENOENT
  sont sans rapport avec une panne GPU. Les vrais compteurs `card0/device` sont
  bien présents et seuls ceux-ci sont utilisés dans les calculs.
- Aucun temps de dispatch, frame, fence ou flip : pas de comptage d'images manquées,
  pas de preuve objective de cadence irrégulière ou de durée anormale de blocage.
- Les fréquences GPU instantanées sont souvent basses et variables ; cela ne prouve
  pas un problème d'alimentation ou un besoin de forcer un profil performance.

## Vérifications prioritaires et optimisations conditionnelles

1. Récupérer l'ordre et les moments approximatifs des actions, ou refaire une capture
   avec marqueurs. Confirmer le mode réellement actif à 120 Hz et rendre accessibles
   les journaux existants de la session, sans changer les paramètres permanents.
2. Si la pointe 20–26 s correspond au geste problématique, profiler le CPU pour
   distinguer `dispatch_pointer`/`grab_motion`, JS, layout/peinture, upload et appels
   driver. Perf absent et paranoid=3 : installation et accès éventuel à autoriser.
   Les fonctions ci-dessus sont des candidates issues de l'audit, pas des coupables.
3. Mesurer les phases internes existantes `LbPhase` et les flips/fences pour départager
   travail CPU tardif, GPU tardif, réveil tardif et attente de vblank. L'exposition passive
   de ces mesures pour des gestes réels nécessiterait un accord avant instrumentation
   interne ou recompilation. Aucun changement de ce type réalisé.
4. Comparer mono/bi-écran si les timings ou le mode suggèrent la présentation ; le
   scanout direct peut changer de chemin en mono-écran, notamment pour la vidéo.

Optimisations à envisager selon les résultats, par pertinence actuelle : réduire le
travail du thread principal si confirmé (layout/peinture, damage, uploads, refus de
drag GPU) ; ajuster pacing/synchronisation multi-sorties si les flips l'établissent ;
agir sur décodage/client vidéo si ses propres mesures l'incriminent. Une optimisation
de capacité GPU/VRAM générale est peu soutenue par cette capture. Aucun réglage de
fréquence, priorité, rendu ou correction du compositeur proposé comme remède établi.
