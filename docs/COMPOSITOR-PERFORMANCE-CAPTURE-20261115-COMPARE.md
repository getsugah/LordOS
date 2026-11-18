# Comparaison interactive et duplication du curseur

Capture : `/tmp/lordos-perf-20261115-195921`, 120 secondes, huit phases de
15 secondes. Les huit débuts/fins sont enregistrés ; 30 échantillons par phase
instrumentée, aucune lecture périodique dans les pauses.

Retour utilisateur : aucune différence entre les phases, bureau saccadé pendant
tout le test. L'amélioration rapportée au premier essai n'est pas reproduite.
Cette comparaison n'incrimine donc pas les lectures GPU comme facteur de fluidité.
Elle ne permet pas non plus d'exclure tout effet d'observation commun aux phases.

| Phase | CPU processus moyen/max, % d'un cœur | CPU principal moyen/max | GPU busy moyen/max |
|---|---:|---:|---:|
| 2, complet | 73,32 / 99,34 | 63,65 / 79,48 | 5,40 / 9 |
| 4, proc seul | 76,72 / 99,55 | 64,16 / 77,65 | non collecté |
| 6, GPU seul | non collecté | non collecté | 5,93 / 8 |
| 8, complet | 67,41 / 93,37 | 60,21 / 79,47 | 6,50 / 13 |

Charge CPU significative pendant les gestes, surtout sur le principal, GPU
globalement peu occupé. Les maxima sont sur environ 0,5 s et ne mesurent pas le
budget par frame. La somme processus atteignant un cœur ne signifie pas que le
principal est saturé : son maximum observé reste autour de 80 %.
Le journal collecté reste vide avec accès restreint. Aucune mesure des flips,
de la durée de peinture ou de la fonction responsable. Les causes envisagées
restent layout/peinture, upload/appels driver, attente de présentation et gestion
multi-sorties ; aucun classement causal ferme ne peut découler du busy seul.

## Indice reproductible rapporté après le test

Précision ultérieure : même en période fluide, le clic maintenu pour déplacer
produit encore le fantôme ; le survol des bords n'en produit alors pas, tandis
qu'il en produit en période saccadée. La présence du fantôme n'établit donc pas
un état saccadé. Comparer séparément les deux gestes dans les deux états ; ne pas
assimiler automatiquement ce défaut de curseur à la cause des performances.

Confirmation utilisateur : au clic maintenu pour saisir une fenêtre sur le
principal, le pointeur normal reste sur le principal et une croix fantôme apparaît
très brièvement sur le secondaire, au moment du clic. Le phénomène se répète.

Lecture ciblée du code local, sans modification :

- `server/src/ui/grab.c:40` : le grab de déplacement sélectionne `CUR_MOVE`.
- `server/src/main.c:1451` (`update_cursor`) : quand l'image change et que le curseur
  matériel est actif, `kms_cursor_image` est appelé successivement sur toutes les
  sorties GPU actives ; puis `hw_cursor_move` masque les sorties hors pointeur.
- `server/src/kms.c:541` (`kms_cursor_image`) : l'appel DRM installe un handle non
  nul sur le CRTC et marque le curseur visible en cas de succès. Il ne conserve
  pas l'état masqué antérieur. Il existe donc une séquence où le changement
  d'image réactive aussi le plan curseur d'un écran hors pointeur, avant masquage.
- `server/src/main.c:399` (`hw_cursor_move`) applique ensuite le masquage par sortie.
  Ces opérations sont séparées ; le code seul ne mesure pas leur visibilité réelle
  sur la machine ni leur durée.
- Autre chemin distinct : passage entre curseur matériel et curseur composé si
  la taille dépasse la capacité d'un plan. L'ancien curseur composé ne disparaît
  qu'avec le rafraîchissement du framebuffer. Aucun log ne confirme ce passage ici.

La première séquence correspond précisément à la localisation et au déclenchement
confirmés : un curseur fantôme sur l'autre écran au changement de forme du clic.
L'hypothèse d'une réactivation transitoire du plan curseur secondaire est donc
fortement étayée par le code et le témoignage concordants, sans confirmation
instrumentée des appels DRM ni mesure de leur effet sur la sortie.
Le binaire installé n'est pas encore comparé au code local et l'utilisation
effective du curseur matériel n'est pas confirmée par les journaux.
Ce constat ne prouve aucun lien causal avec les saccades générales.

