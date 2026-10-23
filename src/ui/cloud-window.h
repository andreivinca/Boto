/* Keeps the Cloud section of one Files window in step: hides Nautilus' own
 * rows for cloud mount points, highlights the remote being browsed, follows
 * the window's tabs, and opens or mounts a remote when its row is clicked.
 *
 * Emits "refreshed" after each refresh.
 */

#pragma once

#include <gtk/gtk.h>

#include "ui/cloud-section.h"

G_BEGIN_DECLS

#define CLOUD_TYPE_WINDOW (cloud_window_get_type ())

G_DECLARE_FINAL_TYPE (CloudWindow, cloud_window, CLOUD, WINDOW, GObject)

void          cloud_window_register_types (GTypeModule *module);

/* Adds a Cloud section to CONTAINER (a vertical GtkBox) in WINDOW, below the
 * NATIVE places list, as found by cloud_nautilus_prepare_sidebar(). */
CloudWindow  *cloud_window_new (GtkWidget *window,
                                GtkWidget *native,
                                GtkWidget *container);

/* The Files window, or NULL once closed. */
GtkWidget    *cloud_window_get_window (CloudWindow *self);

/* Shows LOCATIONS (see cloud_section_set_locations()) and refreshes. */
void          cloud_window_set_locations (CloudWindow *self,
                                          GPtrArray   *locations);

/* Refreshes on the next idle. */
void          cloud_window_queue_refresh (CloudWindow *self);

/* Disconnects from the window when it closes; later calls do nothing. */
void          cloud_window_close (CloudWindow *self);

/* State for diagnostics. */
CloudSection *cloud_window_get_section (CloudWindow *self);
guint         cloud_window_get_hidden_duplicates (CloudWindow *self);
const char   *cloud_window_get_location_uri (CloudWindow *self);

G_END_DECLS
