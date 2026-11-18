# Audit statique de LordOS

Instantané de travail du 18 novembre 2026, basé sur HEAD `51941841` avec les modifications et les ajouts locaux visibles. Les lignes renvoient à cet instantané. Aucun build, test exécuté, lancement du bureau, changement de configuration ou dispositif de mesure. Ce rapport est le seul fichier créé.

## Synthèse

LordOS possède une régulation effective de la production des images. Chaque sortie suit son flip, la préparation en avance est bornée par sortie et les dommages peuvent être fusionnés. La faiblesse principale est le partage du temps de service du thread principal. Les limites sont exprimées en octets, en passages ou en images et laissent subsister des opérations synchrones dont la durée peut dépasser le budget d'une image. Une rafale peut donc retarder plusieurs écrans par le temps de traitement répété, même sans file croissante d'images complètes. La cause de l'épisode Codex CLI reste indéterminée.

## 1. Carte et chemins d'exécution

| Composant | Responsabilité et dépendances observées |
|---|---|
| Session et lancement | `remote/sessiond.c:1411` est un point d'entrée distinct. Les chemins de lancement des applications Linux (`apps.c:1033`) et natives (`app/appspawn.c:82,140`) passent par des processus enfants. |
| Serveur et compositeur | `main.c:2382,2496,2560` sélectionne notamment le mode client natif, initialise le serveur Wayland et orchestre la session. `Server` et ses sorties relient entrées, vue UI, rendu et présentation. La boucle a également des responsabilités réseau, médias, partage et matériel. |
| Wayland | `wl/wl.c` relie instances de protocole, surfaces, rôle de fenêtre et nœud natif. `main.c:2816` appelle `wl_dispatch` qui délègue à libwayland (`wl.c:5644`). Le commit met à jour le contenu et la géométrie, puis applique le rôle et les descendants synchronisés (`wl.c:934–986`). |
| Shell, runtime et SDK | Le thème s'exécute dans QuickJS. Le réconciliateur React fournit un host natif (`runtime/index.js:157–199,323`). Le commit applique les propriétés, transmet le texte à C et appelle les observateurs. Le layout et la peinture sont réalisés ensuite par le compositeur (`main.c:1248,2084`). Le SDK `desktopmodel.js:14–82` centralise des décisions de visibilité, focus, activation et couverture. |
| Applications natives | Le chemin `app_main` (`app/appclient.c:486,545–620`) a son propre client Wayland, son runtime, ses timers et son poll. Le code de l'application passe par `runtime/app-host.js`. Le thread du serveur reste responsable de la réception et de la composition de ses buffers. Le terminal intégré constitue un autre chemin, avec PTY et état VT dans le serveur. |
| GPU et KMS | `main.c:1864–1972` choisit la présentation. Les sorties GPU détiennent chacune une surface EGL/GBM et une texture UI, avec un contexte EGL partagé (`gpu.c:421–439,1334–1349`). KMS soumet les flips et en distribue les retours par CRTC. |
| Travail auxiliaire | Le paint parallèle rejoint ses workers avant de continuer (`par.c:129–143`). Les opérations FS asynchrones ont une file bornée et réveillent la boucle via eventfd (`fsasync.c:240–308`). |

Le couplage traverse la frontière C/JS dans les deux sens. Le serveur déclenche les événements vers le thème et le thème produit des mutations de la scène et des décisions de gestion de fenêtres. Le focus effectif dépend donc de la convergence de l'état Wayland, du modèle de bureau SDK et de l'arbre natif. Le pointeur distingue surfaces parentes et enfants, respecte la région d'entrée, traite les popups en priorité et remet le curseur client à zéro au changement de client (`wl.c:3635–3690`). Le clavier a un état de focus distinct (`wl.c:217,4530,5375`). La conformité de toutes les transitions clavier et des séries de configure/ack n'a pas été établie.

Les globaux `gpu_server` et `gpu_out` (`main.c:1518–1527,1878–1879,1960–1961`) fournissent le contexte implicite du callback de couche. Leur utilisation est séquentielle sur le chemin lu. Une évolution vers plusieurs producteurs ou de la réentrance demanderait de rendre explicites la vue, la sortie, les références de surfaces et la génération GPU avant d'élargir la concurrence. Une partie de la logique de visibilité existe nécessairement à deux niveaux, dans le SDK et dans les indicateurs `xmin/parked/covered` du serveur. L'invariant à maintenir est que les états qui pilotent le focus, le rendu et les callbacks convergent après chaque transition.

