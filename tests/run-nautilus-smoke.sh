#!/bin/sh
# Smoke-test the built module inside the real Nautilus without installing it.
#
# A user and mount namespace swaps Nautilus' extension directory for one that
# holds only this module. Nautilus runs with a temporary home, a private D-Bus
# session and a headless Broadway display, so the desktop session and a running
# Files instance are untouched. A fake rclone lists four remotes: "Mounted" is
# bind-mounted at its default mount point (~/Cloud/Mounted) and opened,
# "Unmounted" is not mounted, "Both" is a combine remote, and "Profile" is
# reused from a running "rclone mount" process on the (tmpfs) home directory,
# so Nautilus' native Home row is a duplicate that the extension must hide.
# "Profile" contains ~/Cloud/Mounted too, but sorts after it, so "Mounted" is
# the one highlighted.
#
# Usage: tests/run-nautilus-smoke.sh MODULE [SECONDS]
set -eu

case "${CLOUD_SMOKE_STAGE:-}" in
"")
    module=$(realpath "$1")
    mkdir -p build
    root=$(mktemp -d "$PWD/build/smoke-XXXXXX")
    trap 'rm -rf "$root"' EXIT
    mkdir -p "$root/bin" "$root/extensions" "$root/runtime" "$root/remote/Documents" "$root/home"
    chmod 700 "$root/runtime"
    cp "$module" "$root/extensions/"
    cat > "$root/bin/rclone" <<EOF
#!/bin/sh
case "\$1" in
listremotes) echo '[{"name":"Unmounted","type":"s3"},{"name":"Both","type":"combine"},
                    {"name":"Mounted","type":"drive"},{"name":"Profile","type":"drive"}]' ;;
mount) if [ "\$2" = Profile: ]; then while kill -0 \$PPID 2>/dev/null; do sleep 0.2; done; exit 0; fi
       echo "\$*" >> '$root/mount.log'; exit 1 ;;
esac
EOF
    chmod +x "$root/bin/rclone"
    cat > "$root/session.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:dir=$root/runtime</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
    nautilus --version 2>/dev/null | tail -n 1
    CLOUD_SMOKE_STAGE=namespace CLOUD_SMOKE_ROOT=$root \
    CLOUD_SMOKE_UID=$(id -u) CLOUD_SMOKE_GID=$(id -g) CLOUD_SMOKE_SECONDS=${2:-15} \
        unshare --map-root-user --mount "$0"
    ;;
namespace)
    root=$CLOUD_SMOKE_ROOT
    mounted=$root/home/Cloud/Mounted
    mount --bind "$root/extensions" "$(pkg-config --variable=extensiondir libnautilus-extension-4)"
    mount -t tmpfs tmpfs "$root/home"
    mkdir -p "$mounted"
    mount --bind "$root/remote" "$mounted"
    # Drop back to the original user ID for Nautilus.
    CLOUD_SMOKE_STAGE=session exec unshare --user --map-user="$CLOUD_SMOKE_UID" \
        --map-group="$CLOUD_SMOKE_GID" "$0"
    ;;
session)
    root=$CLOUD_SMOKE_ROOT
    mounted=$root/home/Cloud/Mounted
    export HOME="$root/home" XDG_DATA_HOME="$root/data" XDG_CONFIG_HOME="$root/home/config" \
           XDG_CACHE_HOME="$root/home/cache" XDG_STATE_HOME="$root/home/state" \
           XDG_RUNTIME_DIR="$root/runtime" PATH="$root/bin:$PATH" \
           GDK_BACKEND=broadway BROADWAY_DISPLAY=:7 GDK_DEBUG=no-portals GTK_A11Y=none \
           GSETTINGS_BACKEND=memory GIO_USE_VFS=local GIO_USE_VOLUME_MONITOR=unix \
           NAUTILUS_CLOUD_DIAGNOSTICS="$root/diagnostics.json"
    unset WAYLAND_DISPLAY DISPLAY DBUS_SESSION_BUS_ADDRESS RCLONE_CONFIG
    gtk4-broadwayd --unixsocket "$root/runtime/http.socket" :7 >/dev/null 2>&1 &
    broadway=$!
    # Display :N listens on broadway<N+1>.socket.
    until [ -S "$root/runtime/broadway8.socket" ]; do sleep 0.05; done
    rclone mount Profile: "$HOME" &
    # The inner script expands its own positional arguments.
    # shellcheck disable=SC2016
    dbus-run-session --config-file="$root/session.conf" -- \
        sh -c 'nautilus --new-window "$1" >"$2/nautilus.log" 2>&1 & pid=$!
               sleep "$3"; kill $pid; wait $pid || true' sh "$mounted/Documents" "$root" "$CLOUD_SMOKE_SECONDS"
    kill $broadway
    echo "--- diagnostics"
    cat "$root/diagnostics.json" 2>/dev/null || echo "(none written)"
    echo
    echo "--- extension log"
    grep -i 'cloud' "$root/nautilus.log" || true
    report=$(tr -d ' \n' < "$root/diagnostics.json" 2>/dev/null || true)
    case "$report" in
    *'"names":["Mounted","Profile","Unmounted"],"rows":3,"hidden_duplicates":1,'*'"selected":["Mounted"]'*)
        echo "PASS" ;;
    *)
        echo "FAIL: unexpected sidebar state" >&2
        exit 1 ;;
    esac
    ;;
esac
