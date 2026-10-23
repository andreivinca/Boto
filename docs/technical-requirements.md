# Technical requirements

Each requirement lists where it is implemented and how it is verified. Code
paths are relative to `src/` (see [architecture.md](architecture.md) for the
layers). Tests: `C:` = `tests/test-core.c`, `S:` = `tests/test-sidebar.c`,
`smoke` = `tests/run-nautilus-smoke.sh`, `manual` = not automated (see
[testing.md](testing.md#manual-checks)).

This document is the specification. Change behaviour here first, then in code
and tests; record the reasoning in [decisions.md](decisions.md).

## Platform and build

| ID | Requirement |
| --- | --- |
| TR-P1 | Native module for the **libnautilus-extension-4** ABI (Nautilus 43+; built and tested against **51.1**). Exports exactly `nautilus_module_initialize`, `nautilus_module_shutdown`, `nautilus_module_list_types` (`module.c`); everything else is hidden (`-fvisibility=hidden`). |
| TR-P2 | GObject types `CloudScanner`, `CloudDragTracker`, `CloudSection`, `CloudWindow` and `CloudSidebarExtension` are registered dynamically on Nautilus' `GTypeModule`, and are final. Nautilus instantiates only `CloudSidebarExtension`, which implements `NautilusMenuProvider` (no menu items). |
| TR-P3 | Dependencies: GTK ≥ 4.12 (`gtk_css_provider_load_from_string`, `gtk_list_box_remove_all`; `gtk_widget_get_color` and `GtkAlertDialog` need 4.10), GLib/GIO ≥ 2.80 (`g_strv_builder_take`), json-glib 1.x, libnautilus-extension-4. Runtime: `rclone`, FUSE3; optionally a systemd user manager (`systemctl`) for mounting at login. Tests also need libadwaita-1 (installed with Nautilus). |
| TR-P4 | Build with `make` (GNU make + pkg-config, C11 `gnu11`). Clean under `-Wall -Wextra -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wformat=2`, with automatic header dependencies. Output `libnautilus-cloud-sidebar.so`. `make install` puts it in `$(pkg-config --variable=extensiondir libnautilus-extension-4)` and the unit template `data/nautilus-cloud@.service` in `$(pkg-config --variable=systemduserunitdir systemd)`, honouring `DESTDIR`; `make uninstall` removes both. |
| TR-P5 | Log domain `nautilus-cloud-sidebar`. `g_message` for the "ready" line and mount failures, `g_warning` for scan and layout failures. |
| TR-P6 | Layering (see [architecture.md](architecture.md#layers-and-rules)): `core/` builds and links without GTK, and Nautilus-private names appear only in `nautilus/cloud-nautilus.c`. |

## Discovery (worker thread, `core/`)

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-D1 | Run `rclone listremotes --json` with stdin on `/dev/null`, stdout and stderr captured, and the inherited environment (so `RCLONE_CONFIG`, `RCLONE_PASSWORD_COMMAND` and the like apply). | `cloud_rclone_list_remotes`, `cloud_command_run` | C: /rclone/list-json, /command/run |
| TR-D2 | Kill rclone after **10 s**; that scan fails. A timeout must not leak descriptors or memory (kill the child, then let communicate finish). | `cloud_command_run`, `CLOUD_RCLONE_TIMEOUT_SECONDS` | C: /command/run (counts `/proc/self/fd`) |
| TR-D3 | If the JSON call exits non-zero **and** stderr contains `unknown flag`, fall back to `rclone listremotes` and take each line (split on CR/LF) that ends in `:` as a remote of type `""`. Any other non-zero exit fails the scan. | `cloud_rclone_list_remotes`, `cloud_rclone_parse_remotes_plain` | C: /rclone/list-plain-fallback, /rclone/parse-plain, /rclone/list-failure |
| TR-D4 | The JSON must be an array of objects with a string `name`; `type` is used if it is a string, else `""`. Anything else fails the scan. Read **only** `name` and `type`, never credentials. | `cloud_rclone_parse_remotes_json` | C: /rclone/parse-json, /rclone/parse-json-invalid |
| TR-D5 | Leave out remotes of type `combine`. | `cloud_discovery_build_locations` | C: /discovery/build-locations, /discovery/scan; S: injection; smoke |
| TR-D6 | Default mount path = **`~/Cloud/<remote name>`** (`g_get_home_dir()`); a remote named `.` or `..` uses `~/Cloud/<id>` instead, so no mount point escapes `~/Cloud`. Location id = first **20 hex chars of SHA-256(name as UTF-8)**; it names only internal things (systemd unit, log file). | `cloud_discovery_mount_directory`, `default_mount_point`, `cloud_location_identifier` | C: /discovery/build-locations, /discovery/mount-points, /location/identifier; smoke |
| TR-D7 | **Reuse existing mounts.** (a) In `/proc/self/mountinfo`, an entry with fstype `fuse.rclone` and source `name:` gives mount point → `name`; fields are `\ooo`-unescaped. (b) A process whose `/proc/<pid>/comm` is `rclone`, whose cmdline (each argument valid UTF-8) has `mount` followed by `name:` and a path, where that path is a mount point (TR-O1), gives path → `name`. (b) overrides (a). Unreadable entries are skipped. | `cloud_mounts_find_rclone`, `cloud_mounts_parse_mountinfo`, `cloud_mounts_unescape` | C: /mounts/mountinfo, /mounts/unescape, /mounts/rclone-processes; S: open-mounted |
| TR-D8 | Sort locations by `g_utf8_casefold(name)` with a **stable** sort, so names that differ only by case keep rclone's order. | `cloud_location_list_sort` | C: /discovery/build-locations |
| TR-D9 | A location carries `name`, `remote` (`name:`), `type`, `id`, `path`, `file` (GFile) and `uri`. It is immutable after `cloud_location_new` and atomically ref-counted, so it can be built on the worker thread and shared by widgets and pending mounts. | `CloudLocation` | C: /location/new |

## Scanning schedule (`services/cloud-scanner`)

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-S1 | Scan once at start, then every **30 s**. Never run two scans at once: skip a tick while one is running. | `cloud_scanner_start`, `scan`; `SCAN_INTERVAL_SECONDS` in `cloud-extension.c` | S: rescan (slow) |
| TR-S2 | Scan on a dedicated thread named `rclone-remote-discovery`; deliver the result back with an idle callback (`G_PRIORITY_DEFAULT_IDLE`). | `scan_thread`, `scan_finished` | S: injection |
| TR-S3 | A failed scan keeps the current list (and logs a warning). The first successful scan, even an empty one, and every later one whose ordered list of `(name, path, type)` differs, replace the list and emit `changed`; then automounting is synced (TR-A2) and every window is repopulated and refreshed. | `scan_finished`, `cloud_location_list_equal`, `locations_changed` (extension) | C: /location/list-equal; S: rescan |

## Sidebar injection

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-U1 | On the first idle after construction, get the default `GtkApplication` (do nothing if there is none), connect `window-added`/`window-removed`, schedule every existing window, and start the scanner. | `start` (extension) | S: injection |
| TR-U2 | Handle only windows whose GType name is `NautilusWindow`, each once (not when already injected or pending). Try every **250 ms**, at most **40** times, then warn "Nautilus sidebar layout was not recognized". Stop if the window leaves the application. | `schedule`, `attempt_inject` (extension), `cloud_nautilus_is_window` | S: injection, second-window |
| TR-U3 | The native sidebar is the first `GtkListBox` in pre-order with CSS class `navigation-sidebar` or `places-sidebar-list`, not named `sidebar_my_computer_listbox` or `cloud-locations`. | `is_native_sidebar` (nautilus) | S: injection; smoke |
| TR-U4 | If a widget named `sidebar_my_computer_wrapper` exists, append the section to it (it must be a `GtkBox`, else warn and give up). Otherwise, while `~/.local/share/nautilus-python/extensions/nautilus-my-computer.py` exists and fewer than **12** attempts have run, wait for My Computer to wrap first. Otherwise find the ancestor `GtkScrolledWindow`; its child (through a `GtkViewport`) must be exactly the native list, never an unknown wrapper. Replace it with a vertical `GtkBox` named `cloud-sidebar-wrapper` holding the native list, then the section. | `cloud_nautilus_prepare_sidebar`, `wrap_sidebar` | S: injection; smoke |
| TR-U5 | The section (`cloud-sidebar-group`, vertical box) holds: a horizontal separator (margins 12/12/6/6); a label **"Cloud"** (`cloud-section-heading`, xalign 0, margin-start 20 so it starts in the row icons' column, margin-top 4, no bottom margin); and a `GtkListBox` `cloud-locations` with class `navigation-sidebar`, single selection, activated on single click, and **no top padding**. The space between the heading and the first row equals the space between the separator and the heading (13 px with Nautilus 51.1's theme). The default heading style (`font-size: 0.82em; font-weight: bold; color: alpha(currentColor, 0.4)`) and the list padding come from a display-wide stylesheet that matches only these widget names. | `cloud_section_init`, `SECTION_CSS`, `add_stylesheet` | S: injection, heading-spacing |
| TR-U6 | One row per location, **built like Nautilus' own rows** (`nautilus-sidebar-row.ui`), so the theme gives both the same padding and height: row (tooltip = name, no focus on click) → `GtkRevealer.sidebar-revealer` (revealed) → box (spacing 0) → 16 px `folder-remote-symbolic` icon (margin-end 8) + label (xalign 0, hexpand, ellipsized in the middle, margin-end 2). Icons and labels line up with Nautilus' rows. The section is hidden when there are no locations. Rows are rebuilt on every accepted scan. | `cloud_section_set_locations`, `create_row` | S: injection, rows-align-with-native, rescan |
| TR-U7 | Match the heading to the first `GtkColumnViewTitle` with a non-empty label: font family (CSS-escaped string, left out if unset), size (`pt`, or `px` if absolute), weight and colour. Reload the CSS only when it changes. | `cloud_nautilus_find_column_title`, `cloud_section_copy_heading_style` | S: heading-matches-columns |
| TR-U8 | Log `Cloud sidebar ready: <names>` when injected. | `add_window` (extension) | smoke |
| TR-U9 | **Drag feedback.** The Cloud rows accept no drops, so while a drag is over the sidebar (the box holding the native list and our section) they are made insensitive (greyed out), as Nautilus does for its own rows that can't take a drop. They return when the drag leaves the sidebar (re-checked on the next idle, because Nautilus' list has its own drop target that makes GTK report false leaves) or, for drags from Files itself, when the drag finishes or is cancelled. Native rows are untouched. | `CloudDragTracker` (`ui/cloud-drag-tracker.c`), `dragging_changed` (window), `cloud_section_set_dimmed` | S: drag-dims-rows; manual |

## Refresh (keeping the sidebar in step, `ui/cloud-window`)

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-R1 | **Hide duplicates.** Hide every `GtkListBoxRow` in the window that has a string `uri` property whose `GFile` equals a location's file. Count them (`hidden_duplicates`). Show a hidden row again when its URI is no longer a location, and drop rows that have left the window. | `hide_duplicates`, `cloud_nautilus_list_place_rows`, `cloud_nautilus_row_get_uri` | S: injection, rescan, release-closed; smoke |
| TR-R2 | **Highlight the open remote.** Active slot = the first `NautilusWindowSlot` whose boolean `active` is true, else the last mapped one. Its `location` (GFile) selects the **first location in sorted order** whose file equals it or is a prefix of it; then unselect every other `GtkListBox` in the window. Otherwise unselect the Cloud list. | `follow_location`, `cloud_location_list_find_containing`, `cloud_nautilus_get_active_slot` | C: /location/find-containing; S: selection-follows-location; smoke |
| TR-R3 | Refresh on: native list `items-changed`, window `notify::title` and `notify::active-slot` (if that property exists), each slot's `notify::location` and `notify::active` (at `G_PRIORITY_LOW`), `get_background_items`, accepted scans, and after navigation. Refreshes are idle callbacks holding a reference to the `CloudWindow`. | `cloud_window_new`, `watch_slots`, `cloud_window_queue_refresh`, `cloud_nautilus_watch_*` | S: selection-follows-location |
| TR-R4 | Refresh must not re-enter itself. It watches each new slot once and releases slots that have left the window (closed tabs). Emits `refreshed` at the end. | `refresh`, `watch_slots` | S: release-closed |
| TR-R5 | When a window is removed from the application, close its `CloudWindow`: disconnect every handler, drop all widget references, and make pending idles and mount callbacks no-ops. | `window_removed` (extension), `cloud_window_close` | S: second-window |

## Opening a remote

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-O1 | "Is mounted": `lstat(path)` succeeds and is not a symlink, and `lstat(path/..)` is on a different device or is the same inode; if `path/..` cannot be looked up, `stat()` the textual parent instead. | `cloud_mounts_is_mount` | C: /mounts/is-mount |
| TR-O2 | Click on a mounted location → **navigate**: activate `slot.open-location` then `win.open-location`, on the active slot then the window, with the location URI as a string parameter; the first that accepts wins, then refresh. Otherwise warn "Nautilus did not accept in-place navigation to <name>". | `open_location`, `navigate` (window), `cloud_nautilus_open_location` | S: open-mounted |
| TR-O3 | Click on an unmounted location → **mount**, unless that remote is already connecting in this window. Create the mount directory and `$XDG_CACHE_HOME/nautilus-cloud-sidebar` (mode 0777 & umask). | `open_location` (window), `cloud_mounter_mount_async` | S: mount-daemon |
| TR-O4 | rclone options, the same with and without systemd: `--vfs-cache-mode full --cache-dir <cache> --vfs-cache-max-size 5G --vfs-cache-max-age 168h --vfs-cache-min-free-space 10G` (opened files stay cached, per remote at most 5 GB and a week, and never below 10 GB free on the disk). `<cache>` is `$XDG_CACHE_HOME/nautilus-cloud-sidebar`. | `data/nautilus-cloud@.service`, `cloud_rclone_mount_argv` | C: /rclone/mount-argv, /rclone/unit-matches-argv |
| TR-O5 | If systemd units are available (TR-A1) and the location mounts at `~/Cloud/<name>`, run `systemctl --user start nautilus-cloud@<escaped name>.service`, which returns once rclone reports the mount ready or failed. Otherwise run rclone itself as a daemon: `rclone mount --daemon --daemon-wait 20s <TR-O4 options> --log-file <cache>/<id>.log -- <name>: <path>`. | `mount_command` (mounter), `cloud_rclone_mount_argv` | C: /rclone/mount-argv; S: mount-systemd, mount-daemon |
| TR-O6 | Spawn with stdout and stderr silenced and wait asynchronously. Succeed only if the command exits 0 **and** the path is now a mount point; then navigate. Otherwise log the error and show an alert "Could not connect to <name>" / "Please check your connection and try again." Clear the connecting flag either way; if the window has closed, do nothing else. | `mount_exited` (mounter), `mounted`, `show_connection_error` (window) | S: mount-daemon, mount-systemd |

## Mounting at login (systemd)

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-A1 | Units are **available** when `systemctl` is on `PATH`, the user manager runs (`<runtime>/systemd/private` exists, `runtime` = `$XDG_RUNTIME_DIR` if set, else `/run/user/<uid>`), and the template `nautilus-cloud@.service` is installed (`~/.config/systemd/user`, `/etc/systemd/user`, the build's unit dir, `/usr/local/lib/systemd/user` or `/usr/lib/systemd/user`). Without them everything else still works by click (TR-O5 daemon mode). | `cloud_systemd_available`, `cloud_systemd_unit_installed` | C: /systemd/unit-installed |
| TR-A2 | **Sync:** whenever the scanner emits `changed`, enable *and start* (`systemctl --user enable --now --no-block`) the unit of every location that mounts at `~/Cloud/<name>`, and disable *and stop* (`disable --now --no-block`) every enabled `nautilus-cloud@*.service` whose remote is no longer listed. Remotes mounted elsewhere by the user, combine remotes and remotes named `.`/`..` get no unit. Never waits; failures are logged. | `cloud_automount_sync`, `cloud_systemd_plan`, `cloud_systemd_list_enabled` | C: /systemd/plan, /systemd/list-enabled |
| TR-A3 | **Unit template** `data/nautilus-cloud@.service`: instance = remote name escaped like `systemd-escape` (`cloud_systemd_escape`); `Type=notify`; `ExecStartPre=mkdir -p %h/Cloud/%I`; `ExecStart=rclone mount <TR-O4 options> -- %I: %h/Cloud/%I`; `ExecStop=-fusermount3 -uz %h/Cloud/%I`; `Restart=on-failure`, `RestartSec=30s`, `TimeoutStopSec=60s`; `WantedBy=default.target`, so enabled remotes mount at every login. Logs go to the journal (`journalctl --user -u nautilus-cloud@<remote>`). | `data/nautilus-cloud@.service`, `cloud_systemd_escape`, `cloud_systemd_unit_name` | C: /systemd/escape, /systemd/unit-name, /rclone/unit-matches-argv; `systemd-analyze --user verify` |

## Diagnostics

| ID | Requirement | Code | Test |
| --- | --- | --- | --- |
| TR-X1 | If `NAUTILUS_CLOUD_DIAGNOSTICS` is set and non-empty, write (atomically) after every refresh a JSON array with one object per open window, in injection order. | `cloud_diagnostics_report` | S: diagnostics, second-window; smoke |

Example:

```json
[
  {
    "heading" : "Cloud",
    "names" : [ "Mounted", "Profile", "Unmounted" ],
    "rows" : 3,
    "hidden_duplicates" : 1,
    "location" : "file:///…/home/Cloud/Mounted/Documents",
    "selected" : [ "Mounted" ]
  }
]
```

## Non-functional requirements

| ID | Requirement |
| --- | --- |
| NFR-1 | The UI thread never waits on rclone or the network. Only small local file operations (`mkdir`, `lstat` when a row is clicked or a mount finishes) run on it. |
| NFR-2 | No memory or descriptor leaks in steady state or on failure paths (checked with ASan and leak detection on `core/`, ASan and UBSan on the GTK test, GCC `-fanalyzer`, and an fd count on timeout). |
| NFR-3 | Never crash Files: every lookup of private structure checks the type or property (name **and** value type) first, and backs off if it doesn't match. |
| NFR-4 | Privacy: never open the rclone config file (rclone reads it); write diagnostics only when asked. Units run with the user manager's environment, so settings like `RCLONE_CONFIG` belong in `~/.config/environment.d/`. |
| NFR-5 | Thread safety: the worker thread runs only `core/` code on its own data; `CloudLocation`s are immutable once built. GTK is used only on the main thread. |
| NFR-6 | Tests run headless and isolated (Broadway, private runtime dir, fake rclone), with no windows on the desktop, no real accounts, and no effect on the running Files. |
| NFR-7 | Maintainability: one responsibility per file, layered dependencies (TR-P6), and documented public headers. |
