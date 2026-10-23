/* The Nautilus extension object. */

#include "cloud-extension.h"

#include <gtk/gtk.h>
#include <nautilus-extension.h>

#include "core/cloud-location.h"
#include "nautilus/cloud-nautilus.h"
#include "services/cloud-automount.h"
#include "services/cloud-scanner.h"
#include "ui/cloud-diagnostics.h"
#include "ui/cloud-window.h"

#define SCAN_INTERVAL_SECONDS 30
#define INJECT_INTERVAL_MS 250
#define INJECT_MAX_ATTEMPTS 40

struct _CloudSidebarExtension
{
    GObject parent_instance;

    CloudScanner *scanner;
    GPtrArray *windows;     /* CloudWindow, in injection order */
    GHashTable *pending;    /* Files windows waiting to be injected (not owned) */
};

static void menu_provider_iface_init (NautilusMenuProviderInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (CloudSidebarExtension, cloud_sidebar_extension, G_TYPE_OBJECT,
                                G_TYPE_FLAG_FINAL,
                                G_IMPLEMENT_INTERFACE_DYNAMIC (NAUTILUS_TYPE_MENU_PROVIDER,
                                                               menu_provider_iface_init))

static CloudWindow *
find_window (CloudSidebarExtension *self,
             GtkWidget             *window)
{
    for (guint i = 0; i < self->windows->len; i++)
    {
        CloudWindow *cloud_window = g_ptr_array_index (self->windows, i);

        if (cloud_window_get_window (cloud_window) == window)
            return cloud_window;
    }
    return NULL;
}

static void
report (CloudSidebarExtension *self)
{
    cloud_diagnostics_report (self->windows, cloud_scanner_get_locations (self->scanner));
}

static void
locations_changed (CloudSidebarExtension *self)
{
    GPtrArray *locations = cloud_scanner_get_locations (self->scanner);

    /* Every remote gets mounted at login from now on; removed ones no longer. */
    cloud_automount_sync (locations);
    for (guint i = 0; i < self->windows->len; i++)
        cloud_window_set_locations (g_ptr_array_index (self->windows, i), locations);
}

static void
add_window (CloudSidebarExtension *self,
            GtkWidget             *window,
            GtkWidget             *native,
            GtkWidget             *container)
{
    CloudWindow *cloud_window = cloud_window_new (window, native, container);
    GPtrArray *locations = cloud_scanner_get_locations (self->scanner);
    g_autoptr(GString) names = g_string_new (NULL);

    g_signal_connect_object (cloud_window, "refreshed", G_CALLBACK (report), self, G_CONNECT_SWAPPED);
    g_ptr_array_add (self->windows, cloud_window);
    cloud_window_set_locations (cloud_window, locations);

    for (guint i = 0; i < locations->len; i++)
        g_string_append_printf (names, "%s%s", i ? ", " : "",
                                ((CloudLocation *) g_ptr_array_index (locations, i))->name);
    g_message ("Cloud sidebar ready: %s", names->str);
}

typedef struct
{
    CloudSidebarExtension *self;
    GtkWidget *window;
    guint attempts;
} InjectAttempt;

static void
inject_attempt_free (gpointer user_data)
{
    InjectAttempt *attempt = user_data;

    g_hash_table_remove (attempt->self->pending, attempt->window);
    g_object_unref (attempt->self);
    g_object_unref (attempt->window);
    g_free (attempt);
}

/* Defer tree changes until the initial view and other extensions settle. */
static gboolean
attempt_inject (gpointer user_data)
{
    InjectAttempt *attempt = user_data;
    GApplication *app = g_application_get_default ();
    GtkWidget *native = NULL;
    GtkWidget *container = NULL;

    attempt->attempts++;
    if (!GTK_IS_APPLICATION (app) ||
        g_list_find (gtk_application_get_windows (GTK_APPLICATION (app)), attempt->window) == NULL)
        return G_SOURCE_REMOVE;

    switch (cloud_nautilus_prepare_sidebar (attempt->window, attempt->attempts, &native, &container))
    {
    case CLOUD_SIDEBAR_READY:
        add_window (attempt->self, attempt->window, native, container);
        return G_SOURCE_REMOVE;
    case CLOUD_SIDEBAR_UNSUPPORTED:
        return G_SOURCE_REMOVE;
    case CLOUD_SIDEBAR_RETRY:
    default:
        break;
    }
    if (attempt->attempts >= INJECT_MAX_ATTEMPTS)
    {
        g_warning ("Nautilus sidebar layout was not recognized");
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
schedule (CloudSidebarExtension *self,
          GtkWindow             *window)
{
    InjectAttempt *attempt;

    if (!cloud_nautilus_is_window (window) || find_window (self, GTK_WIDGET (window)) != NULL ||
        g_hash_table_contains (self->pending, window))
        return;
    g_hash_table_add (self->pending, window);
    attempt = g_new0 (InjectAttempt, 1);
    attempt->self = g_object_ref (self);
    attempt->window = g_object_ref (GTK_WIDGET (window));
    g_timeout_add_full (G_PRIORITY_DEFAULT, INJECT_INTERVAL_MS, attempt_inject, attempt, inject_attempt_free);
}

static void
window_added (GtkApplication        *app,
              GtkWindow             *window,
              CloudSidebarExtension *self)
{
    schedule (self, window);
}

static void
window_removed (GtkApplication        *app,
                GtkWindow             *window,
                CloudSidebarExtension *self)
{
    CloudWindow *cloud_window = find_window (self, GTK_WIDGET (window));

    if (cloud_window == NULL)
        return;
    cloud_window_close (cloud_window);
    g_ptr_array_remove (self->windows, cloud_window);
}

static gboolean
start (gpointer user_data)
{
    CloudSidebarExtension *self = user_data;
    GApplication *app = g_application_get_default ();

    if (!GTK_IS_APPLICATION (app))
        return G_SOURCE_REMOVE;
    g_signal_connect_object (app, "window-added", G_CALLBACK (window_added), self, 0);
    g_signal_connect_object (app, "window-removed", G_CALLBACK (window_removed), self, 0);
    for (GList *l = gtk_application_get_windows (GTK_APPLICATION (app)); l != NULL; l = l->next)
        schedule (self, l->data);
    cloud_scanner_start (self->scanner);
    return G_SOURCE_REMOVE;
}

static GList *
get_file_items (NautilusMenuProvider *provider,
                GList                *files)
{
    return NULL;
}

/* Nautilus builds this menu on every folder change; a cheap moment to resync. */
static GList *
get_background_items (NautilusMenuProvider *provider,
                      NautilusFileInfo     *current_folder)
{
    CloudSidebarExtension *self = CLOUD_SIDEBAR_EXTENSION (provider);

    for (guint i = 0; i < self->windows->len; i++)
        cloud_window_queue_refresh (g_ptr_array_index (self->windows, i));
    return NULL;
}

static void
menu_provider_iface_init (NautilusMenuProviderInterface *iface)
{
    iface->get_file_items = get_file_items;
    iface->get_background_items = get_background_items;
}

static void
cloud_sidebar_extension_dispose (GObject *object)
{
    CloudSidebarExtension *self = CLOUD_SIDEBAR_EXTENSION (object);

    for (guint i = 0; i < self->windows->len; i++)
        cloud_window_close (g_ptr_array_index (self->windows, i));
    g_ptr_array_set_size (self->windows, 0);
    g_clear_object (&self->scanner);
    G_OBJECT_CLASS (cloud_sidebar_extension_parent_class)->dispose (object);
}

static void
cloud_sidebar_extension_finalize (GObject *object)
{
    CloudSidebarExtension *self = CLOUD_SIDEBAR_EXTENSION (object);

    g_ptr_array_unref (self->windows);
    g_hash_table_unref (self->pending);
    G_OBJECT_CLASS (cloud_sidebar_extension_parent_class)->finalize (object);
}

static void
cloud_sidebar_extension_init (CloudSidebarExtension *self)
{
    self->scanner = cloud_scanner_new (SCAN_INTERVAL_SECONDS);
    self->windows = g_ptr_array_new_with_free_func (g_object_unref);
    self->pending = g_hash_table_new (NULL, NULL);
    g_signal_connect_object (self->scanner, "changed", G_CALLBACK (locations_changed), self, G_CONNECT_SWAPPED);
    /* Nautilus creates extensions before its application is ready. */
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, start, g_object_ref (self), g_object_unref);
}

static void
cloud_sidebar_extension_class_init (CloudSidebarExtensionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = cloud_sidebar_extension_dispose;
    object_class->finalize = cloud_sidebar_extension_finalize;
}

static void
cloud_sidebar_extension_class_finalize (CloudSidebarExtensionClass *klass)
{
}

void
cloud_sidebar_extension_register_types (GTypeModule *module)
{
    cloud_sidebar_extension_register_type (module);
}
