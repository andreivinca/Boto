/* Integration test: load the built module into a fake Nautilus window.
 *
 * The fake widgets reuse the Nautilus type names and properties the extension
 * looks for. Run through run-sidebar-test.sh, which provides a headless display.
 */

#include <adwaita.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <string.h>

#define LIVE "cloud-sidebar-test-live"

/* NautilusWindowSlot: "location" and "active" properties, plus slot.open-location. */

typedef struct
{
    GtkBox parent_instance;
    GFile *location;
    gboolean active;
} NautilusWindowSlot;

typedef GtkBoxClass NautilusWindowSlotClass;

G_DEFINE_TYPE (NautilusWindowSlot, nautilus_window_slot, GTK_TYPE_BOX)

enum { SLOT_PROP_LOCATION = 1, SLOT_PROP_ACTIVE };

static char *opened_uri;

static void
open_location_activated (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    g_free (opened_uri);
    opened_uri = g_variant_dup_string (parameter, NULL);
}

static void
nautilus_window_slot_get_property (GObject    *object,
                                   guint       id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    NautilusWindowSlot *slot = (NautilusWindowSlot *) object;

    if (id == SLOT_PROP_LOCATION)
        g_value_set_object (value, slot->location);
    else
        g_value_set_boolean (value, slot->active);
}

static void
nautilus_window_slot_set_property (GObject      *object,
                                   guint         id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    NautilusWindowSlot *slot = (NautilusWindowSlot *) object;

    if (id == SLOT_PROP_LOCATION)
        g_set_object (&slot->location, g_value_get_object (value));
    else
        slot->active = g_value_get_boolean (value);
}

static void
nautilus_window_slot_dispose (GObject *object)
{
    g_clear_object (&((NautilusWindowSlot *) object)->location);
    G_OBJECT_CLASS (nautilus_window_slot_parent_class)->dispose (object);
}

static void
nautilus_window_slot_init (NautilusWindowSlot *slot)
{
    g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
    g_autoptr(GSimpleAction) action = g_simple_action_new ("open-location", G_VARIANT_TYPE_STRING);

    g_signal_connect (action, "activate", G_CALLBACK (open_location_activated), NULL);
    g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (action));
    gtk_widget_insert_action_group (GTK_WIDGET (slot), "slot", G_ACTION_GROUP (group));
}

static void
nautilus_window_slot_class_init (NautilusWindowSlotClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = nautilus_window_slot_get_property;
    object_class->set_property = nautilus_window_slot_set_property;
    object_class->dispose = nautilus_window_slot_dispose;
    g_object_class_install_property (object_class, SLOT_PROP_LOCATION,
                                     g_param_spec_object ("location", NULL, NULL, G_TYPE_FILE,
                                                          G_PARAM_READWRITE));
    g_object_class_install_property (object_class, SLOT_PROP_ACTIVE,
                                     g_param_spec_boolean ("active", NULL, NULL, FALSE,
                                                           G_PARAM_READWRITE));
}

/* NautilusSidebarRow: a native sidebar row with a "uri" property. */

typedef struct
{
    GtkListBoxRow parent_instance;
    char *uri;
} NautilusSidebarRow;

typedef GtkListBoxRowClass NautilusSidebarRowClass;

G_DEFINE_TYPE (NautilusSidebarRow, nautilus_sidebar_row, GTK_TYPE_LIST_BOX_ROW)

static void
nautilus_sidebar_row_get_property (GObject    *object,
                                   guint       id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    g_value_set_string (value, ((NautilusSidebarRow *) object)->uri);
}

static void
nautilus_sidebar_row_set_property (GObject      *object,
                                   guint         id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    g_free (((NautilusSidebarRow *) object)->uri);
    ((NautilusSidebarRow *) object)->uri = g_value_dup_string (value);
}

static void
nautilus_sidebar_row_finalize (GObject *object)
{
    g_free (((NautilusSidebarRow *) object)->uri);
    G_OBJECT_CLASS (nautilus_sidebar_row_parent_class)->finalize (object);
}

static void
nautilus_sidebar_row_init (NautilusSidebarRow *row)
{
}

static void
nautilus_sidebar_row_class_init (NautilusSidebarRowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = nautilus_sidebar_row_get_property;
    object_class->set_property = nautilus_sidebar_row_set_property;
    object_class->finalize = nautilus_sidebar_row_finalize;
    g_object_class_install_property (object_class, 1,
                                     g_param_spec_string ("uri", NULL, NULL, NULL, G_PARAM_READWRITE));
}

/* NautilusWindow */

