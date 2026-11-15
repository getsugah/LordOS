# Validation des boutons supplémentaires de souris

Branche : `fix/mouse-buttons`. État au 15 novembre 2026 : accepté pour les
 tests d'intégration, sans validation fonctionnelle sur une Logitech MX Master 2S.

## Périmètre du correctif

- Conversion commune evdev/DOM dans le serveur et le client LordOS :
  `BTN_SIDE` (0x113) ↔ 3, `BTN_EXTRA` (0x114) ↔ 4.
- Transmission des codes evdev corrects aux clients Wayland. WebKit gérait déjà
  Retour/Suivant ; son code n'a pas été modifié.
- Seuls les trois boutons principaux (DOM 0, 1 et 2) participent au démarrage
  et au maintien d'un déplacement/redimensionnement dans `grab.c`. Le masque
  global des boutons maintenus et les compteurs Wayland restent inchangés.
- Protection des gestionnaires JavaScript (barre de titre du thème par défaut,
  poignées, fenêtres PiP et repli après refus du moteur C). Le thème Classic
  conserve sa restriction préexistante au bouton gauche sur sa barre de titre.
- Les requêtes Wayland de déplacement/redimensionnement doivent nommer une
  pression principale reconnue du client. Une pression latérale continue à
  parvenir au client, mais ne peut pas autoriser ces opérations.

La revue du diff ne relève aucun autre changement fonctionnel. Les modifications
au Makefile et au lanceur de suites servent uniquement aux nouveaux tests.

## Résultats obtenus

| Vérification | Résultat |
| --- | --- |
| `make -C server -j4` | Réussite, sans désactivation explicite de fonctionnalité |
| `npm run build` | Réussite : thèmes, app-host et applications système |
| `mousebuttons-test` | Conversion des cinq boutons et rejet des codes inconnus : réussite |
| `grab-buttons-test` | 3 101 vérifications utilisant le véritable `grab.c` : réussite |
| `window_grab_buttons_test.mjs` | Cinq gestionnaires réels, 50 vérifications : réussite |
| `wl-mousebuttons-test` | Politique de requêtes issue du véritable `wl.c` et quatre événements reçus par libwayland : réussite |
| Tests ciblés existants | Type de périphérique, verrouillage du pointeur, garde du glisser-déposer, secousse et curseur : réussite |
| `git diff --check` | Réussite |

Le test de `grab.c` couvre les trois boutons principaux, leurs combinaisons avec
les boutons latéraux, le déplacement, les huit directions de redimensionnement,
la fin d'opération avec un bouton latéral encore maintenu et le déplacement
scripté. Le même test échoue avec l'ancien `grab.c` sur l'autorisation d'une
pression latérale : il détecte bien la régression.

Le test Wayland utilise des sockets privées et les bibliothèques client/serveur,
sans lancer LordOS. Il vérifie la pression et le relâchement de chaque bouton
latéral. Il ne couvre pas tout le trajet depuis une souris physique jusqu'à
une application graphique réelle.

## Échecs résiduels et limites

- `make test`, puis `make -k test`, restent en échec sur `rconnect-run-test` :
  sept assertions échouent, avec refus du cgroup de test par `lord-sessiond`
  (`not in the session leaf`). Les sources de ce test et ses dépendances sont
  identiques à HEAD avant le correctif souris ; ce problème est indépendant.
- Les suites lancées séparément avec les dépendances JavaScript disponibles
  donnent **155 réussites, une suite en échec et six suites ignorées**.
  L'échec concerne deux assertions de `claude-bridge-test` (affichage du dossier,
  de la branche et des périodes localisées). Ses sources sont également
  inchangées. La cause précise n'a pas été corrigée dans cette tâche.
- Les six exclusions concernent des prérequis absents : données Unicode,
  comparaison libthai, outils WebAssembly, QEMU et environnement WebKit de test.
- LeakSanitizer ne fonctionne pas sous le traçage de cet environnement.
  Les essais concernés ont été relancés avec `ASAN_OPTIONS=detect_leaks=0` ;
  ASan/UBSan restent actifs pour les tests qui les utilisent.
- Certains tests de sockets Unix ont nécessité une exécution hors sandbox.
- Aucun essai fonctionnel avec la MX Master 2S, aucun lancement du compositeur
  corrigé, aucune installation de ce compositeur et aucun redémarrage de session.

## Procédure proposée — en attente de validation

**Méthode recommandée : une VM de test dédiée, avec instantané, sans accès
 direct au GPU ni aux périphériques d'entrée de la machine hôte.**

Depuis l'environnement accessible pendant cette revue, `qemu-system-x86_64`,
`qemu-img`, `virsh`, `virt-manager`, `bwrap` et `/dev/kvm` ne sont pas visibles.
Cela ne démontre pas leur absence sur l'hôte extérieur à cet environnement.
La disponibilité d'une VM doit donc être confirmée avant toute exécution.
Aucun paquet de virtualisation n'a été installé dans cette étape.

1. Choisir une VM LordOS jetable ou restaurable. Garder la session graphique
   actuelle de l'hôte ouverte. Ne partager ni son dossier personnel, ni ses
   sockets de session, ni ses périphériques DRM/evdev avec l'invité.
2. Copier le commit, les sources et les artefacts nécessaires dans un répertoire
   de travail de l'invité. Ne remplacer aucun binaire installé et ne modifier
   aucun service, fichier de configuration système ou réglage de démarrage.
3. Après approbation de la procédure adaptée à cette VM, utiliser une session
   invitée réservée au test et lancer le binaire depuis ce répertoire, avec les
   thèmes reconstruits et une durée limitée (`--timeout`). Ne jamais lancer une
   seconde instance KMS sur le siège de la session hôte active.
4. Vérifier d'abord que le périphérique d'entrée virtuel fournit bien
   `BTN_SIDE` et `BTN_EXTRA`. Un périphérique virtuel limité à trois boutons ne
   permet pas de conclure sur leur prise en charge. Une injection contrôlée
   dans l'invité peut compléter ce test, sans exposer les périphériques hôtes.
5. Dans les thèmes Default et Classic, vérifier : navigation Retour/Suivant dans
   une application Wayland ; gauche/droit/central ; déplacement et les huit
   directions de redimensionnement ; fenêtres maximisées et PiP ; mode gaucher.
6. Sur les barres de titre et poignées, maintenir chaque bouton latéral et bouger
   le pointeur : aucune opération ne doit démarrer. Démarrer ensuite avec chaque
   bouton principal autorisé, maintenir un latéral, puis relâcher le principal :
   le latéral ne doit pas prolonger l'opération. Vérifier aussi les combinaisons
   de boutons principaux selon leur comportement historique.
7. Fermer uniquement les processus de test de l'invité et restaurer l'instantané
   si nécessaire. La session hôte doit rester intacte.

Le mode `--headless` existant évite l'ouverture KMS et l'entrée evdev physique,
mais ne constitue pas une validation de la MX Master. Un simple répertoire
`XDG_RUNTIME_DIR` séparé ne suffit pas non plus à isoler une deuxième session :
le serveur utilise aussi le dossier personnel et des services de session.
Cette voie n'est donc pas proposée comme test matériel sur le compte actif.

Une vérification ultérieure des codes de la souris physique pourrait commencer
par `evtest`, en lecture seule sur son périphérique identifié, **sans `--grab`**.
Elle nécessite de vérifier les droits et ne valide que les codes émis par le
matériel, pas le correctif dans le compositeur. Aucun tel relevé n'a été effectué.

**Ne pas exécuter cette procédure avant validation explicite.**