## A. Défauts confirmés par le code

### A1. Échec de présentation CPU perdu — moyen, confiance élevée

`main.c:1611,1716–1718,1972`, présentation CPU, et `kms.c:408–432`, `kms_flip`. Le chemin CPU remet `cdirty` à zéro puis ignore la valeur de retour. L'échec du modeset initial ou du fallback après l'échec du flip renvoie -1 sans nouvelle présentation. Le code appelant ne récupère pas cette information. Une mise à jour ponctuelle peut manquer jusqu'à une autre invalidation et des échecs persistants ne débouchent sur aucune décision explicite de récupération dans ce chemin. Condition statique suffisante, échec terminal de la soumission CPU. La fréquence sur matériel reste inconnue.

Correction minimale, remonter le statut, rétablir les dommages en cas d'échec et borner les nouvelles tentatives avant de réévaluer la sortie. Risque de régression, spin sur écran retiré et mauvaise alternance des buffers si un retry assimile un échec à un flip en vol.

### A2. Attribution du feedback de présentation au flip principal — élevé, confiance élevée sur le routage

`main.c:2101–2111,2163,2784`, `render_frame` et boucle principale, et `wl.c:3566–3586`, `wl_frame_done`. Le rendu ignore chaque tête dont le flip est encore en vol. Après un passage qui n'est pas uniquement une préparation, le serveur parcourt toutes les surfaces et transfère leurs feedbacks vers `sent_feedback`. La notification physique est déclenchée par le flip principal. Ce dispositif ne relie pas le commit à la soumission précise sur une sortie qui le contient. Deux sorties qui ne terminent pas leurs flips ensemble peuvent désynchroniser cette attribution. Les clients qui se servent de l'horodatage pour leur cadence peuvent recevoir une indication inexacte. Rien ne quantifie l'effet visuel ici.

Correction minimale, transférer le feedback au lot de présentation au moment de la soumission de la sortie choisie et le résoudre lors de son achèvement. Traiter explicitement les surfaces sur plusieurs sorties, les commits remplacés et les sorties disparues. Risque, callbacks oubliés, doublons ou mauvaise résolution en headless et en scanout direct. Conserver la distinction entre callback de production et notification de présentation.

### A3. Préférence de sortie du plein écran ignorée — moyen, confiance élevée

`wl.c:2773–2797,2812–2815`, `top_fullscreen → enter_fullscreen`. Le paramètre `out` est ignoré, la cible est choisie selon le rectangle du nœud puis par défaut selon `out_main`. Le chemin ne permet pas de prendre en compte une préférence explicite divergente. En outre, `fs_surf` est unique, l'entrée d'une autre surface retire l'état fullscreen à la précédente. C'est une politique globale observable, qui limite le plein écran simultané sur deux têtes. Le choix final de sortie relève de la politique du compositeur et le paramètre ignoré ne suffit pas à établir une violation du protocole.

Correction minimale, traduire la ressource de sortie en tête active et utiliser la géométrie actuelle en repli. Risque, changement de ciblage pour les clients qui transmettent une préférence ancienne, notamment après hotplug. Le passage à plusieurs plein-écrans indépendants serait une évolution distincte.

## B. Risques architecturaux démontrables

### B1. Les lots PTY peuvent dépasser le budget de temps — élevé, confiance élevée sur le mécanisme

`main.c:2812–2851,2607–2612`, `term/term.c:25,530–560`, `term/vt.c:1411`. Chaque PTY prêt reçoit jusqu'à environ 1 Mio de lecture par tour. Le quota est par session et peut être dépassé d'un bloc. Chaque lecture appelle le parseur VT, éventuellement la capture et le journal. Si celui-ci est actif, `fwrite` puis `fflush` sont synchrones. Après le lot, le code vérifie notamment l'état du terminal et marque l'écran changé. Les autres descripteurs partagent le tour. La mise en commun de l'invalidation amortit les rendus, sans supprimer le travail de parsing. Une sortie soutenue peut répéter ce coût à chaque tour. Un journal actif et un stockage lent ajoutent une attente conditionnelle.

