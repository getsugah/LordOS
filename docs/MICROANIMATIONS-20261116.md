# Micro-animations — lot shell du 16 novembre 2026

## Audit avant implémentation

- Le SDK React est rendu par le reconciler de `runtime/index.js`, puis par
  `server/src/ui/{node,layout,paint,transform}.c`. Pas de DOM ni de moteur CSS de transitions.
- `lord.setStyle` expose opacité, translation, échelle et rotation. Les transformations
  disposent de rasters en cache ; cela ne garantit pas un coût nul pour l'opacité.
  `node_restyled` peut appeler `node_invalidate`, et `paint_ui` conserve son passage
  de layout natif. Aucun changement de ces chemins dans ce lot.
- `server/src/ui/js_prelude.h` fournit déjà requestAnimationFrame et une horloge
  monotone (`performance.now` / `lord.now`). `main.c:paint_ui` exécute les callbacks
  pendant le rendu ; chaque sortie conserve son flip, son échelle et son rythme.
  Les callbacks JS restent partagés : pas de promesse de cadence indépendante par écran.
- Animations existantes : minimisation vers le Dock dans Lord Flow (260 ms),
  Dock, overview, bureaux (170 ms, ancien timer de 16 ms), indicateurs et fonds vidéo.
- Apparence propose déjà « Réduire les animations ». Ce lot réutilise cette préférence,
  sans seconde préférence concurrente. Elle supprime les nouvelles transitions et
  arrête celles en cours ; les autres animations historiques ne sont pas toutes auditées ici.
- Le chemin Wayland transforme aussi les quads des surfaces. Une fermeture animée
  nécessiterait de conserver un contenu après sa destruction ; une maximisation animée
  doit éviter les configure intermédiaires. Aucune modification de ce cycle de vie ici.

## Changements et journal des fichiers

- `sdk/microanimation.js` : interpolation cubique courte, temps monotone, callbacks
  finis via requestAnimationFrame, annulation explicite, aucune boucle au repos.
- `sdk/microanimation-hooks.js` : propriété native possédée par le hook ; nettoyage
  au démontage, remplacement depuis la valeur affichée, désactivation immédiate.
- `sdk/contextmenu.jsx` : fondu d'ouverture 100 ms des menus et sous-menus.
- `sdk/controls.jsx` : fondu d'ouverture 100 ms de MenuButton et Dropdown.
- `sdk/components.jsx` : translation du curseur Toggle en 120 ms. État et action
  immédiats ; fond et contour restent immédiats.
- `sdk/notify.js` : fondu d'ouverture 140 ms ; lecture directe de reduceMotion car
  les notifications sont montées hors du provider du thème.
- `sdk/desktops.js` : remplacement du timer 16 ms par requestAnimationFrame et
  horloge monotone ; arrêt immédiat quand reduceMotion est activé. Les modifications
  locales préexistantes de ce fichier sont conservées.
- `server/tests/microanimation_test.mjs` : cadence simulée, fin, interruption,
  annulation et retard prolongé ; ajouté à `server/tests/run_suites.sh`.
- `server/tests/{focus_scene_test,menu_keys_test}.mjs` : horloge RAF dans les mocks Node.
- Ce document : audit, journal et validation.

Les correctifs préexistants dans Disques, Fichiers, les entrées, le modèle des
bureaux, les fenêtres et le serveur C n'ont pas été modifiés par ce lot.
La différence initiale est sauvegardée dans `/tmp/lordos-microanimations/before.patch`
(sauvegarde temporaire des différences suivies uniquement).

## Validation effectuée

- Compilation default, classic, app-host et six applications isolées : succès.
  Temps de compilation observés : 99 / 80 / 31 ms et 4–17 ms par application.
  Ces temps ne mesurent pas les performances du bureau.
- microanimation : 60/120/144 Hz simulés, bornes et progression monotone,
  dernière valeur exacte, aucune frame restante à la fin, inversion depuis la
  valeur affichée, fin après une longue pause et aucune écriture après annulation : succès.
- desktop-model : 137 vérifications ; mouse-navigation : OK ; minimize-model : 8 succès.
- files-remote (inclut mountmodel) : 33/33 ; focus-model : 108 succès.
- menu-keys : 31 vérifications ; focus-scene : 41 ; aucun échec.
  Ces deux tests lancent Node et ont nécessité une exécution autorisée hors sandbox.
