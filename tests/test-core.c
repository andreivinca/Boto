/* Unit tests for src/core: plain GLib, no display needed. */

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#include "core/cloud-command.h"
#include "core/cloud-discovery.h"
#include "core/cloud-location.h"
#include "core/cloud-mounts.h"
#include "core/cloud-rclone.h"
#include "core/cloud-systemd.h"

static char *data_home;
static char *bin_dir;

static CloudLocation *
location_at (GPtrArray *locations,
             guint      index)
{
    return g_ptr_array_index (locations, index);
}

static CloudRemote *
remote_at (GPtrArray *remotes,
           guint      index)
{
    return g_ptr_array_index (remotes, index);
}

/* remotes ("name", "type", "name", "type", ..., NULL) */
static GPtrArray *
remotes (const char *first_name, ...)
{
    GPtrArray *result = g_ptr_array_new_with_free_func ((GDestroyNotify) cloud_remote_free);
    va_list args;

    va_start (args, first_name);
    for (const char *name = first_name; name != NULL; name = va_arg (args, const char *))
        g_ptr_array_add (result, cloud_remote_new (name, va_arg (args, const char *)));
    va_end (args);
    return result;
}

static void
write_fake_rclone (const char *script)
{
    g_autofree char *path = g_build_filename (bin_dir, "rclone", NULL);
    g_autofree char *contents = g_strconcat ("#!/bin/sh\n", script, "\n", NULL);

    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
    g_assert_cmpint (g_chmod (path, 0755), ==, 0);
}

static guint
open_descriptors (void)
{
    GDir *dir = g_dir_open ("/proc/self/fd", 0, NULL);
    guint count = 0;

    while (g_dir_read_name (dir) != NULL)
        count++;
    g_dir_close (dir);
    return count;
}

/* Location */

static void
test_location_new (void)
{
    g_autoptr(CloudLocation) location = cloud_location_new ("My Drive", "drive", "/mnt/my drive");

    g_assert_cmpstr (location->remote, ==, "My Drive:");
    g_assert_cmpstr (location->type, ==, "drive");
    g_assert_cmpstr (location->uri, ==, "file:///mnt/my%20drive");
    g_assert_cmpstr (g_file_peek_path (location->file), ==, "/mnt/my drive");
    g_assert_cmpstr (location->id, ==, "428d9cf8159f1d9f5c19");
}

static void
test_location_identifier (void)
{
    /* `printf %s NAME | sha256sum | cut -c1-20` */
    g_autofree char *plain = cloud_location_identifier ("gdrive");
    g_autofree char *unicode = cloud_location_identifier ("Ünï cödé");
    g_autofree char *spaced = cloud_location_identifier ("a b");

    g_assert_cmpstr (plain, ==, "e0337ddfed1f926ec50e");
    g_assert_cmpstr (unicode, ==, "d52cb13a54c8486562c6");
    g_assert_cmpstr (spaced, ==, "c8687a08aa5d6ed20443");
}

static void
test_location_find_containing (void)
{
    g_autoptr(GPtrArray) locations = cloud_location_list_new ();
    g_autoptr(GFile) inside = g_file_new_for_path ("/mnt/a/b/c");
    g_autoptr(GFile) exact = g_file_new_for_path ("/mnt/a/b");
    g_autoptr(GFile) sibling = g_file_new_for_path ("/mnt/ab");

    g_ptr_array_add (locations, cloud_location_new ("outer", "", "/mnt/a"));
    g_ptr_array_add (locations, cloud_location_new ("inner", "", "/mnt/a/b"));
    /* The first match in list order wins. */
    g_assert_cmpstr (cloud_location_list_find_containing (locations, inside)->name, ==, "outer");
    g_assert_true (cloud_location_contains (location_at (locations, 1), exact));
    g_assert_true (cloud_location_contains (location_at (locations, 1), inside));
    g_assert_false (cloud_location_contains (location_at (locations, 1), sibling));
    g_assert_null (cloud_location_list_find_containing (locations, sibling));
    g_assert_null (cloud_location_list_find_containing (locations, NULL));
}

