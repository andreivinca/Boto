/* Mounting remotes at login, without a click. */

#include "services/cloud-automount.h"

#include <gio/gio.h>

#include "core/cloud-discovery.h"
#include "core/cloud-systemd.h"

static void
systemctl_finished (GObject      *source,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    g_autofree char *verb = user_data;
    g_autoptr(GError) error = NULL;

    if (!g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &error))
        g_message ("Could not %s cloud mounts: %s", verb, error->message);
}

/* `systemctl --user VERB --now --no-block UNITS…`: enable or disable, and start
 * or stop, without waiting for the mounts themselves. */
static void
run_systemctl (const char *verb,
               char      **units)
{
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
    g_auto(GStrv) argv = NULL;
    g_autoptr(GSubprocess) process = NULL;
    g_autoptr(GError) error = NULL;

    if (units == NULL || units[0] == NULL)
        return;
    g_strv_builder_add_many (builder, "systemctl", "--user", verb, "--now", "--no-block", NULL);
    g_strv_builder_addv (builder, (const char **) units);
    argv = g_strv_builder_end (builder);
    process = g_subprocess_newv ((const char * const *) argv,
                                 G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error);
    if (process == NULL)
    {
        g_message ("Could not %s cloud mounts: %s", verb, error->message);
        return;
    }
    g_subprocess_wait_check_async (process, NULL, systemctl_finished, g_strdup (verb));
}

void
cloud_automount_sync (GPtrArray *locations)
{
    g_autofree char *mount_directory = NULL;
    g_autofree char *wants_directory = NULL;
    g_auto(GStrv) enabled = NULL;
    g_auto(GStrv) enable = NULL;
    g_auto(GStrv) disable = NULL;

    if (!cloud_systemd_available ())
        return;
    mount_directory = cloud_discovery_mount_directory ();
    wants_directory = cloud_systemd_wants_directory ();
    enabled = cloud_systemd_list_enabled (wants_directory);
    cloud_systemd_plan (locations, mount_directory, (const char * const *) enabled, &enable, &disable);
    run_systemctl ("enable", enable);
    run_systemctl ("disable", disable);
}
