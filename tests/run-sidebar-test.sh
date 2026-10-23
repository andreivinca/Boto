#!/bin/sh
# Run the GTK integration test on a private, headless Broadway display, so no
# window appears on the desktop and a running Files instance is left alone.
set -eu

test_binary=$1
shift

runtime=$(mktemp -d)
number=$(( $$ % 1000 + 100 ))
broadway=

cleanup () {
    if [ -n "$broadway" ]; then
        kill "$broadway" 2>/dev/null || true
        wait "$broadway" 2>/dev/null || true
    fi
    rm -rf "$runtime"
}
trap cleanup EXIT INT TERM

XDG_RUNTIME_DIR=$runtime gtk4-broadwayd --unixsocket "$runtime/http.socket" ":$number" >/dev/null 2>&1 &
broadway=$!
tries=0
# Display :N listens on broadway<N+1>.socket.
until [ -S "$runtime/broadway$((number + 1)).socket" ]; do
    tries=$((tries + 1))
    if [ "$tries" -gt 100 ]; then
        echo "gtk4-broadwayd did not start" >&2
        exit 1
    fi
    sleep 0.05
done

env -u WAYLAND_DISPLAY -u DISPLAY \
    XDG_RUNTIME_DIR="$runtime" GDK_BACKEND=broadway BROADWAY_DISPLAY=":$number" \
    GDK_DEBUG=no-portals GTK_A11Y=none GSETTINGS_BACKEND=memory \
    "$test_binary" "$@"