typedef GtkApplicationWindow NautilusWindow;
typedef GtkApplicationWindowClass NautilusWindowClass;

G_DEFINE_TYPE (NautilusWindow, nautilus_window, GTK_TYPE_APPLICATION_WINDOW)

static void
nautilus_window_init (NautilusWindow *window)
{
}

static void
nautilus_window_class_init (NautilusWindowClass *klass)
{
}

/* NautilusGtkPlacesSidebar: its CSS name scopes the theme's sidebar row rules. */

typedef GtkBox NautilusGtkPlacesSidebar;
typedef GtkBoxClass NautilusGtkPlacesSidebarClass;

G_DEFINE_TYPE (NautilusGtkPlacesSidebar, nautilus_gtk_places_sidebar, GTK_TYPE_BOX)

static void
nautilus_gtk_places_sidebar_init (NautilusGtkPlacesSidebar *sidebar)
{
}

static void
nautilus_gtk_places_sidebar_class_init (NautilusGtkPlacesSidebarClass *klass)
{
    gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "placessidebar");
}

/* Minimal stand-in for Nautilus' module loader. */

typedef GTypeModule TestModule;
typedef GTypeModuleClass TestModuleClass;

G_DEFINE_TYPE (TestModule, test_module, G_TYPE_TYPE_MODULE)

static gboolean
test_module_load (GTypeModule *module)
{
    return TRUE;
}

static void
test_module_unload (GTypeModule *module)
{
}

static void
test_module_init (TestModule *module)
{
}

static void
test_module_class_init (TestModuleClass *klass)
{
    G_TYPE_MODULE_CLASS (klass)->load = test_module_load;
    G_TYPE_MODULE_CLASS (klass)->unload = test_module_unload;
}

/* Test state */

typedef struct
{
    GtkWidget *window;
    GtkWidget *slot;
    GtkWidget *native;
    GtkWidget *home_row;
    GtkWidget *offline_row;
    GtkWidget *live_row;
} FakeWindow;

static const char *module_path;
static char *root;
static char *diagnostics;
static char *offline_id;
static char *offline_path;
static GtkApplication *app;
static GObject *extension;
static FakeWindow first;
static GSubprocess *live_mount;

static char *
root_path (const char *name)
{
    return g_build_filename (root, name, NULL);
}

static void
write_file (const char *name,
            const char *contents,
            int         mode)
{
    g_autofree char *path = root_path (name);

    g_assert_true (g_file_set_contents (path, contents, -1, NULL));
    g_assert_cmpint (g_chmod (path, mode), ==, 0);
}

static char *
read_file (const char *name)
{
    g_autofree char *path = root_path (name);
    char *contents = NULL;

    g_file_get_contents (path, &contents, NULL, NULL);
    return contents;
}

static gboolean
wake (gpointer user_data)
{
    return G_SOURCE_CONTINUE;
}

/* Iterate the main loop until CONDITION holds, failing after TIMEOUT_SECONDS. */
#define WAIT_UNTIL(condition, timeout_seconds) G_STMT_START {                       \
    gint64 deadline = g_get_monotonic_time () + (timeout_seconds) * G_USEC_PER_SEC; \
    guint waker = g_timeout_add (20, wake, NULL);                                   \
    while (!(condition) && g_get_monotonic_time () < deadline)                      \
        g_main_context_iteration (NULL, TRUE);                                      \
    g_source_remove (waker);                                                        \
    if (!(condition))                                                               \
        g_error ("Timed out waiting for %s", #condition);                           \
} G_STMT_END

static void
settle (void)
{
    gint64 until = g_get_monotonic_time () + G_USEC_PER_SEC / 2;

    WAIT_UNTIL (g_get_monotonic_time () >= until, 1);
}

static GtkWidget *
find_named (GtkWidget  *widget,
            const char *name)
{
    if (g_strcmp0 (gtk_widget_get_name (widget), name) == 0)
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_named (child, name);

        if (found != NULL)
            return found;
    }
    return NULL;
}

static GtkListBox *
cloud_list (GtkWidget *win)
{
    return (GtkListBox *) find_named (win, "cloud-locations");
}

static guint
row_count (GtkListBox *list)
{
    guint count = 0;

    for (GtkWidget *child = gtk_widget_get_first_child (GTK_WIDGET (list)); child != NULL;
         child = gtk_widget_get_next_sibling (child))
        if (GTK_IS_LIST_BOX_ROW (child))
            count++;
    return count;
}

static JsonNode *
read_diagnostics (void)
{
    g_autoptr(JsonParser) parser = json_parser_new ();

    if (!json_parser_load_from_file (parser, diagnostics, NULL) || json_parser_get_root (parser) == NULL)
        return NULL;
    return json_node_copy (json_parser_get_root (parser));
}

