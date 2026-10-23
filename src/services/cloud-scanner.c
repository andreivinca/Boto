/* Background rclone discovery. */

#include "services/cloud-scanner.h"

#include "core/cloud-discovery.h"
#include "core/cloud-location.h"

struct _CloudScanner
{
    GObject parent_instance;

    guint interval;
    GPtrArray *locations;
    gboolean scanned;       /* a scan has succeeded */
    gboolean scanning;
    guint timer;
};

enum
{
    CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_DYNAMIC_TYPE_EXTENDED (CloudScanner, cloud_scanner, G_TYPE_OBJECT, G_TYPE_FLAG_FINAL, {})

typedef struct
{
    CloudScanner *scanner;
    GPtrArray *locations;   /* NULL if the scan failed */
} ScanResult;

static void
scan_result_free (gpointer user_data)
{
    ScanResult *result = user_data;

    g_object_unref (result->scanner);
    g_clear_pointer (&result->locations, g_ptr_array_unref);
    g_free (result);
}

/* Main thread. */
static gboolean
scan_finished (gpointer user_data)
{
    ScanResult *result = user_data;
    CloudScanner *self = result->scanner;

    self->scanning = FALSE;
    if (result->locations == NULL ||
        (self->scanned && cloud_location_list_equal (self->locations, result->locations)))
        return G_SOURCE_REMOVE;
    /* The first result counts as a change even when empty: listeners learn
     * that there are no remotes. */
    self->scanned = TRUE;
    g_ptr_array_unref (self->locations);
    self->locations = g_steal_pointer (&result->locations);
    g_signal_emit (self, signals[CHANGED], 0);
    return G_SOURCE_REMOVE;
}

/* Worker thread: touches nothing but RESULT until it is handed back. */
static gpointer
scan_thread (gpointer user_data)
{
    ScanResult *result = user_data;
    g_autoptr(GError) error = NULL;

    result->locations = cloud_discovery_scan (&error);
    if (result->locations == NULL)
        g_warning ("Unable to scan rclone remotes; retaining current locations: %s", error->message);
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, scan_finished, result, scan_result_free);
    return NULL;
}

static gboolean
scan (gpointer user_data)
{
    CloudScanner *self = user_data;
    g_autoptr(GError) error = NULL;
    ScanResult *result;
    GThread *thread;

    if (self->scanning)
        return G_SOURCE_CONTINUE;
    result = g_new0 (ScanResult, 1);
    result->scanner = g_object_ref (self);
    /* A dedicated thread: a scan can block for 20 s, too long for GTask's shared pool. */
    thread = g_thread_try_new ("rclone-remote-discovery", scan_thread, result, &error);
    if (thread == NULL)
    {
        g_warning ("Unable to scan rclone remotes: %s", error->message);
        scan_result_free (result);
        return G_SOURCE_CONTINUE;
    }
    self->scanning = TRUE;
    g_thread_unref (thread);
    return G_SOURCE_CONTINUE;
}

void
cloud_scanner_start (CloudScanner *self)
{
    g_return_if_fail (CLOUD_IS_SCANNER (self));
    g_return_if_fail (self->timer == 0);

    scan (self);
    self->timer = g_timeout_add_seconds (self->interval, scan, self);
}

GPtrArray *
cloud_scanner_get_locations (CloudScanner *self)
{
    g_return_val_if_fail (CLOUD_IS_SCANNER (self), NULL);

    return self->locations;
}

CloudScanner *
cloud_scanner_new (guint interval_seconds)
{
    CloudScanner *self = g_object_new (CLOUD_TYPE_SCANNER, NULL);

    self->interval = interval_seconds;
    return self;
}

static void
cloud_scanner_dispose (GObject *object)
{
    CloudScanner *self = CLOUD_SCANNER (object);

    g_clear_handle_id (&self->timer, g_source_remove);
    G_OBJECT_CLASS (cloud_scanner_parent_class)->dispose (object);
}

static void
cloud_scanner_finalize (GObject *object)
{
    CloudScanner *self = CLOUD_SCANNER (object);

    g_ptr_array_unref (self->locations);
    G_OBJECT_CLASS (cloud_scanner_parent_class)->finalize (object);
}

static void
cloud_scanner_init (CloudScanner *self)
{
    self->locations = cloud_location_list_new ();
}

static void
cloud_scanner_class_init (CloudScannerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = cloud_scanner_dispose;
    object_class->finalize = cloud_scanner_finalize;

    signals[CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                     0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
cloud_scanner_class_finalize (CloudScannerClass *klass)
{
}

void
cloud_scanner_register_types (GTypeModule *module)
{
    cloud_scanner_register_type (module);
}
