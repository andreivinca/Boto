# Architecture

## Repository layout

```
Makefile                        build, check, smoke, install, uninstall, compile_flags.txt (clangd)
install.sh                      one-step install/uninstall: pacman package on Arch, make elsewhere
packaging/arch/                 PKGBUILD (builds this checkout) and pacman install messages
data/
  nautilus-cloud@.service       systemd user unit template: mounts one remote at ~/Cloud/<remote> at login
src/
  module.c                      Nautilus entry points; registers every GObject type
  cloud-extension.{c,h}         the extension object: tracks Files windows, wires layers together
  ui/                           GTK: what the user sees, per window
    cloud-section.{c,h}         CloudSection widget: separator, "Cloud" heading, rows, selection, heading style
    cloud-window.{c,h}          CloudWindow controller: refresh, duplicate hiding, selection, opening/mounting
    cloud-drag-tracker.{c,h}    CloudDragTracker: "dragging" while a drag is over the sidebar
    cloud-diagnostics.{c,h}     NAUTILUS_CLOUD_DIAGNOSTICS report
  nautilus/                     the only code that knows Nautilus' private widget structure
    cloud-nautilus.{c,h}
  services/                     background work behind main-thread APIs (no GTK)
    cloud-scanner.{c,h}         CloudScanner: periodic discovery on a worker thread, "changed" signal
    cloud-mounter.{c,h}         cloud_mounter_mount_async/finish (GTask): start a remote's unit, or rclone --daemon
    cloud-automount.{c,h}       keep one enabled unit per remote, so remotes mount at login
  core/                         plain GLib/GIO/json-glib: no GTK, thread-safe, unit-tested
    cloud-location.{c,h}        CloudLocation model, identifier, list sort/compare/match
    cloud-command.{c,h}         run a command with captured output and a timeout
    cloud-rclone.{c,h}          rclone CLI: list remotes (JSON/plain), parsers, mount command line
    cloud-mounts.{c,h}          mount points, mountinfo, running `rclone mount` processes
    cloud-discovery.{c,h}       one scan = remotes + existing mounts → sorted locations
    cloud-systemd.{c,h}         unit names (systemd-escape), availability, which units to enable/disable
tests/
  test-core.c                   unit tests for src/core (linked without GTK)
  test-sidebar.c                loads the built .so into a fake Nautilus window tree
  run-sidebar-test.sh           runs test-sidebar on a private headless Broadway display
  run-nautilus-smoke.sh         runs the installed Nautilus with only this module, in namespaces
docs/                           this documentation
```

Build outputs (`build/`, `libnautilus-cloud-sidebar.so`, `compile_flags.txt`) are git-ignored.

## Layers and rules

```
 module.c ─► cloud-extension ─► ui ─────► nautilus      (GTK, main thread)
                     │           │
                     ▼           ▼
                   services ◄────┘                       (GObject/GIO, main-thread API)
                     │
                     ▼
                   core                                  (GLib only, any thread)
```

| Layer | May use | Must not | Why |
| --- | --- | --- | --- |
| `core/` | GLib, GIO, json-glib, other `core/` | GTK, Nautilus, global state, the main context | Runs on the discovery thread; unit-tested without a display. `test-core` links only `gio-2.0` and `json-glib-1.0`, so a GTK call there fails to link. |
| `services/` | `core/`, GObject/GIO | GTK | Turns blocking `core/` work into main-thread events: a signal (scanner) or a `GAsyncResult` (mounter). |
| `nautilus/` | GTK | `core/`, `services/`, `ui/` | One file to update when Nautilus changes. It knows type names, widget names, CSS classes, properties and actions, and nothing about clouds. |
| `ui/` | GTK, `nautilus/`, `services/`, `core/` | Nautilus' private names (go through `nautilus/`) | Product behaviour: what to show, hide, select and open. |
| `cloud-extension` | everything above | private names | Wiring only: window discovery, injection retries, fan-out of location changes, diagnostics. |
| `module.c` | type registration | anything else | The ABI surface: three exported functions. |

Conventions for every file:
- One responsibility per file, named `cloud-<thing>.{c,h}`; public functions are `cloud_<thing>_…`.
- Headers say what each function does and who owns the results; `.c` files
  keep helpers `static`. `-Wmissing-prototypes` enforces that.
- GObject types use `G_DECLARE_FINAL_TYPE` plus
  `G_DEFINE_DYNAMIC_TYPE_EXTENDED (…, G_TYPE_FLAG_FINAL, …)`, and expose
  `cloud_<thing>_register_types (GTypeModule *)`, called from `module.c`.