Correction minimale, tester un budget temps global entre blocs, faire tourner le premier PTY servi, conserver l'ordre d'un flux et les notifications de fin de lecture. Déporter le journal uniquement si ce chemin s'avère coûteux. Risque, retard du terminal ou de l'état final en cas de HUP, et perte de lignes si le journal différé n'a pas une politique de pression explicite. Aucune trace n'établit que Codex atteint ce budget.

### B2. Accès GPU et modeset en série — élevé, confiance élevée sur le chemin

`main.c:1876–1912,2098–2110` → `gpu.c:1334–1410` → `gpu.c:403–418` → `kms.c:514–527`. La boucle enchaîne `eglMakeCurrent`, uploads, draw, swap, verrouillage du BO et soumission avant de traiter la sortie suivante. Les appels au pilote peuvent imposer du temps CPU en cas de pression GPU ou de manque de buffers. Le premier rendu et le repli de page flip passent par un modeset synchrone. La lecture de retour pour le partage GPU, lorsqu'elle est demandée, emploie `glReadPixels` avant le swap (`gpu.c:908,1385–1389`). Les fences de mesure (`gpu.c:1457–1461`, `main.c:1552`) ne constituent pas un mécanisme général de régulation.

Le chemin utilise déjà `glTexSubImage2D` partiel (`gpu.c:931–942`), réutilise les textures et associe le framebuffer KMS au BO (`gpu.c:385–400`). Le scissor s'appuie sur l'âge du buffer en scène calme et repasse en composition entière avec des couches Wayland ou curseur logiciel (`main.c:1911`, `gpu.c:1355–1378`). Cette politique est conservatrice pour la validité des pixels. Son coût peut se multiplier par écran. Aucun blocage ordinaire du pilote ni temps d'attente n'est établi statiquement.

Correction minimale, distinguer les erreurs transitoires de flip du repli durable et limiter les readbacks à la demande effective. Si une attente est mesurée, modifier d'abord l'opération responsable. Risque, régressions de reprise, de capture ou de maintien du dernier buffer affiché. Le code ne justifie pas une migration d'API graphique.

### B3. Limites de QuickJS exprimées en tours plutôt qu'en temps — élevé, confiance élevée

`main.c:2600–2605` → `ui/js.c:651–672,88–93` → `ui/js_prelude.h:38–58`. La limite de 64 appels ne borne pas la durée de chacun. Un appel peut trier et exécuter tous les timers échus, puis le drainage exécute les jobs jusqu'à épuisement. Les callbacks d'animation sont également traités par lot. Les exceptions des timers et raf sont interceptées et les runtimes ont une limite mémoire (`ui/js.c:418–419,578–579`). Ces protections ne bornent pas le temps d'un callback. Un travail JS coûteux dans le thème retarde les entrées et les flips du serveur. Pour une application native lancée en processus client, le même risque frappe d'abord sa propre boucle (`appclient.c:582–620`). Son travail côté serveur peut encore coûter du temps via les commits.

Correction minimale, définir des points de reprise entre jobs et entre lots, avec prochain poll immédiatement prêt si du travail subsiste. Mesurer les callbacks longs avant d'envisager leur interruption. Risque, changer l'ordre observable des promesses ou du commit React et dégrader le réveil d'une animation. Le budget doit préserver la cohérence de la scène entre commits.

### B4. Fusion des mises à jour après une partie du travail — moyen, confiance élevée

`wl.c:597–631,934–986,5644` et `main.c:2047–2048,2100–2105`. Le commit prend immédiatement le buffer, avec import DMA-BUF si possible ou copie SHM, puis traite le rôle. Plusieurs commits reçus avant le rendu peuvent ainsi remplacer l'image courante tout en ayant déjà payé leur coût d'ingestion. Sur GPU, la demande de frame marque toutes les sorties GPU. La régulation d'une tête occupée différera la présentation, sans réduire nécessairement le coût d'ingestion. Une rafale de commits ou une activité localisée peut donc coûter sur plusieurs sorties. Le débit des clients suivant les callbacks est déjà limité indirectement.

