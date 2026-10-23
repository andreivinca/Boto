/* The rclone command line: listing remotes and building mount commands.
 *
 * Only remote names and types are ever read; credentials stay in rclone.
 */

#pragma once

#include <gio/gio.h>

#include "core/cloud-location.h"

G_BEGIN_DECLS

#define CLOUD_RCLONE_TIMEOUT_SECONDS 10

/* A remote as rclone lists it. */
typedef struct
{
    char *name;
    char *type;     /* "" if unknown */
} CloudRemote;

CloudRemote *cloud_remote_new (const char *name,
                               const char *type);
void         cloud_remote_free (CloudRemote *remote);

/* Remotes in rclone's order, from `rclone listremotes --json`, falling back to
 * the plain list on rclone versions without --json. Blocks for up to
 * CLOUD_RCLONE_TIMEOUT_SECONDS per call; NULL with ERROR on failure. */
GPtrArray *cloud_rclone_list_remotes (GError **error);

GPtrArray *cloud_rclone_parse_remotes_json (const char  *text,
                                            GError     **error);
GPtrArray *cloud_rclone_parse_remotes_plain (const char *text);

/* The command that mounts LOCATION without systemd: rclone daemonizes once the
 * mount is ready, caching in CACHE_DIR and logging to CACHE_DIR/<id>.log.
 * With systemd, data/nautilus-cloud@.service runs rclone with the same flags. */
GStrv      cloud_rclone_mount_argv (CloudLocation *location,
                                    const char    *cache_dir);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CloudRemote, cloud_remote_free)

G_END_DECLS