- Le nom `mount_model_test.mjs` essayé initialement n'existe pas ; les tests
  pertinents se trouvent dans `files_remote_test.mjs`.

## Limites et validation physique restant à faire

Ces lots sont une implémentation progressive du shell, pas une validation complète de
tous les effets demandés. Fermeture animée uniquement pour MenuButton et Dropdown,
pas d'animation d'ouverture des fenêtres, restauration, maximisation ou transition de vue. Les panneaux cités
ci-dessous ont un fondu d'ouverture. Minimisation existante inchangée. Aucun changement du focus, des
entrées, de la géométrie des fenêtres ou du compositeur C.

Aucune mesure de présentation, d'image manquée ou de charge CPU/GPU du nouveau
code en session réelle. Aucun résultat matériel revendiqué sur le 7800X3D / 7800 XT
à deux écrans. Les tests de modèles et scènes simulées ne remplacent pas ces essais.

Avant déploiement : comparer deux séquences identiques, préférence reduceMotion
désactivée puis activée : repos, menus rapides, notifications, interrupteurs,
ouvertures/fermetures natives et Wayland/XWayland, déplacements entre écrans,
maximisation/restauration/plein écran et interaction sur le second écran.
Vérifier les clics retour, le focus clavier et montage/démontage via Disques.
Utiliser le collecteur existant `tools/diag/compositor-capture.py` pour les compteurs
disponibles ; consigner séparément cadence de présentation, images manquées,
CPU/GPU et disponibilité des compteurs. Ne pas assimiler fréquence de callbacks
JS et fréquence réellement présentée. Retirer les fondus si les mesures révèlent
un surcoût ou des saccades inacceptables.

## Construction

```sh
node tools/build-theme.mjs default classic app-host sysapps
node server/tests/microanimation_test.mjs
node server/tests/menu_keys_test.mjs
node server/tests/focus_scene_test.mjs
```

Les bundles générés sont des artefacts de développement ; aucun paquet système
n'a été installé, aucune session redémarrée et aucun binaire C reconstruit.
La fabrication et la signature d'un paquet installable restent à effectuer après
validation physique, selon le processus habituel du dépôt.

## Deuxième lot : panneaux et sortie des notifications

Audit complémentaire : `wl_tf_begin` et le chemin des rasters de `paint.c` gèrent
ensemble le cadre, les trous et les quads Wayland. Des transformations imbriquées
ont des limitations explicitement documentées dans `wl.c`. Ce lot ne généralise
donc pas les animations aux fenêtres ; une validation Wayland et plein écran
reste nécessaire avant cette étape.

Changements supplémentaires :

- `themes/default/components/ControlCenter.jsx` : ouverture en fondu de 120 ms.
- `themes/default/components/NotificationCenter.jsx` et
  `themes/classic/components/NotificationCenter.jsx` : même ouverture en 120 ms.
- `themes/classic/components/StartMenu.jsx` : même ouverture en 120 ms.
- `sdk/microanimation-hooks.js` : `usePanelEntrance` lit la préférence directement,
  y compris pour les panneaux hors du provider du thème. `useLeavingItems` conserve
  brièvement les données nécessaires au rendu des notifications retirées.
- `sdk/microanimation.js` : calcul borné des notifications sortantes. Un nouvel
  état ne prolonge pas leur délai ; une notification réapparue reprend la même clé.
- `sdk/notify.js` : disparition en 80 ms. Suppression logique et callback immédiats ;
  bandeau sortant avec `pointerEvents: none`. Au plus quatre bandeaux sortants,
  aucune frame au repos, retrait immédiat si reduceMotion est activé.
- Aucun déplacement ajouté aux panneaux ou notifications : leurs zones d'entrée
  restent à leur place. Aucun nouveau flou ni effet d'échelle.
- `server/tests/microanimation_test.mjs` : vérification des délais de sortie,
  des retours pendant une disparition, de la limite de données et de reduceMotion.
- `server/tests/microanimation_scene.mjs` : scène React avec l'horloge de frames
  contrôlée et de vrais composants de notification ; intégrée à `run_suites.sh`.
- `server/tests/focus_scene_test.mjs` : mock des services natifs absents complété
  pour exécuter la notification réelle dans la scène. `classic_startmenu_power_test.mjs`
  reçoit également les callbacks RAF du runtime simulé.

Validation du deuxième lot :