Correction minimale, affecter les dommages aux sorties réellement touchées en incluant l'ancienne emprise et les couches globales, puis ne différer l'ingestion que lorsque les invariants de release et de commit le permettent. Risque, écran secondaire périmé en miroir, en rotation, lors d'un déplacement ou après un hotplug. Il faut préserver la visibilité des sous-surfaces et des popups.

### B5. Barrière de peinture et état implicite de vue — moyen, confiance élevée sur la barrière

`main.c:2147–2159` et `par.c:129–143`. La politique adapte l'usage du pool au temps de peinture et de construction. Elle comporte une hystérésis. Le thread appelant attend néanmoins les workers avant la suite. Un worker lent retarde donc la boucle. Ce lien d'attente est démontré. Aucune contention prolongée ni course précise n'est établie par cette lecture.

Correction minimale conditionnelle, ajuster le seuil ou la taille des lots à partir d'une mesure existante reproductible. Risque, davantage de pression sur le thread principal si le parallélisme est désactivé pendant une charge réellement parallèle. Éviter de rendre le paint asynchrone sans contrat de durée de vie sur la scène.

## C. Hypothèses pour les saccades liées à Codex CLI

| Hypothèse | Gravité potentielle | Confiance dans l'attribution | Ce qu'il manque |
|---|---|---|---|
| Traitement PTY/VT prolongé ou journal synchrone | Élevée | Faible | Confirmer que Codex tourne dans le terminal intégré, connaître le débit par lot, l'état du journal et le coût VT. Un terminal Wayland externe emprunte B4 côté serveur. |
| Accumulation de travail Wayland ou JS | Élevée | Faible | Nombre et coût de commits ou jobs par tour, fenêtre visible et sortie concernée. Les files de protocole et les jobs sont distincts des images préparées, qui sont bornées. |
| Attente EGL/GBM/KMS sur une tête | Élevée | Faible | Backend réellement utilisé, modes actifs, opération longue, état de capture et du pilote. Un flip terminé mais traité tard peut aussi refléter un tour CPU trop long. |
| Cadencement hétérogène et feedback principal | Moyenne | Faible | Fréquences, position du terminal, corrélation entre soumissions et événements de sortie. Le pacing logiciel utilise la fréquence principale en mode soft (`main.c:2690–2704`). Les flips réels sont suivis par tête. |
| Réveil tardif ou contention CPU | Élevée | Faible | Distinguer un thread réveillé tard par l'ordonnanceur d'un thread déjà actif dans PTY, JS, rendu ou en attente de workers. La présence de `poll` et le partage du thread ne démontrent aucun retard de réveil. |

Une dégradation prolongée est compatible avec des dépassements répétés du budget de temps ou avec une attente sous charge. Pour attribuer une cause, il faut aussi expliquer le retour à la normale. Aucun réglage, traçage ou scénario actif n'a été lancé. Les corrections conditionnelles correspondent aux mécanismes des sections A et B.

## Robustesse et protections observées

- Le pipeline garde un état par sortie, invalide la préparation selon la génération GPU, la scène et l'époque du drag (`main.c:1833–1835`). Le passage en avance sans dommage marque un état vide et évite de recommencer jusqu'au flip (`main.c:1991–2005`). Les flips en vol empêchent une nouvelle soumission ordinaire. La défaillance GPU déclenche reprise CPU et dommages complets (`main.c:1966–1970`).
- Les sous-surfaces synchronisées ont un état mis en cache et appliqué avant le flush des descendants. La destruction retire les références dans les trois ordres, visible, pending et caché (`wl.c:805–931`). Les feedbacks d'un commit caché remplacé sont écartés. Ces corrections locales réduisent le risque d'état périmé.
- Le buffer DMA-BUF a un cycle de référence et de retraite distinct. Le chemin de scanout direct vérifie notamment la présence d'une seule tête, l'absence de transformations incompatibles et la capacité de scanout (`wl.c:3550–3557`). Les callbacks des fenêtres cachées sont régulés et les feedbacks correspondants peuvent être écartés sans rendu (`wl.c:3599–3609`).
- Les frontières de sûreté prioritaires sont le commit de surface, l'import de mémoire client, QuickJS vers C et les brokers de capacités. Le corpus lu permet de décrire les responsabilités et plusieurs protections de durée de vie. Il ne permet pas de conclure sur l'ensemble des erreurs mémoire, des accès hors limites, des courses et des contrôles des brokers.
- La séparation par processus des clients natifs isole une exception ou une boucle longue de leur runtime. Le thème et les callbacks exécutés par le serveur restent dans le périmètre de panne du bureau. Les échangeurs asynchrones du serveur illustrent une séparation de charge déjà possible sans généraliser le multithreading du compositeur.

