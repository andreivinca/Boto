/* Everything that depends on Nautilus' private widget structure.
 *
 * Nautilus has no public sidebar extension API, so these functions find their
 * way by type names, widget names, CSS classes, properties and action names
 * that can change between releases. Keep every such assumption here, and in
 * docs/nautilus-internals.md.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Names of the widgets this extension adds; other extensions may look for them. */
#define CLOUD_WRAPPER_NAME "cloud-sidebar-wrapper"
#define CLOUD_SECTION_NAME "cloud-sidebar-group"
#define CLOUD_HEADING_NAME "cloud-section-heading"
#define CLOUD_LIST_NAME    "cloud-locations"

typedef enum
{
    CLOUD_SIDEBAR_RETRY,       /* not ready yet; try again later */
    CLOUD_SIDEBAR_READY,       /* *native and *container are set */
    CLOUD_SIDEBAR_UNSUPPORTED, /* unknown layout; give up on this window */
} CloudSidebarStatus;

/* Whether WINDOW is a Files browser window. */
gboolean           cloud_nautilus_is_window (GtkWindow *window);

/* Finds the native places list of WINDOW and a vertical box to append our
 * section to: Nautilus My Computer's wrapper, or one created around the list.
 * ATTEMPT counts calls for this window, so My Computer gets time to go first. */
CloudSidebarStatus cloud_nautilus_prepare_sidebar (GtkWidget  *window,
                                                   guint       attempt,
                                                   GtkWidget **native,
                                                   GtkWidget **container);

/* Call CHANGED (OBJECT) when the window's title or active tab changes, or
 * when the slot's location or activity changes. Disconnect with
 * g_signal_handlers_disconnect_by_data (instance, OBJECT); OBJECT's
 * finalization disconnects them too. */
void               cloud_nautilus_watch_window (GtkWidget *window,
                                                GCallback  changed,
                                                gpointer   object);
void               cloud_nautilus_watch_slot (GtkWidget *slot,
                                              GCallback  changed,
                                              gpointer   object);

/* Slots (tabs) of WINDOW in tree order; the array does not own them. */
GPtrArray         *cloud_nautilus_list_slots (GtkWidget *window);

/* The active slot, else the last mapped one; NULL if none. */
GtkWidget         *cloud_nautilus_get_active_slot (GtkWidget *window);

/* The folder SLOT shows (transfer full), or NULL. */
GFile             *cloud_nautilus_slot_get_location (GtkWidget *slot);

/* Rows of WINDOW that represent places (they have a "uri"); not owned. */
GPtrArray         *cloud_nautilus_list_place_rows (GtkWidget *window);
char              *cloud_nautilus_row_get_uri (GtkWidget *row);

/* A label of the file list's column header, to copy its style; or NULL. */
GtkWidget         *cloud_nautilus_find_column_title (GtkWidget *window);

/* Unselects every list box in WINDOW except EXCEPT. */
void               cloud_nautilus_unselect_lists (GtkWidget *window,
                                                  GtkWidget *except);

/* Opens URI in the current tab. FALSE if Nautilus did not accept it. */
gboolean           cloud_nautilus_open_location (GtkWidget  *window,
                                                 const char *uri);

G_END_DECLS