static void
test_location_list_equal (void)
{
    g_autoptr(GPtrArray) a = remotes ("one", "drive", "two", "s3", NULL);
    g_autoptr(GPtrArray) b = remotes ("one", "drive", "two", "ftp", NULL);
    g_autoptr(GHashTable) mounts = g_hash_table_new (g_str_hash, g_str_equal);
    g_autoptr(GPtrArray) first = cloud_discovery_build_locations (a, mounts);
    g_autoptr(GPtrArray) again = cloud_discovery_build_locations (a, mounts);
    g_autoptr(GPtrArray) retyped = cloud_discovery_build_locations (b, mounts);
    g_autoptr(GPtrArray) moved = NULL;
    g_autoptr(GPtrArray) shorter = cloud_location_list_new ();

    g_assert_true (cloud_location_list_equal (first, again));
    g_assert_false (cloud_location_list_equal (first, retyped));
    g_assert_false (cloud_location_list_equal (first, shorter));
    g_hash_table_insert (mounts, "two", "/mnt/two");
    moved = cloud_discovery_build_locations (a, mounts);
    g_assert_false (cloud_location_list_equal (first, moved));
}

/* Mounts */

static void
test_mounts_unescape (void)
{
    g_autofree char *space = cloud_mounts_unescape ("/mnt/my\\040drive");
    g_autofree char *backslash = cloud_mounts_unescape ("a\\134b");
    g_autofree char *untouched = cloud_mounts_unescape ("\\9x\\04\\777");

    g_assert_cmpstr (space, ==, "/mnt/my drive");
    g_assert_cmpstr (backslash, ==, "a\\b");
    g_assert_cmpstr (untouched, ==, "\\9x\\04\\777");
}

static void
test_mounts_mountinfo (void)
{
    g_autoptr(GHashTable) mounts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    const char *contents =
        "22 1 0:21 / / rw,relatime shared:1 - btrfs /dev/root rw\n"
        "90 22 0:50 / /home/u/Drive rw,nosuid,nodev - fuse.rclone gdrive: rw,user_id=1000\n"
        "91 22 0:51 / /home/u/My\\040Box rw,nosuid shared:7 - fuse.rclone my\\040box: rw\n"
        "92 22 0:52 / /mnt/other rw - fuse.sshfs host: rw\n"
        "93 22 0:53 / /mnt/custom rw - fuse.rclone customname rw\n"
        "malformed line without separator\n"
        "94 22 - fuse.rclone short: rw\n";

    cloud_mounts_parse_mountinfo (contents, mounts);
    g_assert_cmpuint (g_hash_table_size (mounts), ==, 2);
    g_assert_cmpstr (g_hash_table_lookup (mounts, "gdrive"), ==, "/home/u/Drive");
    g_assert_cmpstr (g_hash_table_lookup (mounts, "my box"), ==, "/home/u/My Box");
}

static void
test_mounts_is_mount (void)
{
    g_autofree char *directory = g_dir_make_tmp ("cloud-test-XXXXXX", NULL);
    g_autofree char *link = g_build_filename (directory, "link", NULL);

    g_assert_true (cloud_mounts_is_mount ("/"));
    g_assert_true (cloud_mounts_is_mount ("/proc"));
    g_assert_false (cloud_mounts_is_mount (directory));
    g_assert_false (cloud_mounts_is_mount ("/nonexistent/path"));
    g_assert_false (cloud_mounts_is_mount (NULL));
    g_assert_cmpint (symlink ("/proc", link), ==, 0);
    g_assert_false (cloud_mounts_is_mount (link));
    g_unlink (link);
    /* Without search permission, ".." cannot be looked up through the directory. */
    g_assert_cmpint (g_chmod (directory, 0600), ==, 0);
    g_assert_false (cloud_mounts_is_mount (directory));
    g_chmod (directory, 0700);
    g_rmdir (directory);
}

