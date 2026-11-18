# Paquets de correction du compositeur — 16 novembre 2026

Livraison locale :
`/home/getsugah/LordOS/server/build/DEB-compositeur-20261115/`.

Le paquet à installer est :
`lordos_0.8.8.8+mousebuttons2.appimage1.fullscreen2.gpu1.compositorfix1_amd64.deb`.

Le paquet de retour arrière, copie exacte du paquet correspondant au binaire
installé avant cette intervention, est :
`lordos_0.8.8.8+mousebuttons2.appimage1.fullscreen2.gpu1_amd64.deb`.

## Installation

Enregistrer son travail. Depuis un terminal :

```sh
cd /home/getsugah/LordOS/server/build/DEB-compositeur-20261115
sha256sum -c SHA256SUMS
sudo dpkg -i ./lordos_0.8.8.8+mousebuttons2.appimage1.fullscreen2.gpu1.compositorfix1_amd64.deb
```

Après une installation réussie, fermer puis rouvrir la session LordOS, ou
redémarrer normalement le PC. Le processus de la session déjà ouverte continue
d'utiliser l'ancien binaire jusqu'à son arrêt. Aucun redémarrage forcé de la
session n'est inclus dans les corrections.

Le paquet reprend les dépendances du paquet actuellement installé. Les scripts
de maintenance sont identiques : ils peuvent notamment recharger systemd et
redémarrer le service de ventilation, comme les installations précédentes.

Vérifier ensuite la version installée :

```sh
dpkg-query -W -f='${Version}\n' lordos
```

Le résultat doit se terminer par `.gpu1.compositorfix1`.

## Retour arrière

Si le comportement se dégrade, réinstaller le paquet de référence :

```sh
cd /home/getsugah/LordOS/server/build/DEB-compositeur-20261115
sudo dpkg -i ./lordos_0.8.8.8+mousebuttons2.appimage1.fullscreen2.gpu1_amd64.deb
```

Fermer puis rouvrir la session, ou redémarrer normalement. L'avertissement de
dpkg indiquant une rétrogradation de version est attendu pour ce retour arrière.

## Corrections incluses

- Les changements d'image du curseur conservent la visibilité de chaque écran.
  Un curseur masqué sur le secondaire n'est plus réactivé par la mise à jour.
- Un échec de masquage DRM ne corrompt plus l'état connu du curseur : une demande
  suivante réessaie. Un échec de mise à jour ou d'affichage entraîne un repli vers
  le curseur logiciel ; les mouvements suivants réessaient les masquages restants.
- Les buffers linéaires du chemin CPU sont contrôlés avant lecture : dimensions,
  stride, dernière adresse accédée, taille d'allocation et synchronisation
  DMA-BUF. Un fichier ordinaire ou un memfd ne peut plus être accepté comme
  DMA-BUF lisible simplement parce que son format annoncé est ARGB/XRGB.
- Les cycles de sous-surfaces et les attributions de rôles incompatibles sont
  rejetés avant mutation. La limite de 16 enfants provoque un refus explicite
  plutôt que l'acceptation silencieuse d'un enfant absent du rendu.

Les correctifs souris, AppImage et plein écran du paquet précédent sont conservés.
Le rendu GPU est compilé explicitement et lié à GBM, EGL et GLES. Aucun réglage
d'écran, de fréquence, de priorité CPU/GPU ni de cadence de présentation n'a changé.

## Rapport avec les saccades

La correction du curseur supprime une séquence précise susceptible de faire
réapparaître brièvement le pointeur sur le secondaire. Elle ne démontre pas la
cause des saccades générales, notamment pendant réduction ou fermeture.

Ce paquet ne contient pas d'optimisation spéculative des copies SHM, de nouvelle
gestion du damage, ni de réécriture des sous-surfaces synchronisées. Ces sujets
restent à traiter séparément. La disparition des saccades n'est pas garantie.

## Vérifications réalisées

- Compilation GPU de production réussie, sans avertissement, dans une copie
  isolée des sources. Build ID : `20261115.51941841.compositorfix1`.
- Analyse statique GCC `-fanalyzer` des trois unités modifiées (`wl.c`, `kms.c`,
  `main.c`) : aucune alerte sur les versions finales.
- Trois nouveaux tests réussis avec ASan/UBSan : buffers et filiation des
  surfaces ; curseur KMS avec erreurs injectées ; politique réelle du curseur
  de `main.c`, extraite pour l'essai et compilée avec des sorties simulées.
- Le test du curseur échoue avec le code antérieur à la correction : il détecte
  effectivement le défaut. Le crash SIGBUS avait été reproduit en enfant isolé
  avant correction ; le nouveau test rejette le même type de fichier invalide.
- Dix-sept tests existants recompilés avec ASan/UBSan puis réussis : framegate,
  wlregion, ptrlock, xwm/x11wire, xwscale, dndgate, keygrab, xdgact, urilist,
  xauth, toolkitenv, textinput, outputs, cursor, fpipe, wl-mousebuttons et kbinhibit.
- Extraction et comparaison du paquet : 1 059 entrées comparées ; seul le
  contenu de `usr/lib/lordos/bin/lord-server` change dans les données installées.
  Version, taille déclarée et empreinte MD5 du binaire sont mises à jour.
- Les 935 entrées de `md5sums` sont valides ; scripts de maintenance et conffiles
  identiques au paquet de référence ; toutes les bibliothèques ELF sont trouvées.
- Les dix fichiers du manifeste de code du paquet sont validés par le nouveau
  binaire en mode `--check-code`, sans initialiser de session graphique.
- Le patch fourni passe `git apply --check` sur l'état local conservé.

LeakSanitizer a été désactivé, car incompatible avec le traçage de cet
environnement. La vérification globale des variables ASan est désactivée pour
les tests incluant `wl.c`, afin de permettre l'élimination des parties non liées
au test ; les vérifications des accès aux allocations restent actives.

Les ioctls DMA-BUF valides et DRM sont simulés dans les nouveaux tests. Aucun
essai visuel sur la RX 7800 XT ni essai d'installation en VM n'a été réalisé pour
ce paquet. Le comportement avec les pilotes et les deux écrans reste donc à
vérifier après installation. Les tests automatisés ne remplacent pas cet essai.

## Sources et traces

La livraison contient le patch `compositorfix1.patch`, les journaux de compilation
et de tests, `package-validation.log`, `manifest.json`, `code-check.txt`,
`ldd.txt` et `SHA256SUMS` pour les deux `.deb`.

Les sources originales du dépôt n'ont pas été modifiées par ces corrections.
Le snapshot complet corrigé se trouve dans :
`/home/getsugah/LordOS/server/build/compositor-fixes-20261115/`.
Le patch est relatif aux sources locales initiales, incluant les changements
préexistants de plein écran. Il n'a pas été appliqué automatiquement au dépôt.

Pour rejouer les nouveaux tests depuis ce snapshot :

```sh
make -C server check-compositor-fixes GPU=0 SESSIOND=0
python3 server/tests/main_cursor_policy_test.py
```

`GPU=0` dans cette commande concerne seulement la compilation des tests isolés ;
le binaire livré est bien compilé avec `GPU=1` et le SDK local.

Les paquets ont été préparés et contrôlés ; aucun n'a été installé sur le PC.