## Suite proportionnée

### Complément : journal natif retrouvé après préparation du binaire

Le journal système était inaccessible, mais le fichier natif
`/var/log/lordos/users/getsugah/compositor.log` est lisible. Il confirme pour cette
session : principal 2560×1440@120 (CRTC 83), secondaire 1920×1080@120 (CRTC 79),
composition radeonsi sur RX 7800 XT, curseur matériel 64×64, pipeline désactivé.
Ces faits remplacent les incertitudes antérieures sur le mode et le curseur actifs.

Pendant la comparaison saccadée, le geste terminé à 19:59:40.349 présente un maximum
de travail de 22,40 ms et un intervalle maximal entre constructions de 125,6 ms.
Celui terminé à 20:00:17.748 a un maximum de travail de 32,03 ms et un intervalle
maximal de 91,2 ms. Ces valeurs démontrent des irrégularités dans la production
des frames, sans encore mesurer le scanout ni attribuer l'intervalle aux seuls
calculs de peinture. Les durées moyennes restent beaucoup plus faibles.

Les lignes de cette fenêtre indiquent des refus fréquents du drag GPU, notamment
`layer: invalid` et `catching up`. C'est une piste concrète à comparer à un geste
fluide identique, pas une cause prouvée. Les valeurs de FPS journalisées peuvent
dépasser 120 en multi-écran car il s'agit de constructions, pas du débit présenté
sur un écran. Ne pas les interpréter comme des images effectivement affichées.

Extraits bornés et tableau CSV conservés dans la capture existante sous
`compositor-drag-existing.log` et `compositor-drag-existing.csv`.

### Geste étiqueté « fait, saccadé » à 20:27

L'utilisateur a réalisé le geste demandé et rapporté un état saccadé.
La fenêtre de journal immédiatement précédente contient un déplacement terminé
à 20:26:38.953 : 2,326 s, travail moyen 0,18 ms, maximum 5,22 ms et intervalle
maximal de construction 9,2 ms. Drag GPU utilisé sur 235/256 tentatives (~92 %).
Les autres lignes sont des gestes très courts ; elles ne couvrent pas dix
secondes de déplacement continu. Les durées des animations de réduction et
restauration ne sont pas présentes dans ce journal.

Ce résultat ne reproduit pas les grands trous de construction du test précédent,
malgré le ressenti saccadé. Il ne permet pas de conclure que le problème a disparu
ni que le ressenti est incorrect. Les statistiques agrégées mélangent le travail
des sorties et ne mesurent ni le déplacement effectivement appliqué à chaque
frame ni la présentation par écran. Le nombre de moves n'est pas un nombre de
présentations distinctes. La mauvaise fluidité peut donc rester invisible dans
ces lignes ; les refus du drag GPU ne constituent pas une explication suffisante
de toutes les saccades observées.

Extrait conservé dans `user-check-saccade-2026-11-15-2026.log` dans la capture.
Il n'est plus justifié de multiplier les mêmes gestes en s'appuyant uniquement
sur ces statistiques : la prochaine étape discriminante est la capture interne
préparée, avec flips par sortie et traitement des événements. Elle requiert une
session manuelle utilisant le binaire instrumenté ; le processus courant ne
peut pas produire ces nouvelles mesures sans changement de session.

Avant de basculer de session pour l'instrumentation, comparer les statistiques
natives de deux gestes identiques, l'un fluide et l'autre saccadé, en notant leur
heure et la même fenêtre. Aucune nouvelle session nécessaire pour cette étape.

La localisation des deux copies est maintenant établie. Récupérer les lignes existantes de démarrage
GPU/KMS et `drag:` de la session réelle, avec un accès autorisé, pour confirmer le
chemin actif et la fréquence. Profiler le principal si les journaux ne suffisent pas.
Une instrumentation interne ou recompilation nécessite l'accord préalable.
Pas de correction, réglage, redémarrage ou capture supplémentaire réalisé ici.
