# Contrôle des sources du compositeur — 15 novembre 2026

Référence Git : `51941841`, avec les modifications locales présentes au moment du
contrôle, notamment celles de `server/src/wl/wl.c`. Aucune source modifiée, aucun
service relancé et aucun accès aux écrans par les essais de cette revue.

Revue ciblée du cœur Wayland (`wl.c`), de ses chemins de buffers, sous-surfaces,
curseurs et rendu, avec lecture des appels dans `main.c` et `kms.c`. Ce contrôle
n'est pas un audit exhaustif des 11 200 lignes de `server/src/wl`, du moteur GPU
ou des pilotes. Les défauts déduits des sources sont distingués des essais exécutés.

## 1. P1 — Buffer linéaire insuffisamment validé : crash du compositeur

Emplacements : `server/src/wl/wl.c:518–543`, `4489–4497` et `4517–4543`.

`dmabuf_usable()` accepte un buffer ARGB/XRGB linéaire sur la seule description de
son format lorsque l'import GPU ne réussit pas. Il ne vérifie pas que le descripteur
correspond à un DMA-BUF ni que sa taille couvre les offsets et lignes demandés.
`map_linear()` effectue ensuite un `mmap`, ignore l'échec de `DMA_BUF_IOCTL_SYNC`,
puis lit les pixels. Un mapping peut réussir alors que la lecture provoque SIGBUS.

**Essai exécuté :** un petit programme inclut directement le `wl.c` local, appelle
`dm_linear()` et `map_linear()` sur un memfd vide décrit comme un buffer 16 × 16,
stride 64. Le premier accepte le format ; le second tue le processus enfant par
SIGBUS (signal 7). Aucun compositeur lancé. Ce test porte sur les fonctions réelles,
pas sur un client Wayland complet. Le chemin d'acceptation protocolaire a été suivi
par lecture du code.

Correction recommandée : valider les plans, strides et plages accessibles avant
l'acceptation ; utiliser une arithmétique vérifiée ; refuser les descripteurs non
appropriés et traiter les erreurs de synchronisation. La taille seule ne suffit
pas pour un fichier ordinaire qui peut être tronqué après validation. Prévoir un
test de client invalide et une politique robuste pour les mappings CPU.

## 2. P1 — Un cycle de sous-surfaces peut bloquer le processus

Emplacements : `server/src/wl/wl.c:1053–1066`, `2800–2802`, `339–342`.

`subc_get()` refuse `s == p`, mais ne parcourt pas les ancêtres. Avec deux surfaces
neuves A et B, les appels « A enfant de B », puis « B enfant de A » passent tous
les contrôles. Un commit de A entre ensuite dans `while (top->parent)` sans sortie.
D'autres chemins parcourent ou dessinent cet arbre récursivement.

Constat statique : scénario suivi dans les fonctions, sans déclencher le blocage
sur une session. Rejeter un parent dont la chaîne d'ancêtres contient l'enfant
avant toute mutation. Ajouter un test protocolaire avec timeout.

## 3. P2 — Changer l'image réactive les curseurs des écrans secondaires

Emplacements : `server/src/main.c:1482–1486`, `server/src/kms.c:569–574`,
`server/src/main.c:398–410`.

`update_cursor()` appelle `kms_cursor_image()` sur chaque sortie. Cette fonction
soumet le handle du curseur au CRTC et marque le curseur visible, même si cette
sortie avait été masquée. Ce n'est qu'après ces appels que `hw_cursor_move()`
masque de nouveau les sorties qui ne portent pas le pointeur.

La séquence crée une fenêtre temporelle de réactivation à l'ancienne position.
Elle fournit une piste concrète pour le curseur fantôme sur le secondaire lors
d'un changement de forme. **L'artefact optique n'a pas été reproduit dans cette
revue**, et ce constat ne prouve pas la cause des saccades générales.

Préserver la visibilité lors d'un changement d'image ; éviter de rendre un plan
visible pour simplement actualiser ses pixels. Tester les changements de forme
sur deux sorties et enregistrer la séquence d'ioctls.

