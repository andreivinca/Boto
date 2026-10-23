/* Keeps the Cloud section of one Files window in step. */

#include "ui/cloud-window.h"

#include "core/cloud-mounts.h"
#include "nautilus/cloud-nautilus.h"
#include "services/cloud-mounter.h"
#include "ui/cloud-drag-tracker.h"

struct _CloudWindow
{
    GObject parent_instance;

    GtkWidget *window;          /* NULL once closed */
    GtkWidget *native;
    CloudSection *section;
    GListModel *native_rows;    /* children of the native places list */
    CloudDragTracker *drag_tracker;
    GPtrArray *locations;
    GHashTable *slots;          /* slots being watched (owned) */
    GHashTable *hidden_rows;    /* native rows hidden as duplicates (owned) */
    GHashTable *connecting;     /* names of remotes being mounted */
    guint hidden;
    char *location_uri;
    gboolean refreshing;
};

enum
{
    REFRESHED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_DYNAMIC_TYPE_EXTENDED (CloudWindow, cloud_window, G_TYPE_OBJECT, G_TYPE_FLAG_FINAL, {})

/* A GHRFunc: whether WIDGET is no longer in WINDOW, e.g. a closed tab. */
static gboolean
left_window (gpointer widget,
             gpointer value,
             gpointer window)
{
    return gtk_widget_get_root (widget) != GTK_ROOT (window);
}

static gboolean
is_location (CloudWindow *self,
             GFile       *file)
{
    for (guint i = 0; i < self->locations->len; i++)
        if (g_file_equal (file, ((CloudLocation *) g_ptr_array_index (self->locations, i))->file))
            return TRUE;
    return FALSE;
}

static void queue_refresh_low (CloudWindow *self);

/* Watch new tabs, and let go of closed ones. */
static void
watch_slots (CloudWindow *self)
{
    g_autoptr(GPtrArray) slots = cloud_nautilus_list_slots (self->window);

    g_hash_table_foreach_remove (self->slots, left_window, self->window);
    for (guint i = 0; i < slots->len; i++)
    {
        GtkWidget *slot = g_ptr_array_index (slots, i);

        if (g_hash_table_contains (self->slots, slot))
            continue;
        g_hash_table_add (self->slots, g_object_ref (slot));
        cloud_nautilus_watch_slot (slot, G_CALLBACK (queue_refresh_low), self);
    }
}

/* Hide Nautilus' rows for cloud mount points. Restore rows of removed remotes,
 * and let go of rows Nautilus has dropped. */
static void
hide_duplicates (CloudWindow *self)
{
    g_autoptr(GHashTable) desired = g_hash_table_new (g_str_hash, g_str_equal);
    g_autoptr(GPtrArray) rows = NULL;
    GHashTableIter iter;
    gpointer row;

    for (guint i = 0; i < self->locations->len; i++)
        g_hash_table_add (desired, ((CloudLocation *) g_ptr_array_index (self->locations, i))->uri);
    g_hash_table_iter_init (&iter, self->hidden_rows);
    while (g_hash_table_iter_next (&iter, &row, NULL))
    {
        g_autofree char *uri = cloud_nautilus_row_get_uri (row);

        if (uri == NULL || !g_hash_table_contains (desired, uri) || left_window (row, NULL, self->window))
        {
            gtk_widget_set_visible (row, TRUE);
            g_hash_table_iter_remove (&iter);
        }
    }

    self->hidden = 0;
    rows = cloud_nautilus_list_place_rows (self->window);
    for (guint i = 0; i < rows->len; i++)
    {
        GtkWidget *place = g_ptr_array_index (rows, i);
        g_autofree char *uri = cloud_nautilus_row_get_uri (place);
        g_autoptr(GFile) file = NULL;

        if (uri == NULL || *uri == '\0')
            continue;
        file = g_file_new_for_uri (uri);
        if (!is_location (self, file))
            continue;
        if (gtk_widget_get_visible (place))
            gtk_widget_set_visible (place, FALSE);
        if (!g_hash_table_contains (self->hidden_rows, place))
            g_hash_table_add (self->hidden_rows, g_object_ref (place));
        self->hidden++;
    }
}

/* Select the remote the current tab is in, if any. */
static void
follow_location (CloudWindow *self)
{
    GtkWidget *slot = cloud_nautilus_get_active_slot (self->window);
    g_autoptr(GFile) location = slot ? cloud_nautilus_slot_get_location (slot) : NULL;
    CloudLocation *current = cloud_location_list_find_containing (self->locations, location);

    g_free (self->location_uri);
    self->location_uri = location ? g_file_get_uri (location) : NULL;
    if (current != NULL)
        cloud_nautilus_unselect_lists (self->window, cloud_section_get_list (self->section));
    cloud_section_select (self->section, current);
}

static void
refresh (CloudWindow *self)
{
    GtkWidget *title;

    if (self->window == NULL || self->refreshing)
        return;
    self->refreshing = TRUE;

    title = cloud_nautilus_find_column_title (self->window);
    if (title != NULL)
        cloud_section_copy_heading_style (self->section, title);
    watch_slots (self);
    hide_duplicates (self);
    follow_location (self);

    self->refreshing = FALSE;
    g_signal_emit (self, signals[REFRESHED], 0);
}

static gboolean
refresh_idle (gpointer user_data)
{
    refresh (user_data);
    return G_SOURCE_REMOVE;
}

static void
queue_refresh_at (CloudWindow *self,
                  int          priority)
{
    g_idle_add_full (priority, refresh_idle, g_object_ref (self), g_object_unref);
}

void
cloud_window_queue_refresh (CloudWindow *self)
{
    g_return_if_fail (CLOUD_IS_WINDOW (self));

    queue_refresh_at (self, G_PRIORITY_DEFAULT_IDLE);
}

static void
queue_refresh_low (CloudWindow *self)
{
    queue_refresh_at (self, G_PRIORITY_LOW);
}

static void
navigate (CloudWindow   *self,
          CloudLocation *location)
{
    if (cloud_nautilus_open_location (self->window, location->uri))
        cloud_window_queue_refresh (self);
    else
        g_warning ("Nautilus did not accept in-place navigation to %s", location->name);
}

static void
show_connection_error (CloudWindow   *self,
                       CloudLocation *location)
{
    g_autoptr(GtkAlertDialog) dialog = gtk_alert_dialog_new ("Could not connect to %s", location->name);

    gtk_alert_dialog_set_detail (dialog, "Please check your connection and try again.");
    gtk_alert_dialog_show (dialog, GTK_WINDOW (self->window));
}

typedef struct
{
    CloudWindow *window;
    CloudLocation *location;
} MountRequest;

static void
mount_request_free (MountRequest *request)
{
    g_object_unref (request->window);
    cloud_location_unref (request->location);
    g_free (request);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MountRequest, mount_request_free)

static void
mounted (GObject      *source,
         GAsyncResult *result,
         gpointer      user_data)
{
    g_autoptr(MountRequest) request = user_data;
    g_autoptr(GError) error = NULL;
    CloudWindow *self = request->window;
    gboolean ok = cloud_mounter_mount_finish (result, &error);

    g_hash_table_remove (self->connecting, request->location->name);
    if (self->window == NULL)
        return;
    if (ok)
    {
        navigate (self, request->location);
        return;
    }
    g_message ("Could not mount %s: %s", request->location->name, error->message);
    show_connection_error (self, request->location);
}

static void
open_location (CloudWindow   *self,
               CloudLocation *location)
{
    MountRequest *request;

    if (self->window == NULL)
        return;
    if (cloud_mounts_is_mount (location->path))
    {
        navigate (self, location);
        return;
    }
    /* Ignore clicks while this remote is already being mounted. */
    if (!g_hash_table_add (self->connecting, g_strdup (location->name)))
        return;
    request = g_new (MountRequest, 1);
    request->window = g_object_ref (self);
    request->location = cloud_location_ref (location);
    cloud_mounter_mount_async (location, NULL, mounted, request);
}

/* The rows accept no drops: grey them out while a drag is over the sidebar,
 * as Nautilus does for its own rows that can't take one. */
static void
dragging_changed (CloudWindow *self)
{
    if (self->section != NULL)
        cloud_section_set_dimmed (self->section, cloud_drag_tracker_get_dragging (self->drag_tracker));
}

CloudWindow *
cloud_window_new (GtkWidget *window,
                  GtkWidget *native,
                  GtkWidget *container)
{
    CloudWindow *self;

    g_return_val_if_fail (GTK_IS_WINDOW (window), NULL);
    g_return_val_if_fail (GTK_IS_BOX (container), NULL);

    self = g_object_new (CLOUD_TYPE_WINDOW, NULL);
    self->window = g_object_ref (window);
    self->native = g_object_ref (native);
    self->section = g_object_ref_sink (CLOUD_SECTION (cloud_section_new ()));
    gtk_box_append (GTK_BOX (container), GTK_WIDGET (self->section));
    g_signal_connect_object (self->section, "activate-location", G_CALLBACK (open_location), self,
                             G_CONNECT_SWAPPED);
    self->drag_tracker = cloud_drag_tracker_new (container);
    g_signal_connect_object (self->drag_tracker, "notify::dragging", G_CALLBACK (dragging_changed), self,
                             G_CONNECT_SWAPPED);
    /* Native rows come and go with mounts; hide new duplicates too. */
    self->native_rows = gtk_widget_observe_children (native);
    g_signal_connect_object (self->native_rows, "items-changed", G_CALLBACK (cloud_window_queue_refresh), self,
                             G_CONNECT_SWAPPED);
    cloud_nautilus_watch_window (window, G_CALLBACK (cloud_window_queue_refresh), self);
    return self;
}

GtkWidget *
cloud_window_get_window (CloudWindow *self)
{
    g_return_val_if_fail (CLOUD_IS_WINDOW (self), NULL);

    return self->window;
}

void
cloud_window_set_locations (CloudWindow *self,
                            GPtrArray   *locations)
{
    g_return_if_fail (CLOUD_IS_WINDOW (self));

    if (self->window == NULL)
        return;
    g_ptr_array_unref (self->locations);
    self->locations = g_ptr_array_ref (locations);
    cloud_section_set_locations (self->section, locations);
    refresh (self);
}

void
cloud_window_close (CloudWindow *self)
{
    GHashTableIter iter;
    gpointer slot;

    g_return_if_fail (CLOUD_IS_WINDOW (self));

    if (self->window == NULL)
        return;
    g_signal_handlers_disconnect_by_data (self->window, self);
    g_signal_handlers_disconnect_by_data (self->native_rows, self);
    g_signal_handlers_disconnect_by_data (self->section, self);
    g_hash_table_iter_init (&iter, self->slots);
    while (g_hash_table_iter_next (&iter, &slot, NULL))
        g_signal_handlers_disconnect_by_data (slot, self);
    g_signal_handlers_disconnect_by_data (self->drag_tracker, self);
    cloud_drag_tracker_stop (self->drag_tracker);
    g_hash_table_remove_all (self->slots);
    g_hash_table_remove_all (self->hidden_rows);
    g_clear_object (&self->drag_tracker);
    g_clear_object (&self->native_rows);
    g_clear_object (&self->section);
    g_clear_object (&self->native);
    g_clear_object (&self->window);
}

CloudSection *
cloud_window_get_section (CloudWindow *self)
{
    g_return_val_if_fail (CLOUD_IS_WINDOW (self), NULL);

    return self->section;
}

guint
cloud_window_get_hidden_duplicates (CloudWindow *self)
{
    g_return_val_if_fail (CLOUD_IS_WINDOW (self), 0);

    return self->hidden;
}

const char *
cloud_window_get_location_uri (CloudWindow *self)
{
    g_return_val_if_fail (CLOUD_IS_WINDOW (self), NULL);

    return self->location_uri;
}

static void
cloud_window_dispose (GObject *object)
{
    cloud_window_close (CLOUD_WINDOW (object));
    G_OBJECT_CLASS (cloud_window_parent_class)->dispose (object);
}

static void
cloud_window_finalize (GObject *object)
{
    CloudWindow *self = CLOUD_WINDOW (object);

    g_ptr_array_unref (self->locations);
    g_hash_table_unref (self->slots);
    g_hash_table_unref (self->hidden_rows);
    g_hash_table_unref (self->connecting);
    g_free (self->location_uri);
    G_OBJECT_CLASS (cloud_window_parent_class)->finalize (object);
}

static void
cloud_window_init (CloudWindow *self)
{
    self->locations = cloud_location_list_new ();
    self->slots = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);
    self->hidden_rows = g_hash_table_new_full (NULL, NULL, g_object_unref, NULL);
    self->connecting = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
cloud_window_class_init (CloudWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = cloud_window_dispose;
    object_class->finalize = cloud_window_finalize;

    signals[REFRESHED] = g_signal_new ("refreshed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                       0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
cloud_window_class_finalize (CloudWindowClass *klass)
{
}

void
cloud_window_register_types (GTypeModule *module)
{
    cloud_window_register_type (module);
}