static guint
diagnostics_windows (void)
{
    g_autoptr(JsonNode) node = read_diagnostics ();

    return node ? json_array_get_length (json_node_get_array (node)) : 0;
}

static char *
joined_strings (JsonObject *object,
                const char *member)
{
    JsonArray *array = json_object_get_array_member (object, member);
    GString *result = g_string_new (NULL);

    for (guint i = 0; i < json_array_get_length (array); i++)
        g_string_append_printf (result, "%s%s", i ? "," : "", json_array_get_string_element (array, i));
    return g_string_free (result, FALSE);
}

/* Built like nautilus-sidebar-row.ui in Nautilus 51. */
static GtkWidget *
add_native_row (GtkWidget  *list,
                const char *label,
                const char *uri)
{
    GtkWidget *row = g_object_new (nautilus_sidebar_row_get_type (), "uri", uri, NULL);
    GtkWidget *revealer = gtk_revealer_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *icon = gtk_image_new_from_icon_name ("folder-symbolic");
    GtkWidget *text = gtk_label_new (label);

    gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), TRUE);
    gtk_widget_add_css_class (revealer, "sidebar-revealer");
    gtk_widget_set_margin_end (icon, 8);
    gtk_box_append (GTK_BOX (box), icon);
    gtk_label_set_xalign (GTK_LABEL (text), 0);
    gtk_widget_set_hexpand (text, TRUE);
    gtk_widget_set_margin_end (text, 2);
    gtk_box_append (GTK_BOX (box), text);
    gtk_revealer_set_child (GTK_REVEALER (revealer), box);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), revealer);
    gtk_list_box_append (GTK_LIST_BOX (list), row);
    return row;
}

/* Sidebar on the left, a slot with a column view on the right. */
static void
build_window (FakeWindow *fake)
{
    GtkWidget *content = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *places = g_object_new (nautilus_gtk_places_sidebar_get_type (), NULL);
    GtkWidget *scrolled = gtk_scrolled_window_new ();
    g_autoptr(GFile) start = g_file_new_for_path (root);
    g_autofree char *offline_uri = g_filename_to_uri (offline_path, NULL, NULL);
    GtkStringList *strings = gtk_string_list_new ((const char *[]) { "item", NULL });
    GtkWidget *view = gtk_column_view_new (GTK_SELECTION_MODEL (gtk_no_selection_new (G_LIST_MODEL (strings))));
    g_autoptr(GtkColumnViewColumn) column = gtk_column_view_column_new ("Name", NULL);

    fake->window = g_object_new (nautilus_window_get_type (), "application", app, NULL);
    fake->native = gtk_list_box_new ();
    gtk_widget_add_css_class (fake->native, "navigation-sidebar");
    fake->home_row = add_native_row (fake->native, "Home", "file:///home");
    fake->offline_row = add_native_row (fake->native, "Offline duplicate", offline_uri);
    fake->live_row = add_native_row (fake->native, "Live duplicate", "file:///proc");
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), fake->native);
    gtk_widget_set_size_request (scrolled, 200, 400);
    gtk_box_append (GTK_BOX (places), scrolled);
    gtk_box_append (GTK_BOX (content), places);

    fake->slot = g_object_new (nautilus_window_slot_get_type (), "orientation", GTK_ORIENTATION_VERTICAL,
                               "active", TRUE, "location", start, "hexpand", TRUE, NULL);
    gtk_column_view_append_column (GTK_COLUMN_VIEW (view), column);
    gtk_widget_set_vexpand (view, TRUE);
    gtk_box_append (GTK_BOX (fake->slot), view);
    gtk_box_append (GTK_BOX (content), fake->slot);

    gtk_window_set_child (GTK_WINDOW (fake->window), content);
    gtk_window_set_default_size (GTK_WINDOW (fake->window), 800, 500);
    gtk_window_present (GTK_WINDOW (fake->window));
}

static GtkWidget *
first_label_in_type (GtkWidget  *widget,
                     const char *type_name,
                     gboolean    inside)
{
    inside = inside || g_strcmp0 (G_OBJECT_TYPE_NAME (widget), type_name) == 0;
    if (inside && GTK_IS_LABEL (widget) && *gtk_label_get_label (GTK_LABEL (widget)) != '\0')
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = first_label_in_type (child, type_name, inside);

        if (found != NULL)
            return found;
    }
    return NULL;
}

