# Test manuel de la version instrumentée

Préparation terminée ; aucune activation n'a été effectuée par l'agent.
Les actions ci-dessous sont déclenchées uniquement par vos commandes.

## 1. Enregistrer le travail puis démarrer le test

Cette commande **ferme votre session graphique**, arrête temporairement le service,
active le lancement de diagnostic et réaffiche l'écran de connexion. Enregistrer
les documents ouverts avant de la lancer. Elle termine proprement le compositeur
de votre compte par SIGTERM, sans SIGKILL ; les applications Wayland reçoivent les
demandes de fermeture normales. Leur conservation en mémoire n'est pas garantie.

```sh
sudo python3 /home/getsugah/LordOS/tools/diag/session-test.py --start-test
```

Se reconnecter normalement au compte getsugah. Le gestionnaire de connexion, les
arguments de session, les thèmes, les ressources installées et les auxiliaires
restent ceux du système. Seul ce compte utilise le compositeur instrumenté.

## 2. Pendant les deux premières minutes après connexion

Répéter le même déplacement de fenêtre, quelques survols de bords, puis trois
réductions/restaurations. Ne pas lancer le collecteur Python précédent en plus.
Noter si le bureau est fluide ou saccadé et si le fantôme apparaît. Si l'état
change, indiquer l'heure approximative. Aucune saisie de marqueur n'est requise.

La capture s'arrête seule après 120 s ou à sa limite mémoire. **Le bureau de test
continue de tourner** ; il ne revient pas automatiquement à la version normale.
Les CSV, possiblement vides jusqu'au vidage de fin, se trouvent dans :

```text
/home/getsugah/LordOS/server/build/compositor-diag-20261115-201859/session-test/runs/
```

## 3. Retour au lancement habituel

Enregistrer de nouveau les éventuels documents ouverts dans la session de test.

```sh
sudo python3 /home/getsugah/LordOS/tools/diag/session-test.py --restore
```

Cette commande ferme la session de test, retire exclusivement le réglage temporaire
créé pour ce test, puis relance le service installé. Se reconnecter normalement.
Les résultats restent dans `runs/`. L'agent peut ensuite les lire directement.

## Ce qui est modifié et comment c'est vérifié

Aucun binaire installé n'est écrasé et aucun paquet n'est installé. L'unique
réglage de lancement est
`/run/systemd/system/lordos.service.d/90-lordos-compositor-diagnostic.conf`.
Il remplace temporairement ExecStart par une copie identique du gestionnaire de
sessions, dans le bundle de test ; cette copie trouve un wrapper qui ajoute les
variables d'instrumentation au compositeur du compte getsugah. Les autres réglages
du service restent appliqués. Ce fichier sous `/run` disparaît aussi au redémarrage
machine ; aucun override sous `/etc` n'est édité ni supprimé.

Les étapes arrêt/changement/rechargement/démarrage sont journalisées dans
`runs/activate-*.log` et `runs/restore-*.log`, avec copie bornée du journal précédent.
En cas d'échec d'activation après création de l'override, le script tente de retirer
son propre override et de redémarrer le lancement installé. Il ne supprime jamais
un override dont le contenu diffère. Les captures internes ne démarrent que dans
la nouvelle session instrumentée, pas dans le greeter.

Vérifications réalisées : hashes du binaire et du gestionnaire copiés, syntaxe du
wrapper, vérification du code installé par `--check-code` (sans session graphique),
tests de l'enregistreur, et trois tests avec services/signaux simulés couvrant le
retour normal, la conservation d'un override local et le rollback après erreur.
Aucun test réel d'arrêt/démarrage du service n'a été exécuté.

Vérification sans activation :

```sh
python3 /home/getsugah/LordOS/tools/diag/session-test.py --check
```