static void
test_mounts_rclone_processes (void)
{
    g_autofree char *rclone = g_build_filename (bin_dir, "rclone", NULL);
    g_autoptr(GSubprocess) flags_first = NULL;
    g_autoptr(GSubprocess) not_mounted = NULL;
    g_autoptr(GSubprocess) mounted = NULL;
    g_autoptr(GHashTable) before = NULL;
    g_autoptr(GHashTable) after = NULL;
    g_autoptr(GError) error = NULL;

    /* The script's comm is "rclone", like the real binary's. It exits with this test. */
    write_fake_rclone ("while kill -0 $PPID 2>/dev/null; do sleep 0.2; done");
    flags_first = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error, rclone, "mount", "--vfs-cache-mode", "off",
                                    "cloud-test-proc:", "/proc", NULL);
    g_assert_no_error (error);
    not_mounted = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error, rclone, "mount", "cloud-test-tmp:",
                                    data_home, NULL);
    g_assert_no_error (error);
    g_usleep (G_USEC_PER_SEC / 5);

    /* Only the remote right after "mount" counts, and only on a real mount point. */
    before = cloud_mounts_find_rclone ();
    g_assert_false (g_hash_table_contains (before, "cloud-test-proc"));
    g_assert_false (g_hash_table_contains (before, "cloud-test-tmp"));
    g_subprocess_force_exit (flags_first);

    mounted = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error, rclone, "mount", "cloud-test-proc:", "/proc", NULL);
    g_assert_no_error (error);
    g_usleep (G_USEC_PER_SEC / 5);
    after = cloud_mounts_find_rclone ();
    g_assert_cmpstr (g_hash_table_lookup (after, "cloud-test-proc"), ==, "/proc");
    g_assert_false (g_hash_table_contains (after, "cloud-test-tmp"));

    g_subprocess_force_exit (mounted);
    g_subprocess_force_exit (not_mounted);
}

/* rclone */

static void
test_rclone_parse_json (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) parsed = cloud_rclone_parse_remotes_json (
        "[{\"name\":\"onedrive\",\"type\":\"onedrive\",\"source\":\"file\",\"description\":\"\"},\n"
        " {\"name\":\"untyped\"},\n"
        " {\"name\":\"nulltype\",\"type\":null}]", &error);

    g_assert_no_error (error);
    g_assert_cmpuint (parsed->len, ==, 3);
    g_assert_cmpstr (remote_at (parsed, 0)->name, ==, "onedrive");
    g_assert_cmpstr (remote_at (parsed, 0)->type, ==, "onedrive");
    g_assert_cmpstr (remote_at (parsed, 1)->type, ==, "");
    g_assert_cmpstr (remote_at (parsed, 2)->type, ==, "");
}

static void
test_rclone_parse_json_invalid (void)
{
    const char *inputs[] = { "", "null", "{\"name\":\"x\"}", "[\"x\"]", "[{\"type\":\"drive\"}]",
                             "[{\"name\":3}]", "[{" };

    for (guint i = 0; i < G_N_ELEMENTS (inputs); i++)
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GPtrArray) parsed = cloud_rclone_parse_remotes_json (inputs[i], &error);

        g_assert_null (parsed);
        g_assert_nonnull (error);
    }
}

static void
test_rclone_parse_plain (void)
{
    g_autoptr(GPtrArray) parsed = cloud_rclone_parse_remotes_plain ("gdrive:\nbox:\r\nnot a remote\nspaced: \n\n");

    g_assert_cmpuint (parsed->len, ==, 2);
    g_assert_cmpstr (remote_at (parsed, 0)->name, ==, "gdrive");
    g_assert_cmpstr (remote_at (parsed, 1)->name, ==, "box");
    g_assert_cmpstr (remote_at (parsed, 1)->type, ==, "");
}

static void
test_rclone_list_json (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) listed = NULL;

    write_fake_rclone ("[ \"$*\" = \"listremotes --json\" ] || exit 9\n"
                       "echo '[{\"name\":\"work\",\"type\":\"drive\"},{\"name\":\"Box\",\"type\":\"box\"}]'");
    listed = cloud_rclone_list_remotes (&error);
    g_assert_no_error (error);
    g_assert_cmpuint (listed->len, ==, 2);
    g_assert_cmpstr (remote_at (listed, 0)->name, ==, "work");
}

static void
test_rclone_list_plain_fallback (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) listed = NULL;

    write_fake_rclone ("if [ \"$2\" = --json ]; then echo 'Error: unknown flag: --json' >&2; exit 1; fi\n"
                       "printf 'old:\\nlegacy:\\n'");
    listed = cloud_rclone_list_remotes (&error);
    g_assert_no_error (error);
    g_assert_cmpuint (listed->len, ==, 2);
    g_assert_cmpstr (remote_at (listed, 1)->name, ==, "legacy");
}