static GtkWidget *
find_alert (void)
{
    g_autoptr(GList) toplevels = gtk_window_list_toplevels ();

    for (GList *l = toplevels; l != NULL; l = l->next)
        if (G_OBJECT_TYPE (l->data) != nautilus_window_get_type () && gtk_widget_get_visible (l->data))
            return l->data;
    return NULL;
}

static void
close_alerts (void)
{
    GtkWidget *alert;

    while ((alert = find_alert ()) != NULL)
        gtk_window_destroy (GTK_WINDOW (alert));
}

static void
test_injection (void)
{
    GtkListBox *cloud = NULL;
    GtkWidget *wrapper;
    GtkWidget *group;
    GtkWidget *heading;
    GtkWidget *row;

    build_window (&first);
    WAIT_UNTIL ((cloud = cloud_list (first.window)) != NULL && row_count (cloud) == 2, 10);

    wrapper = find_named (first.window, "cloud-sidebar-wrapper");
    g_assert_nonnull (wrapper);
    g_assert_true (gtk_widget_get_parent (first.native) == wrapper);
    g_assert_true (GTK_IS_VIEWPORT (gtk_widget_get_parent (wrapper)));
    group = find_named (first.window, "cloud-sidebar-group");
    g_assert_true (gtk_widget_get_parent (group) == wrapper);
    g_assert_true (gtk_widget_get_prev_sibling (group) == first.native);
    g_assert_true (gtk_widget_get_visible (group));
    heading = find_named (first.window, "cloud-section-heading");
    g_assert_cmpstr (gtk_label_get_label (GTK_LABEL (heading)), ==, "Cloud");

    /* Sorted case-insensitively; the combine remote is skipped. */
    row = gtk_widget_get_first_child (GTK_WIDGET (cloud));
    g_assert_cmpstr (gtk_widget_get_tooltip_text (row), ==, LIVE);
    g_assert_cmpstr (gtk_widget_get_tooltip_text (gtk_widget_get_next_sibling (row)), ==, "Offline");

    /* Native rows for cloud mount points are hidden as duplicates. */
    g_assert_true (gtk_widget_get_visible (first.home_row));
    g_assert_false (gtk_widget_get_visible (first.offline_row));
    g_assert_false (gtk_widget_get_visible (first.live_row));
}

static void
test_heading_matches_columns (void)
{
    GtkWidget *heading = find_named (first.window, "cloud-section-heading");
    GtkWidget *title = first_label_in_type (first.window, "GtkColumnViewTitle", FALSE);
    const PangoFontDescription *expected;
    const PangoFontDescription *actual;

    g_assert_nonnull (title);
    expected = pango_context_get_font_description (gtk_widget_get_pango_context (title));
    actual = pango_context_get_font_description (gtk_widget_get_pango_context (heading));
    g_assert_cmpstr (pango_font_description_get_family (actual), ==,
                     pango_font_description_get_family (expected));
    g_assert_cmpint (pango_font_description_get_size (actual), ==, pango_font_description_get_size (expected));
    g_assert_cmpint (pango_font_description_get_weight (actual), ==,
                     pango_font_description_get_weight (expected));
}

static GtkWidget *
first_descendant (GtkWidget *widget,
                  GType      type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = first_descendant (child, type);

        if (found != NULL)
            return found;
    }
    return NULL;
}

static double
x_in_window (GtkWidget *widget)
{
    graphene_point_t point;

    g_assert_nonnull (widget);
    g_assert_true (gtk_widget_compute_point (widget, first.window, &GRAPHENE_POINT_INIT (0, 0), &point));
    return point.x;
}

/* Rows are rebuilt when a scan finishes; returns the first one once it is laid out. */
static GtkWidget *
first_cloud_row (void)
{
    GtkWidget *row = NULL;

    WAIT_UNTIL ((row = GTK_WIDGET (gtk_list_box_get_row_at_index (cloud_list (first.window), 0))) != NULL &&
                gtk_widget_get_width (first_descendant (row, GTK_TYPE_IMAGE)) > 0, 5);
    return row;
}

static void
test_rows_align_with_native (void)
{
    GtkWidget *ours = first_cloud_row ();
    GtkWidget *heading = find_named (first.window, "cloud-section-heading");
    double icon_x = x_in_window (first_descendant (first.home_row, GTK_TYPE_IMAGE));

    /* Icons and labels in the same columns as Nautilus' rows; the heading over the icons. */
    g_assert_cmpfloat_with_epsilon (x_in_window (first_descendant (ours, GTK_TYPE_IMAGE)), icon_x, 0.5);
    g_assert_cmpfloat_with_epsilon (x_in_window (first_descendant (ours, GTK_TYPE_LABEL)),
                                    x_in_window (first_descendant (first.home_row, GTK_TYPE_LABEL)), 0.5);
    g_assert_cmpfloat_with_epsilon (x_in_window (heading), icon_x, 0.5);
}