## Tests et journal de lecture

| Passe | Fichiers et fonctions effectivement parcourus |
|---|---|
| 1. Carte et démarrage | Inventaire des répertoires et de l'état Git, liens de compilation dans `server/Makefile`, repères de lancement dans `main.c`, `remote/sessiond.c`, `apps.c`, `app/appspawn.c` et boucle `app/appclient.c`. |
| 2. Rendu et entrées | `main.c`, points d'entrée de rendu, préparation, passes GPU/CPU et boucle poll. `gpu/gpu.c`, `present`, `out_setup`, `gpu_frame_ex`, uploads et drag. `kms.c`, `kms_flip`, `kms_flip_fb`, `kms_dispatch_heads`. `wl/wl.c`, prise du buffer, commit, état synchronisé, plein écran, scanout, callbacks et hit test. |
| 3. Temps CPU et concurrence | `term/term.c`, `term_dispatch` et `check_secret`, repère `vt_write` dans `term/vt.c`. `ui/js.c`, jobs, événements, timers, arrêt du runtime. `ui/js_prelude.h`, timers et raf. Sections de synchronisation de `par.c` et `fsasync.c`. |
| 4. Recoupements | Retour KMS suivi jusqu'à l'appelant CPU, flux des feedbacks suivi jusqu'au flip principal, comparaison des traitements PTY et client natif. Repères de construction et de suites pour `fpipe_test.c`, `gpu_multioutput_test.c`, `wl_shm_damage_test.c`, tests de scènes et microanimations. |
| 5. Rapport | Séparation des observations statiques, des mécanismes possibles et des causes qui restent à mesurer. |

Les cibles de tests associent des tests de décisions de pipeline, de composition GPU et de pixels transformés, de dommages SHM et des scènes React. `gpu_multioutput_test.c:45,104,135,172` présente des cas d'import, de pixels et de dommages. Les repères `server/Makefile:656–673,1159–1161` et `server/tests/run_suites.sh:294–297` montrent des niveaux de validation complémentaires. Ces tests ne donnent pas à eux seuls la latence réelle d'un swap ou d'un flip sous charge sur plusieurs écrans physiques. L'intégralité du runner, des tests filaires et des assertions de chaque suite n'a pas été relue.

L'audit est partiel à l'échelle du dépôt. Les applications, les thèmes, le SDK, les services système et la distribution ont fait l'objet d'une cartographie ou d'une lecture ciblée. Le code des 39 répertoires recensés sous `apps/`, des brokers, des services réseau, du sandboxing, de XWayland, des outils, de la chaîne de paquets, du VT avancé et des dépendances tierces n'a pas été audité fonction par fonction. La lecture des protocoles couvre les chemins cités. Elle ne constitue pas une matrice de conformité pour tous les messages annoncés ni une revue complète des nettoyages d'erreur, du changement de modes et du hotplug.

## Priorités

1. Corriger la remontée d'échec CPU et maintenir les dommages à travers une reprise bornée.
2. Associer le feedback de présentation aux soumissions et événements de la sortie concernée, en couvrant le cas de fréquences mixtes.
3. Pour les saccades, établir d'abord si le terminal est intégré, si la session compose sur GPU et si une capture est active. Ces faits déterminent quels chemins peuvent être pertinents. Différer toute mesure jusqu'à accord sur une méthode qui préserve le symptôme.
4. Si une rafale CPU est confirmée, introduire une limite de temps au point de service identifié, avec une attention à l'équité entre sources et à l'ordre des événements.
5. Réduire les mises à jour sur les têtes non touchées en préservant les corrections locales, puis prendre en compte la préférence de sortie plein écran.
