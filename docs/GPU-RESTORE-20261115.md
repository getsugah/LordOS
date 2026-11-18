# Rétablissement de la compilation GPU

Le 15 novembre 2026, le journal de la session ouverte à 21:36 indique
`monitor: the session is shown (2 display(s), 4480x1440, CPU)`.
La session précédente utilisait radeonsi sur RX 7800 XT. Les deux écrans
sont volontairement réglés à 60 Hz ; ces réglages ne sont pas modifiés.

Le binaire installé était identique à celui du paquet local
`0.8.8.8+mousebuttons2.appimage1.fullscreen1` et au fichier
`server/build/lord-server` (SHA-256
`a079c2881098713eccaba496cf715824ebbbe2857a8cf47ea2cea6e51eac8f1d`).
Il ne dépendait ni de GBM, ni d'EGL, ni de GLES. La détection locale
`pkg-config --exists gbm egl glesv2` échoue : le Makefile choisit alors
automatiquement GPU=0. Cela explique la compilation CPU ; le lien causal
avec les ralentissements de Firefox reste à confirmer dans une session GPU.

## Reconstruction et installation

Sauvegarde du binaire CPU :
`server/build/gpu-restore/lord-server.cpu-backup`.
Reconstruction du seul objet conditionné par LORD_GPU dans le compositeur,
puis nouvelle édition de liens avec les objets existants :

```sh
env \
  PKG_CONFIG_PATH=/home/getsugah/LordOS/server/build/mouse-package-sdk/root/usr/lib/x86_64-linux-gnu/pkgconfig \
  PKG_CONFIG_SYSROOT_DIR=/home/getsugah/LordOS/server/build/mouse-package-sdk/root \
  make -C server build/lord-server GPU=1 SESSIOND=0 -W src/gpu/gpu.c -j4
```

`-W src/gpu/gpu.c` force la reconstruction de l'objet, car Make ne détecte
pas à lui seul un changement de GPU=0 à GPU=1. Pour les prochains paquets
GPU, conserver explicitement GPU=1 et un SDK accessible, puis vérifier
les dépendances du binaire produit avant de l'empaqueter.

Le paquet corrigé est une copie du paquet fullscreen1 : seuls le binaire,
la version et son entrée md5sums diffèrent. Aucun changement de sources
supplémentaire ni d'instrumentation. Paquet installé avec succès :
`server/build/gpu-restore/lordos_0.8.8.8+mousebuttons2.appimage1.fullscreen1.gpu1_amd64.deb`.

Vérifications : compilation réussie sans avertissement ; GBM/EGL/GLES présents
dans les dépendances ELF ; `--check-code /usr/lib/lordos` valide les dix
fichiers de code (manifeste de développement non signé) ; SHA-256 installé
identique au binaire reconstruit :
`c0bab02e008e7f41c8f19ee68518e5510af46f0f0a0ee55e10c20035d2aba050`.
État dpkg : `install ok installed`.

## Validation restante

La session courante n'a pas été fermée. Le nouveau binaire sera utilisé à
la prochaine ouverture de session. Vérifier alors le démarrage du rendu GPU
dans `compositor.log`, puis comparer le même geste avec Firefox fermé et
ouvert. La restauration du support GPU dans le paquet est vérifiée ; son
activation effective et la disparition des ralentissements restent à tester.
