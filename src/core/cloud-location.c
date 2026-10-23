/* Cloud locations: the rclone remotes the sidebar shows. */

#include "core/cloud-location.h"

#include <string.h>

#define IDENTIFIER_LENGTH 20

static void
cloud_location_clear (CloudLocation *location)
{
    g_free (location->name);
    g_free (location->remote);
    g_free (location->type);
    g_free (location->id);
    g_free (location->path);
    g_clear_object (&location->file);
    g_free (location->uri);
}

CloudLocation *
cloud_location_new (const char *name,
                    const char *type,
                    const char *path)
{
    CloudLocation *location = g_atomic_rc_box_new0 (CloudLocation);

    location->name = g_strdup (name);
    location->remote = g_strconcat (name, ":", NULL);
    location->type = g_strdup (type);
    location->id = cloud_location_identifier (name);
    location->path = g_strdup (path);
    location->file = g_file_new_for_path (path);
    location->uri = g_file_get_uri (location->file);
    return location;
}

CloudLocation *
cloud_location_ref (CloudLocation *location)
{
    return g_atomic_rc_box_acquire (location);
}

void
cloud_location_unref (CloudLocation *location)
{
    g_atomic_rc_box_release_full (location, (GDestroyNotify) cloud_location_clear);
}

char *
cloud_location_identifier (const char *name)
{
    g_autofree char *digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, name, -1);

    return g_strndup (digest, IDENTIFIER_LENGTH);
}

gboolean
cloud_location_contains (CloudLocation *location,
                         GFile         *file)
{
    return g_file_equal (file, location->file) || g_file_has_prefix (file, location->file);
}

GPtrArray *
cloud_location_list_new (void)
{
    return g_ptr_array_new_with_free_func ((GDestroyNotify) cloud_location_unref);
}

static int
compare_names (gconstpointer a,
               gconstpointer b)
{
    const CloudLocation *left = *(CloudLocation * const *) a;
    const CloudLocation *right = *(CloudLocation * const *) b;
    g_autofree char *left_key = g_utf8_casefold (left->name, -1);
    g_autofree char *right_key = g_utf8_casefold (right->name, -1);

    return strcmp (left_key, right_key);
}

/* Case-insensitive and stable, so names that only differ by case keep their order. */
void
cloud_location_list_sort (GPtrArray *locations)
{
    g_ptr_array_sort (locations, compare_names);
}

/* Equal when the same remotes are in the same order, on the same paths, with the same types. */
gboolean
cloud_location_list_equal (GPtrArray *a,
                           GPtrArray *b)
{
    if (a->len != b->len)
        return FALSE;
    for (guint i = 0; i < a->len; i++)
    {
        CloudLocation *left = g_ptr_array_index (a, i);
        CloudLocation *right = g_ptr_array_index (b, i);

        if (g_strcmp0 (left->name, right->name) != 0 ||
            g_strcmp0 (left->path, right->path) != 0 ||
            g_strcmp0 (left->type, right->type) != 0)
            return FALSE;
    }
    return TRUE;
}

/* The first location, in list order, that contains FILE (which may be NULL). */
CloudLocation *
cloud_location_list_find_containing (GPtrArray *locations,
                                     GFile     *file)
{
    for (guint i = 0; file != NULL && i < locations->len; i++)
    {
        CloudLocation *location = g_ptr_array_index (locations, i);

        if (cloud_location_contains (location, file))
            return location;
    }
    return NULL;
}
