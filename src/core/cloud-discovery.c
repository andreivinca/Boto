/* One scan: the rclone remotes and their existing mounts, as sorted locations. */

#include "core/cloud-discovery.h"

#include <string.h>

#include "core/cloud-location.h"
#include "core/cloud-mounts.h"
#include "core/cloud-rclone.h"

char *
cloud_discovery_mount_directory (void)
{
    return g_build_filename (g_get_home_dir (), "Cloud", NULL);
}

/* ~/Cloud/<name>. rclone remote names are safe file names, except that
 * "." and ".." would point elsewhere; those get the identifier instead. */
static char *
default_mount_point (const char *mount_directory,
                     const char *name)
{
    g_autofree char *id = NULL;

    if (g_strcmp0 (name, ".") != 0 && g_strcmp0 (name, "..") != 0 && strchr (name, G_DIR_SEPARATOR) == NULL)
        return g_build_filename (mount_directory, name, NULL);
    id = cloud_location_identifier (name);
    return g_build_filename (mount_directory, id, NULL);
}

GPtrArray *
cloud_discovery_build_locations (GPtrArray  *remotes,
                                 GHashTable *mounts)
{
    GPtrArray *locations = cloud_location_list_new ();
    g_autofree char *mount_directory = cloud_discovery_mount_directory ();

    for (guint i = 0; i < remotes->len; i++)
    {
        CloudRemote *remote = g_ptr_array_index (remotes, i);
        const char *mounted = g_hash_table_lookup (mounts, remote->name);
        g_autofree char *path = NULL;

        /* Combined remotes duplicate the individual accounts in this sidebar. */
        if (g_strcmp0 (remote->type, "combine") == 0)
            continue;
        path = mounted ? g_strdup (mounted) : default_mount_point (mount_directory, remote->name);
        g_ptr_array_add (locations, cloud_location_new (remote->name, remote->type, path));
    }
    cloud_location_list_sort (locations);
    return locations;
}

GPtrArray *
cloud_discovery_scan (GError **error)
{
    g_autoptr(GPtrArray) remotes = cloud_rclone_list_remotes (error);
    g_autoptr(GHashTable) mounts = NULL;

    if (remotes == NULL)
        return NULL;
    mounts = cloud_mounts_find_rclone ();
    return cloud_discovery_build_locations (remotes, mounts);
}
