# Support GPU : corrections du 17 novembre 2026

Ce lot prolonge les modifications locales existantes, notamment les textures de
déplacement indépendantes par sortie. Il ne remplace pas ces modifications.

## Changements réalisés

- `kms_cursor_image` conserve la visibilité du plan lors du changement d'image.
  Un curseur masqué sur le secondaire ne réapparaît plus du seul fait de cet appel.
- Les hotspots sont conservés lorsque le plan est masqué puis affiché.
- Un échec DRM ne modifie plus l'état connu de visibilité et n'empêche plus une
  nouvelle tentative. Un échec de mise à jour d'image laisse également le cache
  du compositeur invalide pour la prochaine actualisation du curseur.
- L'import DMA-BUF essaie une texture 2D lorsque `GL_OES_EGL_image` est disponible,
  puis une texture externe si nécessaire et disponible. Il vérifie les erreurs
  de liaison GL et libère texture et EGLImage en cas d'échec. L'ancien chemin
  exigeait systématiquement les textures externes et ne vérifiait pas la liaison.
- Les modificateurs signalés comme réservés aux textures externes ne sont plus
  annoncés si ce chemin est absent. Une requête de modificateurs EGL en échec
  ne fait plus lire des tableaux de résultats indéfinis.
- Une texture 2D importée est remplacée par une texture ordinaire si elle est
  ensuite passée à l'API d'envoi de pixels CPU.

## Validation effectuée

- Test des appels réels de `kms.c` avec ioctl simulé : 23 vérifications réussies.
  Le même test échoue sur `kms.c` de HEAD lors de l'actualisation du curseur masqué.
- Test hors écran EGL/GLES sur llvmpipe : 263 vérifications réussies. Il conserve
  les scénarios multi-sorties antérieurs et ajoute le chemin texture 2D avec
  échantillonnage et lecture des pixels, le repli externe, le rejet des deux
  cibles, la libération des ressources et le filtrage des modificateurs.
  Pour les nouveaux scénarios DMA-BUF, les fonctions d'import EGL et de liaison
  du pilote sont injectées : ce n'est pas un essai PRIME inter-processus réel.
- Les deux tests passent également sous ASan/UBSan, avec détection des fuites
  désactivée pour ce passage.
- Moteur de rendu : 8 915 vérifications, aucun échec.
- Vérification de compilation des unités modifiées, sans avertissement :
  `main.c`, `kms.c`, `gpu.c` avec GPU et `gpu.c` sans GPU.

L'environnement de ce passage ne présente aucun `/dev/dri`. Aucun essai matériel
KMS, changement de mode, installation de paquet ou redémarrage de session n'a
été effectué. Aucun gain de fluidité physique n'est revendiqué.

## Travail restant pour étendre le rendu GPU

Le shell produit toujours un canvas CPU transféré au GPU ; ce lot ne change pas
ce contrat. Le rendu de texte, des formes, des ombres et du flou du shell n'est
pas devenu un moteur graphique GPU natif.

Les prochaines étapes d'architecture sont de mesurer la présentation par sortie,
de conserver correctement les dommages et caches de peinture par sortie, puis de
définir un backend GPU pour les opérations de peinture. Les dommages des clients
SHM en coordonnées de surface, la validation matérielle des sous-surfaces, le
scanout multi-écran et les configurations multi-GPU restent à traiter et tester.
Supprimer simplement les gardes mono-écran ou activer des options ne réalise
pas ces fonctionnalités.

Tests reproductibles depuis la racine :

```sh
make -C server build/kms-cursor-test
make -C server test-gpu-multioutput
make -C server build/engine-test
```

Le test GPU nécessite les headers GBM/EGL/GLES et les bibliothèques Mesa.
Lors de ce passage, les headers du SDK local
`server/build/mouse-package-sdk/root/usr/include` et les bibliothèques système
`libEGL.so.1`, `libGLESv2.so.2` ont été utilisés explicitement.

