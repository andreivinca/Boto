# Nautilus Cloud Sidebar

Adds a small **CLOUD** section to GNOME Files. It automatically lists every
configured rclone remote, using the remote's name. Combined (`combine`) remotes
are omitted to avoid duplicating the individual accounts. No account names, paths, or
service names are hardcoded, and no locations configuration file is needed.

Discovery runs off the UI thread using `rclone listremotes --json` (with a plain
list fallback for older versions). It respects rclone's normal configuration,
including `RCLONE_CONFIG`, and scans every 30 seconds. Failed scans retain the
last successful list. Adding or removing a remote updates open Files windows.

Every remote is mounted at `~/Cloud/<remote name>` (for example
`~/Cloud/onedrive`) at login, like a OneDrive folder on Windows: on systemd
desktops each remote gets a user service, `nautilus-cloud@<remote>.service`,
which the extension enables when it first sees the remote and disables when the
remote is removed. The folders are there for every application, even before
Files is opened. Clicking a remote opens it in the current tab, mounting it
first if it isn't (other desktops mount on click with rclone's daemon mode).
Existing rclone FUSE mounts elsewhere are reused.

Files are fetched on demand, never synchronized as a whole. Opened files stay
cached under `$XDG_CACHE_HOME/nautilus-cloud-sidebar/` (defaults to `~/.cache/`),
at most 5 GB and one week per remote, and never below 10 GB of free disk. The
cloud section hides duplicate native drive rows and highlights the currently
open remote. Backup tools that walk your home folder should exclude `~/Cloud`,
or they will read the remotes through the mounts.

## Installation

To set up the cloud folders on another computer with just rclone and systemd,
no extension needed, follow [SETUP.md](SETUP.md).


Add your accounts with `rclone config` first, then from a clone of this
repository:

```sh
./install.sh
```

The script installs the extension and restarts Files; from then on every
remote is mounted at `~/Cloud/<remote>`, now and at every login.

- **Arch Linux:** it builds a pacman package from `packaging/arch/PKGBUILD`
  (makepkg installs missing dependencies and asks before installing), so
  `pacman` upgrades and removes it like any other package. Running
  `./install.sh` again after `git pull` upgrades it.
- **Other Linux distributions:** it builds with `make` and installs with
  `sudo make install`. If build dependencies are missing it stops and prints
  the command that installs them:

  | Distribution | Dependencies |
  | --- | --- |
  | Debian, Ubuntu | `sudo apt install build-essential pkg-config libnautilus-extension-dev libgtk-4-dev libjson-glib-dev fuse3`, and rclone from rclone.org: `curl https://rclone.org/install.sh \| sudo bash` |
  | Fedora | `sudo dnf install gcc make pkgconf-pkg-config nautilus-devel gtk4-devel json-glib-devel rclone fuse3` |
  | openSUSE | `sudo zypper install gcc make pkg-config nautilus-devel gtk4-devel json-glib-devel rclone fuse3` |

  It needs GLib 2.80 and GTK 4.12 or newer (Ubuntu 24.04, current Fedora);
  older releases such as Debian 12 can't build it. rclone must be 1.62 or
  newer; Ubuntu 24.04's own package (1.60) is too old, so the script checks.

`./install.sh --check` only reports what is missing and what it would do;
`./install.sh --make` uses make even on Arch.

Nautilus loads native extensions only from its system extension directory
(`pkg-config --variable=extensiondir libnautilus-extension-4`, usually
`/usr/lib/nautilus/extensions-4`), so installation needs root. It also installs
the user unit template `nautilus-cloud@.service` (usually in
`/usr/lib/systemd/user`).

Check the mounts with `systemctl --user list-units 'nautilus-cloud@*'`; logs are
in `journalctl --user -u nautilus-cloud@<remote>`. To keep one remote from
mounting, remove it from rclone or mount it yourself elsewhere. Units run with
your user manager's environment, so set things like `RCLONE_CONFIG` in
`~/.config/environment.d/`.

To uninstall, which stops the mounts and removes the extension (your files and
`~/Cloud` stay):

```sh
./install.sh --uninstall
```

Built against Nautilus 51.1. Sidebar injection uses private GTK widget structure
because Nautilus has no public sidebar extension API; a Nautilus upgrade may
require adapting it. Coexists with Nautilus My Computer 0.13.3 without changing
its source. Authentication is handled by rclone; an encrypted config must be
unlocked through rclone's normal environment before starting Files.

## Testing

```sh
make check   # unit tests, and the module in a fake Nautilus window
make smoke   # the module inside the installed Nautilus
```

Both run headless on a private Broadway display (`gtk4-broadwayd`) with a fake
`rclone`, so no windows appear and your accounts and running Files are not
touched. `make smoke` starts the real Nautilus in private user, mount and D-Bus
namespaces where its extension directory holds only the freshly built module,
which makes it a quick check after a Nautilus upgrade. It needs unprivileged user
namespaces and util-linux 2.38 or later. The 30-second rescan test is skipped by
default; run it with
`tests/run-sidebar-test.sh build/test-sidebar "$PWD/libnautilus-cloud-sidebar.so" -m slow`.

Set `NAUTILUS_CLOUD_DIAGNOSTICS=/path/to/file.json` before starting Files to
have the extension write the state of its in-process widgets there after each
refresh.

Requirements, architecture, design decisions and maintenance notes are in
[`docs/`](docs/README.md).