- Public functions on GObjects start with `g_return_*_if_fail` type checks.
- Nautilus-private strings appear only in `nautilus/cloud-nautilus.c`. Widget
  names this extension adds (`cloud-sidebar-wrapper`, `cloud-sidebar-group`,
  `cloud-section-heading`, `cloud-locations`) are defined once in
  `nautilus/cloud-nautilus.h`, because other extensions and our own layout code
  look for them.

## How Nautilus loads it

1. At startup Nautilus `g_module_open`s every `.so` in
   `/usr/lib/nautilus/extensions-4`. There is no per-user directory for C modules.
2. It calls `nautilus_module_initialize (GTypeModule *)`. `module.c` registers
   `CloudScanner`, `CloudDragTracker`, `CloudSection`, `CloudWindow` and `CloudSidebarExtension`
   on that module, then `nautilus_module_list_types` lists the extension type.
3. Nautilus creates **one** `CloudSidebarExtension` for the life of the
   process. That's often days, because Files runs as a D-Bus service, so leaks
   and retained widgets matter.
4. The extension's `init` creates the scanner and schedules `start` on the
   next idle, when the `GtkApplication` exists.

## Runtime flow

```
CloudSidebarExtension (main thread)
 ├─ start: connect GtkApplication window-added/-removed, schedule existing windows, cloud_scanner_start
 ├─ schedule(window) → every 250 ms, ≤40×: cloud_nautilus_prepare_sidebar
 │      READY → add_window: cloud_window_new + cloud_window_set_locations, log "Cloud sidebar ready"
 ├─ CloudScanner "changed" → cloud_automount_sync (enable/disable units) + cloud_window_set_locations
 ├─ CloudWindow "refreshed" → cloud_diagnostics_report
 ├─ window-removed → cloud_window_close, drop it
 └─ get_background_items → cloud_window_queue_refresh on every window

CloudScanner
 └─ every 30 s (skips while busy): thread "rclone-remote-discovery" → cloud_discovery_scan
        → idle back on main: if the list differs, replace it and emit "changed"

cloud_discovery_scan (worker thread, core/)
 ├─ cloud_rclone_list_remotes → cloud_command_run (rclone listremotes --json, 10 s) [plain fallback]
 ├─ cloud_mounts_find_rclone → mountinfo + /proc/*/cmdline
 └─ cloud_discovery_build_locations → skip combine, paths, cloud_location_list_sort

CloudWindow (one per Files window)
 ├─ triggers: native list items-changed, window notify::title/active-slot,
 │            slot notify::location/active (low priority), set_locations, after navigation
 ├─ refresh: copy heading style → watch/release slots → hide duplicates → follow location → "refreshed"
 ├─ CloudDragTracker on the sidebar box: notify::dragging → cloud_section_set_dimmed
 └─ CloudSection "activate-location":
        mounted? → cloud_nautilus_open_location
        else     → cloud_mounter_mount_async (systemctl --user start nautilus-cloud@<remote>,
                   or rclone --daemon) → navigate, or alert "Could not connect to …"

At login (no Files needed)
 └─ systemd user manager → every enabled nautilus-cloud@<remote>.service → rclone mount at ~/Cloud/<remote>
```

## Module reference

