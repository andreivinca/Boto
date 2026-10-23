# Business requirements

## Problem

People who keep files in several cloud accounts (Google Drive, OneDrive, S3,
FTP servers, Koofr, …) often already use **rclone** to reach them, because one
tool covers every provider. GNOME Files has no good way to show those accounts:
GNOME Online Accounts supports few providers, and mounting rclone remotes by hand
means remembering commands, mount points and cache flags, then finding the
mount in the sidebar among other drives.

## Goal

Make every configured rclone remote a first-class place in GNOME Files: always
visible in the sidebar, one click to open, with no extra configuration and no
copies of the data on disk.

## Users

- Linux desktop users who run GNOME Files (Nautilus 51) under GNOME or another
  desktop, including tiling setups such as Hyprland/Omarchy.
- They have configured one or more remotes with `rclone config`, possibly an
  encrypted config, and may also use a `combine` remote that merges accounts.
- They may run other sidebar extensions, notably **Nautilus My Computer**.

## Business goals and requirements

| ID | Requirement | Why |
| --- | --- | --- |
| BR-1 | Show a **Cloud** section in the Files sidebar listing every rclone remote by its rclone name. | One place to reach all accounts. |
| BR-2 | Need **zero configuration** beyond `rclone config`: no hardcoded account names, paths or service names, no settings file. | Adding an account in rclone is enough; nothing to keep in sync. |
| BR-3 | Pick up added, removed or changed remotes **automatically** while Files is open (within about 30 s). | Users should not restart Files after editing rclone. |
| BR-4 | Leave out `combine` remotes. | They duplicate the accounts they combine. |
| BR-5 | **One click opens a remote** in the current tab, mounting it on demand if it is not mounted yet. | The main interaction; no terminal needed. |
| BR-6 | **Reuse mounts that already exist** (started by the user, a script, or an earlier click). | No duplicate mounts or caches. |
| BR-7 | **Never sync or download the whole remote**; browse on demand and cache only writes (bounded: 2 GB, 24 h). | Cloud accounts can be far larger than the local disk. |
| BR-8 | Mounts and **pending uploads survive closing Files** where the desktop allows it (systemd user session). | Closing a window must not lose data that is still uploading. |
| BR-15 | **Always there, like OneDrive on Windows:** every remote is mounted at `~/Cloud/<remote>` at login, for every application, without opening Files or clicking; files are still fetched on demand and opened ones stay cached. | The cloud folder should behave like a normal folder. |
| BR-9 | **Never read or expose credentials.** Only remote names and types are read; authentication stays in rclone. | Privacy and security; works with encrypted configs. |
| BR-10 | **Look native**: the heading matches Files' own column-header style, rows look like sidebar rows, the open remote is highlighted, and Nautilus' own row for the same mount is hidden. | The feature should feel built in, with no duplicate entries. |
| BR-11 | **Fail safe**: if rclone is missing, slow or broken, keep the last good list and keep Files working; if a Files update changes its layout, show no Cloud section rather than break Files. | An extension must never take Files down. |
| BR-12 | **Coexist with Nautilus My Computer** without changing its source. | The user runs both. |
| BR-13 | Tell the user plainly when a remote cannot be opened ("Could not connect to *name*"). | Failures must be visible, not silent. |
| BR-14 | Be **easy to verify and maintain** across Files upgrades (automated tests, headless smoke test against the installed Files). | Files has no public sidebar API, so upgrades can break the extension. |

## Out of scope (non-goals)

- Creating, editing or authenticating rclone remotes (use `rclone config`).
- Two-way sync, offline copies, or conflict handling.
- An unmount button or mount management UI (unmount with `fusermount3 -u`).
- Per-provider icons or account-usage display.
- Translations: "Cloud" and the error dialog are English-only.
- Desktops or file managers other than GNOME Files 51, and non-Linux systems.

## Success criteria

- After `sudo make install` and restarting Files, every non-combine remote
  appears under **Cloud**, sorted case-insensitively, within a few seconds.
- Clicking a remote opens it in place; a second click while it mounts does not
  start a second mount.
- With the remote mounted, Nautilus' native row for that mount is hidden and the
  Cloud row is highlighted while browsing inside it.
- Breaking rclone (unreachable config, missing binary) leaves the sidebar
  unchanged and Files responsive.
- `make check` and `make smoke` pass on the target system.
