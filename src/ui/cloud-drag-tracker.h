/* Whether a drag-and-drop operation is over an area of the window.
 *
 * "dragging" turns TRUE when a drag enters AREA (or a descendant) and FALSE
 * when it leaves or ends. Descendants with their own drop target make GTK
 * report leaves while the drag is still inside, so leaves are re-checked, and
 * drags from this process are also followed until they finish.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CLOUD_TYPE_DRAG_TRACKER (cloud_drag_tracker_get_type ())

G_DECLARE_FINAL_TYPE (CloudDragTracker, cloud_drag_tracker, CLOUD, DRAG_TRACKER, GObject)

void              cloud_drag_tracker_register_types (GTypeModule *module);

CloudDragTracker *cloud_drag_tracker_new (GtkWidget *area);
gboolean          cloud_drag_tracker_get_dragging (CloudDragTracker *self);

/* Stops watching AREA; "dragging" becomes FALSE. Also done on dispose. */
void              cloud_drag_tracker_stop (CloudDragTracker *self);

G_END_DECLS
