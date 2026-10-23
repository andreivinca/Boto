/* Whether a drag-and-drop operation is over an area of the window. */

#include "ui/cloud-drag-tracker.h"

struct _CloudDragTracker
{
    GObject parent_instance;

    GtkWidget *area;                /* NULL once stopped */
    GtkEventController *motion;
    GdkDrag *drag;                  /* weak: the in-process drag being followed */
    gboolean dragging;
};

enum
{
    PROP_0,
    PROP_DRAGGING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_DYNAMIC_TYPE_EXTENDED (CloudDragTracker, cloud_drag_tracker, G_TYPE_OBJECT, G_TYPE_FLAG_FINAL, {})

static void
set_dragging (CloudDragTracker *self,
              gboolean          dragging)
{
    if (self->dragging == dragging)
        return;
    self->dragging = dragging;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DRAGGING]);
}

static void
forget_drag (CloudDragTracker *self)
{
    if (self->drag != NULL)
        g_signal_handlers_disconnect_by_data (self->drag, self);
    g_clear_weak_pointer (&self->drag);
}

static void
drag_ended (CloudDragTracker *self)
{
    forget_drag (self);
    set_dragging (self, FALSE);
}

static void
entered (GtkDropControllerMotion *motion,
         double                   x,
         double                   y,
         CloudDragTracker        *self)
{
    GdkDrop *drop = gtk_drop_controller_motion_get_drop (motion);
    GdkDrag *drag = drop ? gdk_drop_get_drag (drop) : NULL;

    set_dragging (self, TRUE);
    if (drag == NULL || drag == self->drag)
        return;
    forget_drag (self);
    g_set_weak_pointer (&self->drag, drag);
    g_signal_connect_object (drag, "dnd-finished", G_CALLBACK (drag_ended), self, G_CONNECT_SWAPPED);
    g_signal_connect_object (drag, "cancel", G_CALLBACK (drag_ended), self, G_CONNECT_SWAPPED);
}

static void
moved (GtkDropControllerMotion *motion,
       double                   x,
       double                   y,
       CloudDragTracker        *self)
{
    set_dragging (self, TRUE);
}

static gboolean
check_left (gpointer user_data)
{
    CloudDragTracker *self = user_data;

    if (self->motion != NULL &&
        !gtk_drop_controller_motion_contains_pointer (GTK_DROP_CONTROLLER_MOTION (self->motion)))
        set_dragging (self, FALSE);
    return G_SOURCE_REMOVE;
}

static void
left (GtkDropControllerMotion *motion,
      CloudDragTracker        *self)
{
    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, check_left, g_object_ref (self), g_object_unref);
}

CloudDragTracker *
cloud_drag_tracker_new (GtkWidget *area)
{
    CloudDragTracker *self;

    g_return_val_if_fail (GTK_IS_WIDGET (area), NULL);

    self = g_object_new (CLOUD_TYPE_DRAG_TRACKER, NULL);
    self->area = g_object_ref (area);
    self->motion = gtk_drop_controller_motion_new ();
    g_signal_connect_object (self->motion, "enter", G_CALLBACK (entered), self, 0);
    g_signal_connect_object (self->motion, "motion", G_CALLBACK (moved), self, 0);
    g_signal_connect_object (self->motion, "leave", G_CALLBACK (left), self, 0);
    gtk_widget_add_controller (area, g_object_ref (self->motion));
    return self;
}

gboolean
cloud_drag_tracker_get_dragging (CloudDragTracker *self)
{
    g_return_val_if_fail (CLOUD_IS_DRAG_TRACKER (self), FALSE);

    return self->dragging;
}

void
cloud_drag_tracker_stop (CloudDragTracker *self)
{
    g_return_if_fail (CLOUD_IS_DRAG_TRACKER (self));

    forget_drag (self);
    if (self->motion != NULL && gtk_event_controller_get_widget (self->motion) == self->area)
        gtk_widget_remove_controller (self->area, self->motion);
    g_clear_object (&self->motion);
    g_clear_object (&self->area);
    set_dragging (self, FALSE);
}

static void
cloud_drag_tracker_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    CloudDragTracker *self = CLOUD_DRAG_TRACKER (object);

    switch (property_id)
    {
    case PROP_DRAGGING:
        g_value_set_boolean (value, self->dragging);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
cloud_drag_tracker_dispose (GObject *object)
{
    cloud_drag_tracker_stop (CLOUD_DRAG_TRACKER (object));
    G_OBJECT_CLASS (cloud_drag_tracker_parent_class)->dispose (object);
}

static void
cloud_drag_tracker_init (CloudDragTracker *self)
{
}

static void
cloud_drag_tracker_class_init (CloudDragTrackerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = cloud_drag_tracker_get_property;
    object_class->dispose = cloud_drag_tracker_dispose;

    properties[PROP_DRAGGING] = g_param_spec_boolean ("dragging", NULL, NULL, FALSE,
                                                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
                                                      G_PARAM_EXPLICIT_NOTIFY);
    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
cloud_drag_tracker_class_finalize (CloudDragTrackerClass *klass)
{
}

void
cloud_drag_tracker_register_types (GTypeModule *module)
{
    cloud_drag_tracker_register_type (module);
}
