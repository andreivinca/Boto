# Status, open items and session notes

Last updated: **2026-10-23**.

## Current state

- The module is complete and split into layers (`src/core`, `services`,
  `nautilus`, `ui`, plus `cloud-extension.c` and `module.c`); see
  [architecture.md](architecture.md).
- `make` builds with no warnings under `-Wall -Wextra -Wshadow
  -Wmissing-prototypes -Wstrict-prototypes -Wformat=2` (GCC 16.2.1).
- `make check`: 38 results pass: 25 core tests, plus 13 GTK tests, one of
  which (slow) is skipped by default and passes with `-m slow`.
- `make smoke`: PASS inside the installed Nautilus 51.1.
- ASan/UBSan: clean on both suites; no leaks in `core/`; GCC `-fanalyzer`:
  no findings.
- Layout follows the user's screenshot feedback ([decisions.md](decisions.md#d16)):
  equal space above and below "Cloud", and rows aligned with Nautilus' own.
- **Mounted at login** (D19): one `nautilus-cloud@<remote>` unit per remote is
  enabled and running on the development machine, mounting `~/Cloud/<remote>`. The template is installed at
  `/usr/lib/systemd/user/nautilus-cloud@.service`.
- **Installed** on 2026-10-23 at `/usr/lib/nautilus/extensions-4/libnautilus-cloud-sidebar.so`
  (via `pkexec`, whose password dialog Omarchy's shell provides). Files was restarted and
  logged `Cloud sidebar ready: …` with the user's three remotes. After rebuilding,
  reinstall the same way, then run `nautilus -q`.
- **Published:** `main` on the public repository github.com/andreivinca/Boto;
  release **v1.0.0** (source only). Keep account names and personal paths out of
  the repository.

## Installing (the user runs this: root and a Files restart)

```sh
sudo make install
nautilus -q
```

Then go through the manual checklist in [testing.md](testing.md#manual-checks).

## Open items / known gaps

| Item | Notes |
| --- | --- |
| Clicking a row in the **real** Nautilus is untested | Navigation (`slot.open-location`), the mount flow and the alert were only checked in the fake window. The action name exists in the 51.1 binary. Do the manual checklist after installing. |
| A real `rclone mount` and systemd transient unit are untested | Tests use fake `rclone`/`systemd-run` that only record argv. |
| New remotes while Files is closed | Their unit is enabled the next time Files runs (D19). A `.path` unit watching `rclone.conf` could do it without Files, if ever wanted. |
| Logging out and in with automount | Not yet observed on this machine; see the manual checklist. |
| Grandchild holding rclone's stdout would stall scans | See [decisions.md](decisions.md#d6). |
| `g_warning` under `G_DEBUG=fatal-warnings` | Would abort Files on scan failures; see [decisions.md](decisions.md#d11). |
| GTK 5 | The per-widget CSS provider (D10) and possibly the sidebar structure will need rework. |

## Ideas (not requested; ask before doing)

- Select the most specific matching remote instead of the first (see D9).
- Unmount item in a row context menu.
- Translations for "Cloud" and the alert.
- A PKGBUILD (`makepkg` package) so installation needs no `sudo make install`.
- Meson build if it's ever distributed.

## Environment facts (development machine)

| Thing | Value |
| --- | --- |
| OS | Arch Linux, kernel 7.2, Omarchy (Hyprland, Wayland) |
| Nautilus | 51.1 (`libnautilus-extension-4` 51.1), extension dir `/usr/lib/nautilus/extensions-4` |
| GTK / GLib / json-glib / libadwaita | 4.24.1 / 2.90.1 / 1.10.8 / 1.10.0 |
| rclone | v1.75.2 (`listremotes --json` prints `[` … `]`, one object per line; an empty config prints an empty array) |
| Toolchain | gcc 16.2.1, GNU make, pkg-config; **no meson or ninja** |
| Test tools | `gtk4-broadwayd`, `dbus-run-session`, util-linux 2.42.4 `unshare` (user namespaces work unprivileged), `chromium` (headless screenshots) |
| Filesystems | `/tmp` is **tmpfs**; `/home` is btrfs |
| Other Files extensions | nautilus-python with `nautilus-my-computer.py` (My Computer), localsend, transcode; system modules for image properties, papers and totem |
| rclone config | Several remotes including one `combine` remote (don't put account names in the repo) |

## Notes for future sessions in this environment

- **GateGuard / "Fact-Forcing Gate" hook.** A PreToolUse hook blocks:
  - the first Bash command of a session;
  - the first Write or Edit to each file;
  - any command it considers destructive (`rm`, `kill`, …).

  Each block is lifted by stating the facts it asks for: what calls or uses the
  file, the API or data schema affected, a rollback for deletes, and the user's
  instruction **quoted verbatim**. The first write to a new file is always
  rejected once, even when the facts were stated just before.
- **graphify:** `~/CLAUDE.md` asks for graphify queries and updates, but this
  project has no graph (`~/graphify-out/` has no `graph.json`), so skip them
  unless the user builds one.
- **Don't touch the user's running Files.** `nautilus -q` closes their windows,
  and running `nautilus` on their session bus opens a window in their instance.
  Ask first, or use `make smoke`, which is fully isolated.
- **Root:** sudo needs a password here. Ask the user to run it, e.g. `! sudo make install`.
- `.delta/` in the repo root belongs to another tool (worktrees and clones). It's
  excluded through `.git/info/exclude`; leave it alone.
- clangd errors about missing GLib/GTK headers mean `compile_flags.txt` is
  missing; run `make compile_flags.txt`.
- Background job scratch space: use `$CLAUDE_JOB_DIR/tmp`, not `/tmp`. Tests
  create their own temporary directories and remove them on success; a
  crashed test leaves `/tmp/cloud-sidebar-*` behind (safe to delete).