- Scène animations : **13 vérifications, aucun échec** : état initial avant rendu,
  progression native, préférence modifiée en cours d'effet, démontage et annulation,
  inversion rapide, callback de suppression immédiat, bandeau non cliquable,
  retrait final et absence de frames restantes, suppression immédiate des sorties
  quand la réduction des animations est activée.
- Menus au clavier : **31 vérifications**, focus : **41**, actions du menu Démarrer
  Classic : **9**, fichiers distants : **33**, clics retour : **OK**.
- Test d'interpolation et de conservation bornée : succès.
- Nouvelle compilation default, classic, app-host et applications isolées : succès.
- `git diff --check` : succès.

Commandes complémentaires :

```sh
node server/tests/focus_scene_test.mjs microanimation_scene.mjs
node server/tests/classic_startmenu_power_test.mjs
```

Pas d'installation ni de redémarrage de la session. Les mesures CPU/GPU et de
présentation sur les deux moniteurs restent à réaliser sur ce code. Le modèle
de montage testé ne prouve pas le montage réel d'un disque physique.

## Troisième lot : boutons et coût des fenêtres

Le fondu expérimental de 150 ms des fenêtres natives a été implémenté et testé
dans une scène React (interruption, géométrie, restauration et exclusion Wayland),
puis **retiré** à la suite des mesures de rendu CPU. `sdk/windowshell.jsx` est
inchangé par rapport au début de ce travail. Aucun fondu de fenêtre n'est livré.

`server/tests/engine_test.c` propose maintenant le benchmark optionnel
`BENCH_WINDOW_FADE=1` sur la scène existante de 2 000 lignes. Le second cas utilise
une fenêtre à cheval sur deux sorties simulées (échelles 1 et 1,5). Il compare
des appels de rendu avec opacité fixe à des changements d'opacité. Le témoin
« steady » est un passage de rendu avec opacité fixe, **pas la consommation réelle
du bureau inactif**. La première version du binaire affichait « idle » pour ce témoin ;
le libellé source a été corrigé. Ce test ne pilote pas les sorties KMS ni le GPU.

Dernière mesure CPU, 41 échantillons par cas, après la suite du moteur :

| Sorties simulées | Opacité | Médiane (ms) | p95 (ms) | Maximum (ms) |
| --- | --- | ---: | ---: | ---: |
| 1 | fixe | 2,725 | 2,994 | 3,164 |
| 1 | animée | 3,717 | 3,856 | 3,980 |
| 2 | fixe | 4,045 | 4,121 | 4,173 |
| 2 | animée | 5,818 | 6,031 | 6,128 |

Un passage précédent avait atteint un p95 de 7,316 ms et un maximum de 9,179 ms
sur deux sorties ; ces différences illustrent la variabilité de l'environnement
d'exécution. Aucun résultat n'est attribué au Ryzen/Radeon physique de référence.
Le surcoût de médiane d'environ 44 % dans le cas à deux sorties et la marge réduite
aux hautes fréquences ont motivé le retrait préventif du fondu de grandes fenêtres.
Ce benchmark ne mesure ni une animation finale livrée ni les petits contrôles.

Changements conservés :

- `sdk/components.jsx` : transition du survol des boutons en 90 ms ; opacité de
  l'appui appliquée immédiatement, retour au survol en 90 ms. Le callback de clic
  reste immédiat ; les événements continuent à se propager. Une opacité explicite
  dans les styles fournis par l'appelant garde la priorité. Désactivation du bouton
  sans transition.
- `sdk/microanimation-hooks.js` : prise en compte directe de reduceMotion dans
  tous les contrôles animés, y compris hors du fournisseur de thème. Les applications
  isolées continuent à recevoir cette préférence par les tokens du thème.
- `server/tests/microanimation_scene.mjs` : **20 vérifications réussies** sur le lot
  final : les 13 précédentes plus survol, appui immédiat, exécution du clic avant
  la fin du fondu, réduction des animations hors provider, état désactivé et
  priorité des styles de l'appelant.
- `server/tests/engine_test.c` : benchmark conservé pour guider les essais suivants.

Validation finale : moteur **8 915 vérifications sans échec**, menus clavier **31**,
focus **41**, bureaux **137**, fichiers distants **33**, clics retour **OK**,
test d'interpolation réussi, compilation des deux thèmes, hôte et applications
isolées réussie ; `git diff --check` réussi. Les essais de géométrie spécifiques
au prototype retiré ne constituent pas une validation physique Wayland.