## Deuxième lot : copies SHM et transferts GPU partiels

Les requêtes `wl_surface.damage_buffer` sont désormais accumulées en coordonnées
du buffer. Lorsqu'une copie SHM complète de la surface précédente est disponible
avec les mêmes dimensions et le même format ARGB/XRGB, seules les lignes et
colonnes du rectangle endommagé sont recopiées. Les changements successifs sont
réunis jusqu'au prochain envoi de la texture au GPU, y compris lorsque plusieurs
commits précèdent un rendu. Il s'agit d'un rectangle englobant, pas d'une région
complexe ; deux petites zones éloignées peuvent donc inclure de l'espace intact.

La nouvelle API `gpu_tex_pixels_damage` actualise la zone de la texture existante.
Sans `GL_EXT_unpack_subimage`, le repli envoie des lignes complètes depuis la copie
CPU, qui reste intégralement valide. Une texture neuve, redimensionnée, importée
ou issue d'un ancien contexte exige toujours un remplissage complet. Un échec
OpenGL invalide la texture et laisse le contenu à renvoyer au prochain essai.

Une première image, un changement de dimensions/format, une transition DMA-BUF,
des dommages exprimés en coordonnées de surface, ou l'absence de dommages buffer
exploitables imposent une copie complète. Les coordonnées sont bornées avec une
arithmétique 64 bits avant les calculs de rectangles.

Limites conservées : les commits SHM sans nouvel attachement ne relisent pas un
buffer déjà libéré au client. Un tel commit avec dommages invalide l'historique
de copie pour imposer un remplissage complet au prochain attachement. Le moteur
peut encore repeindre toute la zone de fenêtre ; cette optimisation porte sur
la copie SHM et le transfert de texture, pas sur toute la peinture du shell.

Validation du deuxième lot :

- Test du code SHM réel avec buffer simulé à stride élargi et ressources
  libwayland réelles : copies partielles, cumul des dommages, transmission au
  backend, conservation des dommages en cas d'échec, absence d'envoi redondant,
  format XRGB, redimensionnement, détachement et coordonnées extrêmes réussis.
- Test EGL/GLES sur llvmpipe : 371 vérifications réussies, dont contrôle des
  pixels des textures neuves, des mises à jour partielles, du repli par lignes,
  du clipping, du redimensionnement et du rejet d'une texture trop grande.
- Même test GPU sous ASan/UBSan réussi, détection des fuites désactivée.
- Test SHM sous ASan/UBSan également réussi. GCC utilise
  `--param=asan-globals=0` pour que l'édition de liens isolée puisse éliminer
  les tables de protocoles inutilisées de `wl.c` ; les accès au tas et à la pile
  restent instrumentés, pas les variables globales par ASan dans ce passage.
- Test des boutons Wayland existant réussi ; unités GPU/Wayland vérifiées à la
  compilation avec et sans `LORD_GPU`, sans avertissement.

Aucune nouvelle mesure de présentation matérielle ni installation. Pour lancer
le nouveau test CPU : `make -C server build/wl-shm-damage-test`. La création du
client libwayland de ce test nécessite ici une exécution hors bac à sable.

## Troisième lot : transformations de buffers Wayland

`set_buffer_transform` n'est plus ignoré. Les huit valeurs du protocole sont
validées et enregistrées dans l'état pending ; le commit les applique à l'état
courant. Une valeur extérieure à 0–7 provoque `invalid_transform`.

Les rotations de 90° et 270° échangent les dimensions avant application du buffer
scale et du viewport. Le rectangle de recadrage reste dans l'espace transformé.
La composition GPU convertit les quatre coordonnées de texture vers le buffer
brut : aucune copie CPU supplémentaire pour orienter les fenêtres composées.
Les fenêtres ordinaires, leurs aperçus transformés, menus et plein écran utilisent
ce chemin. Le scanout direct refuse désormais les buffers non normaux : le code
KMS actuel ne programme pas la rotation du plan.