static void
test_rclone_list_failure (void)
{
    const char *scripts[] = {
        "echo 'Failed to load config' >&2; exit 1",
        "if [ \"$2\" = --json ]; then echo 'unknown flag' >&2; fi; exit 1",
    };

    for (guint i = 0; i < G_N_ELEMENTS (scripts); i++)
    {
        g_autoptr(GError) error = NULL;
        g_autoptr(GPtrArray) listed = NULL;

        write_fake_rclone (scripts[i]);
        listed = cloud_rclone_list_remotes (&error);
        g_assert_null (listed);
        g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
    }
}

static void
test_rclone_mount_argv (void)
{
    g_autoptr(CloudLocation) location = cloud_location_new ("Box", "box", "/mnt/box");
    g_auto(GStrv) argv = cloud_rclone_mount_argv (location, "/cache");
    g_autofree char *line = g_strjoinv (" ", argv);
    g_autofree char *expected = g_strdup_printf (
        "rclone mount --daemon --daemon-wait 20s --vfs-cache-mode full --cache-dir /cache "
        "--vfs-cache-max-size 5G --vfs-cache-max-age 168h --vfs-cache-min-free-space 10G "
        "--log-file /cache/%s.log -- Box: /mnt/box", location->id);

    g_assert_cmpstr (line, ==, expected);
}

/* The rclone options between "mount" and "--", with the cache directory's
 * value replaced by CACHE and daemon-only options dropped. */
static char *
cache_options (char **argv)
{
    g_autoptr(GString) options = g_string_new (NULL);
    guint i = 0;

    while (argv[i] != NULL && g_strcmp0 (argv[i], "mount") != 0)
        i++;
    for (i++; argv[i] != NULL && g_strcmp0 (argv[i], "--") != 0; i++)
    {
        if (g_strcmp0 (argv[i], "--daemon") == 0)
            continue;
        if (g_strcmp0 (argv[i], "--daemon-wait") == 0 || g_strcmp0 (argv[i], "--log-file") == 0)
        {
            i++;
            continue;
        }
        g_string_append_printf (options, "%s ", argv[i]);
        if (g_strcmp0 (argv[i], "--cache-dir") == 0)
        {
            g_string_append (options, "CACHE ");
            i++;
        }
    }
    return g_strdup (options->str);
}

static void
test_rclone_unit_matches_argv (void)
{
    g_autofree char *unit_path = g_build_filename (CLOUD_SOURCE_DIR, "data", "nautilus-cloud@.service", NULL);
    g_autofree char *unit = NULL;
    g_auto(GStrv) lines = NULL;
    g_auto(GStrv) exec_start = NULL;
    g_autoptr(CloudLocation) location = cloud_location_new ("Box", "box", "/mnt/box");
    g_auto(GStrv) argv = cloud_rclone_mount_argv (location, "/cache");
    g_autofree char *from_unit = NULL;
    g_autofree char *from_argv = cache_options (argv);

    /* Without systemd the same rclone options apply as in the unit. */
    g_assert_true (g_file_get_contents (unit_path, &unit, NULL, NULL));
    lines = g_strsplit (unit, "\n", -1);
    for (guint i = 0; lines[i] != NULL && exec_start == NULL; i++)
        if (g_str_has_prefix (lines[i], "ExecStart="))
            exec_start = g_strsplit (lines[i] + strlen ("ExecStart="), " ", -1);
    g_assert_nonnull (exec_start);
    g_assert_cmpstr (exec_start[0], ==, "rclone");
    from_unit = cache_options (exec_start);
    g_assert_cmpstr (from_unit, ==, from_argv);
    /* And it mounts where discovery expects: ~/Cloud/<remote>. */
    g_assert_nonnull (strstr (unit, " -- %I: %h/Cloud/%I\n"));
}

/* systemd */