```sh
make -C server build/engine-test GPU=0
(cd server && BENCH_WINDOW_FADE=1 build/engine-test bench)
node tools/build-theme.mjs default classic app-host sysapps
node server/tests/focus_scene_test.mjs microanimation_scene.mjs
```

Les fonctions de montage réel des disques, la présentation physique, les images
manquées et la charge GPU restent à vérifier en session. Aucun paquet installé.

## Quatrième lot : fermeture des menus et minimisation existante

Audit : MenuButton et Dropdown conservent leur état logique dans le composant
déclencheur. Leurs écouteurs clavier et de clic extérieur dépendent de `open`.
Leur surface peinte peut donc être conservée brièvement sans conserver ces écouteurs.
ContextMenu prend au contraire le focus texte pour toute sa durée de montage :
il garde une fermeture immédiate dans ce lot. Aucun changement de routage ni de
prise/restauration du focus pour réaliser les animations.

Changements :

- `sdk/microanimation-hooks.js` : `useMenuPresence` conserve le rendu 80 ms après
  la fermeture logique ; annulation du délai à la réouverture, au démontage ou
  avec reduceMotion. Aucune boucle permanente.
- `sdk/controls.jsx` : MenuButton et Dropdown passent leur opacité de la valeur
  affichée à zéro en 80 ms. Leurs surfaces passent immédiatement à
  `pointerEvents: none` et leurs écouteurs existants suivent toujours `open`.
  Leurs actions restent immédiates. Une réouverture pendant le fondu conserve
  le même nœud et inverse proprement l'animation.
- `themes/default/minimize-hook.js` : extraction du hook existant de
  `themes/default/index.jsx` pour le tester sans démarrer le shell. Même effet
  visuel et architecture ; durée 180 ms au lieu de 260 ms, temps monotone,
  annulation quand la fenêtre est restaurée ou fermée, devient plein écran/PiP,
  ou quand reduceMotion est activé. Aucun nouvel effet sur les surfaces Wayland.
- `themes/default/index.jsx` : importe ce hook au lieu de le définir en ligne.
- `themes/default/minimize.js` : supprime du suivi les fenêtres fermées pour
  éviter l'accumulation d'identifiants.
- `server/tests/microanimation_scene.mjs` : ajout des tests de menus réels et de
  minimisation ; **33 vérifications réussies** sur l'ensemble de cette scène.
- `server/tests/minimize_model_test.mjs` : **9 vérifications réussies**, dont le
  nettoyage du suivi des fenêtres fermées.
- `server/tests/focus_scene.mjs` et `server/tests/list_scene.mjs` : les recherches
  d'éléments interactifs excluent les ancêtres avec pointerEvents none, pour ne
  pas confondre fermeture logique et disparition du rendu. Les vérifications des
  anneaux de focus restent indépendantes de cette recherche.

Validation : tests de réouverture rapide avec même nœud/opacité, action de menu et
choix immédiats, retrait final des nœuds, désactivation immédiate, minimisation à
mi-parcours selon l'horloge monotone, annulation à la restauration/fermeture,
fin à 180 ms et aucune frame restante au repos : succès.

Autres tests : focus **41**, listes/choix **44**, menus clavier **31**, actions du
menu Démarrer **9**, bureaux **137**, fichiers distants **33**, clics retour **OK**,
interpolation et données sortantes **OK**. Compilation default/classic/app-host
et applications isolées réussie, `git diff --check` sans erreur.

```sh
node server/tests/focus_scene_test.mjs microanimation_scene.mjs
node server/tests/focus_scene_test.mjs list_scene.mjs
node server/tests/minimize_model_test.mjs
node tools/build-theme.mjs default classic app-host sysapps
```

Pas de nouvelles mesures CPU/GPU ou de présentation pour ce lot ; aucune
amélioration de performances revendiquée. Le Dock et les autres animations
historiques n'avaient pas encore fait l'objet d'une validation complète de leur arrêt
en cours d'effet à ce stade (voir le lot final ci-dessous). Les fermetures des panneaux et menus contextuels restent
immédiates. Une fermeture du composant parent peut également supprimer immédiatement
la surface d'un MenuButton/Dropdown. Aucun déploiement ni redémarrage de session.

## Lot final et bilan

### Dock et robustesse

- `themes/default/components/dock-autohide.js` : extraction du mécanisme existant
  de visibilité du Dock, sans changer les délais d'intention ou les règles d'entrée.
  La réduction des animations termine le glissement en cours et supprime les frames
  de transition des suivants. Une inversion repart de la position actuellement
  affichée, au lieu de repartir de l'une des extrémités.