## 4. P2 — L'état logiciel du curseur peut diverger après une erreur DRM

Emplacements : `server/src/kms.c:581–588`, `server/src/main.c:1482–1486`.

`kms_cursor_show()` ignore le résultat de l'ioctl et affecte systématiquement
`cursor_visible`. Si le masquage échoue, les appels suivants avec la même demande
sortent immédiatement : le logiciel croit le curseur masqué et ne réessaie pas.
Les erreurs de `kms_cursor_image()` sont aussi ignorées lors des mises à jour,
alors qu'elles sont traitées à l'initialisation GPU.

Ne mettre à jour l'état connu qu'après succès ; propager l'échec et prévoir une
nouvelle tentative ou un repli logiciel cohérent par sortie. Vérifier ce chemin
par injection d'échec d'ioctl, sans nécessiter de pilote réellement défaillant.

## 5. P2 — Synchronisation et empilement des sous-surfaces non implémentés

Emplacements : `server/src/wl/wl.c:1032–1036`, `649–695`, `2970`.

`set_sync`, `set_desync`, `place_above` et `place_below` sont des fonctions vides.
Les commits enfants sont appliqués immédiatement. La position est appliquée sur
le commit enfant, alors qu'elle doit être synchronisée avec le commit du parent.
L'affichage suit l'ordre du tableau `kids`, et la suppression remplace un élément
par le dernier, ce qui peut aussi réordonner les enfants restants.

Conséquences possibles avec des clients qui utilisent ces fonctionnalités :
redimensionnements désynchronisés, contenu enfant visible trop tôt, ordre de
superposition incorrect. Implémenter l'état mis en cache et son application au
commit parent, ainsi qu'un ordre explicite des enfants. Tester les séquences de
requêtes réelles plutôt qu'un modèle qui reproduirait les choix actuels.

## 6. P2 — Les transformations de buffer sont ignorées

Emplacements : `server/src/wl/wl.c:699`, `714–716`.

`set_buffer_transform` est exposé mais sa fonction ne fait rien. Un client qui
envoie un buffer tourné ou réfléchi obtient un rendu et des dimensions logiques
incorrects. Conserver la transformation dans l'état pending/current et l'appliquer
aux dimensions, à l'échantillonnage et aux conversions de coordonnées pertinentes.
Valider aussi la valeur reçue.

## Améliorations de performance et de structure

- **Exploiter le damage client.** `srf_damage()` est vide (`wl.c:625`) et
  `copy_buffer()` copie toutes les lignes à chaque attachement (`465–478`).
  Une surface SHM 3840 × 2160 représente environ 31,6 MiB copiés par commit,
  soit environ 1,85 GiB/s à 60 commits/s, avant les autres traitements. Ce calcul
  est une estimation, pas une mesure de la session. Accumuler les rectangles,
  convertir damage surface/buffer et recopier les seules zones utiles lorsque
  l'historique et les dimensions le permettent ; copie complète sinon.
- **Rendre explicites les limites.** `kids[16]` n'enregistre pas le 17e enfant,
  tout en acceptant son objet et son parent (`1066`). Il peut donc être accepté
  mais absent du rendu. Préférer une collection dimensionnée ou une erreur
  explicite. Examiner de même les capacités globales surfaces/popups/draws.
- **Compléter la validation des rôles.** `subc_get()` ne refuse pas un toplevel
  ou un popup existant et écrase son rôle. Centraliser l'attribution des rôles
  pour éviter des états incompatibles et tester leur cycle de vie.
- **Découper `wl.c`.** Ses 5 538 lignes mêlent protocoles, buffers, presse-papiers,
  DND et rendu. Extraire progressivement les chemins déjà identifiables en gardant
  des tests sur leurs interfaces. Priorité aux défauts fonctionnels ci-dessus
  avant une réorganisation large.

## Vérifications exécutées

### Analyse statique automatique

