/* Everything that depends on Nautilus' private widget structure. */

#include "nautilus/cloud-nautilus.h"

#define WINDOW_TYPE "NautilusWindow"
#define SLOT_TYPE "NautilusWindowSlot"
#define COLUMN_TITLE_TYPE "GtkColumnViewTitle"
#define MY_COMPUTER_WRAPPER "sidebar_my_computer_wrapper"
#define MY_COMPUTER_LIST "sidebar_my_computer_listbox"

/* Wait this many attempts (of 250 ms) for Nautilus My Computer to wrap the sidebar. */
#define MY_COMPUTER_ATTEMPTS 12

/* Pre-order walk of a widget tree: for (w = root; w; w = next_widget (w, root)). */
static GtkWidget *
next_widget (GtkWidget *widget,
             GtkWidget *root)
{
    GtkWidget *child = gtk_widget_get_first_child (widget);

    if (child != NULL)
        return child;
    for (; widget != NULL && widget != root; widget = gtk_widget_get_parent (widget))
    {
        GtkWidget *sibling = gtk_widget_get_next_sibling (widget);

        if (sibling != NULL)
            return sibling;
    }
    return NULL;
}

static gboolean
has_type_name (gpointer    object,
               const char *name)
{
    return g_strcmp0 (G_OBJECT_TYPE_NAME (object), name) == 0;
}

/* Whether OBJECT has a property NAME holding values of TYPE. */
static gboolean
has_property (gpointer    object,
              const char *name,
              GType       type)
{
    GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);

    return pspec != NULL && g_type_is_a (pspec->value_type, type);
}

gboolean
cloud_nautilus_is_window (GtkWindow *window)
{
    return has_type_name (window, WINDOW_TYPE);
}

static gboolean
is_native_sidebar (GtkWidget *widget)
{
    const char *name;

    if (!GTK_IS_LIST_BOX (widget))
        return FALSE;
    name = gtk_widget_get_name (widget);
    return g_strcmp0 (name, MY_COMPUTER_LIST) != 0 && g_strcmp0 (name, CLOUD_LIST_NAME) != 0 &&
           (gtk_widget_has_css_class (widget, "navigation-sidebar") ||
            gtk_widget_has_css_class (widget, "places-sidebar-list"));
}

static gboolean
my_computer_installed (void)
{
    g_autofree char *path = g_build_filename (g_get_home_dir (), ".local", "share", "nautilus-python",
                                              "extensions", "nautilus-my-computer.py", NULL);

    return g_file_test (path, G_FILE_TEST_EXISTS);
}

/* Puts NATIVE in a vertical box inside its scrolled window, so groups can follow it. */
static GtkWidget *
wrap_sidebar (GtkWidget *native)
{
    GtkWidget *scrolled = gtk_widget_get_parent (native);
    GtkWidget *content;
    GtkWidget *wrapper;

    while (scrolled != NULL && !GTK_IS_SCROLLED_WINDOW (scrolled))
        scrolled = gtk_widget_get_parent (scrolled);
    if (scrolled == NULL)
        return NULL;
    content = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (scrolled));
    if (GTK_IS_VIEWPORT (content))
        content = gtk_viewport_get_child (GTK_VIEWPORT (content));
    /* Never replace an unknown wrapper owned by another extension. */
    if (content != native)
        return NULL;

    wrapper = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name (wrapper, CLOUD_WRAPPER_NAME);
    /* Replacing the child destroys the old viewport; keep the list alive meanwhile. */
    g_object_ref (native);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), wrapper);
    gtk_box_append (GTK_BOX (wrapper), native);
    g_object_unref (native);
    return wrapper;
}

CloudSidebarStatus
cloud_nautilus_prepare_sidebar (GtkWidget  *window,
                                guint       attempt,
                                GtkWidget **native,
                                GtkWidget **container)
{
    GtkWidget *sidebar = NULL;
    GtkWidget *wrapper = NULL;

    for (GtkWidget *widget = window; widget != NULL; widget = next_widget (widget, window))
    {
        if (sidebar == NULL && is_native_sidebar (widget))
            sidebar = widget;
        if (wrapper == NULL && g_strcmp0 (gtk_widget_get_name (widget), MY_COMPUTER_WRAPPER) == 0)
            wrapper = widget;
    }
    if (sidebar == NULL)
        return CLOUD_SIDEBAR_RETRY;
    if (wrapper != NULL && !GTK_IS_BOX (wrapper))
    {
        g_warning ("Could not add Cloud sidebar: unexpected %s wrapper", G_OBJECT_TYPE_NAME (wrapper));
        return CLOUD_SIDEBAR_UNSUPPORTED;
    }
    if (wrapper == NULL)
    {
        /* Allow My Computer to wrap the sidebar first if it is installed. */
        if (my_computer_installed () && attempt < MY_COMPUTER_ATTEMPTS)
            return CLOUD_SIDEBAR_RETRY;
        wrapper = wrap_sidebar (sidebar);
        if (wrapper == NULL)
            return CLOUD_SIDEBAR_RETRY;
    }
    *native = sidebar;
    *container = wrapper;
    return CLOUD_SIDEBAR_READY;
}

