#!/bin/sh
# Installs Nautilus Cloud Sidebar for GNOME Files, or removes it.
#
# Arch Linux: builds a pacman package (packaging/arch/PKGBUILD) and installs
# it, so pacman upgrades and removes it like any other package. Other Linux
# distributions: builds with make and installs with sudo make install; missing
# build dependencies are listed with the command that installs them.
# Afterwards Files is restarted, and it mounts every rclone remote at
# ~/Cloud/<remote>, now and at every login.
#
# Usage: ./install.sh [--check] [--make] [--uninstall]
#   --check      only report missing dependencies and what would be done
#   --make       use make and sudo even where pacman is available
#   --uninstall  stop the cloud mounts and remove the extension
set -eu

cd "$(dirname "$0")"

method="pacman"
check_only=false
uninstall=false
for argument in "$@"; do
    case "$argument" in
    --check) check_only=true ;;
    --make) method="make" ;;
    --uninstall) uninstall=true ;;
    -h | --help)
        sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        echo "Unknown option: $argument (see --help)" >&2
        exit 2
        ;;
    esac
done

if [ -t 1 ]; then
    bold=$(printf '\033[1m')
    plain=$(printf '\033[0m')
else
    bold=
    plain=
fi

say() {
    printf '%s%s%s\n' "$bold" "$*" "$plain"
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

have() {
    command -v "$1" >/dev/null 2>&1
}

[ "$(uname -s)" = Linux ] || die "this is an extension for GNOME Files, which runs on Linux."
[ "$(id -u)" -ne 0 ] || die "run it as your normal user; it asks for your password when needed."
if [ "$method" = pacman ] && ! { have pacman && have makepkg; }; then
    method="make"
fi

# The command that installs the build dependencies on this distribution.
install_hint() {
    ID=
    ID_LIKE=
    if [ -r /etc/os-release ]; then
        # shellcheck source=/dev/null
        . /etc/os-release
    fi
    case " $ID $ID_LIKE " in
    *" arch "*)
        echo "sudo pacman -S --needed base-devel libnautilus-extension gtk4 json-glib rclone fuse3" ;;
    *" debian "* | *" ubuntu "*)
        # Their rclone package can be older than 1.62; take it from rclone.org.
        echo "sudo apt install build-essential pkg-config libnautilus-extension-dev libgtk-4-dev libjson-glib-dev fuse3 && curl https://rclone.org/install.sh | sudo bash" ;;
    *" fedora "* | *" rhel "*)
        echo "sudo dnf install gcc make pkgconf-pkg-config nautilus-devel gtk4-devel json-glib-devel rclone fuse3" ;;
    *" suse "* | *" opensuse "*)
        echo "sudo zypper install gcc make pkg-config nautilus-devel gtk4-devel json-glib-devel rclone fuse3" ;;
    *)
        echo "" ;;
    esac
}

missing=
check_make_dependencies() {
    for command in cc make pkg-config rclone fusermount3; do
        have "$command" || missing="$missing $command"
    done
    if have pkg-config; then
        for module in libnautilus-extension-4 gtk4 json-glib-1.0; do
            pkg-config --exists "$module" || missing="$missing $module"
        done
    fi
    if [ -n "$missing" ]; then
        return 0
    fi
    pkg-config --atleast-version=2.80 glib-2.0 ||
        die "GLib 2.80 or newer is needed; this system has $(pkg-config --modversion glib-2.0)."
    pkg-config --atleast-version=4.12 gtk4 ||
        die "GTK 4.12 or newer is needed; this system has $(pkg-config --modversion gtk4)."
}