GCC 14.2.0, `-O0 -Wall -Wextra -fanalyzer`, compilation en objets isolés dans
`/tmp/lordos-compositor-review-20261115/static/`. Les 24 unités suivantes ont
toutes terminé avec un code retour 0 : les 15 fichiers `.c` de `src/wl`,
`main.c`, `kms.c`, `gpu/gpu.c`, `gpu/gscissor.c`, `ui/paint.c`, `ui/grab.c`,
`ui/transform.c`, `outputs.c`, `fpipe.c`.

Pour `gpu/gpu.c`, le vrai chemin GPU a été analysé avec `-DLORD_GPU` et les
headers du SDK local `build/mouse-package-sdk/root/usr/include` ; il ne s'agit
pas seulement du stub sans GPU. Aucun binaire de production n'a été reconstruit.

Deux alertes seulement, toutes deux `-Wanalyzer-fd-leak`, sur les `dup2` de
`xwayland.c:309`. Après examen, elles ne constituent pas une fuite confirmée :
les descripteurs 3, 4 et 5 sont volontairement transmis à Xwayland par `execve` ;
les chemins d'échec du processus enfant terminent par `_exit`. Les avertissements
ont été conservés dans `src_wl_xwayland.c.log`, sans les compter comme bugs.

L'absence d'autres alertes ne réfute pas les défauts relevés manuellement : GCC
ne modélise pas ici les cycles de requêtes Wayland, l'étendue réelle d'un mapping
de fichier ni la visibilité optique d'un plan DRM. Pas de conclusion d'absence
de race entre threads ou d'absence de fuite globale à partir de cette passe.

Commande type depuis `server/` (adapter le fichier source et le nom de sortie) :

```sh
cc -D_GNU_SOURCE -O0 -Wall -Wextra -fanalyzer \
  -Isrc -Ibuild/gen -isystem ../third_party/quickjs \
  -isystem ../third_party/stb -c src/wl/wl.c \
  -o /tmp/lordos-compositor-review-20261115/static/src_wl_wl.c.o
```

### Tests ciblés et reproducer isolé

Huit tests recompilés depuis les sources locales dans un répertoire temporaire,
avec `-Wall -Wextra -fsanitize=undefined`, puis exécutés avec succès : `wlregion`,
`framegate`, `ptrlock`, `xwm` (tests du module x11wire), `xwscale`, `dndgate`,
`outputs`, `fpipe`. Aucun diagnostic UBSan observé.

Également exécutés avec succès, à partir des binaires déjà présents : `keygrab`,
`kbinhibit`, `xdgact`, `urilist`, `xauth`, `toolkitenv`, `textinput`,
`wl-mousebuttons`, `cursor`. Ces résultats ne garantissent pas leur correspondance
avec toutes les sources locales : ces neuf binaires n'ont pas été recompilés.

Les essais sockets de `kbinhibit` et `wl-mousebuttons`, initialement empêchés par
le sandbox, réussissent hors sandbox. `xauth` doit être exécuté depuis `server`
car il inspecte des chemins relatifs. LeakSanitizer, incompatible avec le traçage
de cet environnement, a été désactivé pour les essais concernés ; ce n'est donc
pas une validation d'absence de fuite.

Le reproducer SIGBUS et les huit exécutables recompilés se trouvent dans
`/tmp/lordos-compositor-review-20261115/` (artefacts temporaires). Reproduction :

```sh
cc -D_GNU_SOURCE -O1 -ffunction-sections -fdata-sections \
  -Iserver/src -Iserver/build/gen -isystem third_party/quickjs \
  -isystem third_party/stb \
  /tmp/lordos-compositor-review-20261115/map-probe.c \
  -Wl,--gc-sections -o /tmp/lordos-compositor-review-20261115/map-probe
/tmp/lordos-compositor-review-20261115/map-probe
```

Exécuter depuis la racine du dépôt. Le programme désactive les core dumps et
isole la lecture fautive dans un enfant. Aucune utilisation de DRM ou de la
session graphique.

Ordre conseillé : validation des buffers et rejet des cycles ; visibilité et
erreurs du curseur ; conformité des sous-surfaces ; transformations et damage.