| File | Public API |
| --- | --- |
| `core/cloud-location` | `CloudLocation` (immutable, atomic refcount: `name remote type id path file uri`); `cloud_location_new/ref/unref`, `cloud_location_identifier`, `cloud_location_contains`; `cloud_location_list_new/sort/equal/find_containing` |
| `core/cloud-command` | `cloud_command_run (argv, timeout, out, err, successful, error)` |
| `core/cloud-rclone` | `CloudRemote`; `cloud_rclone_list_remotes`, `cloud_rclone_parse_remotes_json/plain`, `cloud_rclone_mount_argv (location, cache_dir, use_systemd, environment)` (pure) |
| `core/cloud-mounts` | `cloud_mounts_is_mount`, `cloud_mounts_unescape`, `cloud_mounts_parse_mountinfo`, `cloud_mounts_find_rclone` |
| `core/cloud-discovery` | `cloud_discovery_mount_directory`, `cloud_discovery_build_locations`, `cloud_discovery_scan` |
| `core/cloud-systemd` | `cloud_systemd_escape`, `_unit_name`, `_unit_installed`, `_available`, `_mounts_location`, `_wants_directory`, `_list_enabled`, `_plan` |
| `services/cloud-scanner` | `CloudScanner`: `cloud_scanner_new (interval)`, `_start`, `_get_locations`; signal `changed` |
| `services/cloud-mounter` | `cloud_mounter_mount_async/finish` (creates directories, starts the remote's unit or rclone in daemon mode, checks the mount) |
| `services/cloud-automount` | `cloud_automount_sync (locations)` |
| `nautilus/cloud-nautilus` | `cloud_nautilus_is_window`, `_prepare_sidebar` → `CloudSidebarStatus`, `_watch_window/_watch_slot`, `_list_slots`, `_get_active_slot`, `_slot_get_location`, `_list_place_rows`, `_row_get_uri`, `_find_column_title`, `_unselect_lists`, `_open_location`; widget-name constants |
| `ui/cloud-section` | `CloudSection` (GtkBox): `_new`, `_set_locations`, `_get_n_rows`, `_select`, `_get_selected`, `_get_list`, `_set_dimmed`, `_copy_heading_style`; signal `activate-location (CloudLocation *)` |
| `ui/cloud-drag-tracker` | `CloudDragTracker`: `_new (area)`, `_get_dragging`, `_stop`; property `dragging` |
| `ui/cloud-window` | `CloudWindow`: `_new (window, native, container)`, `_get_window`, `_set_locations`, `_queue_refresh`, `_close`, diagnostics getters; signal `refreshed` |
| `ui/cloud-diagnostics` | `cloud_diagnostics_report (windows, locations)` |
| `cloud-extension` | `CloudSidebarExtension` (`NautilusMenuProvider`), `cloud_sidebar_extension_register_types` |

## Where to change what

| To change… | Edit |
| --- | --- |
| rclone mount flags, cache limits | `data/nautilus-cloud@.service` **and** `cloud_rclone_mount_argv` in `core/cloud-rclone.c` (`/rclone/unit-matches-argv` keeps them equal) |
| unit behaviour (restart, stop, login) | `data/nautilus-cloud@.service` |
| which remotes mount at login | `cloud_systemd_plan` in `core/cloud-systemd.c` |
| how remotes are listed or parsed | `core/cloud-rclone.c` |
| which existing mounts are reused | `core/cloud-mounts.c` |
| filtering (combine), sorting, mount directory | `core/cloud-discovery.c`, `core/cloud-location.c` |
| scan interval, injection retries | `cloud-extension.c` constants |
| rclone timeout | `CLOUD_RCLONE_TIMEOUT_SECONDS` in `core/cloud-rclone.h` |
| when systemd units are used | `cloud_systemd_available` in `core/cloud-systemd.c` |
| anything Nautilus renamed or restructured | `nautilus/cloud-nautilus.c` only (+ fake types in `tests/test-sidebar.c`, `docs/nautilus-internals.md`) |
| the section's look, rows, heading style | `ui/cloud-section.c` |
| duplicate hiding, selection, click behaviour, alert text | `ui/cloud-window.c` |
| when rows grey out during drag-and-drop | `ui/cloud-drag-tracker.c` (detection), `cloud_section_set_dimmed` (look) |
| diagnostics format | `ui/cloud-diagnostics.c` (+ smoke-test expectations) |

## Object lifetimes (read before changing signal code)

- **`CloudLocation`** is immutable and atomically ref-counted. The scanner's
  array, every `CloudSection` and `CloudWindow` that shows it, and pending
  mounts each hold references. Arrays are replaced, never modified.
- **`CloudWindow`** is the unit of lifetime for everything per window:
  - Idle refreshes and mount callbacks hold a ref to it.
  - It connects to Nautilus objects with
    `g_signal_connect_object (…, cloud_window, G_CONNECT_SWAPPED)`.
  - `cloud_window_close` (called on `window-removed`, and from `dispose`)
    disconnects them all and drops the widget refs; afterwards `window == NULL`
    turns any late idle or mount callback into a no-op.
- **`CloudSection`** maps row *i* to `locations[i]`. There is no per-row data.
  It holds the locations array it shows.
- **Slots and hidden native rows** are held strongly in `CloudWindow`, so
  pointers stay valid, and released on the next refresh after they leave the
  window. Otherwise closed tabs, with their views, would stay alive.
- **Wrapping the native list** (`cloud_nautilus_prepare_sidebar`) takes a ref
  on it before `gtk_scrolled_window_set_child`, which destroys the old viewport.
- `gtk_widget_observe_children` models are cached and shared by GTK, so
  `CloudWindow` disconnects from the one it uses explicitly.

## Threading

- Only `CloudScanner`'s `scan_thread` runs off the main thread. It calls
  `cloud_discovery_scan` (pure `core/`), which builds new, immutable
  `CloudLocation`s, including their `GFile`. It hands the array back with
  `g_idle_add_full`.
- `cloud_command_run` pushes a **private** `GMainContext` as the thread default,
  so GSubprocess callbacks and the timeout run on the worker. On timeout it
  kills the child and lets communicate finish; see
  [decisions.md](decisions.md#d6).
- `cloud_mounter_mount_async` is called on the main thread and never blocks
  (`g_subprocess_wait_check_async`).
