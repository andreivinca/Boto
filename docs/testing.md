# Testing

Three automated layers plus a short manual checklist. Everything automated runs
headless and isolated: no windows on the desktop, no real rclone accounts, and
the user's running Files is not touched.

| Layer | Command | Time | What it proves |
| --- | --- | --- | --- |
| Unit | `build/test-core` (part of `make check`) | ~2 s | `src/core`: location model, mount detection, rclone parsing and fallback, mount command lines, timeouts, discovery |
| Fake window | `tests/run-sidebar-test.sh build/test-sidebar $PWD/libnautilus-cloud-sidebar.so` (part of `make check`) | ~3 s (+30 s with `-m slow`) | The built `.so` in a fake Nautilus tree styled with libadwaita: injection, heading style, alignment with native rows, heading spacing (rendered pixels), greying out during a drag (simulated controller events), hiding, selection, navigation, both mount command lines, alerts, releasing closed tabs, multiple windows, rescan |
| Real Nautilus | `make smoke` | ~20 s | The module inside the **installed** Nautilus: injection, combine filter, hiding a real native row, selection, diagnostics |

`make check` runs the first two; 38 results (25 core, 13 fake-window), one of
them the slow rescan test, skipped unless you pass `-m slow`.

## Unit tests (`tests/test-core.c`)

- Cover `src/core` only, grouped by module: `/location/…`, `/mounts/…`,
  `/rclone/…`, `/command/…`, `/discovery/…`. The binary links only GIO and
  json-glib, so any GTK use in `core/` breaks the build.
- Sets `HOME` and `XDG_DATA_HOME` and prepends a temp `bin/` to `PATH` **before** GLib
  caches user directories, then writes a fake `rclone` shell script per test.
- `/mounts/rclone-processes` starts fake `rclone mount NAME: PATH` processes.
  A shell script's `comm` is its file name, so it looks like the real binary.
  The fake process exits when the test does (`while kill -0 $PPID`).
- `/rclone/mount-argv` checks the daemon-mode command line exactly, and
  `/rclone/unit-matches-argv` that `data/nautilus-cloud@.service` uses the same
  rclone options and mounts at `%h/Cloud/%I`. `/systemd/*` cover name escaping
  (against `systemd-escape` output) and which units to enable or disable.
- `/command/run` counts `/proc/self/fd` around a timeout. That's the regression
  test for the timeout descriptor leak.

## Fake-window test (`tests/test-sidebar.c`)