Pour le repli CPU, les captures et les images de curseur, une copie orientée est
créée à la demande et réutilisée. Elle est invalidée à chaque nouveau contenu,
changement d'orientation ou lecture de secours depuis le GPU. Elle est libérée
au retour à l'orientation normale ou à la destruction de la surface. Coût :
4 octets par pixel brut pour les surfaces non normales ayant utilisé ce cache ;
la première peinture CPU après modification doit remplir cette copie entière.

Validation :

- EGL/GLES llvmpipe : 466 vérifications réussies. Les nouveaux scénarios dessinent
  une image asymétrique 3×2 dans les huit orientations, puis avec recadrage,
  et vérifient les pixels lus dans le framebuffer réel.
- Test Wayland/CPU réussi : matrices de pixels attendues indépendantes pour
  les huit orientations, dimensions logiques, buffer scale, viewport source
  et destination, état pending, application de l'état de commit et rejet des
  valeurs invalides. L'invalidation du cache lors de copies SHM partielles est
  également vérifiée. Les erreurs de protocole sont interceptées par le test.
- Les deux tests passent sous ASan/UBSan, avec les mêmes réserves d'instrumentation
  que dans le deuxième lot.
- Compilation des unités modifiées vérifiée avec et sans GPU, sans avertissement.

Ces essais ne démarrent pas de compositeur et ne vérifient pas la rotation d'un
moniteur physique. Il s'agit des transformations de buffers clients, pas d'une
nouvelle fonctionnalité de rotation des sorties. Aucun backend Vulkan ajouté.

## Quatrième lot : structure des sous-surfaces

Les demandes `place_above` et `place_below` construisent maintenant un ordre
pending, avec la surface parente à sa place dans cette pile. Le commit du parent
applique cet ordre et les positions demandées par `set_position`. Un commit de
l'enfant n'applique plus sa propre position. Les demandes successives de placement
se combinent dans l'ordre de réception.

Les parcours CPU et GPU dessinent les enfants inférieurs, le parent puis les
enfants supérieurs. Le choix de la surface recevant l'entrée suit l'ordre inverse
et respecte la région d'entrée du parent. La suppression d'un enfant préserve
l'ordre des frères, dans la pile courante comme dans la pile pending.

Les changements structurels forcent une invalidation du rendu, même lorsque
le chemin GPU aurait normalement demandé seulement une nouvelle présentation.
Cela évite de conserver une ancienne liste de quads ou des trous périmés dans
le canvas de l'interface.

La création refuse les cycles d'ancêtres et les surfaces ayant déjà un autre
rôle. La capacité actuelle reste de 16 enfants directs par surface : au-delà,
le client reçoit désormais une erreur de ressources, au lieu d'une acceptation
silencieuse d'un enfant absent du rendu. Une nouvelle association réinitialise
la position à (0, 0).

Validation : nouveau test `make -C server build/wl-subsurface-test` réussi,
également sous ASan/UBSan avec `--param=asan-globals=0`. Les ressources et les
gestionnaires de requêtes Wayland sont réels ; les appels de dessin GPU sont
enregistrés pour vérifier l'ordre de `draw_tree`. Le test vérifie aussi les
entrées, les positions différées, les cycles, les erreurs de rôle, la capacité,
la suppression et l'invalidation du rendu. Il ne vérifie pas de pixels KMS ni
un échange complet de requêtes sur le protocole filaire.

Les tests SHM/transformations sous ASan/UBSan et boutons Wayland passent après
ce changement. La compilation Wayland avec et sans GPU reste vérifiée.

Limite importante : ce lot traite l'état structurel porté par `wl_subsurface`.
Il n'implémente pas encore le cache des commits de contenu exigé par `set_sync`
et `set_desync`, qui restent sans effet. Le comportement synchronisé par défaut,
sa propagation aux descendants, les callbacks et les buffers retenus doivent
être traités ensemble dans un prochain lot. Aucun support complet des
sous-surfaces ni test d'application physique n'est revendiqué ici.

