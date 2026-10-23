# Design decisions

Short records of choices that aren't obvious from the code. Read the relevant
one before "fixing" something that looks odd.

<a id="d1"></a>
## D1: Native C module with GLib/GNOME idioms

The extension is a native `libnautilus-extension-4` module in C. The code
follows GNOME C conventions (4-space indent, a space before call parentheses,
`g_auto*` cleanups, GObject boilerplate macros) so it reads like Nautilus' own
extensions. It must be installed system-wide (see [D14](#d14)).

<a id="d2"></a>
## D2: Plain Makefile, not Meson

Meson and Ninja aren't installed on the development machine, and the project is
small. The Makefile uses pkg-config for flags, honours `CFLAGS`/`LDFLAGS`/`DESTDIR`,
builds into `build/` with automatic header dependencies, and can generate
`compile_flags.txt` for clangd. Switch to Meson if this is ever packaged for a
distribution.

<a id="d3"></a>
## D3: A layered source tree

The code is split by responsibility into layers with one-way dependencies:
`core/` (GLib-only logic) ← `services/` (scanner, mounter) ← `ui/` (section
widget, per-window controller, diagnostics) → `nautilus/` (private-structure
adapter), wired together by `cloud-extension.c` and registered by `module.c`.
Rules and the file map are in [architecture.md](architecture.md#layers-and-rules).
What it buys:
- `core/` is unit-tested without a display and is checked to need no GTK;
- a Nautilus upgrade touches one file;
- each GObject owns its own state;
- the mount command line is a pure function with exact tests.

Sharper parts of the split:
- **`CloudWindow` is a GObject**: its references are the lifetime of everything
  per window (see [D7](#d7)).
- **`CloudSection` is a GtkBox subclass** emitting `activate-location`. Row *i*
  shows `locations[i]`, so rows carry no data.
- **The mounter is a GTask async API** (`cloud_mounter_mount_async/finish`), so
  callers only see success or an error. "Mounted" means rclone exited 0 *and*
  the path is a mount point.
- **The scanner is a GObject with a `changed` signal**, so the extension only
  fans out location lists.
- **`CloudLocation` is immutable**, `GFile` and URI included, and is built
  entirely on the worker thread.

<a id="d4"></a>
## D4: A dedicated discovery thread, not GTask's pool

The scanner runs on a thread of its own named `rclone-remote-discovery`
(`g_thread_try_new`). A scan can block for up to 2 × 10 s; GTask's shared thread
pool also runs Nautilus' own file operations, so occupying it was avoided.

<a id="d5"></a>
## D5: Subprocess timeout on a private main context

`g_subprocess_communicate` (synchronous) has no timeout. `cloud_command_run`
pushes its own `GMainContext`, starts the asynchronous communicate, attaches a
timeout source, and iterates until it finishes.

<a id="d6"></a>
## D6: On timeout, kill the child; never cancel communicate

Cancelling the `GCancellable` makes communicate return on its first failed
operation while its other operations (two pipe reads and the wait) stay pending
on a context nobody iterates again. That leaks about **4 file descriptors** plus
memory per timeout, and a permanently hanging rclone would leak one set every
30 s. So the timer calls `g_subprocess_force_exit` and sets a flag; communicate
completes normally once the pipes close, and the scan reports
`G_IO_ERROR_TIMED_OUT`. `/command/run` counts descriptors to keep it fixed.
**Caveat:** if rclone left a grandchild holding its stdout open, the scan would
wait for that grandchild, and scanning would stop meanwhile. rclone doesn't do
that for `listremotes`.

<a id="d7"></a>
## D7: Callbacks hold the per-window object, never raw pointers

All per-window state lives in a `CloudWindow` GObject:
- idle refreshes and mount callbacks hold a reference to it;
- handlers on Nautilus objects use
  `g_signal_connect_object (obj, sig, cb, cloud_window, G_CONNECT_SWAPPED)`;
- `cloud_window_close` (on `window-removed`) disconnects them all and drops its
  widget references; anything still queued sees `window == NULL` and does
  nothing.

There are no back-pointers to the extension, no qdata on Nautilus' widgets, and
no lookups by raw pointer. See
[architecture.md](architecture.md#object-lifetimes-read-before-changing-signal-code).

<a id="d8"></a>
## D8: Release closed tabs and dropped rows

`CloudWindow` holds strong references to the slots it watches and the native
rows it hides, so the pointers stay valid. On every refresh it drops entries
that have left the window; otherwise closed tabs would stay alive with their
views. A dropped hidden row is made visible first, in case Nautilus reuses it.
Covered by `/sidebar/release-closed`.

<a id="d9"></a>
## D9: Deliberate behaviours

These could be argued with; they're intended:
- Selection uses the **first** location in sorted order that equals or contains
  the current folder, not the most specific one. It only matters if mount points
  are nested.
- The My Computer check looks at `~/.local/share/nautilus-python/...` and ignores
  `$XDG_DATA_HOME`, because that's where nautilus-python loads My Computer from.
- `start` gives up silently if there is no default `GtkApplication` when it
  first runs.
- A refresh is queued whenever Nautilus builds a background context menu
  (`get_background_items`), which is the only `NautilusMenuProvider` hook used.
- English-only strings ("Cloud", the alert texts).

<a id="d10"></a>
## D10: Static look in a display stylesheet; only the copied heading style per widget

The section's fixed styling (the heading's default look and the list's top
padding) is a display-wide stylesheet at application priority (`SECTION_CSS`
in `ui/cloud-section.c`). Its selectors match only our own widget names
(`label#cloud-section-heading`, `list#cloud-locations`). This is GTK 4's
recommended way, and it overrides the theme.

The heading also copies the list header's font and colour at runtime, which can
differ per window, so that part uses a per-widget provider at user priority.
GTK 4 deprecated per-widget providers (4.10) without a replacement. It's wrapped
in `G_GNUC_BEGIN/END_IGNORE_DEPRECATIONS` and will need rework for GTK 5 (e.g.
extend the display stylesheet with a per-window widget name).

<a id="d11"></a>
## D11: Logging levels

`g_message` for "Cloud sidebar ready" and mount failures; `g_warning` for scan
failures, unrecognised layout, navigation refused, and diagnostics write errors.
**Caveat:** under `G_DEBUG=fatal-warnings` a `g_warning` aborts Files, so a
broken rclone would crash it on the next scan. Lower scan failures to
`g_message` if that ever matters.

<a id="d12"></a>
## D12: Defensive parsing and output

Small and invisible in normal use:
- Malformed rclone JSON (non-array, missing name) fails the scan, and malformed
  mountinfo lines are skipped. A bad scan never stops later scans.
- `font-family` is written as an escaped CSS string, and left out when unset.
- Diagnostics are written atomically (`g_file_set_contents`).
- `cloud_mounts_is_mount` falls back to the textual parent when `path/..` can't
  be looked up (a mount root without execute permission).
- Plain `listremotes` output is split on CR and LF.
- `\ooo` unescaping handles only 000–377, so values stay bytes.

<a id="d13"></a>
## D13: XDG directories

Data and cache directories come from `g_get_user_data_dir()` and
`g_get_user_cache_dir()`, which ignore an empty `XDG_*_HOME`. The runtime
directory for the systemd check is `$XDG_RUNTIME_DIR` if set, else
`/run/user/<uid>`, because that's where the user manager's socket lives.
`g_get_user_runtime_dir()` would fall back to the cache directory instead.

<a id="d14"></a>
## D14: System-wide installation only

Nautilus loads C modules only from its compiled-in extension directory, so
`make install` needs root and there is no per-user installation.

<a id="d15"></a>
## D15: Testing with real GTK, not mocks

GTK behaviour (reparenting, viewports, selection, CSS, action muxing) is what
breaks, so the tests run the real module on a real GTK display (Broadway,
headless) with fake Nautilus types styled by libadwaita, plus a smoke run of
the real Nautilus in user and mount namespaces. See [testing.md](testing.md).

<a id="d16"></a>
## D16: Layout matches Nautilus' rows (user request)

On 2026-10-23 the user pointed out, from screenshots, that:
- there was twice as much space below "Cloud" as above it (25 px vs 13 px);
- our icons and labels sat left of Nautilus' rows (icons by 6 px, labels by 2 px).

The earlier layout used its own numbers: box margins 8, spacing 12, heading
margins 18/4/6. Changes:
- **Rows copy Nautilus' row template** (`nautilus-sidebar-row.ui`): a revealer
  that the theme pads (`placessidebar .navigation-sidebar > row > revealer {
  padding: 0 14px }`), icon margin-end 8, label margin-end 2, middle
  ellipsizing. The theme now lays both kinds of row out identically, including
  after theme changes, instead of us copying numbers.
- **Heading:** no bottom margin, and the list has no top padding, so the gaps
  above and below "Cloud" are equal. Margin-start 20 puts it in the icon column.

Verified in the real Nautilus 51.1 (screenshots of the sandboxed instance taken
through Broadway and headless Chromium). The gaps are now 13/13 px and the
icons and labels share columns with the native rows. Guarded by
`/sidebar/rows-align-with-native` and `/sidebar/heading-spacing`, which style
the fake window with libadwaita as Nautilus does.

<a id="d17"></a>
## D17: Grey out the Cloud rows during drag-and-drop (user request)

The user saw other sidebar rows grey out while dragging a folder from the file
view, while ours stayed normal, which suggested they might accept the drop.
Our rows have no drop target, so drops on them are already refused; what was
missing was the feedback.

- **Same mechanism as Nautilus:** rows that can't take a drop become insensitive
  (`gtk_widget_set_sensitive`), so they look exactly like Nautilus' own
  invalid targets.
- **Detection** is a `GtkDropControllerMotion` on the sidebar box, wrapped in
  `CloudDragTracker` (a `dragging` property), so `CloudWindow` only maps it to
  `cloud_section_set_dimmed`.
- **False leaves:** Nautilus' list has its own `GtkDropTarget`, and GTK reports
  a leave on the ancestor's controller when the pointer crosses into it. So a
  leave is re-checked on the next idle with `contains_pointer`. A drag that
  started in Files is also followed until `dnd-finished` or `cancel`, so the
  rows can't stay grey after a drop. The My Computer extension uses the same
  approach.
- **Accepting drops** (copy into a remote) was not requested; it would need a
  mounted remote and a `GtkDropTarget` per row.

<a id="d18"></a>
## D18: Mount points at `~/Cloud/<remote>` (user request)

Mounts used to go to `~/.local/share/nautilus-cloud-sidebar/mounts/<id>`, with a
SHA-256 fragment as the folder name. That showed up as an unreadable path in
Files' path bar. On 2026-10-23 the user chose `~/Cloud/<remote name>`, the
usual place for cloud folders (like `~/Dropbox`, `~/OneDrive`): the path bar
reads *Home › Cloud › onedrive*.

- rclone remote names are already safe folder names (letters, digits, space,
  `_ - . + @`, no `/`). Only `.` and `..` would point outside `~/Cloud`; those
  fall back to the id.
- The id still names the systemd unit (`nautilus-cloud-<id>`) and the log file,
  where readability doesn't matter and unit-name escaping would.
- Being under the home folder outside a dot directory, the mounts get native
  sidebar rows from GIO; duplicate hiding (TR-R1) removes them.
- **Caveat:** tools that walk the home folder (backups, `find ~`, indexers set to
  recurse into `~`) would read the remotes through the mounts. The README tells
  users to exclude `~/Cloud`.
- Existing mounts elsewhere are still reused (TR-D7), so a remote mounted at
  the old location keeps that path until it is unmounted.
- Migration on the development machine: idle old mounts were stopped, the
  empty old mount points and the user's three empty `~/Cloud/<Display Name>`
  folders were removed, and the remotes remount at `~/Cloud/<remote>` on the
  next click.

<a id="d19"></a>
## D19: Mounted at login by systemd user units (user request)

The user wanted the cloud folders always there, like OneDrive on Windows,
without clicking first, and nothing hacky. Options weighed: a unit per remote
(standard, what rclone itself recommends), the extension mounting everything
when Files starts (depends on Files being opened), or a full sync client
(copies everything locally). Chosen: **systemd user units, enabled by the
extension.**

- **rclone still does all mounting.** `data/nautilus-cloud@.service` is a
  template whose `ExecStart` is the same `rclone mount`; systemd starts it at
  login (`WantedBy=default.target`), retries every 30 s on failure (offline at
  login), unmounts at logout, and logs to the journal.
- **The extension only reconciles:** on every `changed` from the scanner it
  enables and starts the unit of each remote it shows, and disables and stops
  the units of remotes that are gone (`systemctl --user enable|disable --now
  --no-block`). No new program; enabling persists, so after the first time,
  remotes mount at login even if Files is never opened. A brand-new remote is
  picked up the next time Files runs. The scanner reports its first result even
  when empty, so units of remotes deleted while Files was closed get cleaned up.
- **One mount mechanism:** a click on an unmounted remote now starts that
  remote's unit (`systemctl --user start` waits for rclone's readiness
  notification) instead of creating a one-off transient unit with
  `systemd-run`. Without systemd, rclone's daemon mode remains the fallback.
- **The mount point lives in two places** (the unit's `%h/Cloud/%I`, and
  `cloud_discovery_mount_directory`) and so do the rclone options (the unit,
  and `cloud_rclone_mount_argv` for the fallback);
  `/rclone/unit-matches-argv` fails if they drift apart.
- **`%I` with spaces:** systemd expands specifiers after splitting the command
  line, so a remote such as "My Drive" stays one argument (checked with a
  throwaway unit before writing the template).
- **Cache: `--vfs-cache-mode full`** (was `writes`), the closest to OneDrive's
  on-demand files: opened files stay cached, 5 GB and 168 h per remote, never
  below 10 GB free (`--vfs-cache-min-free-space`).
- **Environment:** the RCLONE_* pass-through of the transient units is gone;
  units see the user manager's environment (`~/.config/environment.d/`).
- Migration on the development machine: the last transient mount was stopped
  when idle, the template installed, `systemctl --user daemon-reload` run;
  Files' first scan then enabled and started all three remotes.
