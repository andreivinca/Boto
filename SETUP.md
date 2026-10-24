# Cloud folders on a new computer

Every cloud account shows up as a normal folder, `~/Cloud/<account>`, mounted
automatically at login. Files are downloaded only when opened. It works with
any program; the Files extension in this repository is an optional extra.

How it works:

- **rclone** knows your accounts (stored in `~/.config/rclone/rclone.conf`) and
  does the mounting.
- **One systemd user service per account** (`nautilus-cloud@<account>.service`)
  runs `rclone mount` at every login and retries if you are offline.

## 1. Install rclone and FUSE

| System | Command |
| --- | --- |
| Arch | `sudo pacman -S rclone fuse3` |
| Fedora | `sudo dnf install rclone fuse3` |
| Debian, Ubuntu | `sudo apt install fuse3`, then rclone from rclone.org: `sudo -v; curl https://rclone.org/install.sh \| sudo bash` |

rclone must be **1.62 or newer** (`rclone version`). Ubuntu 24.04's own
package is 1.60, too old, hence the rclone.org installer.

## 2. Add your accounts

**Easiest: copy them from a computer that already has them.**

```sh
mkdir -p ~/.config/rclone
scp OLD-COMPUTER:.config/rclone/rclone.conf ~/.config/rclone/
chmod 600 ~/.config/rclone/rclone.conf
rclone config reconnect onedrive:     # OneDrive: sign in once on this computer
```

This file contains your logins. Copy it only over a secure channel (scp, a USB
stick), and never put it in Git or an email.

**Or create them again** with `rclone config`: `n` (new remote), give it a
name, pick the type, answer the questions. What each type needs:

| Account | Type | You enter | rclone stores |
| --- | --- | --- | --- |
| OneDrive | `onedrive` | sign in in the browser, pick the drive | `token`, `drive_id`, `drive_type` |
| Koofr | `koofr` | provider, user, app password | `provider`, `user`, `password` |
| FTP server | `ftp` | host, user, password | `host`, `user`, `pass` |
| Combine (optional) | `combine` | the other remotes to show together | `upstreams` |

A remote in `rclone.conf` looks like this (values shortened):

```ini
[onedrive]
type = onedrive
token = {"access_token":"…","refresh_token":"…"}
drive_id = 0123456789abcdef
drive_type = personal
```

Passwords in that file are only scrambled, not encrypted, which is why it gets
`chmod 600`. (rclone can encrypt the whole file with a password, but then the
services can't start on their own at login.)

Check: `rclone listremotes` lists your accounts, and `rclone lsd onedrive:`
shows the top folders of one.

## 3. Create the mount service (once per computer)

Save this as `~/.config/systemd/user/nautilus-cloud@.service`:

```ini
[Unit]
Description=Cloud folder %I

[Service]
Type=notify
ExecStartPre=mkdir -p %h/Cloud/%I
ExecStart=rclone mount --vfs-cache-mode full --cache-dir %C/nautilus-cloud-sidebar --vfs-cache-max-size 5G --vfs-cache-max-age 168h --vfs-cache-min-free-space 10G -- %I: %h/Cloud/%I
ExecStop=-fusermount3 -uz %h/Cloud/%I
Restart=on-failure
RestartSec=30s
TimeoutStopSec=60s

[Install]
WantedBy=default.target
```

Then run `systemctl --user daemon-reload`.

What it means:

| Part | Meaning |
| --- | --- |
| `%I` / `%h` / `%C` | the account name / your home folder / `~/.cache` |
| `Type=notify` | the service counts as started once the folder is ready |
| `mkdir -p %h/Cloud/%I` | creates `~/Cloud/<account>` |
| `--vfs-cache-mode full` | download a file only when it is opened, and keep it so it opens instantly next time |
| `--cache-dir %C/nautilus-cloud-sidebar` | where those copies are kept (`~/.cache/nautilus-cloud-sidebar`) |
| `--vfs-cache-max-size 5G`, `--vfs-cache-max-age 168h` | keep at most 5 GB per account, nothing older than a week |
| `--vfs-cache-min-free-space 10G` | never fill the disk below 10 GB free |
| `-- %I: %h/Cloud/%I` | mount account `<account>:` at `~/Cloud/<account>` |
| `ExecStop=… fusermount3 -uz` | unmount cleanly when stopped or at logout |
| `Restart=on-failure`, `RestartSec=30s` | offline at login? try again every 30 seconds |
| `WantedBy=default.target` | start at every login |

The name `nautilus-cloud@` is what the optional Files extension expects; keep it
so both can work together.

## 4. Turn it on for each account

```sh
systemctl --user enable --now nautilus-cloud@onedrive.service
```

Repeat for every account except a `combine` remote, which would only repeat
the others. For names with spaces or dashes, let systemd escape them:

```sh
systemctl --user enable --now "nautilus-cloud@$(systemd-escape 'My Drive').service"
```

## 5. Check

```sh
systemctl --user list-units 'nautilus-cloud@*'    # one "running" line per account
ls ~/Cloud/onedrive                               # your files
journalctl --user -u nautilus-cloud@onedrive      # the log, if something is wrong
```

## Everyday use

- **New account:** `rclone config`, then step 4.
- **Stop one:** `systemctl --user disable --now nautilus-cloud@<account>.service`.
- **Backups:** exclude `~/Cloud`, or the backup tool reads your clouds through it.
- **Settings for rclone** such as `RCLONE_CONFIG` go in `~/.config/environment.d/`,
  because the services don't see your shell's variables.

## Optional: the Files extension

It adds a **Cloud** section to the sidebar of GNOME Files and does steps 3 and 4
for you: it enables a service for every account it finds, and disables the
service of an account you remove. To install it, clone this repository and run
`./install.sh` (see the [README](README.md)). Its package brings its own copy of
the service, so afterwards delete yours,
`~/.config/systemd/user/nautilus-cloud@.service`, so you don't keep an old one.