- `themes/default/components/Dock.jsx` : appelle ce hook avec reduceMotion ; les
  délais de réarrangement utilisent l'horloge monotone.
- `sdk/dock.js`, `themes/default/components/Dock.jsx`, `themes/classic/index.jsx` :
  les horodatages et consommateurs de l'effet existant après retrait d'une icône
  utilisent la même horloge monotone. Aucun nouvel effet de particules ajouté.
- Le réarrangement du Dock est déjà interrompu avec reduceMotion (`useGlide`,
  durée nulle), et les effets de retrait d'icône existants cessent leurs callbacks
  et leur rendu avec cette préférence.
- `server/tests/microanimation_scene.mjs` : **41 contrôles réussis**, dont position
  du Dock à mi-parcours, arrêt immédiat, inversion sans saut, absence de rejeu,
  absence de frames après la fin et démontage. Les délais d'intention pour
  montrer/cacher le Dock restent fonctionnels quand les animations sont désactivées.
- `server/tests/{textfield_menu,menubar,classic_taskbar}_test.mjs` : mocks de
  requestAnimationFrame complétés pour les régressions utilisant les nouveaux
  composants animés. `focus_scene_test.mjs` simule aussi la sauvegarde différée
  des paramètres pour permettre les tests dépassant 300 ms.

### Validation finale

Compilation complète : **classic, default, greeter, app-host et six applications
isolées réussis**. Aucun nouveau paquet ni framework. `git diff --check` réussi.

Suite générale : **159 réussites, 0 échec, 5 groupes ignorés**. Manquants : données
Unicode Bidi/Grapheme, comparaison libthai, outils de génération des fixtures WASM,
et environnement WebKit/bubblewrap du test web-probe. Le moteur C avait également
réussi ses **8 915 contrôles** ; aucun code de rendu de production n'a été modifié
après cette vérification.

Le premier passage général a révélé les mocks RAF incomplets, corrigés puis
retestés. Les deux attentes de langue du test claude-bridge échouaient aussi avec
LC_ALL imposé par l'environnement : le test change LANG, que LC_ALL surchargait.
Le passage final a été exécuté sans LC_ALL/LC_MESSAGES. Aucun changement du terminal
n'a été nécessaire.

```sh
npm run build
env -u LC_ALL -u LC_MESSAGES ASAN_OPTIONS=detect_leaks=0 sh server/tests/run_suites.sh
```

### État des demandes

| Interaction | Résultat livré |
| --- | --- |
| Menus contextuels | Ouverture 100 ms ; fermeture immédiate pour conserver leur cycle de focus |
| MenuButton et Dropdown | Ouverture 100 ms, fermeture 80 ms ; interactions arrêtées à la fermeture logique |
| Panneaux principaux | Ouverture 120 ms ; fermeture immédiate |
| Boutons | Survol et relâchement 90 ms ; appui et action immédiats |
| Interrupteurs | Curseur 120 ms ; état fonctionnel immédiat |
| Notifications | Apparition 140 ms et disparition 80 ms ; suppression/action immédiates |
| Bureaux | Animation existante 170 ms synchronisée avec les frames, interruption propre |
| Minimisation Lord Flow | Effet existant 180 ms, horloge monotone, interruption propre |
| Dock | Glissement existant 180 ms ; interruption, inversion et réduction des animations corrigées |
| Ouverture/fermeture des fenêtres | Pas de nouvel effet ; prototype d'ouverture retiré après mesure de son surcoût |
| Maximisation, restauration et plein écran | Géométrie existante conservée ; pas d'animation ajoutée |
| Déplacements/redimensionnements | Réponse directe existante, aucun retard ni inertie ajoutés |
| Changements de vue, sélections de listes | Pas d'animation généralisée ; aucune recréation des vues |

La préférence existante Apparence → Réduire les animations est réutilisée. Elle
désactive les transitions ajoutées, ainsi que les animations de bureaux,
minimisation et Dock traitées ici ; les animations sont actives par défaut.

### Artefacts et installation

