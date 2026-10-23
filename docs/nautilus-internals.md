# Nautilus internals the extension relies on

Nautilus has **no public sidebar extension API**. The extension finds its way
through Nautilus' private widget tree by type names, widget names, CSS classes,
properties and action names. Every one of them can change in a Nautilus or GTK
release. This page lists them all, says what happens when each breaks, and how
to re-check after an upgrade.

Verified against **Nautilus 51.1 / GTK 4.24.1** on 2026-10-23 (`make smoke`
passes, and the binary contains `slot.open-location`).

## Assumptions

| What | Used for | If it changes |
| --- | --- | --- |
| Extensions load from `$(pkg-config --variable=extensiondir libnautilus-extension-4)` (`/usr/lib/nautilus/extensions-4`); there's no user directory | Install location | Module not loaded; no "Cloud sidebar ready" log line |
| `libnautilus-extension-4` ABI: `nautilus_module_*` entry points, `NautilusMenuProvider` iface with `get_file_items` and `get_background_items` | Loading, plus a refresh hook when the background menu is built | Build fails, or the module isn't loaded |
| Window GType name **`NautilusWindow`** | Which windows to inject into | Never injects (silently) |
| Native sidebar is a **`GtkListBox`** with CSS class **`navigation-sidebar`** (or older **`places-sidebar-list`**) | Finding the sidebar | Retries 40 times, then warns "Nautilus sidebar layout was not recognized" |
| The sidebar list sits in a **`GtkScrolledWindow`** (optionally through a `GtkViewport`) **directly** | Wrapping list + Cloud group in one scrollable box | Same warning; nothing is changed |
| Sidebar rows (`NautilusGtkSidebarRow`) have a **string property `uri`** | Hiding native duplicates | Duplicates no longer hidden (harmless) |
| Slot GType name **`NautilusWindowSlot`**, with property **`location`** (`GFile`) and boolean **`active`** | Highlighting the open remote, refreshing on navigation | No highlight. Without `active`, the last mapped slot is used |
| Window property **`active-slot`** (object) | Refresh when the tab changes | Fewer refreshes; `notify::title` still covers most cases |
| Action **`slot.open-location`** (string URI) on the slot, or **`win.open-location`** on the window | In-place navigation on click | Warning "did not accept in-place navigation"; nothing opens |
| GTK private type **`GtkColumnViewTitle`** containing a `GtkLabel` | Copying the list-view header font and colour to the heading | Heading keeps its default style (0.82em bold, 40 % alpha) |
| Native rows are recreated or re-added when mounts change, and `items-changed` fires on the sidebar list | Re-hiding duplicates after mounts | Duplicates may show until the next refresh |
| Native row content follows **`nautilus-sidebar-row.ui`**: row → `GtkRevealer.sidebar-revealer` → box (spacing 0) → icon (margin-end 8) + label (margin-end 2, middle ellipsis). The sidebar has CSS name `placessidebar`, where libadwaita pads `row > revealer` by 14 px horizontally; rows are at least 36 px high with a 6 px side margin | Our rows copy that structure (`create_row`), so the theme aligns them; the heading indent (20 = 6 + 14) assumes it | Our icons and labels drift out of line with native rows. Compare with `gresource extract /usr/bin/nautilus /org/gnome/nautilus/ui/nautilus-sidebar-row.ui` and update `create_row`, `HEADING_INDENT` and the fake rows in `tests/test-sidebar.c` |

## Nautilus My Computer cooperation

Nautilus My Computer (a nautilus-python extension, tested with 0.13.3,
installed here at `~/.local/share/nautilus-python/extensions/nautilus-my-computer.py`)
also rewraps the sidebar. Rules:

- If a widget named **`sidebar_my_computer_wrapper`** exists, append the Cloud
  group to it; it must be a `GtkBox`.
- If `~/.local/share/nautilus-python/extensions/nautilus-my-computer.py` exists,
  wait up to 12 attempts (~3 s) for that wrapper before wrapping ourselves. The
  path is hard-coded to `~/.local/share`, not `$XDG_DATA_HOME`, because that's
  where nautilus-python loads it from.
- Never pick My Computer's own list (`sidebar_my_computer_listbox`) as the
  native sidebar.
- Never replace a scrolled-window child that isn't the bare native list: some
  other extension owns it.

## How GIO decides which mounts get native rows

This matters for duplicate hiding and for tests:

- The unix volume monitor ignores **bind mounts** (mount root ≠ `/`), "system
  internal" filesystems such as **tmpfs** and proc, and mount points under a
  **dot directory** (`~/.local/...`).
- Our mounts at `~/Cloud/<remote>` are FUSE mounts under the home directory
  outside dot directories, so GIO lists them and Nautilus adds a native row for
  each. Duplicate hiding (TR-R1) removes those rows; it also covers rclone mounts
  the user made elsewhere, for example `~/OneDrive`.
- `/tmp` is tmpfs on the development machine, so test mounts there are never
  listed.

## After a Nautilus or GTK upgrade

1. `make clean && make && make smoke`. PASS means injection, duplicate hiding,
   selection and diagnostics still work inside the real Nautilus.
2. If it fails, read the `--- extension log` section of the output:
   - no `Cloud sidebar ready` and a "layout was not recognized" warning means
     the sidebar structure changed;
   - nothing at all means the module wasn't loaded (ABI or extension directory
     changed: check `pkg-config --modversion libnautilus-extension-4` and
     `nautilus --version`).
3. Inspect the real tree with the GTK Inspector: `GTK_DEBUG=interactive nautilus --new-window`.
   That needs a Files instance not already running (`nautilus -q` first, which
   closes the user's windows; ask before doing it). Look for the sidebar's
   `GtkListBox`, its CSS classes, the row type and its `uri` property, and the
   slot type and its properties.
4. Check action names: `strings /usr/bin/nautilus | grep open-location`.
5. Update `src/nautilus/cloud-nautilus.c`, the only file that should need
   changes: the type and widget names at its top, `is_native_sidebar`,
   `cloud_nautilus_prepare_sidebar`, the slot, row and action functions. Mirror
   the change in the fake Nautilus types in `tests/test-sidebar.c`, and update
   this page.

## Reference: Nautilus source locations (upstream, for orientation)

- Module loader: `src/nautilus-module.c` (loads from `NAUTILUS_EXTENSIONDIR`,
  `G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL`, one instance per listed type).
- Sidebar: `src/gtk/nautilusgtkplacessidebar.c`, `src/gtk/nautilusgtksidebarrow.c`
  (the `uri` property).
- Slot: `src/nautilus-window-slot.c` (`location`, `active`, the `slot` action group).
- Window: `src/nautilus-window.c` (`active-slot`, the `win` action group).
