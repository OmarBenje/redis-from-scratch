# Cours 1 — Sockets, file descriptors et TCP

> Prérequis du projet : aucun. Ce document part de zéro.
> Objectif à la fin : comprendre **chaque ligne** du serveur TCP mono-client que tu vas écrire à l'étape 1.

---

## Sommaire

1. [Le file descriptor](#1-le-file-descriptor)
2. [Appel système vs fonction de bibliothèque](#2-appel-système-vs-fonction-de-bibliothèque)
3. [Ce qu'est une socket](#3-ce-quest-une-socket)
4. [Les deux sortes de sockets](#4-les-deux-sortes-de-sockets)
5. [Adresses, ports et boutisme](#5-adresses-ports-et-boutisme)
6. [La séquence serveur : socket, bind, listen, accept](#6-la-séquence-serveur)
7. [`SO_REUSEADDR` et `TIME_WAIT`](#7-so_reuseaddr-et-time_wait)
8. [Lire et écrire : TCP est un flux](#8-lire-et-écrire--tcp-est-un-flux)
9. [Gestion des erreurs et `errno`](#9-gestion-des-erreurs-et-errno)
10. [Mémoire : buffers, ownership, ressources](#10-mémoire--buffers-ownership-ressources)
11. [Bloquant vs non bloquant](#11-bloquant-vs-non-bloquant-avant-goût-de-létape-3)
12. [Outils de travail](#12-outils-de-travail)
13. [Checklist de l'étape 1](#13-checklist-de-létape-1)
14. [Glossaire](#14-glossaire)

---

## 1. Le file descriptor

Sous Unix/Linux, **tout est un fichier**. Un fichier sur disque, un terminal, un tube entre deux processus, une connexion réseau : tout se manipule pareil.

Quand tu ouvres une de ces choses, le noyau te rend un **entier** : `3`, `4`, `5`… C'est le **file descriptor** (fd).

Cet entier n'est rien en soi. C'est un **index dans une table** que le noyau maintient pour ton processus. C'est le noyau qui détient l'objet réel ; toi tu détiens un ticket de vestiaire.

```
Ton processus                Noyau
                             ┌──────────────────────────────┐
   fd = 3  ────────────────► │ 3 → connexion TCP 127.0.0.1  │
   fd = 4  ────────────────► │ 4 → fichier /etc/passwd      │
                             └──────────────────────────────┘
```

Trois fd sont déjà ouverts au démarrage de tout processus :

| fd | Nom | Usage |
|----|-----|-------|
| 0 | `stdin` | entrée standard |
| 1 | `stdout` | sortie standard |
| 2 | `stderr` | sortie d'erreur |

`printf("hi")` est, au fond, un `write(1, "hi", 2)` avec du formatage et de la bufferisation par-dessus.

**Conséquence pour toi :** les fd que tu obtiens commencent donc à `3`. Et un fd valide n'est jamais négatif — ce qui explique pourquoi toutes les fonctions qui en rendent un signalent l'erreur par `-1`.

---

## 2. Appel système vs fonction de bibliothèque

Distinction utile pour lire les pages `man` :

- **Appel système** (`man` section **2**) : tu demandes un service au noyau. `read`, `write`, `socket`, `accept`, `close`. Il y a un changement de contexte vers le noyau — c'est relativement coûteux.
- **Fonction de bibliothèque** (`man` section **3**) : du code utilisateur normal, fourni par la libc. `printf`, `malloc`, `htons`.

```sh
man 2 read     # l'appel système
man 3 printf   # la fonction libc
```

Quand une page man existe dans les deux sections, `man read` seul peut t'ouvrir la mauvaise. Précise toujours le numéro.

**Pourquoi ça compte pour Redis :** un serveur rapide minimise les appels système. C'est exactement la raison d'être de l'event loop à l'étape 3 — un `epoll_wait` pour surveiller 10 000 clients, au lieu de 10 000 appels bloquants.

---

## 3. Ce qu'est une socket

**Une socket est un file descriptor.** Point.

C'est un fd qui, au lieu de désigner un fichier sur ton disque, désigne un point de communication réseau. Tu écris dedans avec `write()`, tu lis avec `read()`, tu le fermes avec `close()` — les mêmes fonctions que pour un fichier.

C'est toute la beauté du design Unix, et la raison pour laquelle les sockets sont moins effrayantes qu'elles n'en ont l'air : tu connais déjà l'interface.

Il existe des fonctions spécifiques au réseau (`send`, `recv`) mais elles ne sont que `write`/`read` avec un argument de flags en plus. Tu peux les ignorer pour l'instant.

---

## 4. Les deux sortes de sockets

**Le piège n°1 du débutant** : croire qu'il n'y a qu'une socket. Il y en a deux catégories, aux rôles complètement distincts.

### La socket d'écoute — une seule pour tout le serveur

Elle ne transporte **aucune donnée**. Elle est un standard téléphonique : elle attend qu'on sonne.

Tu ne feras **jamais** `read()` ni `write()` dessus. La seule opération valide, c'est `accept()`.

### Les sockets de connexion — une par client

Chaque `accept()` réussi crée un **nouveau fd** qui représente une conversation avec **un** client. C'est là, et seulement là, que les données circulent.

```
                      ┌─── fd 4 ── client A (conversation)
   fd 3               ├─── fd 5 ── client B (conversation)
 (écoute) ── accept ──┤
   :6379              └─── fd 6 ── client C (conversation)
```

Trois clients connectés = **4 fd ouverts** (l'écoute + 3 connexions). La socket d'écoute reste ouverte en permanence et continue d'accepter.

Quand le client A part, tu fermes le fd 4 — les autres ne sont pas affectés.

---

## 5. Adresses, ports et boutisme

Pour dire au noyau *où* écouter, tu remplis une structure :

```
struct sockaddr_in {
    sa_family_t    sin_family;   // AF_INET (IPv4)
    in_port_t      sin_port;     // port, en ordre réseau
    struct in_addr sin_addr;     // adresse IP, en ordre réseau
};
```

Trois choses à savoir.

### L'adresse `INADDR_ANY`

Une machine a plusieurs interfaces réseau (loopback `127.0.0.1`, wifi, ethernet…), donc plusieurs adresses IP. `INADDR_ANY` (`0.0.0.0`) veut dire « écoute sur toutes ».

> **Note sécurité** : `INADDR_ANY` rend ton serveur joignable depuis le réseau local. Le vrai Redis se lie par défaut à `127.0.0.1` précisément pour ça (des milliers de Redis ouverts sur Internet se font piller chaque année). Pour du dev en local, `INADDR_ANY` est acceptable ; sache juste ce que tu fais.

### Le boutisme (endianness)

Un `uint16_t` comme `6379` occupe deux octets. Dans quel ordre les ranger en mémoire ?

- **little-endian** : octet de poids faible d'abord — c'est ce que fait ton x86.
- **big-endian** : octet de poids fort d'abord.

Les protocoles réseau ont tranché : **big-endian**, appelé *network byte order*. Sinon un PC x86 et un mainframe ne se comprendraient pas.

D'où les fonctions de conversion (`man 3 htons`) :

| Fonction | Sens | Usage typique |
|----------|------|---------------|
| `htons` | host → network, **s**hort (16 bits) | le port |
| `htonl` | host → network, **l**ong (32 bits) | une adresse IPv4 |
| `ntohs` / `ntohl` | l'inverse | lire un port reçu |

**Oublier `htons()` sur ton port est une erreur classique** : ton serveur écoutera silencieusement sur un port absurde (6379 mal converti donne 60184) et tu chercheras pendant une heure.

### La conversion de pointeur

`bind()` accepte un `struct sockaddr *` générique, alors que tu as un `struct sockaddr_in`. C'est du polymorphisme à l'ancienne, avant que C n'ait de type générique : on caste. Tu verras `(struct sockaddr *)&addr` partout. C'est normal, ce n'est pas un hack de ta part.

---

## 6. La séquence serveur

Quatre appels, dans cet ordre.

```
socket()  →  bind()  →  listen()  →  accept()  →  read()/write()  →  close()
   │           │           │            │
 crée      réserve     passe en    attend un
  le fd    le port      écoute      client
```

### `socket(AF_INET, SOCK_STREAM, 0)`

« Noyau, crée-moi une socket. »

- `AF_INET` — famille d'adresses IPv4 (`AF_INET6` pour IPv6).
- `SOCK_STREAM` — **TCP** : flux d'octets fiable, ordonné, orienté connexion. (`SOCK_DGRAM` serait UDP : datagrammes, non fiable, sans connexion. Redis utilise TCP.)
- `0` — laisse le noyau choisir le protocole par défaut de cette combinaison.

Retourne un fd, ou `-1`. À ce stade, la socket n'est attachée à aucune adresse.

### `bind(fd, addr, addrlen)`

« Réserve le port 6379 pour cette socket. »

Sans `bind`, le noyau n'a aucun moyen de savoir quel trafic entrant t'appartient.

Échoue avec `EADDRINUSE` si le port est déjà pris — soit par un vrai Redis qui tourne, soit par ton propre serveur précédent (voir section suivante).

### `listen(fd, backlog)`

« Cette socket devient une socket d'écoute. »

Le `backlog` est la taille de la file d'attente des connexions **établies mais pas encore `accept()`ées**. Si tu es lent à accepter et que la file déborde, les nouveaux clients sont refusés. Le vrai Redis utilise 511. Pour toi, 16 suffira largement.

### `accept(fd, NULL, NULL)`

« Donne-moi le prochain client. »

**Bloque** jusqu'à ce qu'un client se connecte, puis retourne un **nouveau fd**. Les deux derniers arguments permettent de récupérer l'adresse du client ; `NULL, NULL` si tu t'en fiches.

⚠️ Retiens bien : le fd rendu par `accept()` **n'est pas** celui passé en argument. Confondre les deux (et faire `read()` sur la socket d'écoute) est une erreur fréquente.

---

## 7. `SO_REUSEADDR` et `TIME_WAIT`

Tu lances ton serveur, tu fais `Ctrl-C`, tu le relances : `bind: Address already in use`. Alors que plus rien ne tourne.

**Pourquoi :** quand une connexion TCP se ferme, le côté qui ferme en premier laisse le couple (adresse, port) dans l'état `TIME_WAIT` pendant environ 60 secondes. C'est une protection du protocole : des paquets retardataires de l'ancienne connexion pourraient encore arriver, et TCP veut être sûr qu'ils ne seront pas livrés à une nouvelle connexion sur le même port.

**La solution :** dire au noyau « je sais, laisse-moi quand même » :

```
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))
```

À placer **entre `socket()` et `bind()`**. Après le `bind`, c'est trop tard.

Sans ça, tu perdras un temps considérable à attendre entre chaque test. C'est la première ligne de tout serveur sérieux.

---

## 8. Lire et écrire : TCP est un flux

**La notion la plus importante de ce document.** Elle conditionne toute l'architecture de ton Redis.

### Il n'y a pas de messages

Un client envoie `PING` en RESP, soit 14 octets :

```
*1\r\n$4\r\nPING\r\n
```

Tu appelles `read()`. Tu peux recevoir :

- **les 14 octets d'un coup** ✅
- **6 octets**, puis 8 au `read()` suivant ✅ tout aussi valide
- **28 octets** parce que le client a envoyé deux commandes coup sur coup ✅
- **1 octet**, puis 13 ✅ rare, mais légal

TCP garantit deux choses seulement :

1. **l'ordre** — tu reçois les octets dans l'ordre d'envoi ;
2. **l'intégrité** — pas de perte, pas de corruption, pas de duplication.

TCP ne garantit **aucune frontière de message**. Le concept même de « message » n'existe pas à ce niveau. C'est à **ton protocole** (RESP, étape 2) de définir où une commande commence et où elle finit.

> C'est pour ça qu'on parle de *stream* (`SOCK_STREAM`). Pense à un tuyau d'eau, pas à une boîte aux lettres.

### La conséquence architecturale

Chaque client aura besoin de **son propre buffer d'accumulation** :

```
read() → append au buffer du client
       → tenter de parser une commande complète
           ├─ complète ?   → l'exécuter, la retirer du buffer, réessayer
           └─ incomplète ? → ne rien jeter, attendre le prochain read()
```

On construira exactement ça à l'étape 2. Pour l'instant, retiens simplement que **tu ne peux jamais supposer qu'un `read()` te donne un tout cohérent**.

### Les trois valeurs de retour de `read()`

Tu dois traiter les trois, systématiquement :

| Retour | Signification | Action |
|--------|---------------|--------|
| `> 0` | nombre d'octets **effectivement** lus | traiter exactement ces `n` octets |
| `== 0` | **EOF** : le client a fermé proprement | `close()` le fd, libérer ses ressources |
| `== -1` | erreur, détail dans `errno` | logger, fermer (à l'étape 3, `EAGAIN` deviendra normal) |

⚠️ **`read()` ne met pas de `\0`.** Le buffer n'est **pas** une chaîne C. Tu as `n` octets bruts et rien au-delà. Donc :

- ❌ `printf("%s", buf)` — lira du garbage jusqu'à trouver un zéro par hasard
- ❌ `strlen(buf)` — même problème
- ✅ `write(1, buf, n)` ou `fwrite(buf, 1, n, stdout)`

Et c'est **nécessaire** : RESP est un protocole binaire où une valeur peut légitimement contenir un octet nul. Une chaîne C ne peut pas représenter les données de Redis. Cette contrainte te suivra tout le projet — d'où le type `sds` (*simple dynamic string*, longueur explicite) dans le vrai Redis.

### `write()` peut écrire partiellement

Symétriquement, `write(fd, buf, 100)` peut retourner `40`. Le tampon d'émission du noyau était plein. Il te reste 60 octets à écrire, à toi de rappeler `write()` sur le reste.

Pour l'étape 1 (petites réponses, socket bloquante) tu peux vérifier le retour sans plus. À l'étape 3, gérer les écritures partielles deviendra obligatoire.

---

## 9. Gestion des erreurs et `errno`

**Règle absolue en C système : tu testes la valeur de retour de chaque appel système. Sans exception.**

Il n'y a pas d'exceptions en C. Un `bind()` échoué qui passe inaperçu produit un bug incompréhensible trois appels plus loin.

Le protocole standard :

- l'appel retourne `-1` (ou `NULL` pour ce qui rend un pointeur) ;
- la variable globale `errno` contient le **code** de l'erreur (`EADDRINUSE`, `ECONNRESET`, `EINTR`…) ;
- `perror("bind")` affiche `bind: Address already in use` sur stderr.

Deux pièges :

1. **`errno` n'est valide qu'immédiatement après l'échec.** Un appel réussi peut l'écraser. Si tu veux logger plus tard, copie-le d'abord.
2. **`errno` n'est pas remis à zéro en cas de succès.** Ne teste jamais `errno` sans avoir d'abord vu un retour d'erreur.

Le cas `EINTR` : un appel bloquant (`accept`, `read`) peut être interrompu par un signal et retourner `-1` avec `errno == EINTR`. Ce n'est pas une vraie erreur — la bonne réaction est de **réessayer**. Tu peux l'ignorer à l'étape 1 ; garde-le en tête.

---

## 10. Mémoire : buffers, ownership, ressources

### Pour l'étape 1, pas de `malloc`

Un tableau sur la pile suffit :

```
char buf[4096];
```

Il est alloué à l'entrée de la fonction, libéré automatiquement à la sortie. Pas de fuite possible. On introduira `malloc` à l'étape 3, quand chaque client aura besoin d'un buffer qui **survit** entre deux appels de la boucle — et là, la question de l'ownership deviendra centrale.

### Ne jamais dépasser `n`

Ton buffer fait 4096 octets. `read()` t'en a rendu 30. Les 4066 restants contiennent des **ordures** (ce que la pile contenait avant). Toute lecture au-delà de `n` est un bug.

ASan attrapera les dépassements de tableau. Il n'attrapera **pas** la lecture d'ordures à l'intérieur du buffer — c'est de la mémoire légitimement à toi, juste non initialisée. Seul ton raisonnement te protège là.

### Un fd est une ressource, comme la mémoire

La règle est identique à `malloc`/`free` :

| Acquisition | Libération |
|-------------|------------|
| `malloc()` | `free()` |
| `socket()` / `accept()` | `close()` |

**Chaque fd ouvert doit être fermé sur tous les chemins de sortie, y compris les chemins d'erreur.** Un fd qui fuit est aussi grave qu'une fuite mémoire : le noyau limite le nombre de fd par processus (`ulimit -n`, souvent 1024) et tu finis par ne plus pouvoir accepter personne.

C'est un vrai sujet dans les serveurs réels, et une question d'entretien classique.

### La règle d'ownership à intérioriser dès maintenant

> Pour toute ressource, il existe **exactement un** propriétaire, et c'est lui qui la libère.

Écris-le en commentaire au-dessus de chaque fonction qui alloue quoi que ce soit :

```
/* Le buffer retourné appartient à l'appelant, qui doit le free(). */
```

Ça a l'air pédant maintenant. À l'étape 4, quand ta hash table stockera des clés allouées et que tu devras décider si `hash_set` copie la clé ou en prend possession, cette discipline t'évitera des heures de débogage.

---

## 11. Bloquant vs non bloquant (avant-goût de l'étape 3)

Par défaut, une socket est **bloquante** : `accept()` et `read()` suspendent ton processus jusqu'à ce que quelque chose arrive.

C'est parfait pour l'étape 1 (un client). C'est **fatal** dès deux clients : pendant que tu es bloqué en `read()` sur le client A, le client B est totalement ignoré, même s'il t'envoie des données.

Trois façons d'en sortir :

1. **Un thread par client** — simple, mais ne passe pas l'échelle (mémoire par thread, coût des changements de contexte, verrous partout).
2. **Un processus par client** — pire encore.
3. **Un event loop mono-thread** avec I/O non bloquante — « préviens-moi quand un de ces 10 000 fd a quelque chose à me dire ». C'est `epoll` sous Linux.

**Redis a choisi le 3, et c'est le cœur de sa réputation.** Un seul thread qui traite les commandes, zéro verrou, zéro condition de course sur les données, et des centaines de milliers de requêtes par seconde.

C'est aussi le point le plus intéressant à raconter en entretien. On y arrive à l'étape 3.

---

## 12. Outils de travail

### Les pages man

Ton outil principal. Apprends à les lire — la section **RETURN VALUE** et la section **ERRORS** en particulier.

```sh
man 2 socket    man 2 bind      man 2 listen
man 2 accept    man 2 read      man 2 write
man 2 close     man 2 setsockopt
man 3 htons     man 7 socket    man 7 tcp
```

`man 7 tcp` et `man 7 socket` sont des vues d'ensemble excellentes, à lire une fois calmement.

Si `man 2 socket` ne trouve rien : `sudo apt install manpages-dev`.

### Les sanitizers

```
-fsanitize=address,undefined
```

Compile **toujours** avec ça pendant le développement. ASan détecte les dépassements de buffer, les `use-after-free`, les fuites mémoire, et te donne la ligne exacte. UBSan attrape les comportements indéfinis (dépassements d'entier, déréférencements mal alignés).

Ça ralentit d'un facteur ~2. Pour les benchmarks de fin de projet, tu recompileras sans, avec `-O2`.

### Les warnings

```
-Wall -Wextra -Werror
```

`-Werror` transforme les warnings en erreurs. Ça paraît brutal ; c'est la meilleure décision que tu prendras. En C, un warning ignoré est un segfault en attente.

### Tester à la main

```sh
redis-cli -p 6379 ping        # le vrai client, l'objectif final
nc localhost 6379             # netcat, envoie du texte brut
printf 'PING\r\n' | nc localhost 6379

ss -ltnp | grep 6379          # qui écoute sur ce port ?
```

### Observer le trafic

```sh
strace -e trace=network ./server      # tous les appels réseau de ton programme
sudo tcpdump -i lo -A port 6379       # les octets qui circulent
```

`strace` est extraordinaire pour comprendre ce que ton programme demande vraiment au noyau. À dégainer quand tu ne comprends pas un comportement.

---

## 13. Checklist de l'étape 1

Objectif : `src/server.c`, un serveur qui accepte **un seul** client, affiche ce qu'il reçoit, et se termine proprement.

- [ ] `Makefile` avec `-Wall -Wextra -Werror -g -fsanitize=address,undefined`
- [ ] `socket()` — retour testé
- [ ] `setsockopt(SO_REUSEADDR)` **avant** le `bind`
- [ ] `struct sockaddr_in` remplie, port passé par `htons(6379)`
- [ ] `bind()` — retour testé, `perror` en cas d'échec
- [ ] `listen()` avec un backlog raisonnable
- [ ] `accept()` — retour testé, stocké dans une **variable distincte** du fd d'écoute
- [ ] boucle `read()` traitant les **trois** cas (`>0`, `0`, `-1`)
- [ ] affichage avec `write(1, buf, n)` ou `fwrite` — jamais `printf("%s")`
- [ ] `close()` sur le fd client **et** sur le fd d'écoute, y compris sur les chemins d'erreur
- [ ] `perror()` partout où un appel peut échouer
- [ ] `README.md` démarré

**Validation :**

```sh
./server &
redis-cli -p 6379 ping
```

Tu dois voir apparaître `*1\r\n$4\r\nPING\r\n`. `redis-cli` va rester bloqué à attendre une réponse — c'est normal, tu n'en envoies pas encore. Ce sera l'étape 2.

**Vérification mémoire :** à la sortie, ASan ne doit rapporter **aucune** fuite.

---

## 14. Glossaire

| Terme | Définition |
|-------|-----------|
| **fd** (file descriptor) | Entier positif désignant une ressource ouverte, index dans une table du noyau |
| **appel système** | Requête de service au noyau ; `man` section 2 |
| **socket** | Un fd désignant un point de communication réseau |
| **socket d'écoute** | Socket qui n'accepte que `accept()` ; ne transporte pas de données |
| **socket de connexion** | Socket rendue par `accept()` ; une par client, transporte les données |
| **`AF_INET`** | Famille d'adresses IPv4 |
| **`SOCK_STREAM`** | Type TCP : flux fiable, ordonné, orienté connexion |
| **`SOCK_DGRAM`** | Type UDP : datagrammes, non fiable, sans connexion |
| **backlog** | Taille de la file des connexions en attente d'`accept()` |
| **boutisme / endianness** | Ordre des octets d'un entier multi-octets en mémoire |
| **network byte order** | Big-endian, convention imposée par les protocoles réseau |
| **`TIME_WAIT`** | État TCP post-fermeture (~60 s) qui bloque la réutilisation du port |
| **`SO_REUSEADDR`** | Option socket permettant le `bind` malgré `TIME_WAIT` |
| **EOF** | `read()` retourne 0 : l'autre bout a fermé proprement |
| **`errno`** | Variable globale contenant le code de la dernière erreur système |
| **`EINTR`** | Appel interrompu par un signal ; il faut réessayer |
| **`EAGAIN`** | « Rien à lire pour l'instant » ; normal en mode non bloquant |
| **flux (stream)** | Suite d'octets sans frontières de message |
| **framing** | Le fait, pour un protocole, de délimiter ses messages dans le flux |
| **RESP** | REdis Serialization Protocol — le protocole de Redis (étape 2) |
| **ownership** | Qui est responsable de libérer une ressource |
| **ASan / UBSan** | Sanitizers de mémoire et de comportement indéfini |
| **event loop** | Boucle surveillant N fd à la fois et réagissant aux événements (étape 3) |
| **`epoll`** | Mécanisme Linux de surveillance efficace de nombreux fd |

---

**Suite :** `02-resp.md` — le protocole RESP, le framing, et le buffer d'accumulation par client.
