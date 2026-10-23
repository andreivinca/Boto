/* Mounting a location on demand with rclone. */

#include "services/cloud-mounter.h"

#include <errno.h>

#include "core/cloud-discovery.h"
#include "core/cloud-mounts.h"
#include "core/cloud-rclone.h"
#include "core/cloud-systemd.h"

static void
mount_exited (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
    g_autoptr(GTask) task = user_data;
    CloudLocation *location = g_task_get_task_data (task);
    GError *error = NULL;

    if (!g_subprocess_wait_check_finish (G_SUBPROCESS (source), result, &error))
        g_task_return_error (task, error);
    else if (!cloud_mounts_is_mount (location->path))
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED,
                                 "%s is not mounted on %s", location->remote, location->path);
    else
        g_task_return_boolean (task, TRUE);
}

/* With systemd, start the remote's unit; systemctl returns once rclone reports
 * the mount ready, or failed. Otherwise run rclone as a daemon. */
static GStrv
mount_command (CloudLocation *location,
               const char    *cache)
{
    g_autofree char *mount_directory = cloud_discovery_mount_directory ();
    g_autofree char *unit = NULL;
    const char *argv[] = { "systemctl", "--user", "start", NULL, NULL };

    if (!cloud_systemd_available () || !cloud_systemd_mounts_location (location, mount_directory))
        return cloud_rclone_mount_argv (location, cache);
    unit = cloud_systemd_unit_name (location->name);
    argv[3] = unit;
    return g_strdupv ((char **) argv);
}

void
cloud_mounter_mount_async (CloudLocation       *location,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
    g_autoptr(GTask) task = g_task_new (NULL, cancellable, callback, user_data);
    g_autofree char *cache = g_build_filename (g_get_user_cache_dir (), "nautilus-cloud-sidebar", NULL);
    g_auto(GStrv) argv = NULL;
    g_autoptr(GSubprocess) process = NULL;
    GError *error = NULL;

    g_task_set_source_tag (task, cloud_mounter_mount_async);
    g_task_set_task_data (task, cloud_location_ref (location), (GDestroyNotify) cloud_location_unref);

    if (g_mkdir_with_parents (location->path, 0777) != 0 || g_mkdir_with_parents (cache, 0777) != 0)
    {
        int saved_errno = errno;

        g_task_return_new_error (task, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                                 "Could not create mount directories: %s", g_strerror (saved_errno));
        return;
    }
    argv = mount_command (location, cache);
    process = g_subprocess_newv ((const char * const *) argv,
                                 G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error);
    if (process == NULL)
    {
        g_task_return_error (task, error);
        return;
    }
    g_subprocess_wait_check_async (process, cancellable, mount_exited, g_steal_pointer (&task));
}

gboolean
cloud_mounter_mount_finish (GAsyncResult  *result,
                            GError       **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
    g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == cloud_mounter_mount_async, FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}
