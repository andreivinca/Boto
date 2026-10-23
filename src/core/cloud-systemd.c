/* The systemd user units that keep remotes mounted at ~/Cloud/<remote>. */

#include "core/cloud-systemd.h"

#include <string.h>
#include <unistd.h>

#ifndef CLOUD_SYSTEMD_USER_UNIT_DIR
#define CLOUD_SYSTEMD_USER_UNIT_DIR "/usr/lib/systemd/user"
#endif

#define UNIT_PREFIX "nautilus-cloud@"
#define UNIT_SUFFIX ".service"

/* Same rules as systemd's unit_name_escape(). */
char *
cloud_systemd_escape (const char *text)
{
    static const char valid[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ:_.";
    GString *escaped = g_string_new (NULL);

    for (const char *p = text; *p != '\0'; p++)
    {
        if (*p == '/')
            g_string_append_c (escaped, '-');
        else if ((p == text && *p == '.') || strchr (valid, *p) == NULL)
            g_string_append_printf (escaped, "\\x%02x", (guchar) *p);
        else
            g_string_append_c (escaped, *p);
    }
    return g_string_free (escaped, FALSE);
}

char *
cloud_systemd_unit_name (const char *remote)
{
    g_autofree char *instance = cloud_systemd_escape (remote);

    return g_strconcat (UNIT_PREFIX, instance, UNIT_SUFFIX, NULL);
}

gboolean
cloud_systemd_unit_installed (void)
{
    g_autofree char *user = g_build_filename (g_get_user_config_dir (), "systemd", "user", NULL);
    const char *directories[] = {
        user, "/etc/systemd/user", CLOUD_SYSTEMD_USER_UNIT_DIR, "/usr/local/lib/systemd/user",
        "/usr/lib/systemd/user",
    };

    for (guint i = 0; i < G_N_ELEMENTS (directories); i++)
    {
        g_autofree char *path = g_build_filename (directories[i], CLOUD_SYSTEMD_TEMPLATE, NULL);

        if (g_file_test (path, G_FILE_TEST_EXISTS))
            return TRUE;
    }
    return FALSE;
}

gboolean
cloud_systemd_available (void)
{
    g_autofree char *systemctl = g_find_program_in_path ("systemctl");
    const char *runtime_env = g_getenv ("XDG_RUNTIME_DIR");
    g_autofree char *runtime = NULL;
    g_autofree char *manager = NULL;

    if (systemctl == NULL)
        return FALSE;
    /* Not g_get_user_runtime_dir(), which falls back to the cache directory. */
    runtime = runtime_env ? g_strdup (runtime_env) : g_strdup_printf ("/run/user/%u", (guint) getuid ());
    manager = g_build_filename (runtime, "systemd", "private", NULL);
    return g_file_test (manager, G_FILE_TEST_EXISTS) && cloud_systemd_unit_installed ();
}

gboolean
cloud_systemd_mounts_location (CloudLocation *location,
                               const char    *mount_directory)
{
    g_autofree char *path = g_build_filename (mount_directory, location->name, NULL);

    return g_strcmp0 (location->path, path) == 0;
}

char *
cloud_systemd_wants_directory (void)
{
    return g_build_filename (g_get_user_config_dir (), "systemd", "user", "default.target.wants", NULL);
}

static gboolean
is_instance (const char *unit)
{
    return g_str_has_prefix (unit, UNIT_PREFIX) && g_str_has_suffix (unit, UNIT_SUFFIX) &&
           strlen (unit) > strlen (UNIT_PREFIX UNIT_SUFFIX);
}

GStrv
cloud_systemd_list_enabled (const char *wants_directory)
{
    g_autoptr(GPtrArray) units = g_ptr_array_new_with_free_func (g_free);
    GDir *directory = g_dir_open (wants_directory, 0, NULL);
    const char *name;

    while (directory != NULL && (name = g_dir_read_name (directory)) != NULL)
        if (is_instance (name))
            g_ptr_array_add (units, g_strdup (name));
    if (directory != NULL)
        g_dir_close (directory);
    g_ptr_array_sort_values (units, (GCompareFunc) g_strcmp0);
    g_ptr_array_add (units, NULL);
    return (GStrv) g_ptr_array_steal (units, NULL);
}

void
cloud_systemd_plan (GPtrArray          *locations,
                    const char         *mount_directory,
                    const char * const *enabled,
                    GStrv              *enable,
                    GStrv              *disable)
{
    g_autoptr(GHashTable) wanted = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_autoptr(GStrvBuilder) to_enable = g_strv_builder_new ();
    g_autoptr(GStrvBuilder) to_disable = g_strv_builder_new ();

    for (guint i = 0; i < locations->len; i++)
    {
        CloudLocation *location = g_ptr_array_index (locations, i);
        char *unit;

        /* Remotes mounted elsewhere, or named "." or "..", are left alone. */
        if (!cloud_systemd_mounts_location (location, mount_directory))
            continue;
        unit = cloud_systemd_unit_name (location->name);
        g_strv_builder_add (to_enable, unit);
        g_hash_table_add (wanted, unit);
    }
    for (guint i = 0; enabled != NULL && enabled[i] != NULL; i++)
        if (!g_hash_table_contains (wanted, enabled[i]))
            g_strv_builder_add (to_disable, enabled[i]);
    *enable = g_strv_builder_end (to_enable);
    *disable = g_strv_builder_end (to_disable);
}