Cette limite de synchronisation du quatrième lot est traitée ci-dessous.

## Cinquième lot : commits synchronisés des sous-surfaces

Les sous-surfaces démarrent maintenant en mode synchronisé. Un commit enfant
enregistre son état dans un cache au lieu de modifier immédiatement la surface
visible. L'application du parent vide les caches synchronisés de ses descendants,
y compris les enfants déclarés désynchronisés mais soumis à un ancêtre synchronisé.
`set_desync` applique immédiatement les caches qui deviennent indépendants ;
un ancêtre encore synchronisé continue à imposer l'attente.

Le cache capture les attachements, dimensions/format, buffer scale, transformation,
viewport, région d'entrée, callbacks de frame, retours de présentation et l'état
structurel des enfants. Les requêtes reçues après un commit restent pending et
ne sont pas appliquées par accident au commit du parent. Les positions et ordres
des petits-enfants sont donc eux aussi figés au commit de leur parent direct.
Les enfants supprimés sont retirés des snapshots ; les nouveaux enfants absents
d'un snapshot restent ajoutés au-dessus avec leur position initiale.

Les pixels SHM sont copiés au commit enfant, puis le buffer est libéré au client.
Le client peut donc le réutiliser ou détruire son `wl_buffer` sans modifier l'image
en cache. Un DMA-BUF est retenu par référence et peut également survivre à la
destruction de son objet de protocole. Le retrait d'une ancienne référence ne
déclenche plus `wl_buffer.release` tant qu'une surface, un cache ou un scanout
utilise encore le même objet. Les références retirées sont aussi drainées à
l'arrêt du serveur Wayland.

Les callbacks de frame des commits regroupés sont accumulés, sans être exposés
au rendu avant application du cache. Les retours de présentation des commits
supplantés sont envoyés comme discarded. La destruction d'un cache détruit ses
callbacks sans annoncer de frame et abandonne ses retours de présentation.

La perte du contexte GPU conserve les pixels des surfaces visibles **et** de
leurs caches avant de libérer les textures DMA-BUF partagées. Un buffer tiled
reçu pendant le repli CPU conserve la dernière image CPU disponible. Si le GPU
revient avant l'application du cache, le buffer est réimporté et ses dimensions
réelles remplacent celles de cette image de secours. Le test simule ces transitions.

Validation :

- Tests des copies SHM et transformations précédents toujours réussis.
- Nouveaux scénarios réussis : état caché invisible avant application du parent,
  métadonnées et régions d'entrée isolées des requêtes ultérieures, callbacks
  accumulés, présentation discarded, commits successifs, synchronisation héritée,
  bascule sync/desync, détachement, destruction de buffers, snapshots d'enfants
  supprimés ou créés ultérieurement, références DMA-BUF partagées, réattachement
  du même buffer et perte/retour du contexte GPU.
- Test structurel des sous-surfaces et test des boutons Wayland réussis.
- Tests SHM/synchronisation et structure sous ASan/UBSan réussis, avec
  `--param=asan-globals=0` comme dans les lots précédents. Compilation des unités
  Wayland vérifiée avec et sans GPU, sans avertissement.

Les tests utilisent les gestionnaires et fonctions de commit de production,
des ressources libwayland réelles et des buffers/pilotes injectés. L'application
du parent est exercée par son helper de production ; aucun compositeur complet,
client graphique réel ou scanout KMS n'est démarré. L'import et les lectures GPU
des nouveaux scénarios DMA-BUF sont simulés : ce n'est pas une validation PRIME
matérielle ni une mesure de fluidité.

Coût : une image SHM supplémentaire peut être retenue par sous-surface jusqu'au
commit parent ; la première copie d'un nouveau cache est complète. Les commits
SHM sans nouvel attachement conservent la limite décrite au deuxième lot. La
capacité de 16 enfants directs reste en place. Ce lot ne prétend pas compléter
tous les protocoles Wayland ni la validation physique du compositeur.