/* Vertical runs of rendered ink (non-transparent pixel rows) in WIDGET: start, end, start, end… */
static GArray *
ink_runs (GtkWidget *widget)
{
    int width = gtk_widget_get_width (widget);
    int height = gtk_widget_get_height (widget);
    g_autoptr(GdkPaintable) paintable = gtk_widget_paintable_new (widget);
    GtkSnapshot *snapshot = gtk_snapshot_new ();
    g_autoptr(GskRenderNode) node = NULL;
    g_autoptr(GskRenderer) renderer = gsk_cairo_renderer_new ();
    g_autoptr(GdkTexture) texture = NULL;
    g_autofree guchar *pixels = g_malloc ((gsize) width * height * 4);
    GArray *runs = g_array_new (FALSE, FALSE, sizeof (int));
    int start = -1;

    gdk_paintable_snapshot (paintable, snapshot, width, height);
    node = gtk_snapshot_free_to_node (snapshot);
    g_assert_true (gsk_renderer_realize_for_display (renderer, gtk_widget_get_display (widget), NULL));
    texture = gsk_renderer_render_texture (renderer, node, &GRAPHENE_RECT_INIT (0, 0, width, height));
    gsk_renderer_unrealize (renderer);
    gdk_texture_download (texture, pixels, (gsize) width * 4);
    for (int y = 0; y <= height; y++)
    {
        gboolean ink = FALSE;

        for (int x = 0; y < height && x < width && !ink; x++)
            ink = pixels[((gsize) y * width + x) * 4 + 3] > 20;
        if (ink && start < 0)
            start = y;
        if (!ink && start >= 0)
        {
            int run[2] = { start, y - 1 };

            g_array_append_vals (runs, run, 2);
            start = -1;
        }
    }
    return runs;
}

static void
test_heading_spacing (void)
{
    g_autoptr(GArray) runs = NULL;
    int *run;
    int above, below;

    first_cloud_row ();
    runs = ink_runs (find_named (first.window, "cloud-sidebar-group"));
    run = (int *) runs->data;

    /* Separator, heading, first row: as much space below the heading as above it. */
    g_assert_cmpuint (runs->len, >=, 6);
    above = run[2] - run[1] - 1;
    below = run[4] - run[3] - 1;
    g_test_message ("space above the heading %d px, below %d px", above, below);
    g_assert_cmpint (ABS (above - below), <=, 1);
}

static void
test_diagnostics (void)
{
    g_autoptr(JsonNode) node = NULL;
    g_autofree char *names = NULL;
    g_autofree char *start_uri = g_filename_to_uri (root, NULL, NULL);
    JsonObject *state;

    WAIT_UNTIL ((node = read_diagnostics ()) != NULL, 5);
    g_assert_cmpuint (json_array_get_length (json_node_get_array (node)), ==, 1);
    state = json_array_get_object_element (json_node_get_array (node), 0);
    names = joined_strings (state, "names");
    g_assert_cmpstr (json_object_get_string_member (state, "heading"), ==, "Cloud");
    g_assert_cmpstr (names, ==, LIVE ",Offline");
    g_assert_cmpint (json_object_get_int_member (state, "rows"), ==, 2);
    g_assert_cmpint (json_object_get_int_member (state, "hidden_duplicates"), ==, 2);
    g_assert_cmpstr (json_object_get_string_member (state, "location"), ==, start_uri);
    g_assert_cmpuint (json_array_get_length (json_object_get_array_member (state, "selected")), ==, 0);
}

static void
test_selection_follows_location (void)
{
    GtkListBox *cloud = cloud_list (first.window);
    g_autoptr(GFile) inside = g_file_new_for_path ("/proc/self");
    g_autoptr(GFile) outside = g_file_new_for_path (root);

    gtk_list_box_select_row (GTK_LIST_BOX (first.native), GTK_LIST_BOX_ROW (first.home_row));
    g_object_set (first.slot, "location", inside, NULL);
    WAIT_UNTIL (gtk_list_box_get_selected_row (cloud) != NULL, 5);
    g_assert_cmpstr (gtk_widget_get_tooltip_text (GTK_WIDGET (gtk_list_box_get_selected_row (cloud))), ==, LIVE);
    g_assert_null (gtk_list_box_get_selected_row (GTK_LIST_BOX (first.native)));

    g_object_set (first.slot, "location", outside, NULL);
    WAIT_UNTIL (gtk_list_box_get_selected_row (cloud) == NULL, 5);
}