static void
test_systemd_escape (void)
{
    /* Expected values are `systemd-escape` output. */
    const char *cases[][2] = {
        { "onedrive", "onedrive" },
        { "My Drive", "My\\x20Drive" },
        { "a-b", "a\\x2db" },
        { ".x", "\\x2ex" },
        { "x.y_z:1", "x.y_z:1" },
        { "a/b", "a-b" },
        { "\xc3\xbcn\xc3\xaf+@x", "\\xc3\\xbcn\\xc3\\xaf\\x2b\\x40x" },
    };

    for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
        g_autofree char *escaped = cloud_systemd_escape (cases[i][0]);

        g_assert_cmpstr (escaped, ==, cases[i][1]);
    }
}

static void
test_systemd_unit_name (void)
{
    g_autofree char *plain = cloud_systemd_unit_name ("onedrive");
    g_autofree char *spaced = cloud_systemd_unit_name ("My Drive");

    g_assert_cmpstr (plain, ==, "nautilus-cloud@onedrive.service");
    g_assert_cmpstr (spaced, ==, "nautilus-cloud@My\\x20Drive.service");
}

static void
test_systemd_list_enabled (void)
{
    g_autofree char *wants = g_dir_make_tmp ("cloud-wants-XXXXXX", NULL);
    const char *names[] = { "nautilus-cloud@b.service", "nautilus-cloud@a.service", "nautilus-cloud@.service",
                            "other@a.service", "nautilus-cloud@a.timer" };
    g_auto(GStrv) enabled = NULL;

    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        g_autofree char *path = g_build_filename (wants, names[i], NULL);

        g_assert_cmpint (symlink ("/dev/null", path), ==, 0);
    }
    enabled = cloud_systemd_list_enabled (wants);
    g_assert_cmpuint (g_strv_length (enabled), ==, 2);
    g_assert_cmpstr (enabled[0], ==, "nautilus-cloud@a.service");
    g_assert_cmpstr (enabled[1], ==, "nautilus-cloud@b.service");
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        g_autofree char *path = g_build_filename (wants, names[i], NULL);

        g_unlink (path);
    }
    g_rmdir (wants);

    g_clear_pointer (&enabled, g_strfreev);
    enabled = cloud_systemd_list_enabled ("/nonexistent");
    g_assert_cmpuint (g_strv_length (enabled), ==, 0);
}

static void
test_systemd_plan (void)
{
    g_autoptr(GPtrArray) locations = cloud_location_list_new ();
    const char *enabled[] = { "nautilus-cloud@gone.service", "nautilus-cloud@onedrive.service", NULL };
    g_auto(GStrv) enable = NULL;
    g_auto(GStrv) disable = NULL;

    g_ptr_array_add (locations, cloud_location_new ("onedrive", "onedrive", "/home/u/Cloud/onedrive"));
    g_ptr_array_add (locations, cloud_location_new ("My Drive", "drive", "/home/u/Cloud/My Drive"));
    /* Mounted elsewhere by the user, or not mountable by name: left alone. */
    g_ptr_array_add (locations, cloud_location_new ("own", "s3", "/mnt/own"));
    g_ptr_array_add (locations, cloud_location_new ("..", "ftp", "/home/u/Cloud/0123456789abcdef0123"));

    cloud_systemd_plan (locations, "/home/u/Cloud", enabled, &enable, &disable);
    g_assert_cmpuint (g_strv_length (enable), ==, 2);
    g_assert_cmpstr (enable[0], ==, "nautilus-cloud@onedrive.service");
    g_assert_cmpstr (enable[1], ==, "nautilus-cloud@My\\x20Drive.service");
    g_assert_cmpuint (g_strv_length (disable), ==, 1);
    g_assert_cmpstr (disable[0], ==, "nautilus-cloud@gone.service");
}

static void
test_systemd_unit_installed (void)
{
    g_autofree char *directory = g_build_filename (g_get_user_config_dir (), "systemd", "user", NULL);
    g_autofree char *unit = g_build_filename (directory, CLOUD_SYSTEMD_TEMPLATE, NULL);

    /* A copy in ~/.config/systemd/user counts, like the system-wide one. */
    g_assert_cmpint (g_mkdir_with_parents (directory, 0700), ==, 0);
    g_assert_true (g_file_set_contents (unit, "", 0, NULL));
    g_assert_true (cloud_systemd_unit_installed ());
    g_unlink (unit);
}

