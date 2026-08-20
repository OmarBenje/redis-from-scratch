# redis-from-scratch

A Redis server written in C from the socket layer up — libc only, no dependencies.
The goal is not to reimplement Redis. It is to understand, line by line, what a
network server actually does: file descriptors, the listening socket, the read
loop, and the wire protocol.

## Status — step 1 of a longer build

What the 89 lines of `main.c` do today:

- open a TCP listening socket on port 6379, with `SO_REUSEADDR` set before `bind`
- `bind` / `listen` / `accept` one client
- loop on `read()` into a 4 KB buffer
- walk a RESP2 array in place: argument count, then each `$<len>\r\n<payload>` element
- reply `+PONG\r\n` — a RESP simple string — which the official `redis-cli` accepts

What it does **not** do yet, stated plainly so nobody has to read the source to find out:

- no key-value store: there is no data structure behind the protocol
- no command dispatch: every command is answered `+PONG`, including `SET`
- one client at a time: a single `accept`, no event loop, no `poll`/`epoll`/`kqueue`
- no partial-message handling: each `read()` is parsed on its own, so a command split
  across two TCP segments is not yet reassembled. This is the next thing to fix, and
  it is the reason the read loop is written the way it is.

## Build and run

```sh
make          # gcc -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined
./server      # listens on 127.0.0.1:6379
redis-cli -p 6379 PING
```

The Makefile compiles with `-Werror` and links AddressSanitizer and
UndefinedBehaviorSanitizer in the default target — memory and UB bugs fail loudly
during development rather than silently in production.

## Why the docs are the biggest part of this repository

`docs/cours/` holds ~54 KB of notes I wrote for myself while building this: one on
POSIX sockets and file descriptors, one on `make`. `docs/journal/` is a dated
engineering journal kept while working.

They are longer than the code on purpose. The point of the exercise is to be able to
re-derive the design without looking it up — the notes are the artefact that proves
whether that worked.

Both are written in French.

## Attribution

All C in this repository was typed by me. Claude was used as a tutor — explaining
POSIX sockets and `make` — and to run commits, which is why three early commits carry
an automatic `Co-Authored-By` trailer. No C was generated.

The course notes in `docs/cours/` are my own, written to re-derive the parser design
unaided.