static void
test_open_mounted (void)
{
    GtkListBox *cloud = cloud_list (first.window);

    g_clear_pointer (&opened_uri, g_free);
    g_signal_emit_by_name (cloud, "row-activated", gtk_list_box_get_row_at_index (cloud, 0));
    g_assert_cmpstr (opened_uri, ==, "file:///proc");
}

static void
test_mount_daemon (void)
{
    GtkListBox *cloud = cloud_list (first.window);
    GtkListBoxRow *row = gtk_list_box_get_row_at_index (cloud, 1);
    g_autofree char *cache = g_build_filename (root, "cache", "nautilus-cloud-sidebar", NULL);
    g_autofree char *expected = g_strdup_printf (
        "mount --daemon --daemon-wait 20s --vfs-cache-mode full --cache-dir %s "
        "--vfs-cache-max-size 5G --vfs-cache-max-age 168h --vfs-cache-min-free-space 10G "
        "--log-file %s/%s.log -- Offline: %s\n",
        cache, cache, offline_id, offline_path);
    g_autofree char *log = NULL;

    g_clear_pointer (&opened_uri, g_free);
    g_signal_emit_by_name (cloud, "row-activated", row);
    /* A second click while connecting is ignored. */
    g_signal_emit_by_name (cloud, "row-activated", row);
    WAIT_UNTIL (find_alert () != NULL, 5);
    log = read_file ("mount.log");
    g_assert_cmpstr (log, ==, expected);
    g_assert_null (opened_uri);
    g_assert_true (g_file_test (offline_path, G_FILE_TEST_IS_DIR));
    g_assert_true (g_file_test (cache, G_FILE_TEST_IS_DIR));
    close_alerts ();
}

static void
test_mount_systemd (void)
{
    GtkListBox *cloud = cloud_list (first.window);
    g_autofree char *bus_dir = g_build_filename (g_getenv ("XDG_RUNTIME_DIR"), "systemd", NULL);
    g_autofree char *bus = g_build_filename (bus_dir, "private", NULL);
    g_autofree char *unit_dir = g_build_filename (root, "config", "systemd", "user", NULL);
    g_autofree char *unit = g_build_filename (unit_dir, "nautilus-cloud@.service", NULL);
    g_autofree char *mount_log = root_path ("mount.log");
    g_autofree char *log = NULL;

    /* With a user manager and the unit installed, a click starts the remote's unit. */
    g_mkdir_with_parents (bus_dir, 0700);
    g_mkdir_with_parents (unit_dir, 0700);
    g_assert_true (g_file_set_contents (bus, "", 0, NULL));
    g_assert_true (g_file_set_contents (unit, "", 0, NULL));
    g_unlink (mount_log);
    g_signal_emit_by_name (cloud, "row-activated", gtk_list_box_get_row_at_index (cloud, 1));
    WAIT_UNTIL (find_alert () != NULL, 5);
    g_unlink (bus);
    g_unlink (unit);
    log = read_file ("mount.log");
    g_assert_cmpstr (log, ==, "systemctl --user start nautilus-cloud@Offline.service\n");
    close_alerts ();
}

static void
test_rescan (void)
{
    GtkListBox *cloud = cloud_list (first.window);

    if (!g_test_slow ())
    {
        g_test_skip ("waits for the 30 second rescan; run with -m slow");
        return;
    }
    /* Removing a remote drops its row and restores the native duplicate. */
    write_file ("remotes.json", "[{\"name\":\"" LIVE "\",\"type\":\"drive\"}]", 0644);
    WAIT_UNTIL (row_count (cloud) == 1, 40);
    g_assert_true (gtk_widget_get_visible (first.offline_row));
    g_assert_false (gtk_widget_get_visible (first.live_row));
}

static GtkEventController *
find_drop_motion (GtkWidget *widget)
{
    g_autoptr(GListModel) controllers = gtk_widget_observe_controllers (widget);

    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr(GtkEventController) controller = g_list_model_get_item (controllers, i);

        /* Still owned by the widget after this reference is dropped. */
        if (GTK_IS_DROP_CONTROLLER_MOTION (controller))
            return controller;
    }
    return NULL;
}