/* Command */

static void
test_command_run (void)
{
    const char *echo[] = { "sh", "-c", "read x; echo out:$x; echo err >&2; exit 3", NULL };
    const char *slow[] = { "sleep", "30", NULL };
    const char *missing[] = { "/nonexistent/command", NULL };
    g_autofree char *output = NULL;
    g_autofree char *errors = NULL;
    g_autoptr(GError) error = NULL;
    gboolean successful = TRUE;
    gint64 started;
    guint descriptors;

    /* stdin is /dev/null, so read gets nothing. */
    g_assert_true (cloud_command_run (echo, 5, &output, &errors, &successful, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (output, ==, "out:\n");
    g_assert_cmpstr (errors, ==, "err\n");
    g_assert_false (successful);

    /* A timeout kills the command and leaves nothing behind. */
    descriptors = open_descriptors ();
    started = g_get_monotonic_time ();
    g_assert_false (cloud_command_run (slow, 1, NULL, NULL, &successful, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_assert_cmpint (g_get_monotonic_time () - started, <, 5 * G_USEC_PER_SEC);
    g_assert_cmpuint (open_descriptors (), ==, descriptors);
    g_clear_error (&error);

    g_assert_false (cloud_command_run (missing, 1, NULL, NULL, &successful, &error));
    g_assert_nonnull (error);
}

/* Discovery */

static void
test_discovery_build_locations (void)
{
    g_autoptr(GPtrArray) input = remotes ("zeta", "drive", "all", "combine", "Alpha", "s3",
                                          "beta", "", "alpha", "ftp", NULL);
    g_autoptr(GHashTable) mounts = g_hash_table_new (g_str_hash, g_str_equal);
    g_autoptr(GPtrArray) locations = NULL;
    g_autofree char *mount_directory = cloud_discovery_mount_directory ();
    g_autofree char *expected_directory = g_build_filename (g_getenv ("HOME"), "Cloud", NULL);
    g_autofree char *zeta_path = NULL;

    g_assert_cmpstr (mount_directory, ==, expected_directory);
    g_hash_table_insert (mounts, "beta", "/mnt/beta");
    locations = cloud_discovery_build_locations (input, mounts);
    g_assert_cmpuint (locations->len, ==, 4);
    /* Case-insensitive and stable: "Alpha" stays before "alpha"; combine is left out. */
    g_assert_cmpstr (location_at (locations, 0)->name, ==, "Alpha");
    g_assert_cmpstr (location_at (locations, 1)->name, ==, "alpha");
    g_assert_cmpstr (location_at (locations, 2)->name, ==, "beta");
    g_assert_cmpstr (location_at (locations, 3)->name, ==, "zeta");
    g_assert_cmpstr (location_at (locations, 0)->type, ==, "s3");
    g_assert_cmpstr (location_at (locations, 2)->path, ==, "/mnt/beta");
    /* Not mounted yet: ~/Cloud/<remote name>. */
    zeta_path = g_build_filename (mount_directory, "zeta", NULL);
    g_assert_cmpstr (location_at (locations, 3)->path, ==, zeta_path);
}

static void
test_discovery_mount_points (void)
{
    g_autoptr(GPtrArray) input = remotes ("My Drive", "drive", ".", "s3", "..", "ftp", NULL);
    g_autoptr(GHashTable) mounts = g_hash_table_new (g_str_hash, g_str_equal);
    g_autoptr(GPtrArray) locations = cloud_discovery_build_locations (input, mounts);
    g_autofree char *mount_directory = cloud_discovery_mount_directory ();
    g_autofree char *spaced = g_build_filename (mount_directory, "My Drive", NULL);

    /* Names are used as they are; "." and ".." must not escape ~/Cloud. */
    g_assert_cmpuint (locations->len, ==, 3);
    for (guint i = 0; i < locations->len; i++)
    {
        CloudLocation *location = location_at (locations, i);
        g_autofree char *dirname = g_path_get_dirname (location->path);

        g_assert_cmpstr (dirname, ==, mount_directory);
        if (g_strcmp0 (location->name, "My Drive") == 0)
            g_assert_cmpstr (location->path, ==, spaced);
        else
            g_assert_true (g_str_has_suffix (location->path, location->id));
    }
}

static void
test_discovery_scan (void)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) locations = NULL;

    write_fake_rclone ("echo '[{\"name\":\"work\",\"type\":\"drive\"},"
                       "{\"name\":\"all\",\"type\":\"combine\"},{\"name\":\"Box\",\"type\":\"box\"}]'");
    locations = cloud_discovery_scan (&error);
    g_assert_no_error (error);
    g_assert_cmpuint (locations->len, ==, 2);
    g_assert_cmpstr (location_at (locations, 0)->name, ==, "Box");
    g_assert_cmpstr (location_at (locations, 1)->name, ==, "work");

    write_fake_rclone ("exit 1");
    g_clear_pointer (&locations, g_ptr_array_unref);
    locations = cloud_discovery_scan (&error);
    g_assert_null (locations);
    g_assert_nonnull (error);
}

static void
remove_tree (const char *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    const char *name;

    while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
    {
        g_autofree char *child = g_build_filename (path, name, NULL);

        if (g_file_test (child, G_FILE_TEST_IS_DIR) && !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
            remove_tree (child);
        else
            g_unlink (child);
    }
    if (dir != NULL)
        g_dir_close (dir);
    g_rmdir (path);
}

int
main (int    argc,
      char **argv)
{
    g_autofree char *root = g_dir_make_tmp ("cloud-core-XXXXXX", NULL);
    g_autofree char *config_home = g_build_filename (root, "config", NULL);
    g_autofree char *path = NULL;
    int status;

    /* Before GLib caches the user directories. */
    data_home = g_build_filename (root, "data", NULL);
    bin_dir = g_build_filename (root, "bin", NULL);
    g_mkdir (bin_dir, 0755);
    g_setenv ("HOME", root, TRUE);
    g_setenv ("XDG_DATA_HOME", data_home, TRUE);
    g_setenv ("XDG_CONFIG_HOME", config_home, TRUE);
    path = g_strconcat (bin_dir, ":", g_getenv ("PATH"), NULL);
    g_setenv ("PATH", path, TRUE);

    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/location/new", test_location_new);
    g_test_add_func ("/location/identifier", test_location_identifier);
    g_test_add_func ("/location/find-containing", test_location_find_containing);
    g_test_add_func ("/location/list-equal", test_location_list_equal);
    g_test_add_func ("/mounts/unescape", test_mounts_unescape);
    g_test_add_func ("/mounts/mountinfo", test_mounts_mountinfo);
    g_test_add_func ("/mounts/is-mount", test_mounts_is_mount);
    g_test_add_func ("/mounts/rclone-processes", test_mounts_rclone_processes);
    g_test_add_func ("/rclone/parse-json", test_rclone_parse_json);
    g_test_add_func ("/rclone/parse-json-invalid", test_rclone_parse_json_invalid);
    g_test_add_func ("/rclone/parse-plain", test_rclone_parse_plain);
    g_test_add_func ("/rclone/list-json", test_rclone_list_json);
    g_test_add_func ("/rclone/list-plain-fallback", test_rclone_list_plain_fallback);
    g_test_add_func ("/rclone/list-failure", test_rclone_list_failure);
    g_test_add_func ("/rclone/mount-argv", test_rclone_mount_argv);
    g_test_add_func ("/rclone/unit-matches-argv", test_rclone_unit_matches_argv);
    g_test_add_func ("/systemd/escape", test_systemd_escape);
    g_test_add_func ("/systemd/unit-name", test_systemd_unit_name);
    g_test_add_func ("/systemd/list-enabled", test_systemd_list_enabled);
    g_test_add_func ("/systemd/plan", test_systemd_plan);
    g_test_add_func ("/systemd/unit-installed", test_systemd_unit_installed);
    g_test_add_func ("/command/run", test_command_run);
    g_test_add_func ("/discovery/build-locations", test_discovery_build_locations);
    g_test_add_func ("/discovery/mount-points", test_discovery_mount_points);
    g_test_add_func ("/discovery/scan", test_discovery_scan);
    status = g_test_run ();

    remove_tree (root);
    g_free (data_home);
    g_free (bin_dir);
    return status;
}
