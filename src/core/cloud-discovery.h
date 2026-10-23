/* One scan: the rclone remotes and their existing mounts, as sorted locations. */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* ~/Cloud, where remotes are mounted on demand. */
char      *cloud_discovery_mount_directory (void);

/* Locations for REMOTES (CloudRemote) in display order: combine remotes left
 * out, each on its existing mount from MOUNTS (name → path) or its default
 * mount point ~/Cloud/<remote name>. */
GPtrArray *cloud_discovery_build_locations (GPtrArray  *remotes,
                                            GHashTable *mounts);

/* Runs rclone and inspects mounts; blocks, so call it from a worker thread.
 * NULL with ERROR on failure. */
GPtrArray *cloud_discovery_scan (GError **error);

G_END_DECLS