## Sixième lot : compilation complète et protocole réel

Le compositeur complet a été recompilé dans une copie isolée, avec GBM/EGL/GLES,
sans avertissement et sans bibliothèque manquante. Le test `wl_wire_test.c` est
lié aux objets complets du compositeur ; il remplace l'objet `wl.c` pour observer
les états et utilise un autre point d'entrée, sans démarrer le bureau.

`wl_start` et les requêtes libwayland client/serveur fonctionnent sur un socket
privé dans `/tmp`. Les buffers SHM sont de vrais pools libwayland, sans injection
des accesseurs SHM ni des événements de callbacks. Le test envoie notamment les
requêtes attach, commit, frame, sync/desync, position, empilement, transformation,
destruction et requêtes invalides. Il lit les pixels du parcours CPU de production.

Résultat : 669 contrôles réussis, également avec ASan/UBSan sur `wl.c` et le
harness, cette fois avec vérification des variables globales activée. Les autres
objets liés restent ceux de la compilation de production optimisée. La détection
des fuites est désactivée. Les contrôles incluent les barrières de protocole et
la vidange des ressources, pas 669 scénarios graphiques distincts.

Le test utilise des surfaces racines sans rôle XDG et appelle explicitement la
fin de frame. Il ne vérifie pas encore une fenêtre GTK/Qt, le shell JavaScript,
le rendu KMS ni une présentation GPU physique. Aucune session active modifiée.

Reproduction : `python3 server/tests/run_wl_wire_test.py --sanitize`.
Les dépendances de développement doivent déjà être présentes ; aucun téléchargement
ni installation n'est effectué. Le SDK Mesa local est utilisé lorsqu'il existe.
Les sources, binaires et journaux du premier passage sont conservés dans
`server/build/GPU-validation-20261117/`.

## Septième lot : paquet Debian et préservation de la base installée

La version installée est `0.8.8.9+localfix1.microanimations1.multigpu1`.
Ses sources contiennent des corrections absentes du dépôt courant : la livraison
est donc reconstruite sur cette base et non sur le premier binaire du sixième lot.
Les nouvelles fonctions GPU/Wayland y sont intégrées dans une copie isolée.
Les contrôles de tailles/synchronisation DMA-BUF et de filiation sont conservés ;
`main.c`, sa politique de curseur et tous les autres modules restent ceux de la
base. Les hotspots sont ajoutés sans réactiver un curseur à sa première allocation.

La base finale a été recompilée complètement puis validée à nouveau : 669 contrôles
sur socket Wayland sous ASan/UBSan, 466 contrôles EGL/GLES llvmpipe, tests unitaires
nouveaux et tests de protection DMA-BUF/curseur existants réussis. Les fonctions
de garde conservées et l'identité de `main.c` ont été comparées explicitement.

Livraison : `server/build/DEB-gpuwayland-0.8.8.9/`.
Version : `0.8.8.9+localfix1.microanimations1.multigpu1.gpuwayland1`.
Build : `20261117.gpuwayland1`.

L'archive `.deb` a été extraite indépendamment. Sur 3 035 entrées du payload,
seul `usr/lib/lordos/bin/lord-server` change. Les scripts et conffiles restent
identiques ; seuls control et md5sums changent côté métadonnées. Les 2 882 entrées
MD5 et les dix fichiers du manifeste de code sont vérifiés. Toutes les bibliothèques
du binaire livré sont trouvées, sans ASan/UBSan. Sources exactes, patch vérifié,
journaux, empreintes et paquet de retour arrière sont fournis.

Le paquet n'est pas installé. Version et empreinte du binaire installé sont
inchangées après fabrication. Les essais KMS et de fluidité sur les deux moniteurs
restent à faire ; aucun support GPU universel ni migration Vulkan n'est revendiqué.
