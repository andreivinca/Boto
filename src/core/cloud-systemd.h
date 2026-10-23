/* The systemd user units that keep remotes mounted at ~/Cloud/<remote>.
 *
 * data/nautilus-cloud@.service is a template: one instance per remote, named
 * after the escaped remote name, mounts it at login. Plain GLib, any thread.
 */

#pragma once

#include <gio/gio.h>

#include "core/cloud-location.h"

G_BEGIN_DECLS

#define CLOUD_SYSTEMD_TEMPLATE "nautilus-cloud@.service"

/* TEXT escaped for use in a unit name, as `systemd-escape TEXT` does. */
char     *cloud_systemd_escape (const char *text);

/* "nautilus-cloud@<escaped REMOTE>.service" */
char     *cloud_systemd_unit_name (const char *remote);

/* Whether the template is installed where the user manager looks for units. */
gboolean  cloud_systemd_unit_installed (void);

/* Whether units can be used: systemctl, a running user manager, the template. */
gboolean  cloud_systemd_available (void);

/* Whether LOCATION is the one its unit mounts: at MOUNT_DIRECTORY/<name>. */
gboolean  cloud_systemd_mounts_location (CloudLocation *location,
                                         const char    *mount_directory);

/* Where the user's enabled units are linked: ~/.config/systemd/user/default.target.wants. */
char     *cloud_systemd_wants_directory (void);

/* Enabled instances of the template in WANTS_DIRECTORY, sorted. */
GStrv     cloud_systemd_list_enabled (const char *wants_directory);

/* The units to enable (and start) and to disable (and stop) so that exactly
 * the LOCATIONS that units can mount have an enabled unit. ENABLED is what is
 * enabled now. */
void      cloud_systemd_plan (GPtrArray          *locations,
                              const char         *mount_directory,
                              const char * const *enabled,
                              GStrv              *enable,
                              GStrv              *disable);

G_END_DECLS
