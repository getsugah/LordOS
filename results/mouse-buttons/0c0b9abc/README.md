# Intégration des boutons supplémentaires — 0c0b9abc

Tests effectués le 15 novembre 2026, dans une VM dédiée QEMU/KVM démarrée
sur l'ISO fournie `lordos-live-0.8.8-amd64.iso`. **162 contrôles finaux réussis,
aucun échec final. La Logitech MX Master 2S physique n'a pas été testée.**

## Configuration et provenance

- Correctif : commit local `0c0b9abc` sur `fix/mouse-buttons`.
- ISO : 1 091 844 096 octets, SHA-256
  `e9fc3f51303407718eb7635d073a917fbecd77b24a0fcd1bb1965ad2e682b836`,
  identique à l'empreinte consignée dans `docs/ISO.md`.
- Binaire hôte et copie invitée : même SHA-256
  `2e32246d8dd919a5e0c6a37f0b5245fb95f6789bc0a54f3c07f2b093bee82e5c`.
- QEMU 10.0.13, KVM actif, 2 vCPU, 4 Gio de RAM, écran virtuel 1024×768.
- Noyau et initrd extraits de cette ISO, système live de cette même ISO.
- Aucun réseau invité ; aucun accès direct aux périphériques d'entrée ou au GPU
  de l'hôte. Un dossier de transfert dédié, contenant seulement les artefacts
  du dépôt, était exposé en lecture seule.
- Binaire et thèmes corrigés extraits et exécutés sous `/tmp/mouse-fix` dans
  l'invité. Aucun binaire installé n'a été remplacé. Le service graphique
  préinstallé de l'invité a été arrêté pour libérer son seul GPU virtuel.
- Disque qcow2 sparse de 30 Gio, sans installation d'OS. L'état du système live
  était en RAM. VM arrêtée après collecte ; instantané de base du disque conservé.

## Résultats finaux

| Suite | Réussites | Échecs |
| --- | ---: | ---: |
| Trajet evdev/Wayland et opérations de fenêtres, Default | 51 | 0 |
| Navigation locale Firefox Wayland, Default | 7 | 0 |
| Trajet evdev/Wayland et opérations de fenêtres, Classic | 51 | 0 |
| Décorations Classic : coordonnées affinées et contrôles positifs des huit poignées | 37 | 0 |
| Navigation locale Firefox Wayland, Classic | 7 | 0 |
| Mode gaucher et repli JavaScript d'une fenêtre maximisée, Default | 9 | 0 |
| **Total** | **162** | **0** |

Les fichiers JSON voisins contiennent les observations et géométries mesurées.
Les nombres comptent les assertions exécutées ; certains scénarios sont répétés
entre suites pour vérifier les décorations et la fin des opérations.

Un vrai client Wayland de diagnostic, avec une surface de couleur connue et
les décorations serveur, a été lancé contre le véritable compositeur corrigé.
QMP a injecté les événements dans le périphérique d'entrée **virtuel** :

- Le client reçoit les pressions et relâchements des codes 272 (gauche),
  274 (central), 273 (droit), 275 (`BTN_SIDE`) et 276 (`BTN_EXTRA`).
- Les demandes `xdg_toplevel.move` et `resize` explicitement émises par le
  client avec les numéros de série latéraux n'altèrent pas sa géométrie.
- Les boutons latéraux sur les décorations ne démarrent pas d'opération.
  Les huit poignées sont vérifiées positivement dans Classic avec le bouton
  gauche, puis son relâchement termine l'opération malgré `BTN_EXTRA` maintenu.
- Les trois boutons principaux peuvent déclencher les demandes interactives
  du client ; leur relâchement termine le déplacement/redimensionnement même
  avec `BTN_SIDE` ou `BTN_EXTRA` encore maintenu.
- Classic conserve sa barre de titre réservée au clic gauche. Le central et
  le droit ne déplacent pas la fenêtre ; le clic droit ouvre son menu normal.
- Firefox utilise Wayland et des profils de test sous `/tmp`. Trois pages
  locales colorées permettent d'observer A → B → C, Retour vers B puis A,
  Suivant vers B puis C. Aucun accès Internet n'est nécessaire.
- Le mode gaucher échange seulement gauche/droit ; central et latéraux gardent
  leurs codes. Sur une fenêtre maximisée Default, les latéraux ne déclenchent
  pas le repli JavaScript, mais le bouton principal le déclenche et le termine
  correctement même avec un latéral maintenu.

Les trois tests C ciblés (`mousebuttons`, véritable `grab.c`, filtre/sockets
Wayland) ont également été exécutés avec succès dans l'invité. Les bibliothèques
requises par le binaire sont présentes dans l'ISO sans installation supplémentaire.

## Incidents de préparation et limites

- Le premier démarrage du binaire corrigé a été refusé : l'ancien compositeur
  de l'invité détenait encore le GPU virtuel. Après identification et arrêt de
  cette seule instance invitée, le lancement a réussi.
- Un mode virtuel non standard de 1066×749 présentait une image déformée, avec
  le compositeur de l'ISO comme avec le binaire corrigé. Un mode standard
  1024×768 a été choisi dans les seules préférences de l'utilisateur invité.
  Aucun code graphique n'a été modifié.
- Un premier contrôle visuel Classic a confondu le menu contextuel qui
  recouvrait le rectangle de diagnostic avec une modification de taille. Le
  contrôle final ferme ce menu et compare également les dimensions `configure`
  Wayland : la fenêtre ne se déplace ni ne se redimensionne au clic droit.
- Firefox a signalé l'indisponibilité du bus d'accessibilité dans cette session
  de lancement manuel. La navigation locale et les événements testés fonctionnent.
- L'arrêt live a signalé des montages ISO/loop occupés, puis a atteint
  `Power down`. QEMU s'est terminé ; le disque virtuel est resté sans OS installé.
- L'ensemble des tests globaux n'a pas été relancé : les échecs indépendants
  `rconnect-run-test` et `claude-bridge-test` restent ceux documentés dans
  [la validation initiale](../../../docs/MOUSE-BUTTONS-VALIDATION.md).
- Pas de validation matérielle MX Master 2S, de liaison Bluetooth/Unifying ni de
  latence physique. Les événements QMP contournent aussi le filtre des boutons
  du compositeur hôte non remplacé ; les presser dans la fenêtre de VM ne
  constituerait donc pas, à lui seul, un test matériel fiable du correctif.
- Le PiP reste couvert par les tests des gestionnaires JavaScript de la
  correction initiale ; aucun scénario PiP complet n'a été exercé dans cette VM.

## Artefacts locaux et sécurité

Les scripts, journaux, captures, noyau/initrd, dossier de transfert et disque
virtuel sont conservés dans `server/build/mouse-buttons-vm-0c0b9abc/` (ignoré
par Git). Son `README.md` décrit le démarrage manuel et les limites des scripts
historiques contenant des PID spécifiques à cette session.

Les processus de l'hôte après les essais sont toujours `lord-server` PID 1044
et `lord-sessiond` PID 995/1005, comme avant les essais ; sa socket Wayland reste
présente. Aucun redémarrage de session hôte, aucune modification de configuration
système hôte, aucun remplacement de son compositeur et aucun push GitHub.