void
cloud_nautilus_watch_window (GtkWidget *window,
                             GCallback  changed,
                             gpointer   object)
{
    g_signal_connect_object (window, "notify::title", changed, object, G_CONNECT_SWAPPED);
    if (has_property (window, "active-slot", G_TYPE_OBJECT))
        g_signal_connect_object (window, "notify::active-slot", changed, object, G_CONNECT_SWAPPED);
}

void
cloud_nautilus_watch_slot (GtkWidget *slot,
                           GCallback  changed,
                           gpointer   object)
{
    g_signal_connect_object (slot, "notify::location", changed, object, G_CONNECT_SWAPPED);
    if (has_property (slot, "active", G_TYPE_BOOLEAN))
        g_signal_connect_object (slot, "notify::active", changed, object, G_CONNECT_SWAPPED);
}

GPtrArray *
cloud_nautilus_list_slots (GtkWidget *window)
{
    GPtrArray *slots = g_ptr_array_new ();

    for (GtkWidget *widget = window; widget != NULL; widget = next_widget (widget, window))
        if (has_type_name (widget, SLOT_TYPE))
            g_ptr_array_add (slots, widget);
    return slots;
}

GtkWidget *
cloud_nautilus_get_active_slot (GtkWidget *window)
{
    GtkWidget *fallback = NULL;

    for (GtkWidget *widget = window; widget != NULL; widget = next_widget (widget, window))
    {
        gboolean active = FALSE;

        if (!has_type_name (widget, SLOT_TYPE))
            continue;
        if (has_property (widget, "active", G_TYPE_BOOLEAN))
            g_object_get (widget, "active", &active, NULL);
        if (active)
            return widget;
        if (gtk_widget_get_mapped (widget))
            fallback = widget;
    }
    return fallback;
}

GFile *
cloud_nautilus_slot_get_location (GtkWidget *slot)
{
    GFile *location = NULL;

    if (has_property (slot, "location", G_TYPE_FILE))
        g_object_get (slot, "location", &location, NULL);
    return location;
}

GPtrArray *
cloud_nautilus_list_place_rows (GtkWidget *window)
{
    GPtrArray *rows = g_ptr_array_new ();

    for (GtkWidget *widget = window; widget != NULL; widget = next_widget (widget, window))
        if (GTK_IS_LIST_BOX_ROW (widget) && has_property (widget, "uri", G_TYPE_STRING))
            g_ptr_array_add (rows, widget);
    return rows;
}

char *
cloud_nautilus_row_get_uri (GtkWidget *row)
{
    char *uri = NULL;

    if (has_property (row, "uri", G_TYPE_STRING))
        g_object_get (row, "uri", &uri, NULL);
    return uri;
}

GtkWidget *
cloud_nautilus_find_column_title (GtkWidget *window)
{
    /* Read the actual column heading, independent of its translated text. */
    for (GtkWidget *title = window; title != NULL; title = next_widget (title, window))
    {
        if (!has_type_name (title, COLUMN_TITLE_TYPE))
            continue;
        for (GtkWidget *widget = title; widget != NULL; widget = next_widget (widget, title))
        {
            const char *text = GTK_IS_LABEL (widget) ? gtk_label_get_label (GTK_LABEL (widget)) : NULL;

            if (text != NULL && *text != '\0')
                return widget;
        }
    }
    return NULL;
}

void
cloud_nautilus_unselect_lists (GtkWidget *window,
                               GtkWidget *except)
{
    for (GtkWidget *widget = window; widget != NULL; widget = next_widget (widget, window))
        if (GTK_IS_LIST_BOX (widget) && widget != except)
            gtk_list_box_unselect_all (GTK_LIST_BOX (widget));
}

gboolean
cloud_nautilus_open_location (GtkWidget  *window,
                              const char *uri)
{
    static const char * const actions[] = { "slot.open-location", "win.open-location" };
    g_autoptr(GVariant) parameter = g_variant_ref_sink (g_variant_new_string (uri));
    GtkWidget *targets[] = { cloud_nautilus_get_active_slot (window), window };

    for (guint i = 0; i < G_N_ELEMENTS (targets); i++)
    {
        if (targets[i] == NULL)
            continue;
        for (guint j = 0; j < G_N_ELEMENTS (actions); j++)
            if (gtk_widget_activate_action_variant (targets[i], actions[j], parameter))
                return TRUE;
    }
    return FALSE;
}
