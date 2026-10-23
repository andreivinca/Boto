/* Mounting remotes at login, without a click.
 *
 * Keeps a systemd user unit (data/nautilus-cloud@.service) enabled and started
 * for every remote that mounts at ~/Cloud/<remote>, and disables the units of
 * remotes that are gone. systemd then mounts them at every login, before Files
 * is opened. Does nothing where systemd units can't be used.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Brings the enabled units in line with LOCATIONS (CloudLocation). Returns at
 * once; systemctl runs in the background. */
void cloud_automount_sync (GPtrArray *locations);

G_END_DECLS