Le répertoire `server/build/microanimations-review/` contient le correctif
`microanimations-only.patch`, un inventaire avec empreintes (`files.json`), les
journaux de compilation/tests et le résultat du benchmark CPU.
Le correctif est calculé contre l'état local au début de ce travail, **pas seulement
contre HEAD** : il exclut les corrections préexistantes de Disques, Fichiers,
plein écran, clics et modèle de bureau. Le contrôle inverse du patch est effectué
en lecture seule sur les sources finales ; l'inventaire vérifie également que
les fichiers locaux préexistants hors de ce lot gardent leur contenu initial.
Ne pas appliquer ce correctif une deuxième fois sur ce répertoire déjà modifié.

Les bundles sont construits. Aucun paquet système fabriqué ou installé, aucun
service redémarré. Pour une installation, le dépôt fournit :

```sh
# Construction du paquet complet à partir d'un arbre et de ses binaires cohérents.
make -C server -j4
sh tools/iso/mkdeb.sh server/build/lordos-microanimations.deb
# Installation locale du paquet obtenu, après validation de sa version et du paquet.
sudo apt install ./server/build/lordos-microanimations.deb
```

Ces trois commandes d'installation/construction de paquet ne sont pas exécutées
par ce travail. Le script de paquet vérifie les binaires auxiliaires requis ;
la version provient de `package.json` et n'a pas été modifiée ici. Il faut aligner
la version et les correctifs C locaux avec la version réellement installée avant
de fabriquer le paquet de déploiement. Les modifications d'animations elles-mêmes
ne nécessitent pas de changement du binaire du compositeur.

### Validation physique restante

Les tests logiciels et les mesures CPU synthétiques ne valident pas la machine
7800X3D / RX 7800 XT à deux moniteurs. Restent à effectuer : ouvertures/fermetures
répétées natives et Wayland/XWayland, déplacements rapides entre écrans,
maximisation/restauration/plein écran, interaction sur le second écran pendant le
plein écran du premier, menus rapides, clics retour, focus clavier et
montage/démontage réels dans Disques.

Comparer des séquences identiques avec et sans animations, avec le même fond
**statique** : reduceMotion met aussi en pause les fonds vidéo, qui fausseraient
la comparaison. Relever pour chaque écran la fréquence, les temps réellement
présentés, les images manquées et la charge CPU/GPU lorsque les compteurs sont
disponibles. Le collecteur existant `tools/diag/compositor-capture.py` peut conserver
les mesures disponibles. Les données non accessibles doivent rester indiquées
comme telles. Aucune amélioration de fluidité physique revendiquée à ce stade.

## Compilation demandée ensuite : livraison 0.8.8.9

Le dépôt racine de ce journal indiquait encore 0.8.8.8. La vérification avant
construction a identifié la version installée `0.8.8.9+localfix1` et son arbre
corrigé dans `server/build/LordOS-0.8.8.9`. Une copie isolée a été créée dans
`server/build/microanimations-0.8.8.9` ; les animations y ont été reportées sans
écraser l'arbre initial ou ses modifications. Les correctifs locaux additionnels
de navigation native et d'AppImage du dépôt racine sont également reportés.

Compilation et livraison réalisées :

- Binaire du compositeur compilé explicitement avec GPU=1 et le SDK graphique local,
  build `20261116.microanimations1`, sans avertissement. Bibliothèques résolues.
- Tous les thèmes et bundles compilés pour la version 0.8.8.9.
- Tests rejoués sur cette base : animations 41, menus 31, focus 41, listes 44,
  barres multi-écrans 51, barre Classic 37, menu Démarrer 9, champs texte 27,
  bureaux 137, minimisation 9, fichiers distants 33, clics et AppImage réussis.
  Les trois tests des correctifs locaux du compositeur réussissent également.
- Paquet construit depuis le paquet installé `0.8.8.9+localfix1`, en conservant
  les composants et scripts non modifiés. Nouvelle version :
  `0.8.8.9+localfix1.microanimations1`.
- Archive extraite : 2 888 fichiers réguliers identiques au staging validé,
  dix fichiers de code et 2 882 entrées MD5 vérifiés ; sommes SHA256 des deux
  paquets vérifiées. Le patch source s'applique à la base corrigée 0.8.8.9.

Livraison : `server/build/DEB-microanimations-0.8.8.9/`, avec paquet neuf, paquet
précédent pour retour arrière, patch source, empreintes, manifeste, journaux et
`INSTALLATION.md`. Le paquet n'a **pas** été installé ; aucun redémarrage de session.
La notice de cette livraison donne les commandes exactes d'installation et de
retour arrière, ainsi que les limites des validations physiques.
