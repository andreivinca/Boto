/* The rclone command line: listing remotes and building mount commands. */

#include "core/cloud-rclone.h"

#include <json-glib/json-glib.h>
#include <string.h>

#include "core/cloud-command.h"

CloudRemote *
cloud_remote_new (const char *name,
                  const char *type)
{
    CloudRemote *remote = g_new0 (CloudRemote, 1);

    remote->name = g_strdup (name);
    remote->type = g_strdup (type);
    return remote;
}

void
cloud_remote_free (CloudRemote *remote)
{
    g_free (remote->name);
    g_free (remote->type);
    g_free (remote);
}

static GPtrArray *
remote_list_new (void)
{
    return g_ptr_array_new_with_free_func ((GDestroyNotify) cloud_remote_free);
}

static const char *
string_member (JsonObject *object,
               const char *name)
{
    JsonNode *node = object ? json_object_get_member (object, name) : NULL;

    if (node == NULL || json_node_get_value_type (node) != G_TYPE_STRING)
        return NULL;
    return json_node_get_string (node);
}

GPtrArray *
cloud_rclone_parse_remotes_json (const char  *text,
                                 GError     **error)
{
    g_autoptr(JsonParser) parser = json_parser_new ();
    g_autoptr(GPtrArray) remotes = remote_list_new ();
    JsonNode *root;
    JsonArray *array;

    if (!json_parser_load_from_data (parser, text, -1, error))
        return NULL;
    root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_ARRAY (root))
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                             "rclone did not return a list of remotes");
        return NULL;
    }
    array = json_node_get_array (root);
    for (guint i = 0; i < json_array_get_length (array); i++)
    {
        JsonNode *element = json_array_get_element (array, i);
        JsonObject *object = JSON_NODE_HOLDS_OBJECT (element) ? json_node_get_object (element) : NULL;
        const char *name = string_member (object, "name");
        const char *type = string_member (object, "type");

        if (name == NULL)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                 "rclone returned a remote without a name");
            return NULL;
        }
        g_ptr_array_add (remotes, cloud_remote_new (name, type ? type : ""));
    }
    return g_steal_pointer (&remotes);
}

/* One "name:" per line; LF or CRLF line ends. */
GPtrArray *
cloud_rclone_parse_remotes_plain (const char *text)
{
    GPtrArray *remotes = remote_list_new ();
    g_auto(GStrv) lines = g_strsplit_set (text, "\r\n", -1);

    for (guint i = 0; lines[i] != NULL; i++)
    {
        gsize length = strlen (lines[i]);
        g_autofree char *name = NULL;

        if (length == 0 || lines[i][length - 1] != ':')
            continue;
        name = g_strndup (lines[i], length - 1);
        g_ptr_array_add (remotes, cloud_remote_new (name, ""));
    }
    return remotes;
}

static GPtrArray *
listing_failed (GError **error)
{
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "rclone could not list remotes");
    return NULL;
}

GPtrArray *
cloud_rclone_list_remotes (GError **error)
{
    static const char * const json_argv[] = { "rclone", "listremotes", "--json", NULL };
    static const char * const plain_argv[] = { "rclone", "listremotes", NULL };
    g_autofree char *output = NULL;
    g_autofree char *errors = NULL;
    gboolean successful;

    if (!cloud_command_run (json_argv, CLOUD_RCLONE_TIMEOUT_SECONDS, &output, &errors, &successful, error))
        return NULL;
    if (successful)
        return cloud_rclone_parse_remotes_json (output, error);
    if (strstr (errors, "unknown flag") == NULL)
        return listing_failed (error);

    /* Older rclone versions only print a plain list of remote names. */
    g_clear_pointer (&output, g_free);
    if (!cloud_command_run (plain_argv, CLOUD_RCLONE_TIMEOUT_SECONDS, &output, NULL, &successful, error))
        return NULL;
    if (!successful)
        return listing_failed (error);
    return cloud_rclone_parse_remotes_plain (output);
}

GStrv
cloud_rclone_mount_argv (CloudLocation *location,
                         const char    *cache_dir)
{
    g_autoptr(GStrvBuilder) argv = g_strv_builder_new ();
    g_autofree char *log_name = g_strconcat (location->id, ".log", NULL);
    g_autofree char *log_file = g_build_filename (cache_dir, log_name, NULL);

    /* Opened files stay cached, bounded per remote and by the disk's free space. */
    g_strv_builder_add_many (argv, "rclone", "mount", "--daemon", "--daemon-wait", "20s",
                             "--vfs-cache-mode", "full", "--cache-dir", cache_dir,
                             "--vfs-cache-max-size", "5G", "--vfs-cache-max-age", "168h",
                             "--vfs-cache-min-free-space", "10G",
                             "--log-file", log_file, "--", location->remote, location->path, NULL);
    return g_strv_builder_end (argv);
}
