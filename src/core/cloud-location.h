/* Cloud locations: the rclone remotes the sidebar shows.
 *
 * Plain GLib, safe on any thread. A CloudLocation is immutable once created
 * and atomically reference counted, so threads and widgets can share it.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct
{
    char *name;     /* rclone remote name */
    char *remote;   /* "name:", as rclone expects it */
    char *type;     /* rclone backend type, "" if unknown */
    char *id;       /* cloud_location_identifier (name) */
    char *path;     /* mount point, existing or default */
    GFile *file;    /* path */
    char *uri;      /* path as a URI */
} CloudLocation;

CloudLocation *cloud_location_new (const char *name,
                                   const char *type,
                                   const char *path);
CloudLocation *cloud_location_ref (CloudLocation *location);
void           cloud_location_unref (CloudLocation *location);

/* First 20 hex digits of SHA-256 (NAME); names mount directories and units. */
char          *cloud_location_identifier (const char *name);

/* Whether FILE is the location's directory or inside it. */
gboolean       cloud_location_contains (CloudLocation *location,
                                        GFile         *file);

/* Lists are GPtrArrays of CloudLocation that own their elements. */
GPtrArray     *cloud_location_list_new (void);
void           cloud_location_list_sort (GPtrArray *locations);
gboolean       cloud_location_list_equal (GPtrArray *a,
                                          GPtrArray *b);
CloudLocation *cloud_location_list_find_containing (GPtrArray *locations,
                                                    GFile     *file);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CloudLocation, cloud_location_unref)

G_END_DECLS
