# Nautilus Cloud Sidebar — developer documentation

Start here when picking the project up in a new session. The user-facing
README (install, behaviour summary) is [`../README.md`](../README.md).

| Document | Read it when |
| --- | --- |
| [business-requirements.md](business-requirements.md) | You need to know *why* the extension exists, who it is for, what is in and out of scope. |
| [technical-requirements.md](technical-requirements.md) | You change behaviour. Every requirement has an ID, the code that implements it, and the test that covers it. |
| [architecture.md](architecture.md) | You need the code map, layers, threading model, object lifetimes, or where to change what. |
| [nautilus-internals.md](nautilus-internals.md) | Nautilus or GTK was upgraded, or the sidebar stopped appearing. Lists every private assumption. |
| [testing.md](testing.md) | You run, extend or debug the tests, or verify a change by hand. |
| [decisions.md](decisions.md) | You wonder why something is done a particular way before "fixing" it. |
| [status.md](status.md) | You want the current state, open items, known gaps, environment facts and working notes for this machine. |

## One-paragraph summary

A native C module for GNOME Files (Nautilus 51, `libnautilus-extension-4`)
that adds a **Cloud** section to the sidebar listing every rclone remote. A
worker thread runs `rclone listremotes --json` every 30 seconds; the GTK side
injects a list into Nautilus' private sidebar widget tree, hides Nautilus' own
duplicate rows, highlights the remote currently open, and on click either
navigates to an existing mount or starts `rclone mount` (as a systemd transient
unit when possible).

## Quick commands

```sh
make                 # build libnautilus-cloud-sidebar.so
make check           # unit + fake-window tests, headless (~5 s)
make smoke           # the module inside the real installed Nautilus, sandboxed (~20 s)
sudo make install    # into $(pkg-config --variable=extensiondir libnautilus-extension-4)
nautilus -q          # restart Files to load it (closes the user's Files windows)
```