static void
test_drag_dims_rows (void)
{
    GtkWidget *wrapper = find_named (first.window, "cloud-sidebar-wrapper");
    GtkWidget *list = GTK_WIDGET (cloud_list (first.window));
    GtkEventController *motion = find_drop_motion (wrapper);

    /* Our rows accept no drops, so a drag over the sidebar greys them out. */
    g_assert_nonnull (motion);
    g_assert_true (gtk_widget_get_sensitive (list));
    g_signal_emit_by_name (motion, "enter", 10.0, 10.0);
    g_assert_false (gtk_widget_get_sensitive (list));
    g_assert_true (gtk_widget_get_sensitive (first.home_row));
    g_signal_emit_by_name (motion, "leave");
    WAIT_UNTIL (gtk_widget_get_sensitive (list), 5);
}

static void
test_release_closed (void)
{
    GtkWidget *content = gtk_widget_get_parent (first.slot);
    GtkWidget *tab = g_object_new (nautilus_window_slot_get_type (), NULL);
    GtkWidget *row = add_native_row (first.native, "Live again", "file:///proc");
    g_autoptr(GFile) somewhere = g_file_new_for_path ("/");

    g_object_add_weak_pointer (G_OBJECT (tab), (gpointer *) &tab);
    g_object_add_weak_pointer (G_OBJECT (row), (gpointer *) &row);
    gtk_box_append (GTK_BOX (content), tab);
    g_object_set (first.slot, "location", somewhere, NULL);
    WAIT_UNTIL (!gtk_widget_get_visible (row), 5);
    settle ();

    /* A closed tab and a row Nautilus dropped are not kept alive. */
    gtk_box_remove (GTK_BOX (content), tab);
    gtk_list_box_remove (GTK_LIST_BOX (first.native), row);
    g_object_set (first.slot, "location", NULL, NULL);
    WAIT_UNTIL (tab == NULL && row == NULL, 5);
}

static void
test_second_window (void)
{
    FakeWindow second;

    build_window (&second);
    WAIT_UNTIL (cloud_list (second.window) != NULL, 10);
    WAIT_UNTIL (diagnostics_windows () == 2, 5);
    g_assert_false (gtk_widget_get_visible (second.live_row));

    gtk_window_destroy (GTK_WINDOW (first.window));
    first = second;
    settle ();
    /* Any refresh rewrites the report without the closed window. */
    g_object_set (first.slot, "location", NULL, NULL);
    WAIT_UNTIL (diagnostics_windows () == 1, 5);
}

static void
setup_environment (void)
{
    g_autofree char *bin = NULL;
    g_autofree char *data = NULL;
    g_autofree char *cache = NULL;
    g_autofree char *path = NULL;
    g_autofree char *rclone = NULL;
    g_autofree char *systemctl = NULL;
    g_autofree char *config = NULL;
    g_autofree char *live_rclone = NULL;
    g_autofree char *digest = g_compute_checksum_for_string (G_CHECKSUM_SHA256, "Offline", -1);
    g_autoptr(GError) error = NULL;

    root = g_dir_make_tmp ("cloud-sidebar-XXXXXX", NULL);
    bin = root_path ("bin");
    data = root_path ("data");
    cache = root_path ("cache");
    config = root_path ("config");
    g_mkdir (bin, 0755);
    diagnostics = root_path ("diagnostics.json");
    offline_id = g_strndup (digest, 20);
    offline_path = g_build_filename (root, "Cloud", "Offline", NULL);

    /* Before GLib caches the user directories. */
    g_setenv ("HOME", root, TRUE);
    g_setenv ("XDG_DATA_HOME", data, TRUE);
    g_setenv ("XDG_CACHE_HOME", cache, TRUE);
    g_setenv ("XDG_CONFIG_HOME", config, TRUE);
    g_setenv ("NAUTILUS_CLOUD_DIAGNOSTICS", diagnostics, TRUE);
    path = g_strconcat (bin, ":", g_getenv ("PATH"), NULL);
    g_setenv ("PATH", path, TRUE);

    write_file ("remotes.json",
                "[{\"name\":\"Offline\",\"type\":\"s3\"},{\"name\":\"" LIVE "\",\"type\":\"drive\"},"
                "{\"name\":\"everything\",\"type\":\"combine\"}]", 0644);
    rclone = g_strdup_printf ("#!/bin/sh\n"
                              "case \"$1\" in\n"
                              "listremotes) cat '%1$s/remotes.json' ;;\n"
                              "mount) if [ \"$2\" = " LIVE ": ]; then\n"
                              "           while kill -0 $PPID 2>/dev/null; do sleep 0.2; done; exit 0\n"
                              "       fi\n"
                              "       echo \"$*\" >> '%1$s/mount.log'; exit 1 ;;\n"
                              "esac\n", root);
    write_file ("bin/rclone", rclone, 0755);
    /* Never reach the real user manager from a test. */
    systemctl = g_strdup_printf ("#!/bin/sh\necho systemctl \"$*\" >> '%s/mount.log'\nexit 1\n", root);
    write_file ("bin/systemctl", systemctl, 0755);

    /* An rclone process mounting a real mount point counts as an existing mount. */
    live_rclone = g_build_filename (bin, "rclone", NULL);
    live_mount = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &error,
                                   live_rclone, "mount", LIVE ":", "/proc", NULL);
    g_assert_no_error (error);
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

