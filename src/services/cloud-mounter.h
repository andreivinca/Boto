/* Mounting a location on demand with rclone.
 *
 * Under a systemd user session the mount runs as a transient unit, so it and
 * its pending uploads survive Files closing; otherwise rclone daemonizes.
 */

#pragma once

#include <gio/gio.h>

#include "core/cloud-location.h"

G_BEGIN_DECLS

/* Creates the mount point and cache directory, starts rclone, and completes
 * once rclone reports the mount ready (or fails). */
void     cloud_mounter_mount_async (CloudLocation       *location,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data);

/* TRUE if LOCATION is now mounted; otherwise FALSE with ERROR. */
gboolean cloud_mounter_mount_finish (GAsyncResult  *result,
                                     GError       **error);

G_END_DECLS
