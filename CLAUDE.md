# Nautilus Cloud Sidebar

Native C extension (libnautilus-extension-4, GTK 4) that adds a Cloud section
of rclone remotes to the GNOME Files sidebar.

Read `docs/README.md` first. Then, depending on the task:
- `docs/status.md`: current state, open items, environment and hook notes for this machine.
- `docs/technical-requirements.md`: requirement IDs mapped to functions and tests.
- `docs/nautilus-internals.md`: private Nautilus structure the code depends on, and the upgrade checklist.
- `docs/decisions.md`: why things are done the way they are; check before "fixing" them.

Commands: `make`, `make check` (headless, ~5 s), `make smoke` (real Nautilus in
namespaces, ~20 s). Users install with `./install.sh` (pacman package on Arch via
`packaging/arch/PKGBUILD`, `sudo make install` elsewhere); `--check` is a dry run.

Source layout (dependencies point down; details in `docs/architecture.md`):
- `src/module.c`, `src/cloud-extension.c`: module entry points and wiring.
- `src/ui/`: CloudSection widget, CloudWindow controller, CloudDragTracker, diagnostics.
- `src/nautilus/cloud-nautilus.c`: the only place that knows Nautilus' private widget structure.
- `src/services/`: CloudScanner (worker-thread discovery), cloud-mounter (GTask), cloud-automount (enables a unit per remote).
- `src/core/`: GLib-only logic: location model, rclone CLI, mounts, systemd unit names, command runner. No GTK.
- `data/nautilus-cloud@.service`: the systemd user unit that mounts each remote at ~/Cloud/<remote> at login;
  its rclone options must match `cloud_rclone_mount_argv` (a test checks).

Rules:
- `docs/technical-requirements.md` is the specification: change behaviour there
  first, and record the reasoning in `docs/decisions.md`.
- Respect the layering: no GTK in `src/core`, and no Nautilus-private names
  outside `src/nautilus`. New logic goes in the lowest layer that can hold it,
  with a test.
- Every behaviour change needs a test in `tests/` and an update to
  `docs/technical-requirements.md`.
- Don't run `nautilus` or `nautilus -q` against the user's session; use `make smoke`.
- Don't commit unless asked.