# The mount service uses --vfs-cache-min-free-space, new in rclone 1.62.
check_rclone_version() {
    have rclone || return 0
    version=$(rclone version 2>/dev/null | sed -n '1s/^rclone v\([0-9]*\)\.\([0-9]*\).*/\1 \2/p')
    [ -n "$version" ] || return 0
    major=${version% *}
    minor=${version#* }
    if [ "$major" -lt 1 ] || { [ "$major" -eq 1 ] && [ "$minor" -lt 62 ]; }; then
        die "rclone $major.$minor is too old; 1.62 or newer is needed. Install the current one with:
  curl https://rclone.org/install.sh | sudo bash"
    fi
}

check_pacman_dependencies() {
    # makepkg installs the rest from the PKGBUILD's dependencies.
    for command in git sudo; do
        have "$command" || missing="$missing $command"
    done
}

report_missing() {
    hint=$(install_hint)
    echo "Missing:$missing" >&2
    if [ -n "$hint" ]; then
        echo "Install the build dependencies with:" >&2
        echo "  $hint" >&2
    else
        echo "Install a C compiler, make, pkg-config, the Nautilus extension, GTK 4 and json-glib" >&2
        echo "development files, rclone and FUSE 3 with your package manager." >&2
    fi
    exit 1
}

# Every user unit the extension enabled, as names systemctl accepts
# (systemctl disable takes no wildcards).
enabled_units() {
    wants=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/default.target.wants
    if [ -d "$wants" ]; then
        (cd "$wants" && ls -1 -- nautilus-cloud@*.service 2>/dev/null) || true
    fi
}

stop_mounts() {
    units=$(enabled_units)
    if [ -n "$units" ] && have systemctl; then
        say "Stopping the cloud mounts…"
        # Unit names are escaped (no spaces), so splitting them is safe.
        # shellcheck disable=SC2086
        systemctl --user disable --now $units
    fi
}

module_path() {
    directory=$(pkg-config --variable=extensiondir libnautilus-extension-4 2>/dev/null || true)
    echo "${directory:-/usr/lib/nautilus/extensions-4}/libnautilus-cloud-sidebar.so"
}

package_installed() {
    have pacman && pacman -Qq nautilus-cloud-sidebar >/dev/null 2>&1
}

restart_files() {
    if pgrep -x nautilus >/dev/null 2>&1; then
        say "Restarting Files (its open windows close)…"
        nautilus -q 2>/dev/null || true
        tries=0
        while pgrep -x nautilus >/dev/null 2>&1 && [ "$tries" -lt 40 ]; do
            sleep 0.25
            tries=$((tries + 1))
        done
    fi
    [ "${1:-}" = start ] || return 0
    desktop=/usr/share/applications/org.gnome.Nautilus.desktop
    if have gio && [ -f "$desktop" ]; then
        gio launch "$desktop" >/dev/null 2>&1 || true
    fi
}

if $uninstall; then
    if $check_only; then
        echo "Would stop these mounts: $(enabled_units | tr '\n' ' ')"
        if package_installed; then
            echo "Would run: sudo pacman -R nautilus-cloud-sidebar"
        else
            echo "Would run: sudo make uninstall"
        fi
        exit 0
    fi
    stop_mounts
    if package_installed; then
        say "Removing the package…"
        sudo pacman -R nautilus-cloud-sidebar
    else
        say "Removing the installed files…"
        sudo make uninstall
    fi
    restart_files
    say "Done. ~/Cloud and your files in the cloud are untouched."
    exit 0
fi

if [ "$method" = pacman ]; then
    check_pacman_dependencies
else
    check_make_dependencies
fi
if [ -n "$missing" ]; then
    report_missing
fi
check_rclone_version

if $check_only; then
    echo "Dependencies: all found."
    if [ "$method" = pacman ]; then
        echo "Would build and install the pacman package from packaging/arch (makepkg -si)."
    else
        echo "Would run: make && sudo make install && systemctl --user daemon-reload"
    fi
    echo "Would then restart Files."
    exit 0
fi

if [ "$method" = pacman ]; then
    module=$(module_path)
    if [ -e "$module" ] && ! pacman -Qqo "$module" >/dev/null 2>&1; then
        say "Removing an earlier installation made with make, so pacman can own the files…"
        sudo make uninstall
    fi
    say "Building and installing the package (makepkg asks before installing)…"
    (cd packaging/arch && makepkg --syncdeps --install --clean --cleanbuild --force)
else
    say "Building…"
    make
    say "Installing (sudo)…"
    sudo make install
    if have systemctl; then
        systemctl --user daemon-reload 2>/dev/null || true
    fi
fi

restart_files start

if have rclone && ! rclone listremotes --ask-password=false </dev/null 2>/dev/null | grep -q .; then
    say "No rclone remotes yet: add your accounts with 'rclone config'; Files picks them up within 30 seconds."
fi
say "Done. Your remotes are mounted at ~/Cloud/<remote>; see them in the Cloud section of Files."
