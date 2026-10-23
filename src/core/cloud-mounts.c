/* Mount points, and the rclone mounts that already exist. */

#include "core/cloud-mounts.h"

#include <string.h>
#include <sys/stat.h>

gboolean
cloud_mounts_is_mount (const char *path)
{
    struct stat self, parent;
    g_autofree char *parent_path = NULL;

    if (path == NULL || lstat (path, &self) != 0 || S_ISLNK (self.st_mode))
        return FALSE;
    parent_path = g_build_filename (path, "..", NULL);
    if (lstat (parent_path, &parent) != 0)
    {
        /* PATH may not be searchable, e.g. a mount root without execute permission. */
        g_autofree char *canonical = g_canonicalize_filename (path, NULL);

        g_free (parent_path);
        parent_path = g_path_get_dirname (canonical);
        if (stat (parent_path, &parent) != 0)
            return FALSE;
    }
    return self.st_dev != parent.st_dev || self.st_ino == parent.st_ino;
}

static gboolean
is_octal (char c,
          char highest)
{
    return c >= '0' && c <= highest;
}

char *
cloud_mounts_unescape (const char *value)
{
    GString *result = g_string_sized_new (strlen (value));

    for (const char *p = value; *p != '\0'; p++)
    {
        if (p[0] == '\\' && is_octal (p[1], '3') && is_octal (p[2], '7') && is_octal (p[3], '7'))
        {
            g_string_append_c (result, (char) (((p[1] - '0') << 6) | ((p[2] - '0') << 3) | (p[3] - '0')));
            p += 3;
        }
        else
        {
            g_string_append_c (result, *p);
        }
    }
    return g_string_free (result, FALSE);
}

/* Whitespace-separated fields, ignoring runs of separators. */
static GStrv
split_fields (const char *text)
{
    g_auto(GStrv) parts = g_strsplit_set (text, " \t", -1);
    g_autoptr(GStrvBuilder) fields = g_strv_builder_new ();

    for (guint i = 0; parts[i] != NULL; i++)
        if (parts[i][0] != '\0')
            g_strv_builder_add (fields, parts[i]);
    return g_strv_builder_end (fields);
}

void
cloud_mounts_parse_mountinfo (const char *contents,
                              GHashTable *mounts)
{
    g_auto(GStrv) lines = g_strsplit (contents, "\n", -1);

    /* "<id> <parent> <dev> <root> <mount point> <options> [tags] - <fstype> <source> <options>" */
    for (guint i = 0; lines[i] != NULL; i++)
    {
        const char *separator = strstr (lines[i], " - ");
        g_autofree char *left_text = NULL;
        g_auto(GStrv) left = NULL;
        g_auto(GStrv) right = NULL;
        g_autofree char *source = NULL;
        gsize length;

        if (separator == NULL)
            continue;
        left_text = g_strndup (lines[i], separator - lines[i]);
        left = split_fields (left_text);
        right = split_fields (separator + 3);
        if (g_strv_length (left) < 5 || g_strv_length (right) < 2)
            continue;
        length = strlen (right[1]);
        if (g_strcmp0 (right[0], "fuse.rclone") != 0 || length == 0 || right[1][length - 1] != ':')
            continue;
        source = g_strndup (right[1], length - 1);
        g_hash_table_replace (mounts, cloud_mounts_unescape (source), cloud_mounts_unescape (left[4]));
    }
}

/* NUL-separated arguments; NULL unless every one is valid UTF-8. */
static GPtrArray *
split_cmdline (const char *data,
               gsize       length)
{
    g_autoptr(GPtrArray) args = g_ptr_array_new_with_free_func (g_free);
    gsize start = 0;

    for (gsize i = 0; i <= length; i++)
    {
        if (i == length || data[i] == '\0')
        {
            if (!g_utf8_validate_len (data + start, i - start, NULL))
                return NULL;
            g_ptr_array_add (args, g_strndup (data + start, i - start));
            start = i + 1;
        }
    }
    return g_steal_pointer (&args);
}

/* `rclone mount name: path` processes, which may use a custom --devname. */
static void
add_rclone_processes (GHashTable *mounts)
{
    g_autoptr(GError) error = NULL;
    GDir *proc = g_dir_open ("/proc", 0, &error);
    const char *entry;

    if (proc == NULL)
    {
        g_warning ("Could not inspect existing rclone mounts: %s", error->message);
        return;
    }
    while ((entry = g_dir_read_name (proc)) != NULL)
    {
        g_autofree char *comm_path = NULL;
        g_autofree char *comm = NULL;
        g_autofree char *cmdline_path = NULL;
        g_autofree char *cmdline = NULL;
        g_autoptr(GPtrArray) args = NULL;
        gsize length;
        guint index;
        const char *remote;
        const char *path;
        gsize remote_length;

        if (entry[strspn (entry, "0123456789")] != '\0')
            continue;
        comm_path = g_build_filename ("/proc", entry, "comm", NULL);
        if (!g_file_get_contents (comm_path, &comm, NULL, NULL) || strcmp (g_strstrip (comm), "rclone") != 0)
            continue;
        cmdline_path = g_build_filename ("/proc", entry, "cmdline", NULL);
        if (!g_file_get_contents (cmdline_path, &cmdline, &length, NULL) ||
            (args = split_cmdline (cmdline, length)) == NULL)
            continue;
        if (!g_ptr_array_find_with_equal_func (args, "mount", g_str_equal, &index) || index + 2 >= args->len)
            continue;
        remote = g_ptr_array_index (args, index + 1);
        path = g_ptr_array_index (args, index + 2);
        remote_length = strlen (remote);
        if (remote_length > 0 && remote[remote_length - 1] == ':' && cloud_mounts_is_mount (path))
            g_hash_table_replace (mounts, g_strndup (remote, remote_length - 1), g_strdup (path));
    }
    g_dir_close (proc);
}

GHashTable *
cloud_mounts_find_rclone (void)
{
    GHashTable *mounts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_autoptr(GError) error = NULL;
    g_autofree char *contents = NULL;

    if (!g_file_get_contents ("/proc/self/mountinfo", &contents, NULL, &error))
    {
        g_warning ("Could not inspect existing rclone mounts: %s", error->message);
        return mounts;
    }
    cloud_mounts_parse_mountinfo (contents, mounts);
    add_rclone_processes (mounts);
    return mounts;
}
