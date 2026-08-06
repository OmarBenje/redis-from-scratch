# Cours 2 — Make et les Makefiles

> Prérequis : savoir qu'un programme C se compile avec `gcc`. Rien d'autre.
> Objectif à la fin : écrire toi-même le `Makefile` du projet, et comprendre **chaque caractère** dedans.

---

## Sommaire

1. [Le problème que make résout](#1-le-problème-que-make-résout)
2. [Ce que fait vraiment `gcc`](#2-ce-que-fait-vraiment-gcc) — dont [`-o`, `-c`, `-g`](#21--o--c--g--trois-lettres-trois-catégories)
3. [Anatomie d'une règle](#3-anatomie-dune-règle) — dont [une cible, c'est ce qui sort](#31--une-cible-cest-ce-qui-sort)
4. [La tabulation](#4-la-tabulation)
5. [Le graphe de dépendances](#5-le-graphe-de-dépendances)
6. [Premier Makefile complet](#6-premier-makefile-complet)
7. [Les cibles factices et `.PHONY`](#7-les-cibles-factices-et-phony)
8. [Les variables](#8-les-variables)
9. [Les variables automatiques](#9-les-variables-automatiques)
10. [Les règles de motif](#10-les-règles-de-motif)
11. [Le piège des fichiers `.h`](#11-le-piège-des-fichiers-h)
12. [Les flags de compilation qui comptent](#12-les-flags-de-compilation-qui-comptent)
13. [Débugger un Makefile](#13-débugger-un-makefile)
14. [Le Makefile cible du projet](#14-le-makefile-cible-du-projet)
15. [Erreurs fréquentes](#15-erreurs-fréquentes)
16. [Glossaire](#16-glossaire)

---

## 1. Le problème que make résout

Au début, tu compiles à la main :

```sh
gcc -Wall -Wextra -g -o server main.c store.c net.c resp.c
```

Ça marche. Puis trois choses se dégradent :

1. **Tu retapes la ligne cent fois par jour.** Et un jour tu oublies `-g`, et `gdb` ne te montre plus rien d'utile.
2. **Tu recompiles tout à chaque fois.** Sur 4 fichiers c'est instantané ; sur un vrai projet c'est des minutes perdues à chaque `printf` de debug.
3. **Personne d'autre ne sait comment compiler ton projet.** Ni ton correcteur, ni toi dans trois semaines.

`make` répond aux trois. C'est un programme qui lit un fichier nommé `Makefile` dans le dossier courant, et qui y trouve la réponse à une seule question :

> **Quel fichier est plus vieux que ce dont il dépend ?**

Tout le reste découle de ça. Make compare des **dates de modification**. Si le résultat est plus récent que ses ingrédients, il n'y a rien à refaire.

---

## 2. Ce que fait vraiment `gcc`

Impossible d'écrire un bon Makefile sans savoir ce qu'on automatise. `gcc main.c -o prog` enchaîne en réalité **quatre étapes** :

```
main.c
  │  (1) préprocesseur — cpp
  │      remplace les #include par le contenu des fichiers,
  │      développe les #define, traite les #ifdef
  ▼
main.i      ← du C pur, sans une seule directive #
  │  (2) compilation — cc1
  ▼
main.s      ← de l'assembleur
  │  (3) assemblage — as
  ▼
main.o      ← code machine, mais INCOMPLET
  │  (4) édition de liens — ld
  ▼
prog        ← exécutable
```

L'étape qui nous intéresse est la coupure entre **(3)** et **(4)**.

Un fichier **`.o`** (*fichier objet*) contient du code machine, mais il a des **trous**. Si `main.c` appelle `store_get()` qui est définie dans `store.c`, le `.o` de `main` contient en substance : « ici, appelle quelque chose qui s'appelle `store_get` — je ne sais pas où c'est, à toi de voir ».

L'**éditeur de liens** (*linker*) prend tous les `.o`, résout ces références croisées, y ajoute le code de la bibliothèque C standard (`printf`, `malloc`…), et produit l'exécutable.

D'où :

- `gcc -c main.c` → produit `main.o`, **s'arrête avant le linking**. Le `-c` veut dire « compile only ».
- `gcc -o server main.o store.o` → **uniquement** le linking.

**Pourquoi c'est capital pour make :** un `.o` par `.c` donne à make une granularité fine. Tu modifies `store.c` ? Seul `store.o` est reconstruit, puis on relinke. `main.o`, `net.o`, `resp.o` sont intacts.

C'est aussi ce qui explique deux erreurs très différentes que tu vas rencontrer :

| Message | Étape | Cause typique |
|---|---|---|
| `error: implicit declaration of function 'store_get'` | compilation | tu as oublié le `#include "store.h"` |
| `undefined reference to 'store_get'` | **linking** | tu as oublié de compiler/linker `store.c` |

Le second est le classique absolu du débutant en Makefile : le prototype existe (le `.h` est inclus, donc la compilation passe), mais le `.o` correspondant n'est jamais donné au linker.

### 2.1 — `-o`, `-c`, `-g` : trois lettres, trois catégories

Ces trois flags se ressemblent (un tiret, une lettre) mais ne jouent pas du tout dans la même catégorie :

| Flag | Question à laquelle il répond |
|---|---|
| `-o` | **Où** je mets le résultat ? |
| `-c` | **Jusqu'où** je vais dans la chaîne ? |
| `-g` | **Quoi** je mets dedans ? |

**`-o` — le nom du fichier de sortie.** *output*. Prend **toujours un argument** : le nom du fichier à créer.

```sh
gcc -o server server.c     # crée un fichier nommé "server"
gcc -o toto   server.c     # crée un fichier nommé "toto"
gcc           server.c     # sans -o → crée "a.out" (nom par défaut historique)
```

C'est purement du nommage. `-o` ne change **rien** à ce que gcc fabrique, seulement où il l'écrit.

> **Piège.** Le fichier qui suit `-o` est écrasé. Ne tape **jamais** `gcc -o server.c server.c` : gcc remplace ta source par l'exécutable, ton code est perdu. Ça arrive à tout le monde une fois.

**`-c` — s'arrêter avant le linking.** *compile only*. Ne prend **aucun argument**. Il dit à gcc : « fais les étapes 1 à 3, pas la 4 ».

```
server.c → [préproc] → [compil] → [assemblage] → server.o → [LINKING] → server
                                                     ↑                      ↑
                                             -c s'arrête ici        sans -c on va jusqu'ici
```

- **avec `-c`** → un `.o`. Code machine **incomplet** : les appels aux fonctions définies ailleurs sont des trous. Ça ne se lance pas.
- **sans `-c`** → un **exécutable**. Complet, lançable avec `./server`.

Vérifie-le toi-même, c'est parlant :

```sh
gcc -c server.c
./server.o        # → Permission denied / Exec format error
file server.o     # → "relocatable"  = incomplet
file server       # → "executable"   = complet
```

`relocatable` contre `executable` : c'est le mot du système pour dire exactement ça.

`-c` et `-o` **se combinent**, puisqu'ils répondent à deux questions différentes :

```sh
gcc -c src/store.c -o build/store.o
#   ↑ arrête-toi au .o     ↑ et mets-le là
```

**`-g` — embarquer les infos de debug.** Ne change ni l'étape, ni le nom : il change le **contenu**. gcc ajoute une table qui relie le code machine à ton source — quelle instruction vient de quelle ligne, comment s'appellent tes variables, quelle tête ont tes `struct`.

Sans cette table, ton binaire ne sait plus qu'il a été écrit en C :

```
# sans -g
Program received signal SIGSEGV
0x0000000000401136 in ?? ()
```

```
# avec -g
Program received signal SIGSEGV
0x0000000000401136 in store_get (s=0x0, key=0x4005e4 "foo") at store.c:42
42          return s->entries[i].value;
```

Fichier, ligne, fonction, valeur des arguments. Tu vois immédiatement que `s` vaut `0x0` — un pointeur NULL. Sans `-g`, tu as une adresse hexadécimale et rien d'autre. Même chose pour `valgrind`, qui ne te dira à quelle ligne tu as fui de la mémoire que si `-g` est là.

Le coût est un binaire plus gros sur le disque. Il n'est **pas plus lent** : la table de debug est ignorée à l'exécution. Aucune raison de s'en priver en développement.

### 2.2 — Les trois ensemble, et pourquoi CFLAGS existe

```sh
gcc -Wall -Wextra -g -c store.c -o store.o
     └────┬────┘   │  │         └─ où : dans store.o
          │        │  └─ jusqu'où : arrête-toi au .o
          │        └─ quoi : avec les infos de debug
          └─ quoi : avec tous les avertissements
```

Ce rangement explique la structure du Makefile, qui n'est pas arbitraire :

```make
CFLAGS := -Wall -Wextra -g        # le "quoi" : identique pour tous les fichiers

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@   # le "jusqu'où" et le "où" : propres à chaque règle
```

`-Wall -Wextra -g` vont dans `CFLAGS` parce qu'ils sont les mêmes partout. `-c` et `-o` restent dans la recette parce qu'ils dépendent de la règle. Regarde d'ailleurs la règle de linking :

```make
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
```

Pas de `-c` (on veut aller jusqu'à l'exécutable), pas de `CFLAGS` (il n'y a plus rien à compiler, seulement à assembler).

---

## 3. Anatomie d'une règle

L'unité de base du Makefile est la **règle** :

```make
cible: dépendance1 dépendance2
	commande1
	commande2
```

- **cible** (*target*) : en général, le nom du fichier à produire.
- **dépendances** (*prerequisites*) : les fichiers dont la cible est faite.
- **recette** (*recipe*) : les commandes shell qui fabriquent la cible. Chaque ligne commence par une **tabulation**.

L'algorithme de make, pour une cible donnée :

1. Traiter d'abord, récursivement, chacune de ses dépendances (elles peuvent être elles-mêmes des cibles).
2. Si la cible n'existe pas → exécuter la recette.
3. Si la cible existe mais qu'**au moins une** dépendance est plus récente qu'elle → exécuter la recette.
4. Sinon → afficher `make: 'cible' is up to date.` et ne rien faire.

Exemple minimal :

```make
main.o: main.c
	gcc -Wall -Wextra -g -c main.c
```

Lis-le à voix haute : « `main.o` est fabriqué à partir de `main.c`, par cette commande. »

### 3.1 — Une cible, c'est ce qui **sort**

Erreur de lecture très fréquente : croire que `server:` désigne un fichier qu'on possède déjà. Retourne la lecture.

```
cible: ingrédients
```

`server` n'existe pas *encore*. C'est précisément **ce que la règle fabrique**. Les `.c` et les `.o` sont à droite des deux-points : ce sont les ingrédients.

```make
server: main.o store.o
	gcc -o server main.o store.o
```

Regarde le `-o server` dans la commande : c'est gcc qui **crée** un fichier nommé `server`. La cible et l'argument de `-o` désignent le même fichier — c'est toujours vrai, et c'est ce qui rend `$@` possible (§9).

Un `.c` ne peut donc jamais être une cible principale : tu ne le *produis* pas, tu l'écris toi-même.

```
   .c    →    .o    →   server
 (écrits)  (produits)  (produit)
   toi        make       make
```

Dans le graphe, les `.c` sont les **feuilles** (le point de départ), l'exécutable est la **racine** (le point d'arrivée).

**Et pourquoi le nom `server` ?** Aucune raison technique. C'est le nom que tu donnes à ton exécutable, celui que tu taperas : `./server`. Mets `redis-bis` si tu préfères — c'est tout l'intérêt de la variable `TARGET`, un seul endroit à changer.

Attention à une coïncidence trompeuse : dans `gcc -o server server.c`, les deux mots se ressemblent mais désignent **deux fichiers différents** — celui qu'on crée, celui qu'on lit. `gcc -o toto server.c` marche tout aussi bien. Le nom de sortie n'a aucun lien avec le nom de la source ; c'est l'habitude qui les fait coïncider.

Preuve en trente secondes — le fichier naît et meurt sous tes yeux :

```sh
ls          # pas de fichier "server"
make
ls          # server est apparu, ainsi que les .o
./server    # tu lances ton programme
make clean
ls          # server a disparu
```

### 3.2 — « Mais `gcc -o server server.c` suffit, non ? »

Oui. Et tant que tu n'as **qu'un seul fichier**, c'est exactement ce qu'il faut faire.

Ta ligne a une seule limite : elle ne tient plus dès qu'il y a plusieurs fichiers. Ton Redis aura le réseau, le stockage clé/valeur, le parsing du protocole — pas 2000 lignes dans un `server.c`. Tu peux encore tout compiler d'un coup :

```sh
gcc -Wall -Wextra -g -o server main.c store.c net.c resp.c
```

Ça marche, mais gcc **recompile les quatre fichiers à chaque fois**. Un `printf` de debug dans `main.c` ? Les trois autres sont recompilés pour rien.

Et surtout : sous cette forme, make ne voit qu'une **boîte noire**. Soit il relance tout, soit rien — il n'a aucune prise. En découpant en `.o`, il compare quatre paires de dates au lieu d'une et ne relance que le nécessaire.

C'est la vraie réponse : **on découpe en `.o` pour donner à make quelque chose à optimiser.**

En attendant d'avoir plusieurs fichiers, commence honnêtement là où tu en es. Ce Makefile est parfaitement valable :

```make
CC     := gcc
CFLAGS := -Wall -Wextra -g

server: server.c
	$(CC) $(CFLAGS) -o server server.c
```

Il ne t'apporte pas encore de compilation incrémentale — il t'apporte de ne plus retaper la ligne, et de ne plus oublier `-g`. Le découpage en `.o` viendra avec le deuxième fichier.

**Par défaut, make construit la première cible du fichier.** `make` tout court est équivalent à `make <première-cible>`. C'est pourquoi on met toujours l'exécutable final en haut. Tu peux aussi viser une cible précise : `make main.o`, `make clean`.

---

## 4. La tabulation

> **Les lignes de recette doivent commencer par un caractère de tabulation (`\t`), pas par des espaces.**

C'est une règle de syntaxe absolue, et c'est le plus mauvais choix de design de l'histoire d'Unix (son auteur, Stuart Feldman, l'a reconnu publiquement). Si tu mets des espaces :

```
Makefile:3: *** missing separator.  Stop.
```

**Ce message veut toujours dire « tu as mis des espaces au lieu d'une tabulation ».** Retiens-le, tu le verras.

Le piège : beaucoup d'éditeurs convertissent automatiquement les tabulations en espaces. Pour VS Code, mets dans `.vscode/settings.json` :

```json
{
  "[makefile]": { "editor.insertSpaces": false }
}
```

Pour vérifier ce que ton fichier contient vraiment :

```sh
cat -A Makefile     # une tabulation s'affiche ^I, une fin de ligne $
```

Tu dois voir `^I` au début de chaque ligne de recette.

---

## 5. Le graphe de dépendances

Make ne lit pas ton Makefile de haut en bas comme un script. Il en construit un **graphe**, puis le parcourt depuis la cible demandée.

Pour un projet avec `main.c`, `store.c`, `net.c` :

```
                    server
                   /   |   \
                  /    |    \
            main.o  store.o  net.o
              |        |        |
            main.c  store.c  net.c
```

Quand tu tapes `make` :

1. Cible `server`. Elle dépend de `main.o`, `store.o`, `net.o` → make les traite d'abord.
2. Cible `main.o`. Elle dépend de `main.c`. `main.c` n'est la cible d'aucune règle et existe sur le disque → c'est une **feuille**, rien à faire.
   `main.o` existe-t-il ? Est-il plus récent que `main.c` ? Sinon → on compile.
3. Idem pour `store.o` et `net.o`.
4. Retour à `server` : si un `.o` vient d'être reconstruit, il est plus récent que `server` → on relinke.

**Conséquence pratique :** l'ordre des règles dans le fichier n'a aucune importance (sauf pour savoir laquelle est la première). C'est le graphe qui décide, pas la position.

Vérifie ta compréhension : tu touches `store.c` et tu relances `make`. Que se passe-t-il ?

<details>
<summary>Réponse</summary>

`store.o` est recompilé (sa dépendance `store.c` est plus récente que lui), puis `server` est relinké (sa dépendance `store.o` est plus récente que lui). `main.o` et `net.o` ne sont **pas** touchés — leurs `.c` n'ont pas bougé. Deux commandes exécutées au lieu de quatre.

</details>

---

## 6. Premier Makefile complet

Version explicite, sans aucune astuce. Écris exactement ça pour commencer :

```make
server: main.o store.o
	gcc -o server main.o store.o

main.o: main.c
	gcc -Wall -Wextra -g -c main.c

store.o: store.c
	gcc -Wall -Wextra -g -c store.c

clean:
	rm -f server main.o store.o
```

Ça marche, et c'est lisible. Ses défauts, qu'on va corriger dans l'ordre :

1. `clean` n'est pas un fichier — c'est une **cible factice**, et ça pose un problème réel (§7).
2. `gcc -Wall -Wextra -g` est répété. Ajouter un flag = éditer partout, et en oublier un (§8).
3. Les noms de fichiers sont écrits deux ou trois fois chacun (§9).
4. Les règles `.o` sont identiques au nom près. Avec dix fichiers, dix copies (§10).
5. Si tu modifies `store.h`, **rien n'est recompilé**. C'est un vrai bug (§11).

---

## 7. Les cibles factices et `.PHONY`

`clean` n'est pas un fichier à produire ; c'est le nom d'une action. Make ne le sait pas — pour lui, `clean` est un fichier qui devrait apparaître après la recette.

Le jour où un fichier nommé `clean` existe dans ton dossier (ça arrive : un exécutable de test, un fichier créé par erreur), make raisonne ainsi : « la cible `clean` existe, elle n'a aucune dépendance, donc rien n'est plus récent qu'elle, donc elle est à jour ». Et tu obtiens :

```
make: 'clean' is up to date.
```

Ton `make clean` ne fait plus rien, sans erreur, et tu perds une heure.

La solution est de déclarer explicitement les cibles qui ne sont pas des fichiers :

```make
.PHONY: clean all re
```

`.PHONY` est une **cible spéciale** (elles commencent toutes par un point). Elle dit à make : « ces noms ne correspondent jamais à des fichiers, exécute toujours leur recette ».

Cibles factices conventionnelles, que tout correcteur s'attend à trouver :

| Cible | Rôle |
|---|---|
| `all` | construit tout — souvent la première cible du fichier |
| `clean` | supprime les fichiers intermédiaires (`.o`) |
| `fclean` | `clean` + supprime aussi l'exécutable |
| `re` | `fclean` puis `all` — reconstruction totale |

Le `-f` de `rm -f` signifie « ne râle pas si le fichier n'existe pas ». Sans lui, un `make clean` sur un dossier déjà propre échoue, et make s'arrête en erreur.

---

## 8. Les variables

Une variable en make, c'est du remplacement de texte. On la définit avec `=` ou `:=`, on l'utilise avec `$(NOM)`.

```make
CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = server
OBJS    = main.o store.o

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c
```

Maintenant, ajouter `-std=c11` se fait à **un seul endroit**.

### `=` contre `:=`

Différence réelle, qui mord un jour ou l'autre :

- `=` — **affectation différée**. La valeur est calculée au moment de l'*utilisation*.
- `:=` — **affectation immédiate**. La valeur est calculée à la *définition*.

```make
A = bonjour
B = $(A) le monde
A = salut

# $(B) vaut "salut le monde"  — avec = , A est relu à l'usage
```

```make
A := bonjour
B := $(A) le monde
A := salut

# $(B) vaut "bonjour le monde" — avec := , A a été figé à la définition
```

**Règle simple : utilise `:=` par défaut.** Le comportement est prévisible, et c'est celui que tu attends. Réserve `=` aux cas où tu veux vraiment de la paresse.

### Noms conventionnels

Ces noms ne sont pas obligatoires mais universellement compris (et certains sont pré-remplis par make) :

| Variable | Contenu |
|---|---|
| `CC` | le compilateur C (`gcc`, `clang`) |
| `CFLAGS` | les flags de compilation |
| `LDFLAGS` | les flags de linking (chemins de bibliothèques) |
| `LDLIBS` | les bibliothèques à lier (`-lpthread`, `-lm`) |
| `SRCS` | la liste des fichiers source |
| `OBJS` | la liste des fichiers objet |

### Dériver `OBJS` de `SRCS`

Écrire les deux listes à la main, c'est se garantir un oubli. Make sait transformer une liste :

```make
SRCS := main.c store.c net.c
OBJS := $(SRCS:.c=.o)
```

`$(SRCS:.c=.o)` est une **substitution de suffixe** : dans chaque mot de `SRCS`, remplace `.c` final par `.o`. `OBJS` vaut donc `main.o store.o net.o`. Une seule liste à maintenir.

> **Et `$(wildcard *.c)` ?** Ça existe, ça liste automatiquement tous les `.c`. Je te le déconseille sur ce projet : le jour où tu laisses traîner un `test_brouillon.c`, il est compilé et linké en silence, avec potentiellement un second `main`. Une liste explicite est un choix, pas une contrainte.

---

## 9. Les variables automatiques

Dans une recette, make définit des variables qui désignent les éléments de la règle en cours. Ce sont les plus utiles :

| Variable | Signifie |
|---|---|
| `$@` | le nom de la **cible** |
| `$<` | la **première** dépendance |
| `$^` | **toutes** les dépendances (doublons supprimés) |

Moyen mnémotechnique : `@` ressemble à une cible qu'on vise, `<` est ce qui *entre* dans la règle, `^` pointe vers *toute* la liste au-dessus.

Notre Makefile devient :

```make
$(TARGET): $(OBJS)
	$(CC) -o $@ $^

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@
```

Lis la première recette : « compilateur, sortie = *la cible*, entrée = *toutes les dépendances* ». Plus aucun nom de fichier n'est répété. Renommer `server` en `redis-bis` ne demande qu'à changer `TARGET`.

Attention à `-o $@` dans la règle de compilation : sans lui, `gcc -c src/main.c` écrirait `main.o` **dans le dossier courant**, pas à côté de la source. Sois explicite.

---

## 10. Les règles de motif

Les règles `.o` sont toutes identiques au nom près. Une **règle de motif** (*pattern rule*) les factorise, grâce au caractère `%` qui capture n'importe quelle chaîne :

```make
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
```

À lire : « pour fabriquer *n'importe quel* `X.o`, prends `X.c` et applique cette recette ». Le `%` capturé à gauche vaut le même texte qu'à droite.

Quand make cherche à construire `store.o` et ne trouve aucune règle explicite, il essaie ses règles de motif : `%` = `store`, dépendance = `store.c`, qui existe. Il applique.

Trois lignes remplacent désormais toutes les règles `.o`, quel qu'en soit le nombre. Le Makefile ne grossit plus quand le projet grossit.

> **Note :** make a des *règles implicites* prédéfinies qui savent déjà faire `.c` → `.o`. C'est pourquoi un Makefile minuscule fonctionne parfois par magie. Écris quand même ta règle explicite : la magie qu'on ne comprend pas se retourne toujours contre soi.

---

## 11. Le piège des fichiers `.h`

Voilà le vrai bug, celui qui coûte le plus de temps.

État actuel : `store.o` dépend de `store.c`. Mais `store.c` fait `#include "store.h"`, et `main.c` aussi.

Tu modifies `store.h` — tu changes un `struct`, disons, en ajoutant un champ. Tu lances `make` :

```
make: 'server' is up to date.
```

**Rien n'est recompilé.** Aucun `.c` n'a changé. Or `main.o` a été compilé avec l'ancienne définition du `struct` (ancienne taille, anciens décalages de champs) et `store.o` avec la nouvelle. Le linker, lui, ne voit que des adresses : il ne remarque rien.

Résultat : un exécutable qui se construit sans le moindre avertissement et qui corrompt sa mémoire à l'exécution. Segfault dans une fonction qui n'a pas changé, ou pire, valeurs silencieusement fausses. C'est exactement le genre de bug qui te fera douter de ton code alors que ton code est juste.

**Le réflexe de survie**, quand un comportement devient inexplicable après avoir touché un `.h` : `make re` (ou `make fclean && make`). Si le bug disparaît, tu tenais ça.

### Solution 1 — déclarer les headers à la main

```make
main.o:  main.c  store.h net.h
store.o: store.c store.h
```

Correct, mais tu dois maintenir ça à jour à chaque `#include` ajouté. Tu oublieras.

Version brutale mais honnête, acceptable sur un projet de cette taille :

```make
HEADERS := store.h net.h resp.h

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@
```

Tout `.o` dépend de tous les headers. Ça sur-compile (toucher un header recompile tout), mais c'est **toujours correct** — et sur 5 fichiers, la sur-compilation est invisible. Simple et juste bat astucieux et faux.

### Solution 2 — laisser gcc les trouver

`gcc -MMD -MP` génère, à côté de chaque `.o`, un fichier `.d` contenant la vraie règle de dépendance, déduite des `#include` réels :

```
main.o: main.c store.h net.h
```

Puis on demande à make de lire ces fichiers :

```make
CFLAGS := -Wall -Wextra -g -MMD -MP
DEPS   := $(OBJS:.o=.d)

-include $(DEPS)
```

- `-MMD` : génère le `.d` pendant la compilation (les `M` finaux ignorent les headers système, `<stdio.h>` ne changera pas).
- `-MP` : ajoute une cible bidon pour chaque header ; évite que make casse si tu **supprimes** un header.
- `-include` : inclut ces fichiers **s'ils existent**. Le tiret initial veut dire « ne te plains pas s'ils manquent » — au tout premier `make`, aucun `.d` n'existe encore.

C'est exact et automatique. C'est la vraie solution ; utilise-la quand la §14 te semblera confortable. N'oublie pas d'ajouter les `.d` au `clean`.

---

## 12. Les flags de compilation qui comptent

Les flags ne sont pas de la décoration. Sur ce projet — sockets, `malloc`, pointeurs — ils sont ta première ligne de défense.

| Flag | Effet | Verdict |
|---|---|---|
| `-Wall` | avertissements courants | **obligatoire** |
| `-Wextra` | avertissements supplémentaires (paramètre inutilisé, comparaison signé/non-signé…) | **obligatoire** |
| `-g` | infos de debug pour `gdb` / `valgrind` | **obligatoire** en dev |
| `-std=c11` | fige la version du langage | recommandé |
| `-Werror` | transforme tout avertissement en erreur | recommandé (discipline) |
| `-O2` | optimisations | pour la version finale, pas en debug |
| `-fsanitize=address` | détecte débordements et use-after-free à l'exécution | **à connaître** |

Sur `-Wall -Wextra` : en C, un avertissement n'est pas un détail de style. `implicit declaration`, `comparison between pointer and integer`, `control reaches end of non-void function` décrivent des bugs qui vont segfaulter. **Ton Makefile ne doit jamais produire un seul avertissement.** Si tu en tolères un, tu en toléreras vingt, et le vingt-et-unième sera celui qui comptait.

`-fsanitize=address` (ASan) mérite une cible dédiée. Il instrumente ton binaire pour qu'il s'arrête net, avec une trace lisible, au moment exact d'un débordement de buffer ou d'un `free` sur pointeur déjà libéré — au lieu de segfaulter mystérieusement trois fonctions plus loin. Il faut le passer **à la compilation et au linking** :

```make
debug: CFLAGS += -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: re
```

(Syntaxe : une variable définie devant une cible ne vaut que pour cette cible et ses dépendances.)

---

## 13. Débugger un Makefile

Quatre outils, dans l'ordre où tu t'en serviras :

**`make -n`** — affiche les commandes sans les exécuter (*dry run*). Le premier réflexe quand make ne fait pas ce que tu crois.

```sh
make -n
```

**`make -p`** — vide toute la base de données interne : variables, règles implicites, valeurs effectives. Verbeux, mais imbattable pour comprendre d'où sort une valeur.

**`$(info ...)`** — un `printf` de Makefile, évalué à la lecture du fichier :

```make
$(info OBJS vaut : $(OBJS))
```

Mets-le après tes définitions de variables quand une liste te semble fausse. Neuf fois sur dix, tu verras une liste vide et tu tiendras ton bug.

**`cat -A Makefile`** — pour l'éternelle question de la tabulation (§4).

Un dernier point à savoir : **chaque ligne de recette s'exécute dans un shell séparé.** Donc ceci ne marche pas :

```make
build:
	cd src
	gcc -c main.c      # ← s'exécute dans le dossier de départ, pas dans src/
```

Il faut une seule ligne logique, avec `&&` et un `\` de continuation :

```make
build:
	cd src && gcc -c main.c
```

---

## 14. Le Makefile cible du projet

Voici où tu dois arriver. **Ne le copie pas** : construis-le par étapes (§6 → §7 → §8 → §9 → §10 → §11), en vérifiant à chaque fois que `make` fonctionne encore. Un Makefile qu'on comprend ligne par ligne est un outil ; un Makefile copié est une superstition.

```make
CC      := gcc
CFLAGS  := -Wall -Wextra -Werror -std=c11 -g
LDFLAGS :=
LDLIBS  :=

TARGET  := server
SRCS    := main.c store.c net.c resp.c
OBJS    := $(SRCS:.c=.o)
HEADERS := store.h net.h resp.h

.PHONY: all clean fclean re

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(TARGET)

re: fclean all
```

Note au passage la ligne `fclean: clean` : une cible factice peut dépendre d'une autre. Make traite `clean` d'abord, puis la recette de `fclean`. C'est ainsi que `re` enchaîne `fclean` puis `all`.

### Exercice de lecture

Réponds sans lancer make :

1. Tu tapes `make` deux fois de suite. Combien de commandes `gcc` la deuxième fois ?
2. Tu modifies `resp.c`. Que se passe-t-il exactement ?
3. Tu modifies `net.h`. Que se passe-t-il exactement ?
4. Tu supprimes `store.o` à la main. Que se passe-t-il ?

<details>
<summary>Réponses</summary>

1. **Zéro.** Tous les `.o` sont plus récents que leurs `.c` et headers ; `server` est plus récent que tous les `.o`. `make: Nothing to be done for 'all'.`
2. `resp.o` est recompilé, puis `server` relinké. **Deux** commandes. Les trois autres `.o` sont intacts.
3. **Tous** les `.o` sont recompilés (ils dépendent tous de `$(HEADERS)`), puis relinkés. C'est la sur-compilation de la §11 — correcte, juste un peu généreuse. Avec `-MMD` (solution 2), seuls les `.o` qui incluent réellement `net.h` seraient touchés.
4. `store.o` n'existe plus → make le recompile depuis `store.c`, puis relinke. Make ne raisonne pas sur « ce qui a changé » mais sur « ce qui manque ou est périmé ».

</details>

---

## 15. Erreurs fréquentes

| Message / symptôme | Cause réelle |
|---|---|
| `missing separator` | espaces au lieu d'une tabulation en début de recette (§4) |
| `No rule to make target 'store.c', needed by 'store.o'` | fichier absent, ou faute de frappe dans `SRCS` |
| `undefined reference to 'store_get'` | le `.o` correspondant n'est pas dans `OBJS` — étape de **linking** (§2) |
| `make: 'clean' is up to date.` | `clean` non déclarée dans `.PHONY`, et un fichier `clean` existe (§7) |
| `make: Nothing to be done for 'all'.` | tout est à jour — souvent normal, mais suspect si tu viens de toucher un `.h` (§11) |
| segfault inexplicable après édition d'un `.h` | dépendances headers manquantes → `make re` pour confirmer (§11) |
| une variable semble vide | `$(info $(VAR))` pour vérifier ; souvent un `$` oublié ou `$VAR` au lieu de `$(VAR)` |
| `cd` sans effet dans une recette | chaque ligne = un shell neuf (§13) |

Un mot sur `$VAR` : en make, `$X` ne lit qu'**un seul caractère**. `$OBJS` est interprété comme `$(O)BJS` — la variable `O` (vide) suivie du texte `BJS`. **Toujours des parenthèses.**

Et si tu as besoin d'un `$` littéral dans une commande shell (variable du shell, pas de make), il faut le doubler : `$$`.

---

## 16. Glossaire

| Terme | Définition |
|---|---|
| **cible** (*target*) | ce qu'une règle produit ; en général un nom de fichier |
| **dépendance** (*prerequisite*) | fichier dont la cible est faite ; si plus récent, la recette rejoue |
| **recette** (*recipe*) | les lignes de commande d'une règle, précédées d'une tabulation |
| **cible factice** (*phony*) | cible qui n'est pas un fichier (`clean`, `all`) ; à déclarer dans `.PHONY` |
| **règle de motif** (*pattern rule*) | règle générique utilisant `%` pour couvrir une famille de fichiers |
| **fichier objet** (`.o`) | code machine compilé mais non lié ; références externes non résolues |
| **édition de liens** (*linking*) | assemblage des `.o` en un exécutable, résolution des symboles |
| **symbole** | nom d'une fonction ou variable globale, tel que vu par le linker |
| **variable automatique** | `$@`, `$<`, `$^` — désignent les éléments de la règle en cours |
| **`-MMD`** | option gcc générant les dépendances de headers dans un `.d` |

---

## À faire maintenant

1. Écris la version §6 à la main. Lance `make`, puis `make` une seconde fois, et **observe** qu'il ne fait rien.
2. Touche un `.c` (`touch store.c`), relance, observe quelles commandes sortent.
3. Fais évoluer ton fichier étape par étape jusqu'à la §14, en vérifiant après chaque changement.
4. Réponds à l'exercice de lecture §14 **avant** de regarder les réponses.

Quand tu as ton Makefile, montre-le-moi : on le relira ensemble.