/* Broadway sometimes reports frame timing glitches when idle; they are not failures. */
static GLogWriterOutput
log_writer (GLogLevelFlags   level,
            const GLogField *fields,
            gsize            n_fields,
            gpointer         user_data)
{
    const char *domain = NULL;
    const char *message = NULL;

    for (gsize i = 0; i < n_fields; i++)
    {
        if (g_strcmp0 (fields[i].key, "GLIB_DOMAIN") == 0)
            domain = fields[i].value;
        else if (g_strcmp0 (fields[i].key, "MESSAGE") == 0)
            message = fields[i].value;
    }
    if (g_strcmp0 (domain, "Gdk") == 0 && message != NULL && strstr (message, "gdk_frame_timings_submitted"))
        return G_LOG_WRITER_HANDLED;
    return g_log_writer_default (level, fields, n_fields, user_data);
}

static void
activate (GApplication *application,
          int          *status)
{
    GModule *module = g_module_open (module_path, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    GTypeModule *type_module = g_object_new (test_module_get_type (), NULL);
    void (*initialize) (GTypeModule *) = NULL;
    void (*list_types) (const GType **, int *) = NULL;
    const GType *types;
    int count;

    g_application_hold (application);
    /* Style everything with libadwaita's stylesheet, as in Nautilus. */
    adw_init ();
    g_assert_nonnull (module);
    g_assert_true (g_module_symbol (module, "nautilus_module_initialize", (gpointer *) &initialize));
    g_assert_true (g_module_symbol (module, "nautilus_module_list_types", (gpointer *) &list_types));
    g_type_module_use (type_module);
    initialize (type_module);
    list_types (&types, &count);
    g_assert_cmpint (count, ==, 1);
    extension = g_object_new (types[0], NULL);

    *status = g_test_run ();

    gtk_window_destroy (GTK_WINDOW (first.window));
    g_application_release (application);
}

int
main (int    argc,
      char **argv)
{
    g_autofree char *bus = NULL;
    int status = 1;

    g_test_init (&argc, &argv, NULL);
    if (argc < 2)
    {
        g_printerr ("Usage: %s MODULE\n", argv[0]);
        return 2;
    }
    module_path = argv[1];
    g_log_set_writer_func (log_writer, NULL, NULL);
    bus =g_build_filename (g_get_user_runtime_dir (), "systemd", "private", NULL);
    if (g_getenv ("XDG_RUNTIME_DIR") == NULL || g_file_test (bus, G_FILE_TEST_EXISTS))
    {
        g_printerr ("Run through run-sidebar-test.sh, which provides a private runtime directory\n");
        return 2;
    }
    setup_environment ();

    g_test_add_func ("/sidebar/injection", test_injection);
    g_test_add_func ("/sidebar/heading-matches-columns", test_heading_matches_columns);
    g_test_add_func ("/sidebar/rows-align-with-native", test_rows_align_with_native);
    g_test_add_func ("/sidebar/heading-spacing", test_heading_spacing);
    g_test_add_func ("/sidebar/diagnostics", test_diagnostics);
    g_test_add_func ("/sidebar/selection-follows-location", test_selection_follows_location);
    g_test_add_func ("/sidebar/open-mounted", test_open_mounted);
    g_test_add_func ("/sidebar/mount-daemon", test_mount_daemon);
    g_test_add_func ("/sidebar/mount-systemd", test_mount_systemd);
    g_test_add_func ("/sidebar/rescan", test_rescan);
    g_test_add_func ("/sidebar/drag-dims-rows", test_drag_dims_rows);
    g_test_add_func ("/sidebar/release-closed", test_release_closed);
    g_test_add_func ("/sidebar/second-window", test_second_window);

    app = gtk_application_new ("org.example.CloudSidebarTest", G_APPLICATION_NON_UNIQUE);
    g_application_set_default (G_APPLICATION (app));
    g_signal_connect (app, "activate", G_CALLBACK (activate), &status);
    g_application_run (G_APPLICATION (app), 1, argv);

    g_subprocess_force_exit (live_mount);
    g_object_unref (live_mount);
    remove_tree (root);
    return status;
}
