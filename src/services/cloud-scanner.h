/* Background rclone discovery: scans now and then periodically on a worker
 * thread, and emits "changed" on the main context after the first successful
 * scan and whenever the locations change. A failed scan keeps the last good
 * locations. */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define CLOUD_TYPE_SCANNER (cloud_scanner_get_type ())

G_DECLARE_FINAL_TYPE (CloudScanner, cloud_scanner, CLOUD, SCANNER, GObject)

void          cloud_scanner_register_types (GTypeModule *module);

CloudScanner *cloud_scanner_new (guint interval_seconds);

/* Scans immediately, then every interval. Call once. */
void          cloud_scanner_start (CloudScanner *self);

/* The current locations (CloudLocation), sorted for display. The array is
 * replaced, never modified, when the locations change, so holders may keep a
 * reference to it. Initially empty. */
GPtrArray    *cloud_scanner_get_locations (CloudScanner *self);

G_END_DECLS
