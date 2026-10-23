/* Mount points, and the rclone mounts that already exist. */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* Whether PATH is a mount point: not a symlink, and on a different device than
 * its parent (or the same inode, for "/"). */
gboolean    cloud_mounts_is_mount (const char *path);

/* Undo the \ooo escapes of /proc/self/mountinfo fields. */
char       *cloud_mounts_unescape (const char *value);

/* Adds "remote name → mount point" for each fuse.rclone mount whose source is
 * rclone's default "name:" to MOUNTS (string → string, owning both). */
void        cloud_mounts_parse_mountinfo (const char *contents,
                                          GHashTable *mounts);

/* Remote name → mount point for every existing rclone mount: mountinfo
 * entries, then running `rclone mount name: path` processes, which win. */
GHashTable *cloud_mounts_find_rclone (void);

G_END_DECLS