- Defines GObject types with Nautilus' names: `NautilusWindow`
  (`GtkApplicationWindow`), `NautilusWindowSlot` (`GtkBox` with `location`,
  `active` and a `slot.open-location` action that records the URI),
  `NautilusSidebarRow` (`GtkListBoxRow` with `uri`, its content built like
  `nautilus-sidebar-row.ui`), and `NautilusGtkPlacesSidebar` (a box whose CSS
  name `placessidebar` scopes the theme's sidebar row rules).
- Calls `adw_init()`, so everything is styled by libadwaita's stylesheet as in
  Nautilus. Layout checks therefore use the theme's real paddings:
  - `/sidebar/rows-align-with-native` compares widget positions: our icon and
    label against the native row's, and the heading against the icon column;
  - `/sidebar/heading-spacing` renders the section to pixels
    (`gtk_widget_paintable_new` + Cairo renderer) and requires the blank rows
    above and below "Cloud" to match within 1 px.
- Loads the module like Nautilus does (`g_module_open` + a minimal
  `GTypeModule` subclass + `nautilus_module_initialize/list_types`).
- Fake `rclone`: `listremotes` prints `remotes.json`; `mount` logs its argv to
  `mount.log` and exits 1, except `mount cloud-sidebar-test-live: /proc`, which
  stays alive as an "existing mount" on `/proc`. A fake `systemctl` logs too, so a test never reaches the real user manager.
- Tests run in order and share one window; `second-window` replaces it.
- Run directly with extra GTest flags, e.g.
  `tests/run-sidebar-test.sh build/test-sidebar "$PWD/libnautilus-cloud-sidebar.so" -m slow --verbose`.
- It refuses to run outside `run-sidebar-test.sh`, which provides a private
  `XDG_RUNTIME_DIR`, so the systemd branch can't spawn a real user unit.

## Real-Nautilus smoke test (`tests/run-nautilus-smoke.sh`)

Stages (the script re-executes itself with `CLOUD_SMOKE_STAGE`):

1. **Outer:** create `build/smoke-XXXXXX` (removed on exit), a fake `rclone`
   listing `Unmounted` (s3), `Both` (combine), `Mounted` (drive) and `Profile`
   (drive), and a D-Bus config **without service directories**, so nothing is
   D-Bus-activated (no tracker, portals or gvfs).
2. **namespace** (`unshare --map-root-user --mount`): bind-mount a directory
   holding only this `.so` over the system extension directory; mount a tmpfs
   on the sandbox `HOME`; bind-mount a fake remote onto `Mounted`'s default
   mount point `~/Cloud/Mounted`.
3. **session** (`unshare --user --map-user=<uid>`, back to the normal uid):
   start `gtk4-broadwayd` on a Unix socket, start a fake
   `rclone mount Profile: $HOME` process (so the always-present native **Home** row
   is a duplicate to hide; `Profile` sorts after `Mounted`, so the more specific
   `Mounted` is the one selected), then run `nautilus --new-window <Mounted>/Documents`
   inside `dbus-run-session --config-file=…`, wait (default 15 s; second
   argument), and kill it.
4. Print the diagnostics JSON and the extension's log lines; PASS when names
   are `Mounted, Profile, Unmounted`, rows 3, hidden_duplicates 1, selected `Mounted`.

Requirements: unprivileged user namespaces, util-linux ≥ 2.38 (`--map-user`),
`gtk4-broadwayd`, `dbus-run-session`, the installed `nautilus`.

## Looking at the real thing

To see what Nautilus actually draws (for layout work), serve the sandboxed
Broadway display on a local port instead of a Unix socket and screenshot it
with headless Chromium:
- in a copy of `run-nautilus-smoke.sh`, start broadwayd with
  `gtk4-broadwayd --address 127.0.0.1 --port <port> :7`;
- after about 9 s run
  `chromium --headless=new --user-data-dir=<temp dir> --window-size=1000,640 --timeout=5000 --screenshot=out.png http://127.0.0.1:<port>/`.

Nothing appears on the desktop. Measure gaps by scanning the PNG's pixel rows;
that's how the D16 numbers were taken.

## Sanitizers

```sh
make clean
make CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined" \
     LDFLAGS="-fsanitize=address,undefined" libnautilus-cloud-sidebar.so build/test-core build/test-sidebar
ASAN_OPTIONS=detect_leaks=1 build/test-core
ASAN_OPTIONS=detect_leaks=0 LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
  tests/run-sidebar-test.sh build/test-sidebar "$PWD/libnautilus-cloud-sidebar.so" -m slow
make clean && make    # back to a normal build before installing
```

Static analysis (no output means no findings):

```sh
F="$(pkg-config --cflags gtk4 libnautilus-extension-4 json-glib-1.0)"
for f in $(find src -name '*.c'); do gcc -std=gnu11 -fanalyzer -O1 -Isrc $F -c -o /dev/null "$f"; done
```

Leak detection is off for the GTK test because GTK and fontconfig produce
unrelated reports. Both runs were clean on 2026-10-23.

## Manual checks

Not automated. Do these in the user's real session after `sudo make install`
and `nautilus -q`:

- [ ] Cloud section appears under the native places (or under My Computer's
      section), heading styled like the list header.
- [ ] After `systemctl --user daemon-reload` and opening Files, every remote is
      mounted at `~/Cloud/<remote>` (`systemctl --user list-units 'nautilus-cloud@*'`
      shows one running unit each; `ls ~/.config/systemd/user/default.target.wants/`
      lists them).
- [ ] After logging out and back in, `~/Cloud/<remote>` is mounted before Files is opened.
- [ ] Stop one (`systemctl --user stop nautilus-cloud@<remote>`), then click it:
      it mounts again and opens in the current tab. A second click while it
      mounts does nothing; clicking a mounted one opens it at once.
- [ ] Browsing inside the remote highlights its row; leaving unhighlights it.
- [ ] Dragging a file or folder from the view over the sidebar greys out the
      Cloud rows (like other rows that can't take a drop); they return after
      the drop, after cancelling, or when the drag leaves the sidebar.
- [ ] A remote with a broken config shows "Could not connect to <name>".
- [ ] `rclone config` add/delete of a remote updates the section within ~30 s,
      and enables (mounts) or disables (unmounts) its unit.
- [ ] Closing Files keeps the mounts and finishes uploads.

## Pitfalls already found (don't rediscover them)

- **`g_utf8_validate_len` rejects embedded NULs.** `/proc/<pid>/cmdline` must
  be split first and each argument validated, otherwise no process ever matches.
- **Broadway display `:N` listens on `broadway<N+1>.socket`** in
  `$XDG_RUNTIME_DIR`. Use `--unixsocket` so broadwayd opens no TCP port.
- **GTest makes warnings fatal.** Broadway sometimes logs
  `Gdk-WARNING … gdk_frame_timings_submitted()` after ~30 s idle. GDK logs
  through structured logging, which skips `g_test_log_set_fatal_handler`, so
  `test-sidebar` filters it with `g_log_set_writer_func`.
- **Helper processes that outlive a crashed test keep pipes open** and hang
  `make`/`| tail`. Fakes must exit with their parent and not inherit stdout.
- **GSubprocess + cancellation on a private context leaks** (pending operations
  hold the context's eventfd and the pipes). Kill the child instead.
- **Never run `nautilus` against the user's session bus in a test.** It's
  single-instance: it would open a window in the user's running Files instead.
  Use a private bus (see the smoke test).
- **GIO hides bind mounts, tmpfs and dot-directory mounts** from the sidebar, so
  a test duplicate row needs the `Home` trick above or a real FUSE mount.
- `/tmp` is tmpfs on this machine; the smoke test keeps its files in `build/`.
- Mounting inside `unshare` needs `--map-root-user`; drop back to the real uid
  with a nested `unshare --user --map-user=<uid>`, or Nautilus runs as "root".
